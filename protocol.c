#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include "protocol.h"

/* macOS has no MSG_NOSIGNAL; the server ignores SIGPIPE instead. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MAX_TOKENS 8

/* ---------- private helpers ---------- */

/* Copies src into dst (size incl. '\0'). 1 if non-empty and fits, else 0. */
static int copy_field(char *dst, size_t size, const char *src)
{
    size_t len = strnlen(src, size);
    if (len == 0 || len >= size) {
        return 0;
    }
    memset(dst, 0, size);
    memcpy(dst, src, len);
    return 1;
}

/* Parses a whole string as an int in [min, max]. 1 on success, else 0. */
static int parse_int(const char *s, int min, int max, int *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < min || v > max) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* Parses "YYYY-MM-DDTHH:MM" as local time. 1 on success, else 0. */
static int parse_time(const char *s, time_t *out)
{
    struct tm tm;
    char extra;
    memset(&tm, 0, sizeof tm);

    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d%c", &tm.tm_year, &tm.tm_mon,
               &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &extra) != 5) {
        return 0;
    }
    if (tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 || tm.tm_mday > 31 ||
        tm.tm_hour > 23 || tm.tm_min > 59) {
        return 0;
    }
    tm.tm_year -= 1900;
    tm.tm_mon  -= 1;
    tm.tm_isdst = -1;   /* let the system decide daylight saving */

    int want_mon = tm.tm_mon;
    int want_day = tm.tm_mday;

    time_t t = mktime(&tm);
    if (t == (time_t)-1) {
        return 0;
    }
    /* mktime "fixes" impossible dates (Feb 31 -> Mar 3); reject those */
    if (tm.tm_mon != want_mon || tm.tm_mday != want_day) {
        return 0;
    }
    *out = t;
    return 1;
}

/* Sends all len bytes, retrying on partial writes and interrupts. */
static int send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        data += n;
        len  -= (size_t)n;
    }
    return 0;
}

/* ---------- public functions ---------- */

ResStatus parse_command(const char *line, Command *cmd)
{
    if (line == NULL || cmd == NULL) {
        return RES_ERR_INVALID;
    }
    memset(cmd, 0, sizeof *cmd);

    /* Tokenize a private copy so the caller's line is untouched */
    char copy[MAX_LINE];
    size_t len = strnlen(line, sizeof copy);
    if (len >= sizeof copy) {
        return RES_ERR_INVALID;
    }
    memcpy(copy, line, len + 1);

    char *tok[MAX_TOKENS];
    int ntok = 0;
    char *save = NULL;
    for (char *t = strtok_r(copy, " \t", &save); t != NULL;
         t = strtok_r(NULL, " \t", &save)) {
        if (ntok == MAX_TOKENS) {
            return RES_ERR_INVALID;   /* too many arguments */
        }
        tok[ntok++] = t;
    }
    if (ntok == 0) {
        return RES_ERR_INVALID;
    }

    const char *word = tok[0];
    int args = ntok - 1;

    if (strcasecmp(word, "LIST") == 0 && args == 0) {
        cmd->type = CMD_LIST;
        return RES_OK;
    }
    if (strcasecmp(word, "QUIT") == 0 && args == 0) {
        cmd->type = CMD_QUIT;
        return RES_OK;
    }
    if (strcasecmp(word, "SHOW") == 0 && args == 1) {
        cmd->type = CMD_SHOW;
        return copy_field(cmd->trip_id, TRIP_ID_LEN, tok[1]) ? RES_OK : RES_ERR_INVALID;
    }
    if (strcasecmp(word, "DELTRIP") == 0 && args == 1) {
        cmd->type = CMD_DELTRIP;
        return copy_field(cmd->trip_id, TRIP_ID_LEN, tok[1]) ? RES_OK : RES_ERR_INVALID;
    }
    if (strcasecmp(word, "ADDTRIP") == 0 && args == 5) {
        cmd->type = CMD_ADDTRIP;
        if (copy_field(cmd->trip_id, TRIP_ID_LEN, tok[1]) &&
            copy_field(cmd->origin, CITY_LEN, tok[2]) &&
            copy_field(cmd->destination, CITY_LEN, tok[3]) &&
            parse_time(tok[4], &cmd->departure) &&
            parse_int(tok[5], 1, INT_MAX, &cmd->num_seats)) {
            return RES_OK;
        }
        return RES_ERR_INVALID;
    }
    if (strcasecmp(word, "WAITLIST") == 0 && args == 2) {
        cmd->type = CMD_WAITLIST;
        if (copy_field(cmd->trip_id, TRIP_ID_LEN, tok[1]) &&
            copy_field(cmd->name, NAME_LEN, tok[2])) {
            return RES_OK;
        }
        return RES_ERR_INVALID;
    }

    /* HOLD / CONFIRM / CANCEL <trip> <seat> <name> */
    CommandType type;
    if (strcasecmp(word, "HOLD") == 0) {
        type = CMD_HOLD;
    } else if (strcasecmp(word, "CONFIRM") == 0) {
        type = CMD_CONFIRM;
    } else if (strcasecmp(word, "CANCEL") == 0) {
        type = CMD_CANCEL;
    } else {
        return RES_ERR_INVALID;   /* unknown command or wrong arg count */
    }
    if (args != 3) {
        return RES_ERR_INVALID;
    }
    cmd->type = type;
    if (copy_field(cmd->trip_id, TRIP_ID_LEN, tok[1]) &&
        parse_int(tok[2], 1, INT_MAX, &cmd->seat_no) &&
        copy_field(cmd->name, NAME_LEN, tok[3])) {
        return RES_OK;
    }
    return RES_ERR_INVALID;
}

ssize_t proto_read_line(int fd, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len < 2) {
        return -1;
    }

    size_t used = 0;
    for (;;) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) {
            if (used == 0) {
                return 0;   /* clean EOF */
            }
            break;          /* last line had no '\n'; return what we have */
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (c == '\n') {
            if (used == 0) {
                continue;   /* skip blank lines so 0 always means EOF */
            }
            break;
        }
        if (used == buf_len - 1) {
            return -2;      /* too long: caller should close the connection */
        }
        buf[used++] = c;
    }

    if (used > 0 && buf[used - 1] == '\r') {   /* accept "\r\n" too */
        used--;
    }
    buf[used] = '\0';
    return (ssize_t)used;
}

int proto_send_line(int fd, const char *line)
{
    if (line == NULL) {
        return -1;
    }
    if (send_all(fd, line, strlen(line)) < 0) {
        return -1;
    }
    return send_all(fd, "\n", 1);
}

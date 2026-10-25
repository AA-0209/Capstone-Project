/*
 * Bus reservation server.
 *
 *   ./server [port] [database]      defaults: 5050 trips.gdbm
 *
 * Forks one child per client. Each child opens its own database handle
 * after fork(), so the flock in db.c really excludes other children.
 */
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "db.h"
#include "protocol.h"
#include "trip.h"

#define DEFAULT_DB     "trips.gdbm"
#define MAX_TRIPS      128
#define SHOW_BUF_LEN   8192
#define LIST_BUF_LEN   (MAX_TRIPS * MAX_LINE + MAX_LINE)
#define IDLE_TIMEOUT   300   /* seconds a client may sit idle before disconnect */
#define SEND_TIMEOUT   30    /* seconds a send may block on a client not reading */

static volatile sig_atomic_t g_stop = 0;

/* ---------- signals ---------- */

/* Reap every finished child so none are left as zombies. */
static void on_sigchld(int sig)
{
    (void)sig;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    errno = saved;
}

/* Ctrl+C / kill: stop accepting and exit cleanly. */
static void on_stop(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;               /* a vanished client must not kill us */
    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        return -1;
    }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigchld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        return -1;
    }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;               /* no SA_RESTART: accept() must wake up */
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        return -1;
    }
    return 0;
}

/* ---------- reply helpers ---------- */

static const char *res_message(ResStatus rc)
{
    switch (rc) {
    case RES_OK:                return "ok";
    case RES_ERR_INVALID:       return "invalid command or argument (type HELP on the client)";
    case RES_ERR_TAKEN:         return "seat is already taken";
    case RES_ERR_NOT_HELD:      return "you don't hold that seat, or your hold expired";
    case RES_ERR_NO_SEATS:      return "trip is full; use WAITLIST";
    case RES_ERR_WAITLIST_FULL: return "waitlist is full";
    case RES_ERR_NOT_FOUND:     return "not found";
    case RES_ERR_EXISTS:        return "already exists";
    case RES_ERR_IO:            return "server storage error";
    case RES_ERR_DEPARTED:      return "trip has already departed";
    case RES_ERR_SEATS_OPEN:    return "seats are still available; use HOLD";
    case RES_ERR_HAS_SEAT:      return "passenger already has a seat on this trip";
    }
    return "unknown error";
}

static void send_error(int fd, ResStatus rc)
{
    char line[MAX_LINE];
    snprintf(line, sizeof line, "ERR %d %s", (int)rc, res_message(rc));
    proto_send_line(fd, line);
}

/* Sends a multi-line buffer one line at a time. */
static void send_lines(int fd, char *text)
{
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        proto_send_line(fd, line);
    }
}

static void format_time(time_t t, char *out, size_t len)
{
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL || strftime(out, len, "%Y-%m-%dT%H:%M", &tm) == 0) {
        snprintf(out, len, "unknown");
    }
}

/* ---------- command handlers ---------- */

/*
 * Builds the whole reply in memory while locked, then unlocks BEFORE
 * sending. A client that stops reading can then never hold the lock
 * and freeze everyone else's bookings (AI audit finding #1).
 */
static void do_list(int fd, TripDB *db)
{
    char ids[MAX_TRIPS][TRIP_ID_LEN];
    time_t now = time(NULL);

    char *buf = malloc(LIST_BUF_LEN);
    if (buf == NULL) {
        send_error(fd, RES_ERR_IO);
        return;
    }

    if (db_lock(db, 0) != RES_OK) {
        free(buf);
        send_error(fd, RES_ERR_IO);
        return;
    }
    int n = db_list(db, ids, MAX_TRIPS);
    if (n < 0) {
        db_unlock(db);
        free(buf);
        send_error(fd, RES_ERR_IO);
        return;
    }

    size_t off = (size_t)snprintf(buf, LIST_BUF_LEN, "%s\n",
                                  n == 0 ? "OK 0 trips (use ADDTRIP to create one)" : "OK");
    for (int i = 0; i < n; i++) {
        Trip t;
        if (db_read(db, ids[i], &t) != RES_OK) {
            continue;
        }
        trip_expire_holds(&t, now);   /* display only; saved on next write */
        char when[32];
        format_time(t.departure, when, sizeof when);
        int w = snprintf(buf + off, LIST_BUF_LEN - off,
                         "TRIP %s %s -> %s %s seats %d/%d waitlist %d\n",
                         t.id, t.origin, t.destination, when,
                         seatmap_available(&t.seats), t.seats.num_seats,
                         waitlist_count(&t.waitlist));
        if (w < 0 || (size_t)w >= LIST_BUF_LEN - off) {
            break;   /* buffer full; stop adding (can't happen at MAX_TRIPS) */
        }
        off += (size_t)w;
    }
    db_unlock(db);

    send_lines(fd, buf);   /* network I/O happens with no lock held */
    free(buf);
}

static void do_show(int fd, TripDB *db, const Command *cmd)
{
    Trip t;
    ResStatus rc = db_lock(db, 0);
    if (rc == RES_OK) {
        rc = db_read(db, cmd->trip_id, &t);
        db_unlock(db);
    }
    if (rc != RES_OK) {
        send_error(fd, rc);
        return;
    }

    trip_expire_holds(&t, time(NULL));   /* display only */

    char *buf = malloc(SHOW_BUF_LEN);
    if (buf == NULL) {
        send_error(fd, RES_ERR_IO);
        return;
    }
    if (trip_format(&t, buf, SHOW_BUF_LEN) < 0) {
        send_error(fd, RES_ERR_IO);
    } else {
        proto_send_line(fd, "OK");
        send_lines(fd, buf);
    }
    free(buf);
}

static void do_addtrip(int fd, TripDB *db, const Command *cmd)
{
    Trip t;
    ResStatus rc = trip_init(&t, cmd->trip_id, cmd->origin, cmd->destination,
                             cmd->departure, cmd->num_seats);
    if (rc == RES_OK) {
        rc = db_lock(db, 1);
        if (rc == RES_OK) {
            rc = db_create(db, &t);
            db_unlock(db);
        }
    }
    if (rc != RES_OK) {
        send_error(fd, rc);
        return;
    }
    char line[MAX_LINE];
    snprintf(line, sizeof line, "OK CREATED %s with %d seats", t.id, t.seats.num_seats);
    proto_send_line(fd, line);
}

static void do_deltrip(int fd, TripDB *db, const Command *cmd)
{
    ResStatus rc = db_lock(db, 1);
    if (rc == RES_OK) {
        rc = db_delete(db, cmd->trip_id);
        db_unlock(db);
    }
    if (rc != RES_OK) {
        send_error(fd, rc);
        return;
    }
    proto_send_line(fd, "OK DELETED");
}

/*
 * HOLD / CONFIRM / CANCEL / WAITLIST all follow the same locked sequence:
 *   lock -> read -> expire holds -> change -> write -> unlock
 */
static void do_booking(int fd, TripDB *db, const Command *cmd)
{
    Trip t;
    char reply[MAX_LINE];
    char promoted[NAME_LEN] = "";
    int position = 0;
    time_t now = time(NULL);

    ResStatus rc = db_lock(db, 1);
    if (rc != RES_OK) {
        send_error(fd, rc);
        return;
    }

    rc = db_read(db, cmd->trip_id, &t);
    if (rc != RES_OK) {
        db_unlock(db);
        send_error(fd, rc);
        return;
    }

    int expired = trip_expire_holds(&t, now);

    switch (cmd->type) {
    case CMD_HOLD:
        rc = trip_hold(&t, cmd->seat_no, cmd->name, now);
        snprintf(reply, sizeof reply, "OK HELD %d %d", cmd->seat_no, HOLD_SECONDS);
        break;
    case CMD_CONFIRM:
        rc = trip_confirm(&t, cmd->seat_no, cmd->name, now);
        snprintf(reply, sizeof reply, "OK BOOKED %d", cmd->seat_no);
        break;
    case CMD_CANCEL:
        rc = trip_cancel_and_promote(&t, cmd->seat_no, cmd->name, now,
                                     promoted, sizeof promoted);
        if (promoted[0] != '\0') {
            snprintf(reply, sizeof reply, "OK CANCELLED PROMOTED %s", promoted);
        } else {
            snprintf(reply, sizeof reply, "OK CANCELLED");
        }
        break;
    case CMD_WAITLIST:
        rc = trip_join_waitlist(&t, cmd->name, now, &position);
        snprintf(reply, sizeof reply, "OK WAITLISTED %d", position);
        break;
    default:
        rc = RES_ERR_INVALID;
        break;
    }

    /* Save if the command succeeded, or if expiry changed anything */
    if (rc == RES_OK || expired > 0) {
        ResStatus wrc = db_update(db, &t);
        if (rc == RES_OK && wrc != RES_OK) {
            rc = wrc;
        }
    }
    db_unlock(db);

    if (rc == RES_OK) {
        proto_send_line(fd, reply);
    } else {
        send_error(fd, rc);
    }
}

/* ---------- per-client loop (runs in the child) ---------- */

static void handle_client(int fd, const char *db_path)
{
    TripDB db;
    if (db_open(&db, db_path) != RES_OK) {   /* opened AFTER fork() */
        send_error(fd, RES_ERR_IO);
        proto_send_line(fd, "END");
        return;
    }

    char line[MAX_LINE];
    for (;;) {
        ssize_t n = proto_read_line(fd, line, sizeof line);
        if (n == -2) {
            send_error(fd, RES_ERR_INVALID);
            proto_send_line(fd, "END");
            printf("[%d] line too long, closing connection\n", (int)getpid());
            break;
        }
        if (n <= 0) {
            break;   /* client disconnected or read error */
        }
        if (line[0] == '\0') {
            continue;
        }

        printf("[%d] %s\n", (int)getpid(), line);
        fflush(stdout);

        Command cmd;
        if (parse_command(line, &cmd) != RES_OK) {
            send_error(fd, RES_ERR_INVALID);
            proto_send_line(fd, "END");
            continue;
        }

        int quit = 0;
        switch (cmd.type) {
        case CMD_LIST:    do_list(fd, &db);          break;
        case CMD_SHOW:    do_show(fd, &db, &cmd);    break;
        case CMD_ADDTRIP: do_addtrip(fd, &db, &cmd); break;
        case CMD_DELTRIP: do_deltrip(fd, &db, &cmd); break;
        case CMD_HOLD:
        case CMD_CONFIRM:
        case CMD_CANCEL:
        case CMD_WAITLIST: do_booking(fd, &db, &cmd); break;
        case CMD_QUIT:
            proto_send_line(fd, "OK BYE");
            quit = 1;
            break;
        }
        if (proto_send_line(fd, "END") < 0 || quit) {
            break;
        }
    }

    db_close(&db);
}

/* ---------- main ---------- */

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    const char *db_path = DEFAULT_DB;

    if (argc > 1) {
        char *end;
        long p = strtol(argv[1], &end, 10);
        if (*end != '\0' || p < 1 || p > 65535) {
            fprintf(stderr, "usage: %s [port] [database]\n", argv[0]);
            return 1;
        }
        port = (int)p;
    }
    if (argc > 2) {
        db_path = argv[2];
    }

    if (install_signals() < 0) {
        perror("sigaction");
        return 1;
    }

    /* Put the server and its children in their own process group, so the
       kill(0, SIGTERM) at shutdown reaches only them. Already true when
       started from a terminal; harmless if it fails. */
    setpgid(0, 0);

    /* Make sure the database can be created/opened before accepting anyone */
    TripDB check;
    if (db_open(&check, db_path) != RES_OK) {
        fprintf(stderr, "cannot open database %s\n", db_path);
        return 1;
    }
    db_close(&check);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("setsockopt SO_REUSEADDR");   /* not fatal: restart may need a short wait */
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(listen_fd, 16) < 0) {
        perror("bind/listen");
        close(listen_fd);
        return 1;
    }

    printf("Server listening on port %d, database %s (Ctrl+C to stop)\n", port, db_path);
    fflush(stdout);

    while (!g_stop) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED || errno == EPROTO ||
                errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;   /* signal or one bad connection: keep serving */
            }
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) {
                perror("accept (out of resources, retrying)");
                sleep(1);   /* give finished children time to free descriptors */
                continue;
            }
            perror("accept");
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_fd);
            continue;
        }
        if (pid == 0) {                 /* child */
            /* Ctrl+C / kill must end children too, not just the parent */
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            close(listen_fd);

            /* Drop clients that go idle or stop reading our replies */
            struct timeval rcv = { IDLE_TIMEOUT, 0 };
            struct timeval snd = { SEND_TIMEOUT, 0 };
            if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv) < 0 ||
                setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd) < 0) {
                perror("setsockopt timeout");
            }
            printf("[%d] client connected\n", (int)getpid());
            fflush(stdout);
            handle_client(client_fd, db_path);
            close(client_fd);
            printf("[%d] client disconnected\n", (int)getpid());
            fflush(stdout);
            _exit(0);
        }
        close(client_fd);               /* parent: child owns this socket now */
    }

    close(listen_fd);
    printf("\nServer shutting down\n");

    /* Stop any children still serving clients, then reap them.
       The parent gets this SIGTERM too, but on_stop just sets g_stop again. */
    kill(0, SIGTERM);
    while (wait(NULL) > 0) {
    }
    return 0;
}

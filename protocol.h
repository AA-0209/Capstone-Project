#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <time.h>
#include <sys/types.h>
#include "common.h"

#define DEFAULT_PORT   5050
#define MAX_LINE       512

/*
 * Line-based text protocol. Client sends one command per line ('\n');
 * server replies with one or more lines, ending with a line "END".
 * Fields are separated by spaces, so names and cities cannot contain spaces.
 *
 *   LIST                                         -> trip ids + seats available
 *   SHOW     <trip>                              -> seat map
 *   ADDTRIP  <trip> <origin> <dest> <YYYY-MM-DDTHH:MM> <seats>  -> OK CREATED
 *   DELTRIP  <trip>                              -> OK DELETED
 *   HOLD     <trip> <seat> <name>                -> OK HELD <seat> <seconds>
 *   CONFIRM  <trip> <seat> <name>                -> OK BOOKED <seat>
 *   CANCEL   <trip> <seat> <name>                -> OK CANCELLED [PROMOTED <name>]
 *   WAITLIST <trip> <name>                       -> OK WAITLISTED <position>
 *   QUIT                                         -> closes connection
 *
 * Errors: "ERR <code> <message>" where <code> is the ResStatus number.
 */

typedef enum {
    CMD_LIST, CMD_SHOW, CMD_ADDTRIP, CMD_DELTRIP, CMD_HOLD,
    CMD_CONFIRM, CMD_CANCEL, CMD_WAITLIST, CMD_QUIT
} CommandType;

typedef struct {
    CommandType type;
    char   trip_id[TRIP_ID_LEN];
    char   name[NAME_LEN];
    char   origin[CITY_LEN];
    char   destination[CITY_LEN];
    time_t departure;
    int    seat_no;
    int    num_seats;
} Command;

/* Parses one line into cmd. RES_ERR_INVALID on unknown command,
   wrong argument count, bad numbers, or fields that are too long. */
ResStatus parse_command(const char *line, Command *cmd);

/* Reads up to '\n' (stripped) into buf.
   Returns length, 0 on EOF, -1 on I/O error, -2 if the line is too long
   (caller should close the connection). */
ssize_t proto_read_line(int fd, char *buf, size_t buf_len);

/* Sends the string plus '\n', handling partial writes, using MSG_NOSIGNAL
   so a closed client can't kill the process. 0 on success, -1 on error. */
int proto_send_line(int fd, const char *line);

#endif /* PROTOCOL_H */

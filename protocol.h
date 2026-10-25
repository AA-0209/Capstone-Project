#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <sys/types.h>

#define DEFAULT_PORT   5050
#define MAX_LINE       512

/*
 * Line-based text protocol. Client sends one command per line ('\n'),
 * server replies with one or more lines, ending with a line "END".
 *
 *   LIST                                 -> trip ids + seats available
 *   SHOW     <trip>                      -> seat map
 *   HOLD     <trip> <seat> <name>        -> OK HELD <seat> <seconds>
 *   CONFIRM  <trip> <seat> <name>        -> OK BOOKED <seat>
 *   CANCEL   <trip> <seat> <name>        -> OK CANCELLED [PROMOTED <name>]
 *   WAITLIST <trip> <name>               -> OK WAITLISTED <position>
 *   QUIT                                 -> closes connection
 *
 * Errors: "ERR <code> <message>" where <code> matches ResStatus.
 */

/* Reads up to '\n' (stripped) into buf; returns length, 0 on EOF, -1 on error.
   Lines longer than buf_len-1 are rejected with -1 (bounds check). */
ssize_t proto_read_line(int fd, char *buf, size_t buf_len);

/* Sends the full string plus '\n', handling partial writes. 0 on success, -1 on error. */
int proto_send_line(int fd, const char *line);

#endif /* PROTOCOL_H */
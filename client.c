/*
 * Bus reservation client.
 *
 *   ./client [host] [port]      defaults: 127.0.0.1 5050
 *
 * Reads commands from the keyboard, sends each one to the server,
 * and prints the reply lines until the server sends "END".
 */
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include "protocol.h"

static void print_help(void)
{
    puts("Commands:");
    puts("  LIST                                        list all trips");
    puts("  SHOW     <trip>                             seat map for a trip");
    puts("  ADDTRIP  <trip> <from> <to> <YYYY-MM-DDTHH:MM> <seats>");
    puts("  DELTRIP  <trip>                             delete a trip");
    puts("  HOLD     <trip> <seat> <name>               hold a seat for 2 minutes");
    puts("  CONFIRM  <trip> <seat> <name>               book a held seat");
    puts("  CANCEL   <trip> <seat> <name>               cancel a hold or booking");
    puts("  WAITLIST <trip> <name>                      join the waitlist (full trips)");
    puts("  HELP                                        show this list");
    puts("  QUIT                                        disconnect");
    puts("Names and cities cannot contain spaces.");
}

/* Connects to host:port. Returns a socket fd, or -1. */
static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints, *res, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "cannot resolve %s: %s\n", host, gai_strerror(err));
        return -1;
    }

    int fd = -1;
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Prints reply lines until "END". Returns 0, or -1 if the server went away. */
static int read_reply(int fd)
{
    char line[MAX_LINE];
    for (;;) {
        ssize_t n = proto_read_line(fd, line, sizeof line);
        if (n <= 0) {
            return -1;
        }
        if (strcmp(line, "END") == 0) {
            return 0;
        }
        puts(line);
    }
}

int main(int argc, char *argv[])
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    char default_port[8];
    snprintf(default_port, sizeof default_port, "%d", DEFAULT_PORT);
    const char *port = argc > 2 ? argv[2] : default_port;

    int fd = connect_to(host, port);
    if (fd < 0) {
        fprintf(stderr, "could not connect to %s:%s (is the server running?)\n", host, port);
        return 1;
    }
    printf("Connected to %s:%s. Type HELP for commands.\n", host, port);

    char input[MAX_LINE];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (fgets(input, sizeof input, stdin) == NULL) {
            break;   /* Ctrl+D or end of piped input */
        }

        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[--len] = '\0';
        } else if (len == sizeof input - 1) {
            /* Line didn't fit: throw away the rest so it isn't sent later */
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
            puts("Line too long, ignored.");
            continue;
        }
        if (len == 0) {
            continue;
        }
        if (strcasecmp(input, "HELP") == 0) {
            print_help();
            continue;
        }

        if (proto_send_line(fd, input) < 0 || read_reply(fd) < 0) {
            puts("Server closed the connection.");
            break;
        }
        if (strcasecmp(input, "QUIT") == 0) {
            break;
        }
    }

    close(fd);
    return 0;
}

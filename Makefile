# Bus Reservation System — INET 3101 Capstone
#
#   make          build ./server and ./client
#   make objs     compile only the modules written so far (no linking)
#   make clean    remove build output

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDLIBS  = -lgdbm

# macOS: unhide BSD functions (flock) and find Homebrew's gdbm
ifeq ($(shell uname),Darwin)
    BREW    := $(shell brew --prefix 2>/dev/null)
    CFLAGS  += -D_DARWIN_C_SOURCE -I$(BREW)/include
    LDFLAGS += -L$(BREW)/lib
endif

SHARED_OBJS = seatmap.o waitlist.o trip.o db.o protocol.o
SERVER_OBJS = server.o $(SHARED_OBJS)
CLIENT_OBJS = client.o protocol.o

all: server client

server: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

client: $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Modules finished so far; extended as each step is completed
objs: seatmap.o waitlist.o trip.o db.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies: a change to a header rebuilds the files that use it
seatmap.o:  seatmap.c seatmap.h common.h
waitlist.o: waitlist.c waitlist.h common.h
trip.o:     trip.c trip.h seatmap.h waitlist.h common.h
db.o:       db.c db.h trip.h seatmap.h waitlist.h common.h
protocol.o: protocol.c protocol.h common.h
server.o:   server.c db.h trip.h seatmap.h waitlist.h protocol.h common.h
client.o:   client.c protocol.h common.h

clean:
	rm -f *.o server client

.PHONY: all objs clean

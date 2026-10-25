#ifndef TRIP_H
#define TRIP_H

#include <stddef.h>
#include <time.h>
#include "seatmap.h"

#define TRIP_ID_LEN    16
#define CITY_LEN       32
#define WAITLIST_MAX   20

/* Circular queue stored inline (no pointers) so it persists with the trip. */
typedef struct {
    char names[WAITLIST_MAX][NAME_LEN];
    int  head;    /* index of the oldest entry */
    int  count;   /* number of entries */
} Waitlist;

typedef struct {
    char     id[TRIP_ID_LEN];        /* e.g. "MSP-DUL-0800" — database key */
    char     origin[CITY_LEN];
    char     destination[CITY_LEN];
    time_t   departure;
    SeatMap  seats;
    Waitlist waitlist;
} Trip;

ResStatus trip_init(Trip *trip, const char *id, const char *origin,
                    const char *destination, time_t departure, int num_seats);

/* Waitlist operations */
ResStatus waitlist_push(Waitlist *wl, const char *name);
ResStatus waitlist_pop(Waitlist *wl, char *out, size_t out_len);
int       waitlist_contains(const Waitlist *wl, const char *name);

/* Cancel a booking; if someone is waitlisted, they get the freed seat (HELD).
   promoted_out receives their name, or "" if nobody was waiting. */
ResStatus trip_cancel_and_promote(Trip *trip, int seat_no, const char *name,
                                  time_t now, char *promoted_out, size_t out_len);

/* Writes a human-readable summary into buf (for server replies / printing).
   Returns bytes written, or -1 if buf was too small. */
int trip_format(const Trip *trip, char *buf, size_t buf_len);

#endif /* TRIP_H */
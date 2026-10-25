#ifndef TRIP_H
#define TRIP_H

#include <stddef.h>
#include <time.h>
#include "common.h"
#include "seatmap.h"
#include "waitlist.h"

typedef struct {
    char     id[TRIP_ID_LEN];        /* database key */
    char     origin[CITY_LEN];
    char     destination[CITY_LEN];
    time_t   departure;
    SeatMap  seats;
    Waitlist waitlist;
} Trip;

/* Zeroes the whole struct first (no random padding bytes on disk),
   then validates and fills it. */
ResStatus trip_init(Trip *trip, const char *id, const char *origin,
                    const char *destination, time_t departure, int num_seats);

/*
 * Booking operations. The server calls these, never seatmap_* directly,
 * so the waitlist and departure rules are always applied.
 * All return RES_ERR_DEPARTED if now >= departure.
 */
ResStatus trip_hold(Trip *trip, int seat_no, const char *name, time_t now);
ResStatus trip_confirm(Trip *trip, int seat_no, const char *name, time_t now);

/* Rejects if seats are still free (RES_ERR_SEATS_OPEN) or the name already
   has a seat (RES_ERR_HAS_SEAT). position_out gets their 1-based spot. */
ResStatus trip_join_waitlist(Trip *trip, const char *name, time_t now,
                             int *position_out);

/* Frees the seat; if anyone is waiting, the first person is BOOKED into it
   immediately. promoted_out gets their name, or "" if nobody was waiting. */
ResStatus trip_cancel_and_promote(Trip *trip, int seat_no, const char *name,
                                  time_t now, char *promoted_out, size_t out_len);

/* Releases expired holds and gives each freed seat to the next waitlisted
   person. Run at the start of every locked operation. Returns seats freed. */
int trip_expire_holds(Trip *trip, time_t now);

/* Human-readable summary, one item per line, each line < MAX_LINE.
   Returns bytes written, or -1 if buf is too small. */
int trip_format(const Trip *trip, char *buf, size_t buf_len);

#endif /* TRIP_H */
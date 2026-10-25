#ifndef SEATMAP_H
#define SEATMAP_H

#include <time.h>
#include "common.h"

#define MAX_SEATS      56   /* typical coach bus */
#define HOLD_SECONDS   120  /* an unconfirmed hold is released after this */

typedef enum {
    SEAT_FREE   = 0,
    SEAT_HELD   = 1,
    SEAT_BOOKED = 2
} SeatStatus;

typedef struct {
    int        number;               /* 1-based seat number */
    SeatStatus status;
    char       passenger[NAME_LEN];  /* empty only when FREE */
    time_t     hold_expires;         /* only meaningful when HELD */
} Seat;

/*
 * Fixed-size array (no pointers) so the whole map is saved to the
 * database as one block. The struct is visible to other files for that
 * reason, but ONLY seatmap.c may change Seat fields; everyone else
 * reads them through seatmap_get().
 */
typedef struct {
    int  num_seats;                  /* 1..MAX_SEATS */
    Seat seats[MAX_SEATS];
} SeatMap;

ResStatus seatmap_init(SeatMap *map, int num_seats);

/* Lifecycle: FREE -> HELD -> BOOKED, or HELD/BOOKED -> FREE.
   Names must be non-empty and shorter than NAME_LEN. */
ResStatus seatmap_hold(SeatMap *map, int seat_no, const char *name, time_t now);
ResStatus seatmap_confirm(SeatMap *map, int seat_no, const char *name, time_t now);
ResStatus seatmap_release(SeatMap *map, int seat_no, const char *name);

/* FREE -> BOOKED directly; used when promoting someone from the waitlist */
ResStatus seatmap_assign(SeatMap *map, int seat_no, const char *name);

/*
 * Frees every HELD seat past hold_expires and writes their seat numbers
 * into released (up to max). Returns how many were freed.
 * Call trip_expire_holds() instead so freed seats go to the waitlist.
 */
int seatmap_expire_holds(SeatMap *map, time_t now, int *released, int max);

/* Reporting (all return -1 / NULL on bad input) */
int         seatmap_available(const SeatMap *map);              /* count of FREE seats */
int         seatmap_find_free(const SeatMap *map);              /* first free seat no */
int         seatmap_find_passenger(const SeatMap *map, const char *name); /* seat no, or -1 */
const Seat *seatmap_get(const SeatMap *map, int seat_no);

#endif /* SEATMAP_H */
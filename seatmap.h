#ifndef SEATMAP_H
#define SEATMAP_H

#include <stddef.h>
#include <time.h>

#define MAX_SEATS      56   /* typical coach bus */
#define NAME_LEN       32
#define HOLD_SECONDS   120  /* a held seat is released if not confirmed */

/* Shared result codes used across the project */
typedef enum {
    RES_OK = 0,
    RES_ERR_INVALID,     /* bad argument / out-of-range seat */
    RES_ERR_TAKEN,       /* seat already held or booked by someone else */
    RES_ERR_NOT_HELD,    /* confirm/release on a seat this passenger doesn't hold */
    RES_ERR_FULL,        /* no seats left / waitlist full */
    RES_ERR_NOT_FOUND,   /* trip or record missing */
    RES_ERR_EXISTS,      /* create on an existing key */
    RES_ERR_IO           /* file, db, or socket failure */
} ResStatus;

typedef enum {
    SEAT_FREE = 0,
    SEAT_HELD,
    SEAT_BOOKED
} SeatStatus;

typedef struct {
    int        number;               /* 1-based seat number */
    SeatStatus status;
    char       passenger[NAME_LEN];  /* empty when FREE */
    time_t     hold_expires;         /* only meaningful when HELD */
} Seat;

/* Fixed-size array (no pointers) so the whole map can be written
   to the database as one block. */
typedef struct {
    int  num_seats;                  /* 1..MAX_SEATS */
    Seat seats[MAX_SEATS];
} SeatMap;

ResStatus seatmap_init(SeatMap *map, int num_seats);

/* Lifecycle: FREE -> HELD -> BOOKED, or HELD/BOOKED -> FREE */
ResStatus seatmap_hold(SeatMap *map, int seat_no, const char *name, time_t now);
ResStatus seatmap_confirm(SeatMap *map, int seat_no, const char *name, time_t now);
ResStatus seatmap_release(SeatMap *map, int seat_no, const char *name);

/* Frees any HELD seat past its hold_expires; returns number released */
int seatmap_expire_holds(SeatMap *map, time_t now);

/* Reporting */
int  seatmap_available(const SeatMap *map);          /* count of FREE seats */
int  seatmap_find_free(const SeatMap *map);          /* first free seat no, or -1 */
const Seat *seatmap_get(const SeatMap *map, int seat_no);  /* NULL if out of range */

#endif /* SEATMAP_H */
#ifndef COMMON_H
#define COMMON_H

/* Size limits shared by every module */
#define NAME_LEN       32   /* passenger name incl. '\0'; no spaces allowed */
#define TRIP_ID_LEN    16   /* trip id incl. '\0', e.g. "MSP-DUL-0800" */
#define CITY_LEN       32

/*
 * Result codes used across the project.
 * Numbers are fixed because they are sent to clients ("ERR <code> ...").
 * Never reorder or reuse a number; only add new ones at the end.
 */
typedef enum {
    RES_OK                 = 0,
    RES_ERR_INVALID        = 1,   /* bad argument, empty/long name, seat out of range */
    RES_ERR_TAKEN          = 2,   /* seat held or booked by someone else */
    RES_ERR_NOT_HELD       = 3,   /* confirm/cancel on a seat this name doesn't have */
    RES_ERR_NO_SEATS       = 4,   /* trip is full */
    RES_ERR_WAITLIST_FULL  = 5,
    RES_ERR_NOT_FOUND      = 6,   /* trip id doesn't exist */
    RES_ERR_EXISTS         = 7,   /* duplicate trip id or duplicate waitlist name */
    RES_ERR_IO             = 8,   /* file, database, or socket failure */
    RES_ERR_DEPARTED       = 9,   /* trip has already left */
    RES_ERR_SEATS_OPEN     = 10,  /* can't waitlist while seats are free */
    RES_ERR_HAS_SEAT       = 11   /* passenger already holds/booked a seat on this trip */
} ResStatus;

#endif /* COMMON_H */

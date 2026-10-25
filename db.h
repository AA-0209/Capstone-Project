#ifndef DB_H
#define DB_H

#include "common.h"
#include "trip.h"

/*
 * Persistence layer: one GDBM file.
 *   key   = trip id INCLUDING its '\0'  (strlen(id) + 1, used everywhere)
 *   value = raw Trip struct; db_read rejects any record whose size
 *           isn't exactly sizeof(Trip) (RES_ERR_IO).
 *
 * Concurrency rules:
 *   - The server forks one child per client. Each child calls db_open()
 *     AFTER fork(), so no lock descriptor is shared between processes.
 *   - The CALLER must hold the lock around every CRUD call:
 *       reads  (LIST, SHOW)            -> db_lock(db, 0)  shared
 *       writes (HOLD, CONFIRM, CANCEL,
 *               WAITLIST, ADDTRIP,
 *               DELTRIP)                -> db_lock(db, 1)  exclusive
 *   - Write pattern: lock -> read -> trip_expire_holds -> change -> update -> unlock.
 *     Always unlock on error paths too.
 *   - The GDBM file is opened and closed inside each CRUD call.
 */

typedef struct {
    char path[256];      /* path to the .gdbm file */
    int  lock_fd;        /* fd of "<path>.lock", -1 when closed */
} TripDB;

ResStatus db_open(TripDB *db, const char *path);   /* creates files if missing */
void      db_close(TripDB *db);

ResStatus db_lock(TripDB *db, int exclusive);      /* blocks until acquired */
ResStatus db_unlock(TripDB *db);

/* CRUD (caller holds the lock) */
ResStatus db_create(TripDB *db, const Trip *trip);           /* RES_ERR_EXISTS if present */
ResStatus db_read(TripDB *db, const char *id, Trip *out);    /* RES_ERR_NOT_FOUND if absent */
ResStatus db_update(TripDB *db, const Trip *trip);           /* RES_ERR_NOT_FOUND if absent */
ResStatus db_delete(TripDB *db, const char *id);             /* RES_ERR_NOT_FOUND if absent */

/* Fills ids with up to max trip ids; returns count, or -1 on error */
int db_list(TripDB *db, char ids[][TRIP_ID_LEN], int max);

#endif /* DB_H */

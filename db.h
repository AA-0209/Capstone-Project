#ifndef DB_H
#define DB_H

#include <gdbm.h>
#include "trip.h"

/*
 * Persistence layer: one GDBM file, key = trip id, value = raw Trip struct.
 *
 * Concurrency: the server forks one child per client. Every read-modify-write
 * (hold, confirm, cancel) is wrapped in db_lock()/db_unlock(), which takes an
 * exclusive flock() on a separate lock file. Each operation opens and closes
 * the GDBM file inside the lock so no handle is shared across fork().
 */

typedef struct {
    char path[256];      /* path to the .gdbm file */
    int  lock_fd;        /* fd of "<path>.lock", -1 when closed */
} TripDB;

ResStatus db_open(TripDB *db, const char *path);   /* creates files if missing */
void      db_close(TripDB *db);

ResStatus db_lock(TripDB *db);     /* flock(LOCK_EX), blocks */
ResStatus db_unlock(TripDB *db);

/* CRUD */
ResStatus db_create(TripDB *db, const Trip *trip);                 /* RES_ERR_EXISTS if present */
ResStatus db_read(TripDB *db, const char *id, Trip *out);          /* RES_ERR_NOT_FOUND if absent */
ResStatus db_update(TripDB *db, const Trip *trip);                 /* RES_ERR_NOT_FOUND if absent */
ResStatus db_delete(TripDB *db, const char *id);

/* Fills ids with up to max trip ids; returns count, or -1 on error */
int db_list(TripDB *db, char ids[][TRIP_ID_LEN], int max);

#endif /* DB_H */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include <gdbm.h>
#include "db.h"

/* ---------- private helpers ---------- */

/* A valid id is non-empty and fits in TRIP_ID_LEN including '\0'. */
static int valid_id(const char *id)
{
    if (id == NULL) {
        return 0;
    }
    size_t len = strnlen(id, TRIP_ID_LEN);
    return len > 0 && len < TRIP_ID_LEN;
}

/* Builds the GDBM key for an id. The '\0' is included (strlen + 1)
   everywhere, so stored keys and lookup keys always match. */
static datum make_key(const char *id)
{
    datum key;
    key.dptr  = (char *)id;
    key.dsize = (int)strlen(id) + 1;
    return key;
}

/* Opens the GDBM file for one operation. GDBM_NOLOCK because our own
   flock on the .lock file already controls who can touch the database. */
static GDBM_FILE open_gdbm(const TripDB *db, int write)
{
    int mode = (write ? GDBM_WRITER : GDBM_READER) | GDBM_NOLOCK;
    return gdbm_open(db->path, 0, mode, 0644, NULL);
}

/*
 * A record from disk is not trusted just because its size matches.
 * Force every string to end in '\0' (so printing can't run past the
 * buffer), and check every number that is used as an array index.
 * Returns 1 if the record is usable, 0 if it is corrupt.
 */
static int sanitize_trip(Trip *t)
{
    t->id[TRIP_ID_LEN - 1] = '\0';
    t->origin[CITY_LEN - 1] = '\0';
    t->destination[CITY_LEN - 1] = '\0';

    if (t->seats.num_seats < 1 || t->seats.num_seats > MAX_SEATS) {
        return 0;
    }
    for (int i = 0; i < MAX_SEATS; i++) {
        Seat *s = &t->seats.seats[i];
        s->passenger[NAME_LEN - 1] = '\0';
        if (s->status != SEAT_FREE && s->status != SEAT_HELD && s->status != SEAT_BOOKED) {
            return 0;
        }
    }

    Waitlist *w = &t->waitlist;
    if (w->head < 0 || w->head >= WAITLIST_MAX || w->count < 0 || w->count > WAITLIST_MAX) {
        return 0;
    }
    for (int i = 0; i < WAITLIST_MAX; i++) {
        w->names[i][NAME_LEN - 1] = '\0';
    }
    return 1;
}

/* ---------- open / close / locking ---------- */

ResStatus db_open(TripDB *db, const char *path)
{
    if (db == NULL || path == NULL) {
        return RES_ERR_INVALID;
    }

    memset(db, 0, sizeof *db);
    db->lock_fd = -1;

    size_t len = strnlen(path, sizeof db->path);
    if (len == 0 || len >= sizeof db->path) {
        return RES_ERR_INVALID;
    }
    memcpy(db->path, path, len);

    /* Lock file sits next to the database: "<path>.lock" */
    char lock_path[sizeof db->path + 8];
    snprintf(lock_path, sizeof lock_path, "%s.lock", db->path);

    db->lock_fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (db->lock_fd < 0) {
        return RES_ERR_IO;
    }

    /* Create the database file if it doesn't exist yet (under the lock,
       so two processes starting at once can't both create it). */
    if (db_lock(db, 1) != RES_OK) {
        db_close(db);
        return RES_ERR_IO;
    }
    GDBM_FILE dbf = gdbm_open(db->path, 0, GDBM_WRCREAT | GDBM_NOLOCK, 0644, NULL);
    ResStatus rc = (dbf != NULL) ? RES_OK : RES_ERR_IO;
    if (dbf != NULL) {
        gdbm_close(dbf);
    }
    db_unlock(db);

    if (rc != RES_OK) {
        db_close(db);
    }
    return rc;
}

void db_close(TripDB *db)
{
    if (db != NULL && db->lock_fd >= 0) {
        close(db->lock_fd);   /* closing also releases any flock */
        db->lock_fd = -1;
    }
}

ResStatus db_lock(TripDB *db, int exclusive)
{
    if (db == NULL || db->lock_fd < 0) {
        return RES_ERR_INVALID;
    }
    int op = exclusive ? LOCK_EX : LOCK_SH;
    while (flock(db->lock_fd, op) != 0) {
        if (errno != EINTR) {   /* retry if a signal interrupted the wait */
            return RES_ERR_IO;
        }
    }
    return RES_OK;
}

ResStatus db_unlock(TripDB *db)
{
    if (db == NULL || db->lock_fd < 0) {
        return RES_ERR_INVALID;
    }
    return flock(db->lock_fd, LOCK_UN) == 0 ? RES_OK : RES_ERR_IO;
}

/* ---------- CRUD (caller holds the lock) ---------- */

ResStatus db_create(TripDB *db, const Trip *trip)
{
    if (db == NULL || trip == NULL || !valid_id(trip->id)) {
        return RES_ERR_INVALID;
    }

    GDBM_FILE dbf = open_gdbm(db, 1);
    if (dbf == NULL) {
        return RES_ERR_IO;
    }

    datum value;
    value.dptr  = (char *)trip;
    value.dsize = (int)sizeof *trip;

    /* GDBM_INSERT refuses to overwrite: returns 1 if the key exists */
    int rc = gdbm_store(dbf, make_key(trip->id), value, GDBM_INSERT);
    gdbm_close(dbf);

    if (rc == 1) {
        return RES_ERR_EXISTS;
    }
    return rc == 0 ? RES_OK : RES_ERR_IO;
}

ResStatus db_read(TripDB *db, const char *id, Trip *out)
{
    if (db == NULL || out == NULL || !valid_id(id)) {
        return RES_ERR_INVALID;
    }

    GDBM_FILE dbf = open_gdbm(db, 0);
    if (dbf == NULL) {
        return RES_ERR_IO;
    }

    datum value = gdbm_fetch(dbf, make_key(id));   /* value.dptr is malloc'd */
    gdbm_close(dbf);

    if (value.dptr == NULL) {
        return RES_ERR_NOT_FOUND;
    }

    ResStatus rc = RES_OK;
    if (value.dsize != (int)sizeof *out) {
        rc = RES_ERR_IO;   /* record from a different build: don't trust it */
    } else {
        memcpy(out, value.dptr, sizeof *out);
        if (!sanitize_trip(out)) {
            rc = RES_ERR_IO;   /* corrupt record: bad seat count, status, or queue index */
        }
    }
    free(value.dptr);
    return rc;
}

ResStatus db_update(TripDB *db, const Trip *trip)
{
    if (db == NULL || trip == NULL || !valid_id(trip->id)) {
        return RES_ERR_INVALID;
    }

    GDBM_FILE dbf = open_gdbm(db, 1);
    if (dbf == NULL) {
        return RES_ERR_IO;
    }

    datum key = make_key(trip->id);
    ResStatus rc;
    if (!gdbm_exists(dbf, key)) {
        rc = RES_ERR_NOT_FOUND;
    } else {
        datum value;
        value.dptr  = (char *)trip;
        value.dsize = (int)sizeof *trip;
        rc = gdbm_store(dbf, key, value, GDBM_REPLACE) == 0 ? RES_OK : RES_ERR_IO;
    }

    gdbm_close(dbf);
    return rc;
}

ResStatus db_delete(TripDB *db, const char *id)
{
    if (db == NULL || !valid_id(id)) {
        return RES_ERR_INVALID;
    }

    GDBM_FILE dbf = open_gdbm(db, 1);
    if (dbf == NULL) {
        return RES_ERR_IO;
    }

    datum key = make_key(id);
    ResStatus rc;
    if (!gdbm_exists(dbf, key)) {
        rc = RES_ERR_NOT_FOUND;
    } else {
        rc = gdbm_delete(dbf, key) == 0 ? RES_OK : RES_ERR_IO;
    }

    gdbm_close(dbf);
    return rc;
}

int db_list(TripDB *db, char ids[][TRIP_ID_LEN], int max)
{
    if (db == NULL || ids == NULL || max < 0) {
        return -1;
    }

    GDBM_FILE dbf = open_gdbm(db, 0);
    if (dbf == NULL) {
        return -1;
    }

    int count = 0;
    datum key = gdbm_firstkey(dbf);          /* each key.dptr is malloc'd */
    while (key.dptr != NULL) {
        /* Only accept keys shaped like ours: fits, and ends with '\0' */
        if (count < max && key.dsize > 1 && key.dsize <= TRIP_ID_LEN &&
            key.dptr[key.dsize - 1] == '\0') {
            memcpy(ids[count], key.dptr, (size_t)key.dsize);
            count++;
        }
        datum next = gdbm_nextkey(dbf, key);
        free(key.dptr);
        key = next;
    }

    gdbm_close(dbf);
    return count;
}

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "trip.h"

/* ---------- private helpers ---------- */

/* Copies src into dst (size bytes incl. '\0'). Returns 1 if src is
   non-empty and fits, 0 otherwise. dst is always left null-terminated. */
static int copy_field(char *dst, size_t size, const char *src)
{
    if (src == NULL) {
        return 0;
    }
    size_t len = strnlen(src, size);
    if (len == 0 || len >= size) {
        return 0;
    }
    memset(dst, 0, size);
    memcpy(dst, src, len);
    return 1;
}

static int departed(const Trip *trip, time_t now)
{
    return now >= trip->departure;
}

/*
 * Gives seat_no to the first waitlisted person who doesn't already have a
 * seat on this trip. Writes their name to promoted_out (or "" if nobody).
 * Returns 1 if someone was promoted, 0 otherwise.
 */
static int promote_into(Trip *trip, int seat_no, char *promoted_out, size_t out_len)
{
    char name[NAME_LEN];

    if (promoted_out != NULL && out_len > 0) {
        promoted_out[0] = '\0';
    }

    while (waitlist_pop(&trip->waitlist, name, sizeof name) == RES_OK) {
        if (seatmap_find_passenger(&trip->seats, name) != -1) {
            continue;   /* already got a seat another way; skip them */
        }
        if (seatmap_assign(&trip->seats, seat_no, name) != RES_OK) {
            return 0;
        }
        if (promoted_out != NULL && out_len > 0) {
            snprintf(promoted_out, out_len, "%s", name);
        }
        return 1;
    }
    return 0;
}

/* Appends formatted text to buf at *off. Returns 0, or -1 if it didn't fit. */
static int append(char *buf, size_t buf_len, size_t *off, const char *fmt, ...)
{
    if (*off >= buf_len) {
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, buf_len - *off, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= buf_len - *off) {
        return -1;
    }
    *off += (size_t)n;
    return 0;
}

/* ---------- public functions ---------- */

ResStatus trip_init(Trip *trip, const char *id, const char *origin,
                    const char *destination, time_t departure, int num_seats)
{
    if (trip == NULL) {
        return RES_ERR_INVALID;
    }

    memset(trip, 0, sizeof *trip);   /* no random padding bytes on disk */

    if (!copy_field(trip->id, sizeof trip->id, id) ||
        !copy_field(trip->origin, sizeof trip->origin, origin) ||
        !copy_field(trip->destination, sizeof trip->destination, destination)) {
        return RES_ERR_INVALID;
    }

    ResStatus rc = seatmap_init(&trip->seats, num_seats);
    if (rc != RES_OK) {
        return rc;
    }

    waitlist_init(&trip->waitlist);
    trip->departure = departure;
    return RES_OK;
}

ResStatus trip_hold(Trip *trip, int seat_no, const char *name, time_t now)
{
    if (trip == NULL) {
        return RES_ERR_INVALID;
    }
    if (departed(trip, now)) {
        return RES_ERR_DEPARTED;
    }
    if (seatmap_get(&trip->seats, seat_no) == NULL) {
        return RES_ERR_INVALID;    /* seat number out of range */
    }
    if (seatmap_find_passenger(&trip->seats, name) != -1) {
        return RES_ERR_HAS_SEAT;   /* one seat per passenger per trip */
    }
    if (seatmap_available(&trip->seats) == 0) {
        return RES_ERR_NO_SEATS;
    }
    return seatmap_hold(&trip->seats, seat_no, name, now);
}

ResStatus trip_confirm(Trip *trip, int seat_no, const char *name, time_t now)
{
    if (trip == NULL) {
        return RES_ERR_INVALID;
    }
    if (departed(trip, now)) {
        return RES_ERR_DEPARTED;
    }
    return seatmap_confirm(&trip->seats, seat_no, name, now);
}

ResStatus trip_join_waitlist(Trip *trip, const char *name, time_t now,
                             int *position_out)
{
    if (trip == NULL) {
        return RES_ERR_INVALID;
    }
    if (departed(trip, now)) {
        return RES_ERR_DEPARTED;
    }
    if (seatmap_available(&trip->seats) > 0) {
        return RES_ERR_SEATS_OPEN;
    }
    if (seatmap_find_passenger(&trip->seats, name) != -1) {
        return RES_ERR_HAS_SEAT;
    }

    ResStatus rc = waitlist_push(&trip->waitlist, name);
    if (rc != RES_OK) {
        return rc;
    }
    if (position_out != NULL) {
        *position_out = waitlist_position(&trip->waitlist, name);
    }
    return RES_OK;
}

ResStatus trip_cancel_and_promote(Trip *trip, int seat_no, const char *name,
                                  time_t now, char *promoted_out, size_t out_len)
{
    if (promoted_out != NULL && out_len > 0) {
        promoted_out[0] = '\0';
    }
    if (trip == NULL) {
        return RES_ERR_INVALID;
    }
    if (departed(trip, now)) {
        return RES_ERR_DEPARTED;
    }

    ResStatus rc = seatmap_release(&trip->seats, seat_no, name);
    if (rc != RES_OK) {
        return rc;
    }

    promote_into(trip, seat_no, promoted_out, out_len);
    return RES_OK;
}

int trip_expire_holds(Trip *trip, time_t now)
{
    if (trip == NULL) {
        return 0;
    }

    int released[MAX_SEATS];
    int n = seatmap_expire_holds(&trip->seats, now, released, MAX_SEATS);

    for (int i = 0; i < n && i < MAX_SEATS; i++) {
        promote_into(trip, released[i], NULL, 0);
    }
    return n;
}

int trip_format(const Trip *trip, char *buf, size_t buf_len)
{
    if (trip == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }

    char when[32] = "unknown";
    struct tm tm_dep;
    if (localtime_r(&trip->departure, &tm_dep) != NULL) {
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm_dep);
    }

    size_t off = 0;
    buf[0] = '\0';

    if (append(buf, buf_len, &off, "TRIP %s %s -> %s departs %s\n",
               trip->id, trip->origin, trip->destination, when) < 0 ||
        append(buf, buf_len, &off, "AVAILABLE %d/%d WAITLIST %d\n",
               seatmap_available(&trip->seats), trip->seats.num_seats,
               waitlist_count(&trip->waitlist)) < 0) {
        return -1;
    }

    static const char *labels[] = { "FREE", "HELD", "BOOKED" };
    for (int s = 1; s <= trip->seats.num_seats; s++) {
        const Seat *seat = seatmap_get(&trip->seats, s);
        if (append(buf, buf_len, &off, "SEAT %d %s %s\n", seat->number,
                   labels[seat->status], seat->passenger) < 0) {
            return -1;
        }
    }
    return (int)off;
}

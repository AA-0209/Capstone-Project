#include <string.h>
#include "seatmap.h"

/* ---------- private helpers ---------- */

/* A valid name is non-empty and fits in NAME_LEN including '\0'. */
static int valid_name(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    size_t len = strnlen(name, NAME_LEN);
    return len > 0 && len < NAME_LEN;
}

/* Returns a pointer to the seat, or NULL if seat_no is out of range. */
static Seat *seat_at(SeatMap *map, int seat_no)
{
    if (map == NULL || seat_no < 1 || seat_no > map->num_seats) {
        return NULL;
    }
    return &map->seats[seat_no - 1];
}

/* Puts a seat back to FREE and wipes the passenger name. */
static void clear_seat(Seat *seat)
{
    seat->status = SEAT_FREE;
    seat->hold_expires = 0;
    memset(seat->passenger, 0, sizeof seat->passenger);
}

/* Copies a name that has already passed valid_name(). */
static void set_name(Seat *seat, const char *name)
{
    memset(seat->passenger, 0, sizeof seat->passenger);
    memcpy(seat->passenger, name, strlen(name));
}

/* ---------- public functions ---------- */

ResStatus seatmap_init(SeatMap *map, int num_seats)
{
    if (map == NULL || num_seats < 1 || num_seats > MAX_SEATS) {
        return RES_ERR_INVALID;
    }

    memset(map, 0, sizeof *map);
    map->num_seats = num_seats;
    for (int i = 0; i < num_seats; i++) {
        map->seats[i].number = i + 1;
        map->seats[i].status = SEAT_FREE;
    }
    return RES_OK;
}

ResStatus seatmap_hold(SeatMap *map, int seat_no, const char *name, time_t now)
{
    Seat *seat = seat_at(map, seat_no);
    if (seat == NULL || !valid_name(name)) {
        return RES_ERR_INVALID;
    }
    if (seat->status != SEAT_FREE) {
        return RES_ERR_TAKEN;
    }

    seat->status = SEAT_HELD;
    seat->hold_expires = now + HOLD_SECONDS;
    set_name(seat, name);
    return RES_OK;
}

ResStatus seatmap_confirm(SeatMap *map, int seat_no, const char *name, time_t now)
{
    Seat *seat = seat_at(map, seat_no);
    if (seat == NULL || !valid_name(name)) {
        return RES_ERR_INVALID;
    }
    /* Must be held, by this person, and not expired yet */
    if (seat->status != SEAT_HELD ||
        strcmp(seat->passenger, name) != 0 ||
        now >= seat->hold_expires) {
        return RES_ERR_NOT_HELD;
    }

    seat->status = SEAT_BOOKED;
    seat->hold_expires = 0;
    return RES_OK;
}

ResStatus seatmap_release(SeatMap *map, int seat_no, const char *name)
{
    Seat *seat = seat_at(map, seat_no);
    if (seat == NULL || !valid_name(name)) {
        return RES_ERR_INVALID;
    }
    if (seat->status == SEAT_FREE || strcmp(seat->passenger, name) != 0) {
        return RES_ERR_NOT_HELD;
    }

    clear_seat(seat);
    return RES_OK;
}

ResStatus seatmap_assign(SeatMap *map, int seat_no, const char *name)
{
    Seat *seat = seat_at(map, seat_no);
    if (seat == NULL || !valid_name(name)) {
        return RES_ERR_INVALID;
    }
    if (seat->status != SEAT_FREE) {
        return RES_ERR_TAKEN;
    }

    seat->status = SEAT_BOOKED;
    seat->hold_expires = 0;
    set_name(seat, name);
    return RES_OK;
}

int seatmap_expire_holds(SeatMap *map, time_t now, int *released, int max)
{
    if (map == NULL) {
        return 0;
    }

    int freed = 0;
    for (int i = 0; i < map->num_seats; i++) {
        Seat *seat = &map->seats[i];
        if (seat->status == SEAT_HELD && now >= seat->hold_expires) {
            clear_seat(seat);
            if (released != NULL && freed < max) {
                released[freed] = seat->number;
            }
            freed++;
        }
    }
    return freed;
}

int seatmap_available(const SeatMap *map)
{
    if (map == NULL) {
        return -1;
    }

    int count = 0;
    for (int i = 0; i < map->num_seats; i++) {
        if (map->seats[i].status == SEAT_FREE) {
            count++;
        }
    }
    return count;
}

int seatmap_find_free(const SeatMap *map)
{
    if (map == NULL) {
        return -1;
    }

    for (int i = 0; i < map->num_seats; i++) {
        if (map->seats[i].status == SEAT_FREE) {
            return map->seats[i].number;
        }
    }
    return -1;
}

int seatmap_find_passenger(const SeatMap *map, const char *name)
{
    if (map == NULL || !valid_name(name)) {
        return -1;
    }

    for (int i = 0; i < map->num_seats; i++) {
        const Seat *seat = &map->seats[i];
        if (seat->status != SEAT_FREE && strcmp(seat->passenger, name) == 0) {
            return seat->number;
        }
    }
    return -1;
}

const Seat *seatmap_get(const SeatMap *map, int seat_no)
{
    if (map == NULL || seat_no < 1 || seat_no > map->num_seats) {
        return NULL;
    }
    return &map->seats[seat_no - 1];
}
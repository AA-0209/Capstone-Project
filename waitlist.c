#include <string.h>
#include "waitlist.h"

/* Converts "i-th person in line" into the actual array index. */
static int slot(const Waitlist *wl, int i)
{
    return (wl->head + i) % WAITLIST_MAX;
}

void waitlist_init(Waitlist *wl)
{
    if (wl != NULL) {
        memset(wl, 0, sizeof *wl);
    }
}

ResStatus waitlist_push(Waitlist *wl, const char *name)
{
    if (wl == NULL || name == NULL) {
        return RES_ERR_INVALID;
    }
    size_t len = strnlen(name, NAME_LEN);
    if (len == 0 || len >= NAME_LEN) {
        return RES_ERR_INVALID;
    }
    if (waitlist_contains(wl, name)) {
        return RES_ERR_EXISTS;
    }
    if (wl->count >= WAITLIST_MAX) {
        return RES_ERR_WAITLIST_FULL;
    }

    char *dest = wl->names[slot(wl, wl->count)];
    memset(dest, 0, NAME_LEN);
    memcpy(dest, name, len);
    wl->count++;
    return RES_OK;
}

ResStatus waitlist_pop(Waitlist *wl, char *out, size_t out_len)
{
    if (wl == NULL || out == NULL || out_len == 0) {
        return RES_ERR_INVALID;
    }
    if (wl->count == 0) {
        return RES_ERR_NOT_FOUND;
    }

    char *front = wl->names[wl->head];
    size_t len = strnlen(front, NAME_LEN);
    if (len >= out_len) {
        return RES_ERR_INVALID;   /* caller's buffer is too small */
    }

    memcpy(out, front, len);
    out[len] = '\0';
    memset(front, 0, NAME_LEN);

    wl->head = (wl->head + 1) % WAITLIST_MAX;
    wl->count--;
    return RES_OK;
}

int waitlist_position(const Waitlist *wl, const char *name)
{
    if (wl == NULL || name == NULL) {
        return -1;
    }
    for (int i = 0; i < wl->count; i++) {
        if (strncmp(wl->names[slot(wl, i)], name, NAME_LEN) == 0) {
            return i + 1;
        }
    }
    return -1;
}

int waitlist_contains(const Waitlist *wl, const char *name)
{
    return waitlist_position(wl, name) != -1;
}

int waitlist_count(const Waitlist *wl)
{
    return wl == NULL ? 0 : wl->count;
}
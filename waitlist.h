#ifndef WAITLIST_H
#define WAITLIST_H

#include <stddef.h>
#include "common.h"

#define WAITLIST_MAX   20

/* Circular queue stored inline (no pointers) so it is saved with the trip. */
typedef struct {
    char names[WAITLIST_MAX][NAME_LEN];
    int  head;    /* index of the oldest entry */
    int  count;   /* number of entries */
} Waitlist;

void waitlist_init(Waitlist *wl);

/* Adds name to the back. Returns RES_ERR_INVALID (empty/too long),
   RES_ERR_EXISTS (already waiting), or RES_ERR_WAITLIST_FULL. */
ResStatus waitlist_push(Waitlist *wl, const char *name);

/* Removes the front name into out. RES_ERR_NOT_FOUND if empty. */
ResStatus waitlist_pop(Waitlist *wl, char *out, size_t out_len);

int waitlist_contains(const Waitlist *wl, const char *name);  /* 1 or 0 */
int waitlist_position(const Waitlist *wl, const char *name);  /* 1-based, or -1 */
int waitlist_count(const Waitlist *wl);

#endif /* WAITLIST_H */

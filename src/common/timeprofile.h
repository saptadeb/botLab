#ifndef __TIMEPROFILE_H__
#define __TIMEPROFILE_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "zarray.h"

#ifdef __cplusplus
extern "C" {
#endif

struct timeprofile_entry {
    char name[32];
    int64_t utime;
};

typedef struct timeprofile timeprofile_t;
struct timeprofile {
    int64_t utime;
    zarray_t *stamps;
};

static inline int64_t
utime_now (void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}

static inline timeprofile_t *
timeprofile_create (void)
{
    timeprofile_t *tp = calloc (1, sizeof(*tp));
    tp->stamps = zarray_create (sizeof(struct timeprofile_entry));
    tp->utime = utime_now ();

    return tp;
}

static inline void
timeprofile_destroy (timeprofile_t *tp)
{
    zarray_destroy (tp->stamps);
    free (tp);
}

static inline void
timeprofile_clear (timeprofile_t *tp)
{
    zarray_clear (tp->stamps);
    tp->utime = utime_now ();
}

static inline void
timeprofile_stamp (timeprofile_t *tp, const char *name)
{
    struct timeprofile_entry tpe;

    strncpy (tpe.name, name, sizeof(tpe.name));
    tpe.name[sizeof(tpe.name)-1] = 0;
    tpe.utime = utime_now ();

    zarray_add (tp->stamps, &tpe);
}

static inline void
timeprofile_display (timeprofile_t *tp)
{
    int64_t lastutime = tp->utime;

    for (int i = 0; i < zarray_size (tp->stamps); i++) {
        struct timeprofile_entry *stamp;

        zarray_get_volatile (tp->stamps, i, &stamp);

        double cumtime = (stamp->utime - tp->utime)/1000000.0;

        double parttime = (stamp->utime - lastutime)/1000000.0;

        printf ("%2d %32s %15f ms %15f ms\n",
                i, stamp->name, parttime*1000, cumtime*1000);

        lastutime = stamp->utime;
    }
}

static inline uint64_t
timeprofile_total_utime (timeprofile_t *tp)
{
    if (0==zarray_size (tp->stamps))
        return 0;

    struct timeprofile_entry *first, *last;
    zarray_get_volatile (tp->stamps, 0, &first);
    zarray_get_volatile (tp->stamps, zarray_size(tp->stamps) - 1, &last);

    return last->utime - first->utime;
}

#ifdef __cplusplus
}
#endif

#endif //__TIMEPROFILE_H__

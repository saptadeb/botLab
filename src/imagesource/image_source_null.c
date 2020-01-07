#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <dirent.h>

#include "common/string_util.h"
#include "common/url_parser.h"
#include "common/zarray.h"

#include "image_source.h"
#include "pnm.h"

#define IMPL_TYPE 0x3325a716

struct null_format
{
    int width, height;
    char format[32];
};

typedef struct impl_null impl_null_t;
struct impl_null {
    // computed at instantiation time
    zarray_t *formats;
    int fidx;
    int type;

    float fps;
    uint64_t last_frame_utime;
};

static void
fill_solid (struct null_format *nf, image_source_data_t *isd)
{
    if (0==strcmp (nf->format, "GRAY8"))
        isd->datalen = nf->width * nf->height;
    else if (0==strcmp (nf->format, "RGB"))
        isd->datalen = nf->width * nf->height * 3;
    else
        assert (0);
    isd->data = malloc (isd->datalen);
    memset (isd->data, 128, isd->datalen);
}

static void
fill_random (struct null_format *nf, image_source_data_t *isd)
{
    if (0==strcmp (nf->format, "GRAY8"))
        isd->datalen = nf->width * nf->height;
    else if (0==strcmp (nf->format, "RGB"))
        isd->datalen = nf->width * nf->height * 3;
    else
        assert (0);

    isd->data = malloc (isd->datalen);
    for (int i = 0; i < isd->datalen; i++) {
        ((uint8_t*) isd->data)[i] = random() & 0xff;
    }
}

static int64_t
utime_now (void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}

static int
num_formats (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_null_t *impl = (impl_null_t*) isrc->impl;

    return zarray_size (impl->formats);
}

static void
get_format (image_source_t *isrc, int idx, image_source_format_t *fmt)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_null_t *impl = (impl_null_t*) isrc->impl;

    memset (fmt, 0, sizeof(*fmt));

    struct null_format *nf;
    zarray_get (impl->formats, idx, &nf);

    fmt->width = nf->width;
    fmt->height = nf->height;
    strcpy (fmt->format, nf->format);
}

static int
get_current_format (image_source_t *isrc)
{
    return 0;
}

static int
set_format (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_null_t *impl = (impl_null_t*) isrc->impl;

    impl->fidx = idx;

    return 0;
}

static int
set_named_format (image_source_t *isrc, const char *desired_format)
{
    assert (isrc->impl_type == IMPL_TYPE);

    printf ("not implemented\n");
    assert (0);

    return 0;
}

static int
num_features (image_source_t *isrc)
{
    return 2;
}

static const char *
get_feature_name (image_source_t *isrc, int idx)
{
    switch (idx) {
        case 0:
            return "fps";
        case 1:
            return "type";
    }

    assert (0);
    return NULL;
}

static char *
get_feature_type (image_source_t *isrc, int idx)
{
    switch (idx) {
        case 0: // fps
            return strdup ("f,.1,100");
        case 1: //type
            return strdup ("c,0=solid,1=random,");
    }

    return NULL;
}

static double
get_feature_value (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_null_t *impl = (impl_null_t*) isrc->impl;

    switch (idx){
        case 0:
            return impl->fps;
        case 1:
            return impl->type;
        default:
            return 0;
    }
}

static int
set_feature_value (image_source_t *isrc, int idx, double v)
{
    impl_null_t *impl = (impl_null_t*) isrc->impl;

    switch (idx)  {
        case 0: {
            if (v != 0)
                v = fmax(0.1, v);
            impl->fps = v;
            break;
        }
        case 1: {
            impl->type = v;
            break;
        }

        default:
            return 0;
    }

    return 0;
}

static int
start (image_source_t *isrc)
{
    return 0;
}

static int
get_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_null_t *impl = (impl_null_t*) isrc->impl;

    memset (frmd, 0, sizeof(*frmd));

    struct null_format *nf;
    zarray_get (impl->formats, impl->fidx, &nf);

    frmd->ifmt.width = nf->width;
    frmd->ifmt.height = nf->height;
    strcpy (frmd->ifmt.format, nf->format);

    switch (impl->type) {
        case 0: {
            fill_solid (nf, frmd);
            break;
        }
        case 1: {
            fill_random (nf, frmd);
            break;
        }
    }

    int64_t utime = utime_now ();

    int64_t goal_utime;

    // use fps.
    int64_t goal_delta_utime = (uint64_t) (1000000 / impl->fps);
    goal_utime = impl->last_frame_utime + goal_delta_utime;

    int64_t should_delay = goal_utime - utime;

    if (should_delay > 0 && impl->last_frame_utime != 0) {
        if (should_delay > 2000000) {
            printf ("image_source_filedir: suspiciously long delay (wrong timescale?). Shortening it.\n");
            should_delay = 2000000;
        }

        usleep (should_delay);
    }

    impl->last_frame_utime = utime;
    return 0;
}

static int
release_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    free (frmd->data);

    return 0;
}

static int
stop (image_source_t *isrc)
{
    return 0;
}

static int
my_close (image_source_t *isrc)
{
    return 0;
}

static void
print_info (image_source_t *isrc)
{
}

image_source_t *
image_source_null_open (url_parser_t *urlp)
{
    image_source_t *isrc = calloc (1, sizeof(*isrc));
    isrc->impl_type = IMPL_TYPE;

    impl_null_t *impl = calloc (1, sizeof(*impl));
    isrc->impl = impl;

    impl->formats = zarray_create (sizeof(struct null_format*));

    // fill in fmt. (These will be overwritten later.)
    if (1) {
        struct null_format *nf = calloc (1, sizeof(*nf));
        nf->width = 640;
        nf->height = 480;
        strcpy (nf->format, "GRAY8");
        zarray_add (impl->formats, &nf);
    }

    if (1) {
        struct null_format *nf = calloc (1, sizeof(*nf));
        nf->width = 640;
        nf->height = 480;
        strcpy (nf->format, "RGB");
        zarray_add (impl->formats, &nf);
    }

    impl->fps = 10;

    isrc->num_formats = num_formats;
    isrc->get_format = get_format;
    isrc->get_current_format = get_current_format;
    isrc->set_format = set_format;
    isrc->set_named_format = set_named_format;
    isrc->num_features = num_features;
    isrc->get_feature_name = get_feature_name;
    isrc->get_feature_type = get_feature_type;
    isrc->get_feature_value = get_feature_value;
    isrc->set_feature_value = set_feature_value;
    isrc->start = start;
    isrc->get_frame = get_frame;
    isrc->release_frame = release_frame;
    isrc->stop = stop;
    isrc->close = my_close;
    isrc->print_info = print_info;

    return isrc;
}

void
image_source_enumerate_null (zarray_t *urls)
{
    char *p = strdup ("null://");
    zarray_add (urls, &p);
}

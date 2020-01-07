#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "common/string_util.h"
#include "common/url_parser.h"

/** File format: just a list of records that look like this:
    (big endian)

    uint64_t MAGIC;
    uint64_t utime;
    uint32_t width;
    uint32_t height;
    uint32_t fmtlen;
    char format[fmtlen];
    uint32_t buflen;
    char buf[buflen];

    Note that the format should remain constant during decode. Note
    that this implementation returns the format of the next frame when
    get_format is called (which helps in this case...)
**/

#include "image_source.h"

#define IMPL_TYPE 0x571118ea

#define MAGIC 0x17923349ab10ea9aUL

typedef struct islog_frame islog_frame_t;
struct islog_frame {
    uint64_t utime;
    uint32_t width, height;
    uint32_t fmtlen;
    char *format;
    uint32_t buflen;
    char *buf;
};

typedef struct impl_islog impl_islog_t;
struct impl_islog {
    FILE *f;

    islog_frame_t *last_frame, *next_frame;

    image_source_format_t *fmt;

    double fps; // when zero, use timing in the log
    int loop;

    uint64_t last_frame_utime;
};

static int64_t
utime_now(void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}

static int
read_u32 (FILE *f, uint32_t *v)
{
    uint8_t buf[4];

    int res = fread (buf, 1, 4, f);
    if (res != 4)
        return -1;

    *v = (buf[0]<<24) + (buf[1]<<16) + (buf[2]<<8) + buf[3];
    return 0;
}

static int
read_u64 (FILE *f, uint64_t *v)
{
    uint32_t h, l;

    if (read_u32 (f, &h) || read_u32 (f, &l))
        return -1;

    *v = (((uint64_t) h)<<32) + l;
    return 0;
}

static int
read_frame (image_source_t *isrc, islog_frame_t *frame)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    int pos = 0;

    // synchronize on MAGIC
    while (1) {
        int c;

        c = fgetc (impl->f);
        if (c==EOF)
            return -1;

        if (c == ((MAGIC >> (56 - pos*8)) & 0xff)) {
            pos++;
            if (pos == 8)
                break;
            continue;
        }

        // restart sync.
        pos = 0;
        continue;
    }

    if (read_u64 (impl->f, &frame->utime))
        return -1;

    if (read_u32 (impl->f, &frame->width))
        return -1;

    if (read_u32 (impl->f, &frame->height))
        return -1;

    if (read_u32 (impl->f, &frame->fmtlen))
        return -1;

    frame->format = calloc (1, frame->fmtlen + 1);

    if (fread (frame->format, 1, frame->fmtlen, impl->f) != frame->fmtlen)
        return -1;

    if (read_u32 (impl->f, &frame->buflen))
        return -1;

    frame->buf = calloc (1, frame->buflen);

    if (fread (frame->buf, 1, frame->buflen, impl->f) != frame->buflen)
        return -1;

    return 0;
}

static int
num_formats (image_source_t *isrc)
{
    return 1;
}

static void
get_format (image_source_t *isrc, int idx, image_source_format_t *fmt)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    fmt->width = impl->next_frame->width;
    fmt->height = impl->next_frame->height;

    strcpy (fmt->format, impl->next_frame->format);
}

static int
get_current_format (image_source_t *isrc)
{
    return 0;
}

static int
set_format (image_source_t *isrc, int idx)
{
    assert (idx==0);

    return 0;
}

static int
set_named_format (image_source_t *isrc, const char *desired_format)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    if (0==strcmp (desired_format, impl->fmt->format))
        return 0;

    return -1;
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
            return "loop";
    }

    assert (0);
    return NULL;
}

static char *
get_feature_type (image_source_t *isrc, int idx)
{
    switch (idx) {
        case 0:
            return strdup ("f,0.1,100");
        case 1:
            return strdup ("b");
    }
    return NULL;
}

static double
get_feature_value (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    switch (idx)  {
        case 0:
            return impl->fps;
        case 1:
            return impl->loop;
        default:
            return 0;
    }
}

static int
set_feature_value (image_source_t *isrc, int idx, double v)
{
    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    switch (idx)  {
        case 0:
            if (v != 0)
                v = fmax(0.1, v);
            impl->fps = v;
            break;
        case 1:
            impl->loop = (int) v;
            break;
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
    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    memset(frmd, 0, sizeof(*frmd));

    islog_frame_t *new_frame = calloc (1, sizeof(*new_frame));

    int res = read_frame (isrc, new_frame);

    if (res) {
        if (impl->loop) {
            fseek (impl->f, 0, SEEK_SET);

            res = read_frame (isrc, new_frame);

            if (res)
                return res;
        }
        else {
            usleep (10000); // prevent a get_frame spin.
            free (new_frame);
            return res;
        }
    }

    // done with last_frame
    if (impl->last_frame != NULL) {
        free (impl->last_frame->format);
        free (impl->last_frame->buf);
        free (impl->last_frame);
    }

    impl->last_frame = impl->next_frame;
    impl->next_frame = new_frame;

    uint64_t utime = utime_now ();

    int64_t goal_delay = 0;

    if (impl->fps != 0)
        goal_delay = 1.0E6/impl->fps;
    else
        goal_delay = impl->next_frame->utime - impl->last_frame->utime;

    int64_t delay_so_far = utime - impl->last_frame_utime;
    int64_t should_delay = goal_delay - delay_so_far;

    if (should_delay > 0)
        usleep (should_delay);

    impl->last_frame_utime = utime_now ();

    frmd->data = impl->last_frame->buf;
    frmd->datalen = impl->last_frame->buflen;

    frmd->ifmt.width = new_frame->width;
    frmd->ifmt.height = new_frame->height;
    strcpy(frmd->ifmt.format, new_frame->format);

    frmd->utime = new_frame->utime;

    return 0;
}

static int
release_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    // free() calls occurs on subsequent call to get_frame();
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
//    assert(isrc->impl_type == IMPL_TYPE);
//    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    return 0;
}

static void
print_info (image_source_t *isrc)
{
    impl_islog_t *impl = (impl_islog_t *) isrc->impl;

    printf ("========================================\n");
    printf (" ISLog Info\n");
    printf ("========================================\n");
    printf ("\tFPS: %f\n", impl->fps);
    printf ("\tLoop: %d\n", impl->loop);
    printf ("\tWidth: %d\n", impl->fmt->width);
    printf ("\tHeight: %d\n", impl->fmt->height);
}

image_source_t *
image_source_islog_open (url_parser_t *urlp)
{
    const char *location = url_parser_get_path (urlp);

    image_source_t *isrc = calloc (1, sizeof(*isrc));
    impl_islog_t *impl = calloc (1, sizeof(*impl));
    impl->fmt = calloc (1, sizeof(image_source_format_t));

    isrc->impl_type = IMPL_TYPE;
    isrc->impl = impl;

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

    impl->loop = 1;
    impl->fps = 10;

    impl->f = fopen (location, "rb");
    if (impl->f == NULL)
        goto error;

    impl->next_frame = calloc (1, sizeof(islog_frame_t));
    int res = read_frame (isrc, impl->next_frame);
    if (res)
        goto error;

    return isrc;

error:
    if (impl->f != NULL)
        fclose (impl->f);

    free (impl->fmt);
    free (impl);
    free (isrc);

    return NULL;
}


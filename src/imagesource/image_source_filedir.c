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
#include <png.h>

#include "common/string_util.h"
#include "common/zarray.h"
#include "common/url_parser.h"

#include "pnm.h"
#include "image_source.h"

#define IMPL_TYPE 0x16827172

typedef struct impl_filedir impl_filedir_t;
struct impl_filedir {
    // computed at instantiation time
    int width, height;
    char format[32];

    float fps;
    int timescale;
    int loop;

    zarray_t *files;
    int pos;

    uint64_t last_frame_utime;
};

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
    return 1;
}

static void
get_format (image_source_t *isrc, int idx, image_source_format_t *fmt)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_filedir_t *impl = (impl_filedir_t*) isrc->impl;

    memset (fmt, 0, sizeof(*fmt));
    fmt->width = impl->width;
    fmt->height = impl->height;
    strcpy (fmt->format, impl->format);
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
    return 0;
}

static int
num_features (image_source_t *isrc)
{
    return 3;
}

static const char *
get_feature_name (image_source_t *isrc, int idx)
{
    switch(idx) {
        case 0:
            return "fps";
        case 1:
            return "timescale";
        case 2:
            return "loop";
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
        case 1: // timescale
            return strdup ("c,1000000=s,1000=ms,1=us,0=fps");
        case 2: // loop
            return strdup ("b");
    }
    return NULL;
}

static double
get_feature_value (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_filedir_t *impl = (impl_filedir_t*) isrc->impl;

    switch (idx)  {
        case 0:
            return impl->fps;
        case 1:
            return impl->timescale;
        case 2:
            return impl->loop;
        default:
            return 0;
    }
}

static int
set_feature_value (image_source_t *isrc, int idx, double v)
{
    impl_filedir_t *impl = (impl_filedir_t*) isrc->impl;

    switch (idx)  {
        case 0:
            if (v != 0)
                v = fmax(0.1, v);
            impl->fps = v;
            break;
        case 1:
            impl->timescale = (int) v;
            break;
        case 2:
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

void
abort_ (const char * s, ...)
{
    va_list args;
    va_start (args, s);
    vfprintf (stderr, s, args);
    fprintf (stderr, "\n");
    va_end (args);
    abort ();
}

static int
get_frame_pnm (image_source_t *isrc, image_source_data_t *frmd, const char *file_name)
{
    pnm_t *pnm = pnm_create_from_file (file_name);
    if (!pnm)
        return -1;

    frmd->ifmt.width = pnm->width;
    frmd->ifmt.height = pnm->height;
    frmd->utime = utime_now ();
    frmd->priv = NULL;

    switch (pnm->format) {
        case 5: {
            strcpy (frmd->ifmt.format, "GRAY8");
            frmd->datalen = pnm->width * pnm->height;
            frmd->data = pnm->buf;
            pnm->buf = NULL; // we stole your buffer.
            pnm_destroy (pnm);
            return 0;
        }

        case 6: {
            strcpy (frmd->ifmt.format, "RGB");
            frmd->datalen = pnm->width * pnm->height * 3;
            frmd->data = pnm->buf;
            pnm->buf = NULL; // we stole your buffer.
            pnm_destroy (pnm);
            return 0;
        }

        default: {
            pnm_destroy (pnm);
            return -1;
        }
    }

    assert (0);
    return 0;
}

static int
get_frame_png (image_source_t *isrc, image_source_data_t *frmd, const char *file_name)
{
    int res = -1;

    png_structp png_ptr;
    png_infop info_ptr;
    //int number_of_passes;
    png_bytep *row_pointers;

    unsigned char header[8];    // 8 is the maximum size that can be checked

    /* open file and test for it being a png */
    FILE *fp = fopen (file_name, "rb");
    if (!fp)
        abort_ ("[read_png_file] File %s could not be opened for reading", file_name);
    fread (header, 1, 8, fp);
    if (png_sig_cmp (header, 0, 8))
        abort_ ("[read_png_file] File %s is not recognized as a PNG file", file_name);

    /* initialize stuff */
    png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr)
        abort_ ("[read_png_file] png_create_read_struct failed");

    info_ptr = png_create_info_struct (png_ptr);
    if (!info_ptr)
        abort_ ("[read_png_file] png_create_info_struct failed");

    if (setjmp (png_jmpbuf(png_ptr)))
        abort_ ("[read_png_file] Error during init_io");

    png_init_io (png_ptr, fp);
    png_set_sig_bytes (png_ptr, 8);

    png_read_info (png_ptr, info_ptr);

    int width = png_get_image_width (png_ptr, info_ptr);
    int height = png_get_image_height (png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth (png_ptr, info_ptr);

    //number_of_passes = png_set_interlace_handling(png_ptr);
    png_read_update_info (png_ptr, info_ptr);

    /* read file */
    if (setjmp (png_jmpbuf(png_ptr)))
        abort_ ("[read_png_file] Error during read_image");

    assert (bit_depth == 8);

    if (png_get_color_type (png_ptr, info_ptr) == PNG_COLOR_TYPE_GRAY) {

        frmd->ifmt.width = width;
        frmd->ifmt.height = height;
        strcpy (frmd->ifmt.format, "GRAY");

        frmd->datalen = width * height;
        frmd->data = malloc (frmd->datalen);
        frmd->utime = utime_now ();
        frmd->priv = NULL;

        row_pointers = malloc (sizeof(png_bytep) * height);
        for (int y = 0; y < height; y++)
            row_pointers[y] = &((uint8_t*) frmd->data)[y*width];

        png_read_image (png_ptr, row_pointers);

        res = 0;
        goto finish;
    }

    if (png_get_color_type (png_ptr, info_ptr) == PNG_COLOR_TYPE_RGBA) {

        frmd->ifmt.width = width;
        frmd->ifmt.height = height;
        strcpy(frmd->ifmt.format, "RGBA");

        frmd->datalen = width * height * 4;
        frmd->data = malloc (frmd->datalen);
        frmd->utime = utime_now ();
        frmd->priv = NULL;

        row_pointers = malloc (sizeof(png_bytep) * height);
        for (int y = 0; y < height; y++)
            row_pointers[y] = &((uint8_t*) frmd->data)[y*width * 4];

        png_read_image (png_ptr, row_pointers);

        res = 0;
        goto finish;
    }

    if (png_get_color_type (png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB) {

        frmd->ifmt.width = width;
        frmd->ifmt.height = height;
        strcpy(frmd->ifmt.format, "RGB");

        frmd->datalen = width * height * 3;
        frmd->data = malloc (frmd->datalen);
        frmd->utime = 0;
        frmd->priv = NULL;

        row_pointers = malloc (sizeof(png_bytep) * height);
        for (int y = 0; y < height; y++)
            row_pointers[y] = &((uint8_t*) frmd->data)[y*width * 3];

        png_read_image (png_ptr, row_pointers);

        res = 0;
        goto finish;
    }

    printf ("image_source_filedir: Unknown PNG image format.\n");

finish:

    fclose (fp);
    return res;
}

// try to decipher an integer from a time code.
// e.g., frame0284376.png gets decoded as 284376.
static uint32_t
get_time_code (const char *s)
{
    uint32_t acc = 0;

    for ( ; *s != 0; s++) {
        if (*s >= '0' && *s <= '9') {
            acc *= 10;
            acc += *s - '0';
        }
    }

    return acc;
}

static int
get_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_filedir_t *impl = (impl_filedir_t*) isrc->impl;

  tryagain:
    memset (frmd, 0, sizeof(*frmd));

    if (impl->pos == zarray_size (impl->files)) {
        if (impl->loop)
            impl->pos = 0;
        else {
            usleep (10000); // prevent get_frame spins
            return -1;
        }
    }

    const char *path;
    zarray_get(impl->files, impl->pos, &path);
    impl->pos++;

    if (str_ends_with (path, ".png")) {
        int res = get_frame_png (isrc, frmd, path);
        if (res)
            return res;
    }
    else if (str_ends_with (path, ".pnm") || str_ends_with (path, ".ppm") || str_ends_with (path, ".pgm")) {
        int res = get_frame_pnm (isrc, frmd, path);
        if (res)
            return res;
    }
    else {
        printf ("Unknown image type %s\n", path);
        impl->pos++;
        goto tryagain;
    }

    int64_t utime = utime_now ();

    int64_t goal_utime;

    if (impl->timescale == 0) {
        // use fps.
        int64_t goal_delta_utime = (uint64_t) (1000000 / impl->fps);
        goal_utime = impl->last_frame_utime + goal_delta_utime;
    }
    else {
        if (impl->pos == 0 || impl->pos >= zarray_size(impl->files))
            goal_utime = utime;
        else {
            const char *path;
            zarray_get (impl->files, impl->pos-1, &path);
            uint32_t t0 = get_time_code (path);
            zarray_get (impl->files, impl->pos, &path);
            uint32_t t1 = get_time_code (path);
            goal_utime = impl->last_frame_utime + impl->timescale * (t1 - t0);
        }
    }

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
//    assert(isrc->impl_type == IMPL_TYPE);
//    impl_islog_t *impl = (impl_islog_t*) isrc->impl;

    return 0;
}

static void
print_info (image_source_t *isrc)
{
}

// caller must allocate 'path'; we will then own that memory.
static void
add_path (image_source_t *isrc, char *path)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_filedir_t *impl = (impl_filedir_t*) isrc->impl;

    if (str_ends_with (path, ".png") ||
        str_ends_with (path, ".ppm") || str_ends_with (path, ".pnm") || str_ends_with (path, ".pgm")) {
        zarray_add (impl->files, &path);
    }
    else
        printf ("ignoring unknown file type: %s\n", path);
}

static int
mysort (const void *_a, const void *_b)
{
    char *a = *((char**) _a);
    char *b = *((char**) _b);

    return strcmp (a, b);
}

image_source_t *
image_source_filedir_open (url_parser_t *urlp)
{
    const char *location = url_parser_get_path (urlp);

    image_source_t *isrc = calloc(1, sizeof(*isrc));
    isrc->impl_type = IMPL_TYPE;

    impl_filedir_t *impl = calloc (1, sizeof(*impl));
    isrc->impl = impl;
    impl->files = zarray_create (sizeof(char*));

    if (0==strcmp (url_parser_get_protocol (urlp), "file://"))
        add_path (isrc, strdup (url_parser_get_path (urlp)));

    if (0==strcmp (url_parser_get_protocol (urlp), "dir://")) {
        DIR *dir = opendir (url_parser_get_path(urlp));

        if (dir == NULL) {
            printf ("image_source_filedir: Directory not found: %s\n",
                    url_parser_get_path (urlp));
            return NULL;
        }

        while (1) {
            struct dirent *entry = readdir (dir);
            if (entry == NULL)
                break;

            if (0==strcmp (entry->d_name, ".") || 0==strcmp (entry->d_name, ".."))
                continue;

            add_path (isrc, sprintf_alloc ("%s/%s", location, entry->d_name));
        }

        closedir (dir);
    }

    zarray_sort (impl->files, mysort);

    if (0==zarray_size (impl->files)) {
        printf ("image_source_filedir: didn't find any files.");
        return NULL;
    }

    // fill in fmt. (These will be overwritten later.)
    impl->width = 128;
    impl->height = 128;
    strcpy(impl->format, "NONE");

    impl->fps = 10;
    impl->timescale = 0;
    impl->loop = 1;

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

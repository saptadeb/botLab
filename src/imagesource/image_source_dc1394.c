#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <asm/types.h>
#include <assert.h>
#include <inttypes.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdarg.h>

#include <dc1394/control.h>
#include <dc1394/vendor/avt.h>

#include "common/string_util.h"

#include "image_source.h"

// XXX: We only attempt to support dc1394 Format 7 cameras

#define IMPL_TYPE 0x44431394

struct format {
    int width, height;
    char format[32];

    dc1394video_mode_t dc1394_mode;
    int format7_mode_idx;
    int color_coding_idx;
};

typedef struct impl_dc1394 impl_dc1394_t;
struct impl_dc1394 {
    int                   fd;

    dc1394_t              *dc1394;
    dc1394camera_t        *cam;

    int                   nformats;
    struct format         *formats;
    int                   current_format_idx;

    int                   num_buffers;

    dc1394video_frame_t   *current_frame;

    dc1394featureset_t    features;

    uint32_t              packet_size;

    uint32_t              started;
};

static int strobe_warned = 0;

static void
print_strobe_warning (void)
{
    if (strobe_warned == 0) {
        printf ("image_source_dc1394.c: Decoding strobe duration & delay number format not supported.\n");
        printf ("image_source_dc1394.c: Example values for FireFly MV: Delay = 2 (1ms), 6 (2ms), 14 (3ms), 1 (4ms).\n");
        printf ("image_source_dc1394.c: Contact developer if interested.\n");

        strobe_warned = 1;
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
strposat (const char *haystack, const char *needle, int haystackpos)
{
    int idx = haystackpos;
    int needlelen = strlen (needle);

    while (haystack[idx] != 0) {
        if (0==strncmp (&haystack[idx], needle, needlelen))
            return idx;

        idx++;
    }

    return -1; // not found.
}

static int
strpos (const char *haystack, const char *needle)
{
    return strposat (haystack, needle, 0);
}

// convert a base-16 number in ASCII ('len' characters long) to a 64
// bit integer. Result is written to *ov, 0 is returned if parsing is
// successful. Otherwise -1 is returned.
static int
strto64 (const char *s, int maxlen, int64_t *ov)
{
    int64_t acc = 0;
    for (int i = 0; i < maxlen; i++) {
        char c = s[i];
        if (c==0)
            break;
        int ic = 0;
        if (c >= 'a' && c <='f')
            ic = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            ic = c - 'A' + 10;
        else if (c >= '0' && c <= '9')
            ic = c - '0';
        else
            printf("%c", c); //return -1;
        acc = (acc<<4) + ic;
    }

    *ov = acc;
    return 0;
}


static const char *
toformat (dc1394color_coding_t color, dc1394color_filter_t filter)
{
    switch (color) {
        case DC1394_COLOR_CODING_MONO8:
            return "GRAY8";
        case DC1394_COLOR_CODING_RAW8:
            switch (filter) {
                case DC1394_COLOR_FILTER_RGGB:
                    return "BAYER_RGGB";
                case DC1394_COLOR_FILTER_GBRG:
                    return "BAYER_GBRG";
                case DC1394_COLOR_FILTER_GRBG:
                    return "BAYER_GRBG";
                case DC1394_COLOR_FILTER_BGGR:
                    return "BAYER_BGGR";
                default:
                    return "GRAY";
            }
        case DC1394_COLOR_CODING_YUV411:
            return "YUV422";
        case DC1394_COLOR_CODING_YUV422:
            return "UYVY";
        case DC1394_COLOR_CODING_YUV444:
            return "IYU2";
        case DC1394_COLOR_CODING_RGB8:
            return "RGB";
        case DC1394_COLOR_CODING_MONO16:
            return "GRAY16";

            // XXX it's not clear from IIDC_1.31.pdf that any of these are big endian

        case DC1394_COLOR_CODING_RGB16:
            return "BE_RGB16";
        case DC1394_COLOR_CODING_MONO16S:
            return "BE_SIGNED_GRAY16";
        case DC1394_COLOR_CODING_RGB16S:
            return "BE_SIGNED_RGB16";
        case DC1394_COLOR_CODING_RAW16:
            switch (filter) {
                case DC1394_COLOR_FILTER_RGGB:
                    return "BAYER_RGGB16";
                case DC1394_COLOR_FILTER_GBRG:
                    return "BAYER_GBRG16";
                case DC1394_COLOR_FILTER_GRBG:
                    return "BAYER_GRBG16";
                case DC1394_COLOR_FILTER_BGGR:
                    return "BAYER_BGGR16";
                default:
                    return "GRAY16";
            }
    }
    return "UNKNOWN";
}

static int
num_formats (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    return impl->nformats;
}

static void
get_format (image_source_t *isrc, int idx, image_source_format_t *fmt)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    assert (idx>=0 && idx < impl->nformats);

    memset (fmt, 0, sizeof(*fmt));
    fmt->width = impl->formats[idx].width;
    fmt->height = impl->formats[idx].height;
    strcpy (fmt->format, impl->formats[idx].format);
}

static int
get_current_format (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    return impl->current_format_idx;
}

static int
set_format (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    assert (idx>=0 && idx < impl->nformats);

    impl->current_format_idx = idx;

    return 0;
}

static int
set_named_format (image_source_t *isrc, const char *desired_format)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    char *format_name = strdup (desired_format);
    int colonpos = strpos (desired_format, ":");
    int xpos = strpos (desired_format, "x");
    int width = -1;
    int height = -1;
    if (colonpos >= 0 && xpos > colonpos) {
        free (format_name);
        format_name = strndup (desired_format, colonpos);
        char *swidth = strndup (&desired_format[colonpos+1], xpos-colonpos-1);
        char *sheight = strdup (&desired_format[xpos+1]);

        width = atoi (swidth);
        height = atoi (sheight);

        free (swidth);
        free (sheight);
    }

    int nformats = num_formats (isrc);
    int fidx = -1;

    for (int i=0; i < nformats; i++)
    {
        image_source_format_t fmt;

        get_format (isrc, i, &fmt);

        if (0==strcmp (fmt.format, format_name)) {
            if (width == -1 || height == -1 || (fmt.width == width && fmt.height == height)) {
                fidx = i;
                break;
            }
        }
    }

    // if no matching format found...
    if (fidx < 0 || fidx >= impl->nformats) {
        printf ("Matching format '%s' not found. Valid formats are:\n", desired_format);
        for (int i=0; i < nformats; i++)
        {
            image_source_format_t fmt;
            get_format (isrc, i, &fmt);
            printf ("\t[fidx: %d] width: %d height: %d name: '%s'\n",
                    i, fmt.width, fmt.height, fmt.format);
        }
        printf ("\tFormat resolution not required.  Exiting.\n");
        exit (EXIT_FAILURE);
    }

    impl->current_format_idx = fidx;
    free (format_name);
    return 0;
}

static int
num_features (image_source_t *isrc)
{
    // don't forget: feature index starts at 0
    return 39;
}

static char *
dc1394_feature_id_to_string (unsigned int id)
{
    switch (id) {
        case DC1394_FEATURE_BRIGHTNESS:
            return "brightness";
        case DC1394_FEATURE_EXPOSURE:
            return "exposure";
        case DC1394_FEATURE_SHARPNESS:
            return "sharpness";
        case DC1394_FEATURE_WHITE_BALANCE:
            return "white balance";
        case DC1394_FEATURE_HUE:
            return "hue";
        case DC1394_FEATURE_SATURATION:
            return "saturation";
        case DC1394_FEATURE_GAMMA:
            return "gamma";
        case DC1394_FEATURE_SHUTTER:
            return "shutter";
        case DC1394_FEATURE_GAIN:
            return "gain";
        case DC1394_FEATURE_IRIS:
            return "iris";
        case DC1394_FEATURE_FOCUS:
            return "focus";
        case DC1394_FEATURE_TEMPERATURE:
            return "temperature";
        case DC1394_FEATURE_TRIGGER:
            return "trigger";
        case DC1394_FEATURE_TRIGGER_DELAY:
            return "trigger delay";
        case DC1394_FEATURE_WHITE_SHADING:
            return "white shading";
        case DC1394_FEATURE_FRAME_RATE:
            return "frame rate";
        case DC1394_FEATURE_ZOOM:
            return "zoom";
        case DC1394_FEATURE_PAN:
            return "pan";
        case DC1394_FEATURE_TILT:
            return "tilt";
        case DC1394_FEATURE_OPTICAL_FILTER:
            return "optical filter";
        case DC1394_FEATURE_CAPTURE_SIZE:
            return "capture size";
        case DC1394_FEATURE_CAPTURE_QUALITY:
            return "capture quality";
        default:
            printf ("image_source_dc1394.c: cannot determine feature name for feature %d\n", id);
            return "unknown";
    }

    printf ("image_source_dc1394.c: cannot determine feature name for feature %d\n", id);
    return "unknown";
}

static const char *
get_feature_name (image_source_t *isrc, int idx)
{
    switch(idx) {
        case 0:
            return "white-balance-manual";
        case 1:
            return "white-balance-red";
        case 2:
            return "white-balance-blue";
        case 3:
            return "exposure-manual";
        case 4:
            return "exposure";
        case 5:
            return "brightness-manual";
        case 6:
            return "brightness";
        case 7:
            return "shutter-manual";
        case 8:
            return "shutter";
        case 9:
            return "gain-manual";
        case 10:
            return "gain";
        case 11:
            return "gamma-manual";
        case 12:
            return "gamma";
        case 13:
            return "hdr";
        case 14:
            return "frame-rate-manual";
        case 15:
            return "frame-rate";
        case 16:
            return "timestamps-enable";
        case 17:
            return "frame-counter-enable";
        case 18:
            return "strobe-0-manual";
        case 19:
            return "strobe-0-polarity";
        case 20:
            return "strobe-0-delay";
        case 21:
            return "strobe-0-duration";
        case 22:
            return "strobe-1-manual";
        case 23:
            return "strobe-1-polarity";
        case 24:
            return "strobe-1-delay";
        case 25:
            return "strobe-1-duration";
        case 26:
            return "strobe-2-manual";
        case 27:
            return "strobe-2-polarity";
        case 28:
            return "strobe-2-delay";
        case 29:
            return "strobe-2-duration";
        case 30:
            return "strobe-3-manual";
        case 31:
            return "strobe-3-polarity";
        case 32:
            return "strobe-3-delay";
        case 33:
            return "strobe-3-duration";
        case 34:
            return "external-trigger-manual";
        case 35:
            return "external-trigger-mode";
        case 36:
            return "external-trigger-polarity";
        case 37:
            return "external-trigger-source";
        case 38:
            return "software-trigger";
        default:
            return NULL;
    }
}

static dc1394feature_info_t *
find_feature (image_source_t *isrc, dc1394feature_t id)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    for (int i = 0; i < DC1394_FEATURE_NUM; i++) {
        if (impl->features.feature[i].id == id) {
            dc1394_feature_get (impl->cam, &impl->features.feature[i]);

            return &impl->features.feature[i];
        }
    }

    return NULL;
}

static uint32_t
flip_endianness (uint32_t in)
{
    uint32_t out = 0;

    for (int i=0; i < 32; i++)
        out |= ((in >> (32 - (i+1))) & 0x1) << i;

    return out;
}

static uint32_t
get_strobe_inquiry_offset (uint32_t strobe)
{
    switch (strobe) {
        case 0:
            return 0x100;
        case 1:
            return 0x104;
        case 2:
            return 0x108;
        case 3:
            return 0x10C;
    }

    return 0;
}

static uint32_t
get_strobe_control_offset (uint32_t strobe)
{
    switch (strobe) {
        case 0:
            return 0x200;
        case 1:
            return 0x204;
        case 2:
            return 0x208;
        case 3:
            return 0x20C;
    }

    return 0;
}

static double
get_strobe_min (image_source_t *isrc, uint32_t strobe)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_inquiry_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    value = flip_endianness (value);
    return (double) ((value >> 8) & 0xFFF);
}

static double
get_strobe_max (image_source_t *isrc, uint32_t strobe)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_inquiry_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    value = flip_endianness (value);
    return (double) ((value >> 20) & 0xFFF);
}

static uint32_t
get_strobe_power (image_source_t *isrc, uint32_t strobe)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    value = flip_endianness (value);
    return (value >> 6) & 0x1;
}

static uint32_t
get_strobe_polarity (image_source_t *isrc, uint32_t strobe)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    value = flip_endianness (value);
    return (value >> 7) & 0x1;
}

static double
get_strobe_delay (image_source_t *isrc, uint32_t strobe)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    print_strobe_warning ();
    value = flip_endianness (value);
    return (double) ((value >> 8) & 0xFFF);
}

static double
get_strobe_duration (image_source_t *isrc, uint32_t strobe)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    print_strobe_warning ();
    value = flip_endianness (value);
    return (double) ((value >> 20) & 0xFFF);
}

static dc1394error_t
set_strobe_power (image_source_t *isrc, uint32_t strobe, uint32_t v)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    // return -1 if we can't set this field
    uint32_t inquiry_value;
    int inquiry_offset = get_strobe_inquiry_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, inquiry_offset, &inquiry_value) != DC1394_SUCCESS)
        return 0;

    inquiry_value = flip_endianness (inquiry_value);
    if (((inquiry_value >> 5) & 0x1) == 0)
        return -1;

    // actually set it
    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    // make big endian
    value = flip_endianness (value);
    if (v == 1)
        value |= (1 << 6);
    else
        value &= ~(1 << 6);

    // make little endian
    value = flip_endianness (value);

    return dc1394_set_strobe_register (impl->cam, offset, value);
}

static dc1394error_t
set_strobe_polarity (image_source_t *isrc, uint32_t strobe, uint32_t v)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    // return -1 if we can't set this field
    uint32_t inquiry_value;
    int inquiry_offset = get_strobe_inquiry_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, inquiry_offset, &inquiry_value) != DC1394_SUCCESS)
        return 0;

    inquiry_value = flip_endianness (inquiry_value);
    if (((inquiry_value >> 6) & 0x1) == 0)
        return -1;

    // actually set it
    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    // make big endian
    value = flip_endianness (value);
    if (v == 1)
        value |= (1 << 7);
    else
        value &= ~(1 << 7);

    // make little endian
    value = flip_endianness (value);

    return dc1394_set_strobe_register (impl->cam, offset, value);
}

static dc1394error_t
set_strobe_delay (image_source_t *isrc, uint32_t strobe, uint32_t v)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    // make big endian
    value = flip_endianness (value);
    value &= ~(0xFFF << 8);
    value |= (v & 0xFFF) << 8;
    // make little endian
    value = flip_endianness (value);

    print_strobe_warning ();
    return dc1394_set_strobe_register (impl->cam, offset, value);
}

static dc1394error_t
set_strobe_duration (image_source_t *isrc, uint32_t strobe, uint32_t v)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    uint32_t value;
    int offset = get_strobe_control_offset (strobe);
    if (dc1394_get_strobe_register (impl->cam, offset, &value) != DC1394_SUCCESS)
        return 0;

    // make big endian
    value = flip_endianness (value);
    value &= ~(0xFFF << 20);
    value |= (v & 0xFFF) << 20;
    // make little endian
    value = flip_endianness (value);

    print_strobe_warning ();
    return dc1394_set_strobe_register (impl->cam, offset, value);
}

static char *
get_feature_type (image_source_t *isrc, int idx)
{
    switch(idx) {
        case 0: // white-balance-manual
            return strdup ("b");
        case 1: // white-balance-red
        case 2: // white-balance-blue
            return sprintf_alloc ("i,%d,%d",
                                  find_feature (isrc, DC1394_FEATURE_WHITE_BALANCE)->min,
                                  find_feature (isrc, DC1394_FEATURE_WHITE_BALANCE)->max);
        case 3: // exposure-manual
            return strdup ("b");
        case 4: // exposure
            return sprintf_alloc ("i,%d,%d",
                                  find_feature (isrc, DC1394_FEATURE_EXPOSURE)->min,
                                  find_feature (isrc, DC1394_FEATURE_EXPOSURE)->max);
        case 5: // brightness-manual
            return strdup ("b");
        case 6: // brightness
            return sprintf_alloc ("i,%d,%d",
                                  find_feature (isrc, DC1394_FEATURE_BRIGHTNESS)->min,
                                  find_feature (isrc, DC1394_FEATURE_BRIGHTNESS)->max);
        case 7: // shutter-manual
            return strdup ("b");
        case 8: // shutter
            return sprintf_alloc ("f,%f,%f",
                                  find_feature (isrc, DC1394_FEATURE_SHUTTER)->abs_min * 1000,
                                  find_feature (isrc, DC1394_FEATURE_SHUTTER)->abs_max * 1000);
        case 9: // gain-manual
            return strdup ("b");
        case 10: // gain
            return sprintf_alloc ("f,%f,%f",
                                  find_feature (isrc, DC1394_FEATURE_GAIN)->abs_min,
                                  find_feature (isrc, DC1394_FEATURE_GAIN)->abs_max);
        case 11: // gamma-manual
            return strdup ("b");
        case 12: // gamma
            return sprintf_alloc ("i,%d,%d",
                                  find_feature (isrc, DC1394_FEATURE_GAMMA)->min,
                                  find_feature (isrc, DC1394_FEATURE_GAMMA)->max);
        case 13: // hdr
            return strdup ("b");
        case 14: // frame-rate-mode
            return strdup ("b");
        case 15: // frame-rate
            return sprintf_alloc ("f,%f,%f",
                                  find_feature (isrc, DC1394_FEATURE_FRAME_RATE)->abs_min,
                                  find_feature (isrc, DC1394_FEATURE_FRAME_RATE)->abs_max);
        case 16: // timestamps-enable
            return strdup ("b");
        case 17: // frame-counter-enable
            return strdup ("b");
        case 18: // strobe-0-manual
            return strdup ("b");
        case 19: // strobe-0-polarity
            return strdup ("b");
        case 20: // strobe-0-delay
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 0),
                                  get_strobe_max (isrc, 0));
        case 21: // strobe-0-duration
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 0),
                                  get_strobe_max (isrc, 0));
        case 22: // strobe-1-manual
            return strdup ("b");
        case 23: // strobe-1-polarity
            return strdup ("b");
        case 24: // strobe-1-delay
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 1),
                                  get_strobe_max (isrc, 1));
        case 25: // strobe-1-duration
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 1),
                                  get_strobe_max (isrc, 1));
        case 26: // strobe-2-manual
            return strdup ("b");
        case 27: // strobe-2-polarity
            return strdup ("b");
        case 28: // strobe-2-delay
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 2),
                                  get_strobe_max (isrc, 2));
        case 29: // strobe-2-duration
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 2),
                                  get_strobe_max (isrc, 2));
        case 30: // strobe-3-manual
            return strdup ("b");
        case 31: // strobe-3-polarity
            return strdup ("b");
        case 32: // strobe-3-delay
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 3),
                                  get_strobe_max (isrc, 3));
        case 33: // strobe-3-duration
            return sprintf_alloc ("f,%f,%f",
                                  get_strobe_min (isrc, 3),
                                  get_strobe_max (isrc, 3));
        case 34: // external-trigger-manual
            return strdup ("b");
        case 35: // external-trigger-mode
            return strdup ("b");
        case 36: // external-trigger-polarity
            return strdup ("b");
        case 37: // external-trigger-source
            return sprintf_alloc ("i,%d,%d",
                                  DC1394_TRIGGER_SOURCE_MIN,
                                  DC1394_TRIGGER_SOURCE_MAX);
        case 38:
            return strdup ("b");
        default:
            assert (0);
    }
}

// Some features are controlled via ON/OFF (e.g., WHITE_BALANCE), some
// by mode AUTO/MANUAL (e.g., most of the rest). We try to hide this
// variation and just "make it work".
static double
get_feature_value (image_source_t *isrc, int idx)
{
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    switch (idx) {
        case 0: { // white-balance-manual
            dc1394switch_t mode = DC1394_OFF;
            dc1394_feature_get_power (impl->cam, DC1394_FEATURE_WHITE_BALANCE, &mode);
            return mode == DC1394_ON;
        }

        case 1:   // white-balance-red
        case 2: { // white-balance-blue
            uint32_t b=0, r=0;

            dc1394_feature_whitebalance_get_value (impl->cam, &b, &r);

            if (idx == 1)
                return r;

            return b;
        }

        case 3: { // exposure-manual
            dc1394feature_mode_t mode = DC1394_FEATURE_MODE_AUTO;
            dc1394_feature_get_mode (impl->cam, DC1394_FEATURE_EXPOSURE, &mode);
            return mode == DC1394_FEATURE_MODE_MANUAL;
        }
        case 4: { // exposure
            uint32_t v = 0;
            dc1394_feature_get_value (impl->cam, DC1394_FEATURE_EXPOSURE, &v); // XXX error checking
            return v;
        }

        case 5: {// brightness-manual
            dc1394feature_mode_t mode = DC1394_FEATURE_MODE_AUTO;
            dc1394_feature_get_mode (impl->cam, DC1394_FEATURE_BRIGHTNESS, &mode);
            return mode == DC1394_FEATURE_MODE_MANUAL;
        }
        case 6: { // brightness
            uint32_t v = 0;
            dc1394_feature_get_value (impl->cam, DC1394_FEATURE_BRIGHTNESS, &v); // XXX error checking
            return v;
        }

        case 7: { // shutter-manual
            dc1394feature_mode_t mode = DC1394_FEATURE_MODE_AUTO;
            dc1394_feature_get_mode (impl->cam, DC1394_FEATURE_SHUTTER, &mode);
            return mode == DC1394_FEATURE_MODE_MANUAL;
        }
        case 8: { // shutter
            float v = 0;
            dc1394_feature_get_absolute_value (impl->cam, DC1394_FEATURE_SHUTTER, &v); // XXX error checking
            return v * 1e3;
        }

        case 9: { // gain-manual
            dc1394feature_mode_t mode = DC1394_FEATURE_MODE_AUTO;
            dc1394_feature_get_mode (impl->cam, DC1394_FEATURE_GAIN, &mode);
            return mode == DC1394_FEATURE_MODE_MANUAL;
        }
        case 10: { // gain
            float v = 0;
            dc1394_feature_get_absolute_value (impl->cam, DC1394_FEATURE_GAIN, &v); // XXX error checking
            return v;
        }

        case 11: { // gamma-manual
            dc1394switch_t mode = DC1394_OFF;
            dc1394_feature_get_power (impl->cam, DC1394_FEATURE_GAMMA, &mode);
            return mode == DC1394_ON;
        }

        case 12: { // gamma
            uint32_t v = 0;
            dc1394_feature_get_value (impl->cam, DC1394_FEATURE_GAMMA, &v); // XXX error checking
            return v;
        }

        case 13: { // hdr
            uint64_t offset = 0x0F00000ULL;

            // write address of imager register to pointgrey pass-through register
            if (dc1394_set_register (impl->cam, offset + 0x1a00, 0x0f) != DC1394_SUCCESS)
                return -1;

            uint32_t value;
            if (dc1394_get_register (impl->cam, offset + 0x1a04, &value) != DC1394_SUCCESS)
                return -1;

            return (value & 0x40) ? 1 : 0;
        }

        case 14: { // frame-rate-mode
            dc1394feature_mode_t mode = DC1394_FEATURE_MODE_AUTO;
            dc1394_feature_get_mode (impl->cam, DC1394_FEATURE_FRAME_RATE, &mode);
            return mode == DC1394_FEATURE_MODE_MANUAL;
        }

        case 15: { // frame rate
            float v = 0;
            dc1394_feature_get_absolute_value (impl->cam, DC1394_FEATURE_FRAME_RATE, &v); // XXX error checking
            return v;
        }

        case 16: { // timestamps-enable
            uint32_t value;
            if (dc1394_get_adv_control_register (impl->cam, 0x2F8, &value) != DC1394_SUCCESS)
                return 0;

            return (value & 0x01) >> 0;
        }

        case 17: { // frame-counter-enable
            uint32_t value;
            if (dc1394_get_adv_control_register (impl->cam, 0x2F8, &value) != DC1394_SUCCESS)
                return 0;

            return (value & 0x40) >> 6;
        }

        case 18: { // strobe-0-manual
            return get_strobe_power (isrc, 0);
        }

        case 19: { // strobe-0-polarity
            return get_strobe_polarity (isrc, 0);
        }

        case 20: { // strobe-0-delay
            return get_strobe_delay (isrc, 0);
        }

        case 21: { // strobe-0-duration
            return get_strobe_duration (isrc, 0);
        }

        case 22: { // strobe-1-manual
            return get_strobe_power (isrc, 1);
        }

        case 23: { // strobe-1-polarity
            return get_strobe_polarity (isrc, 1);
        }

        case 24: { // strobe-1-delay
            return get_strobe_delay (isrc, 1);
        }

        case 25: { // strobe-1-duration
            return get_strobe_duration (isrc, 1);
        }

        case 26: { // strobe-2-manual
            return get_strobe_power (isrc, 2);
        }

        case 27: { // strobe-2-polarity
            return get_strobe_polarity (isrc, 2);
        }

        case 28: { // strobe-2-delay
            return get_strobe_delay (isrc, 2);
        }

        case 29: { // strobe-2-duration
            return get_strobe_duration (isrc, 2);
        }

        case 30: { // strobe-3-manual
            return get_strobe_power (isrc, 3);
        }

        case 31: { // strobe-3-polarity
            return get_strobe_polarity (isrc, 3);
        }

        case 32: { // strobe-3-delay
            return get_strobe_delay (isrc, 3);
        }

        case 33: { // strobe-3-duration
            return get_strobe_duration (isrc, 3);
        }

        case 34: { // external-trigger-manual
            impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

            dc1394switch_t ext_pwr;
            if (dc1394_external_trigger_get_power (impl->cam, &ext_pwr) != DC1394_SUCCESS)
                return 0;

            return ext_pwr;
        }

        case 35: { // external-trigger-mode
            impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

            dc1394trigger_mode_t mode;
            if (dc1394_external_trigger_get_mode (impl->cam, &mode) != DC1394_SUCCESS)
                return 0;

            return mode - DC1394_TRIGGER_MODE_MIN;
        }

        case 36: { // external-trigger-polarity
            impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

            dc1394bool_t polarity_capable;
            if (dc1394_external_trigger_has_polarity (impl->cam, &polarity_capable) != DC1394_SUCCESS)
                return 0;

            if (polarity_capable == DC1394_FALSE)
                return 0;

            dc1394trigger_polarity_t polarity;
            if (dc1394_external_trigger_get_polarity (impl->cam, &polarity) != DC1394_SUCCESS)
                return 0;

            return polarity;
        }

        case 37: { // external-trigger-source
            impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

            dc1394trigger_source_t source;
            if (dc1394_external_trigger_get_source (impl->cam, &source) != DC1394_SUCCESS)
                return 0;

            return source;
        }

        case 38: { // software-trigger
            dc1394switch_t pwr = DC1394_OFF;
            if (dc1394_software_trigger_get_power (impl->cam, &pwr) != DC1394_SUCCESS)
                return 0;

            return pwr;
        }

        default:
            return 0;
    }
}

static int
set_feature_value (image_source_t *isrc, int idx, double v)
{
    uint32_t r, b;
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    dc1394_feature_whitebalance_get_value (impl->cam, &b, &r);

    switch (idx) {
        case 0: { // white-balance-manual
            if (v==1) {
                dc1394_feature_set_power (impl->cam, DC1394_FEATURE_WHITE_BALANCE, DC1394_ON);
                dc1394_feature_set_mode (impl->cam, DC1394_FEATURE_WHITE_BALANCE, DC1394_FEATURE_MODE_MANUAL);
            } else {
                dc1394_feature_set_power (impl->cam, DC1394_FEATURE_WHITE_BALANCE, DC1394_OFF);
            }
            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_WHITE_BALANCE, DC1394_OFF);

            break;
        }

        case 1: // white-balance-red
        case 2: { // white-balance-blue
            uint32_t b=0, r=0;

            dc1394_feature_whitebalance_get_value (impl->cam, &b, &r);

            if (idx==1)
                r = (uint32_t) v;
            if (idx==2)
                b = (uint32_t) v;

            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_WHITE_BALANCE, DC1394_OFF);
            dc1394_feature_whitebalance_set_value (impl->cam, (uint32_t) b, (uint32_t) r);
            break;
        }

        case 3: { // exposure-manual
            dc1394_feature_set_power (impl->cam, DC1394_FEATURE_EXPOSURE, DC1394_ON);
            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_EXPOSURE, DC1394_OFF);
            dc1394_feature_set_mode (impl->cam, DC1394_FEATURE_EXPOSURE, v!=0 ? DC1394_FEATURE_MODE_MANUAL :
                                     DC1394_FEATURE_MODE_AUTO);
            break;
        }

        case 4: { // exposure
            dc1394_feature_set_value (impl->cam, DC1394_FEATURE_EXPOSURE, (uint32_t) v);
            break;
        }

        case 5: { // brightness-manual
            dc1394_feature_set_power (impl->cam, DC1394_FEATURE_BRIGHTNESS, DC1394_ON);
            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_BRIGHTNESS, DC1394_OFF);
            dc1394_feature_set_mode (impl->cam, DC1394_FEATURE_BRIGHTNESS, v!=0 ? DC1394_FEATURE_MODE_MANUAL :
                                     DC1394_FEATURE_MODE_AUTO);
            break;
        }

        case 6: // brightness
            dc1394_feature_set_value (impl->cam, DC1394_FEATURE_BRIGHTNESS, (uint32_t) v);
            break;

        case 7: { // shutter-manual
            dc1394_feature_set_power (impl->cam, DC1394_FEATURE_SHUTTER, DC1394_ON);
            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_SHUTTER, DC1394_ON);
            dc1394_feature_set_mode (impl->cam, DC1394_FEATURE_SHUTTER, v!=0 ? DC1394_FEATURE_MODE_MANUAL :
                                     DC1394_FEATURE_MODE_AUTO);
            break;
        }
        case 8: { // shutter
            dc1394_feature_set_absolute_value (impl->cam, DC1394_FEATURE_SHUTTER, ((float) v)/1e3);
            break;
        }

        case 9: { // gain-manual
            dc1394_feature_set_power (impl->cam, DC1394_FEATURE_GAIN, DC1394_ON);
            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_GAIN, DC1394_ON);
            dc1394_feature_set_mode (impl->cam, DC1394_FEATURE_GAIN, v!=0 ? DC1394_FEATURE_MODE_MANUAL :
                                     DC1394_FEATURE_MODE_AUTO);
            break;
        }
        case 10: { // gain
            dc1394_feature_set_absolute_value (impl->cam, DC1394_FEATURE_GAIN, (float) v);
            break;
        }

        case 11: { // gamma-manual
            dc1394_feature_set_power (impl->cam, DC1394_FEATURE_GAMMA, v!=0 ? DC1394_ON : DC1394_OFF);
            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_GAMMA, DC1394_OFF);
            dc1394_feature_set_mode (impl->cam, DC1394_FEATURE_GAMMA, DC1394_FEATURE_MODE_MANUAL);
            break;
        }

        case 12: { // gamma
            dc1394_feature_set_value (impl->cam, DC1394_FEATURE_GAMMA, (uint32_t) v);
            break;
        }

        case 13: { // hdr

            uint64_t offset = 0x0F00000ULL;

            // write address of imager register to pointgrey pass-through register
            if (dc1394_set_register (impl->cam, offset + 0x1a00, 0x0f) != DC1394_SUCCESS)
                return -1;

            // enable HDR mode
            uint32_t value;
            if (dc1394_get_register (impl->cam, offset + 0x1a04, &value) != DC1394_SUCCESS)
                return -1;

            if (v == 1)
                value |= 0x40;
            else
                value &= (~0x40);

            if (dc1394_set_register (impl->cam,  offset + 0x1a04, value) != DC1394_SUCCESS)
                return -1;

            // enable automatic knee point timing
            if (dc1394_set_register (impl->cam,  offset + 0x1a00, 0x0a) != DC1394_SUCCESS)
                return -1;

            if (dc1394_get_register (impl->cam,  offset + 0x1a04, &value) != DC1394_SUCCESS)
                return -1;

            if (v == 1)
                value |= 0x100;
            else
                value &= (~0x100);

            if (dc1394_set_register (impl->cam,  offset + 0x1a04, value) != DC1394_SUCCESS)
                return -1;

            break;
        }

        case 14: { // frame-rate-mode
            dc1394_feature_set_power (impl->cam, DC1394_FEATURE_FRAME_RATE, DC1394_ON);
            dc1394_feature_set_absolute_control (impl->cam, DC1394_FEATURE_FRAME_RATE, DC1394_ON);
            dc1394_feature_set_mode (impl->cam, DC1394_FEATURE_FRAME_RATE, v!=0 ? DC1394_FEATURE_MODE_MANUAL :
                                     DC1394_FEATURE_MODE_AUTO);

            break;
        }

        case 15: { // frame rate
            dc1394_feature_set_absolute_value(impl->cam, DC1394_FEATURE_FRAME_RATE, (float) v);
            break;
        }

        case 16: { // timestamps-enable
            uint32_t value;
            if (dc1394_get_adv_control_register (impl->cam, 0x2F8, &value) != DC1394_SUCCESS)
                return -1;

            value &= (~0x01);
            value |= ((int) v) << 0;

            if (dc1394_set_adv_control_register (impl->cam, 0x2F8, value) != DC1394_SUCCESS)
                return -1;

            break;
        }

        case 17: { // frame-counter-enable
            uint32_t value;
            if (dc1394_get_adv_control_register (impl->cam, 0x2F8, &value) != DC1394_SUCCESS)
                return -1;

            value &= (~0x40);
            value |= ((int) v) << 6;

            if (dc1394_set_adv_control_register (impl->cam, 0x2F8, value) != DC1394_SUCCESS)
                return -1;

            break;
        }

        case 18: { // strobe-0-manual
            return set_strobe_power (isrc, 0, v);
        }

        case 19: { // strobe-0-polarity
            return set_strobe_polarity (isrc, 0, v);
        }

        case 20: { // strobe-0-delay
            return set_strobe_delay (isrc, 0, v);
        }

        case 21: { // strobe-0-duration
            return set_strobe_duration (isrc, 0, v);
        }

        case 22: { // strobe-1-manual
            return set_strobe_power (isrc, 1, v);
        }

        case 23: { // strobe-1-polarity
            return set_strobe_polarity (isrc, 1, v);
        }

        case 24: { // strobe-1-delay
            return set_strobe_delay (isrc, 1, v);
        }

        case 25: { // strobe-1-duration
            return set_strobe_duration (isrc, 1, v);
        }

        case 26: { // strobe-2-manual
            return set_strobe_power (isrc, 2, v);
        }

        case 27: { // strobe-2-polarity
            return set_strobe_polarity (isrc, 2, v);
        }

        case 28: { // strobe-2-delay
            return set_strobe_delay (isrc, 2, v);
        }

        case 29: { // strobe-2-duration
            return set_strobe_duration (isrc, 2, v);
        }

        case 30: { // strobe-3-manual
            return set_strobe_power (isrc, 3, v);
        }

        case 31: { // strobe-3-polarity
            return set_strobe_polarity (isrc, 3, v);
        }

        case 32: { // strobe-3-delay
            return set_strobe_delay (isrc, 3, v);
        }

        case 33: { // strobe-3-duration
            return set_strobe_duration (isrc, 3, v);
        }

        case 34: { // external-trigger-manual
            return dc1394_external_trigger_set_power (impl->cam, (uint32_t) v);
        }

        case 35: { // external-trigger-mode
            return dc1394_external_trigger_set_mode (impl->cam, DC1394_TRIGGER_MODE_MIN + (uint32_t) v);
        }

        case 36: { // external-trigger-polarity
            dc1394bool_t polarity_capable;
            if (dc1394_external_trigger_has_polarity (impl->cam, &polarity_capable) != DC1394_SUCCESS)
                return -1;

            if (polarity_capable == DC1394_FALSE)
                return -1;

            return dc1394_external_trigger_set_polarity (impl->cam, (uint32_t) v);
        }

        case 37: { // external-trigger-source
            return dc1394_external_trigger_set_source (impl->cam, (uint32_t) v);
        }

        case 38: { // software-trigger
            return dc1394_software_trigger_set_power (impl->cam, (uint32_t) v);
        }

        default:
            return 0;
    }

    return 0;
}

static int
start (image_source_t *isrc)
{
    int have_reset_bus = 0;

restart:
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    struct format *format = &impl->formats[impl->current_format_idx];

    dc1394_video_set_mode (impl->cam, format->dc1394_mode);
    dc1394_video_set_iso_speed (impl->cam, DC1394_ISO_SPEED_400);

    assert(dc1394_is_video_mode_scalable (format->dc1394_mode));

    dc1394format7modeset_t info;
    dc1394_format7_get_modeset (impl->cam, &info);

    dc1394format7mode_t *mode = info.mode + format->format7_mode_idx;
    dc1394color_coding_t color_coding = mode->color_codings.codings[format->color_coding_idx];

    dc1394_format7_set_image_size (impl->cam, format->dc1394_mode,
                                   format->width, format->height);

    dc1394_format7_set_image_position (impl->cam, format->dc1394_mode, 0, 0);

    dc1394_format7_set_color_coding (impl->cam, format->dc1394_mode, color_coding);

    uint32_t psize_unit, psize_max;
    dc1394_format7_get_packet_parameters (impl->cam, format->dc1394_mode, &psize_unit, &psize_max);

    if (impl->packet_size == 0)
        impl->packet_size = psize_max; //4096;
    else {
        impl->packet_size = psize_unit * (impl->packet_size / psize_unit);
        if (impl->packet_size > psize_max)
            impl->packet_size = psize_max;
        if (impl->packet_size < psize_unit)
            impl->packet_size = psize_unit;
    }

    printf ("psize_unit: %d, psize_max: %d, packet_size: %d\n",
            psize_unit, psize_max, impl->packet_size);

    dc1394_format7_set_packet_size (impl->cam, format->dc1394_mode, impl->packet_size);
    uint64_t bytes_per_frame;
    dc1394_format7_get_total_bytes (impl->cam, format->dc1394_mode, &bytes_per_frame);

    if (bytes_per_frame * impl->num_buffers > 25000000) {
        printf ("Reducing dc1394 buffers from %d to ", impl->num_buffers);
        impl->num_buffers = 25000000 / bytes_per_frame;
        printf ("%d\n", impl->num_buffers);
    }

    /* Using libdc1394 for iso streaming */
    if (dc1394_capture_setup (impl->cam, impl->num_buffers,
                              DC1394_CAPTURE_FLAGS_DEFAULT) != DC1394_SUCCESS)
        goto fail;

    if (dc1394_video_set_transmission (impl->cam, DC1394_ON) != DC1394_SUCCESS)
        goto fail;

    impl->fd = dc1394_capture_get_fileno (impl->cam);

    impl->started = 1;

    return 0;

fail:
    if (have_reset_bus) {
        fprintf (stderr, "----------------------------------------------------------------\n");
        fprintf (stderr, "Error: failed to initialize dc1394 stream\n");
        fprintf (stderr, "\nIF YOU HAVE HAD A CAMERA FAIL TO EXIT CLEANLY OR\n");
        fprintf (stderr, " THE BANDWIDTH HAS BEEN OVER SUBSCRIBED TRY (to reset):\n");
        fprintf (stderr, "dc1394_reset_bus\n\n");
        fprintf (stderr, "----------------------------------------------------------------\n");
        return -1;
    } else {
        fprintf (stderr, "----------------------------------------------------------------\n");
        fprintf (stderr, "image_source_dc1394: Camera startup failed, reseting bus.\n");
        fprintf (stderr, "(this is harmless if the last program didn't quit cleanly,\n");
        fprintf (stderr, "but things may not work well if bandwidth is over-subscribed.\n");
        fprintf (stderr, "----------------------------------------------------------------\n");

        dc1394_reset_bus (impl->cam);

        have_reset_bus = 1;
        goto restart;
    }
}

static int
get_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t*) isrc->impl;

    memset (frmd, 0, sizeof(*frmd));

    if (impl->started == 0)
        printf ("image_source_dc1394: get_frame called on a source that has not been started. Crash imminent.\n");

    assert (impl->current_frame == NULL);

    while (1) {

        if (dc1394_capture_dequeue (impl->cam, DC1394_CAPTURE_POLICY_WAIT, &impl->current_frame) != DC1394_SUCCESS) {
            printf ("DC1394 dequeue failed\n");
            return -1;
        }

        if (impl->current_frame->frames_behind > 0 || dc1394_capture_is_frame_corrupt (impl->cam, impl->current_frame) == DC1394_TRUE) {
            dc1394_capture_enqueue (impl->cam, impl->current_frame);
            continue;
        }

        break;
    }

    frmd->data = impl->current_frame->image;
    frmd->datalen= impl->current_frame->image_bytes;
    frmd->ifmt.width = impl->formats[impl->current_format_idx].width;
    frmd->ifmt.height = impl->formats[impl->current_format_idx].height;
    strcpy(frmd->ifmt.format, impl->formats[impl->current_format_idx].format);;

    frmd->utime = utime_now (); //XXX Can we use information from camera to improve this?

    return 0;
}

static int
release_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    assert(isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    dc1394_capture_enqueue (impl->cam, impl->current_frame);
    impl->current_frame = NULL;

    return 0;
}

static int
stop (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    dc1394_video_set_transmission (impl->cam, DC1394_OFF);

    dc1394_capture_stop (impl->cam);

    impl->started = 0;

    return 0;
}

static int
my_close (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    return close (impl->fd);
}

static void
print_info (image_source_t *isrc)
{
    impl_dc1394_t *impl = (impl_dc1394_t *) isrc->impl;

    // camera info
    printf ("========================================\n");
    printf (" DC1394 Info\n");
    printf ("========================================\n");
    dc1394_camera_print_info (impl->cam, stdout);

    // feature info
    dc1394_feature_get_all (impl->cam, &impl->features);
    dc1394_feature_print_all (&impl->features, stdout);

    // strobe info
    {
        printf ("Strobe registers:\n");

        uint32_t val = 0;
        int offset = 0;
        if (dc1394_get_strobe_register (impl->cam, offset, &val) == DC1394_SUCCESS) {
            printf ("\tlocation: 0x%08lX reads little endian: 0x%08X (big endian: 0x%08X)\n",
                    impl->cam->strobe_control_csr + offset,
                    val,
                    flip_endianness (val));
        }
        else {
            printf ("\terror reading location 0x%08lX\n",
                    impl->cam->strobe_control_csr + offset);
        }

        printf ("\n\tThe following registers are only known to be defined for Point Grey cameras:\n");
        for (int offset=0x100; offset <= 0x10C; offset += 0x4) {
            if (dc1394_get_strobe_register (impl->cam, offset, &val) == DC1394_SUCCESS) {
                printf("\tlocation: 0x%08lX reads little endian: 0x%08X (big endian: 0x%08X)\n",
                       impl->cam->strobe_control_csr + offset,
                       val,
                       flip_endianness (val));
            }
            else {
                printf ("\terror reading location 0x%08lX\n",
                        impl->cam->strobe_control_csr + offset);
            }
        }

        for (int offset=0x200; offset <= 0x20C; offset += 0x4) {
            if (dc1394_get_strobe_register (impl->cam, offset, &val) == DC1394_SUCCESS) {
                printf ("\tlocation: 0x%08lX reads little endian: 0x%08X (big endian: 0x%08X)\n",
                       impl->cam->strobe_control_csr + offset,
                       val,
                       flip_endianness (val));
            }
            else {
                printf ("\terror reading location 0x%08lX\n",
                        impl->cam->strobe_control_csr + offset);
            }
        }
    }

    // trigger info
    {
        printf ("Trigger:\n");
//        dc1394error_t res = 0;

        dc1394trigger_mode_t mode;
        if (dc1394_external_trigger_get_mode (impl->cam, &mode) == DC1394_SUCCESS) {
            switch (mode) {
                case DC1394_TRIGGER_MODE_0:
                    printf("\tmode 0\n");
                    break;
                case DC1394_TRIGGER_MODE_1:
                    printf("\tmode 1\n");
                    break;
                case DC1394_TRIGGER_MODE_2:
                    printf("\tmode 2\n");
                    break;
                case DC1394_TRIGGER_MODE_3:
                    printf("\tmode 3\n");
                    break;
                case DC1394_TRIGGER_MODE_4:
                    printf("\tmode 4\n");
                    break;
                case DC1394_TRIGGER_MODE_5:
                    printf("\tmode 5\n");
                    break;
                case DC1394_TRIGGER_MODE_14:
                    printf("\tmode 14\n");
                    break;
                case DC1394_TRIGGER_MODE_15:
                    printf("\tmode 15\n");
                    break;
            }
        }
        else
            printf ("\terror reading mode\n");

        dc1394trigger_polarity_t polarity;
        if (dc1394_external_trigger_get_polarity (impl->cam, &polarity) == DC1394_SUCCESS) {
            switch (polarity) {
                case DC1394_TRIGGER_ACTIVE_LOW:
                    printf ("\tactive low\n");
                    break;
                case DC1394_TRIGGER_ACTIVE_HIGH:
                    printf ("\tactive high\n");
                    break;
            }
        }
        else
            printf ("\terror reading polarity\n");

        dc1394bool_t polarity_capable;
        if (dc1394_external_trigger_has_polarity (impl->cam, &polarity_capable) == DC1394_SUCCESS)
            printf ("\tpolarity %s\n", polarity_capable == DC1394_TRUE ? "capable" : "incapable");
        else
            printf ("\terror reading if polarity capable\n");

        dc1394switch_t ext_pwr;
        if (dc1394_external_trigger_get_power (impl->cam, &ext_pwr) == DC1394_SUCCESS)
            printf ("\texternally enabled: %s\n", ext_pwr == DC1394_ON ? "true" : "false");
        else
            printf ("\terror reading external trigger power\n");

        dc1394switch_t sw_pwr;
        if (dc1394_software_trigger_get_power (impl->cam, &sw_pwr) == DC1394_SUCCESS)
            printf ("\tsoftware enabled: %s\n", sw_pwr == DC1394_ON ? "true" : "false");
        else
            printf ("\terror reading if software powered\n");

        dc1394trigger_source_t source;
        if (dc1394_external_trigger_get_source (impl->cam, &source) == DC1394_SUCCESS) {
            switch (source) {
                case DC1394_TRIGGER_SOURCE_0:
                    printf ("\tsource (current): GPIO 0\n");
                    break;
                case DC1394_TRIGGER_SOURCE_1:
                    printf ("\tsource (current): GPIO 1\n");
                    break;
                case DC1394_TRIGGER_SOURCE_2:
                    printf ("\tsource (current): GPIO 2\n");
                    break;
                case DC1394_TRIGGER_SOURCE_3:
                    printf ("\tsource (current): GPIO 3\n");
                    break;
                case DC1394_TRIGGER_SOURCE_SOFTWARE:
                    printf ("\tsource (current): Software\n");
                    break;
            }
        }
        else
            printf ("\terror reading source\n");

        dc1394trigger_sources_t sources;
        if (dc1394_external_trigger_get_supported_sources (impl->cam, &sources) == DC1394_SUCCESS) {
            printf ("\tsources supported:\n");
            for (int i=0; i < sources.num; i++) {
                printf ("\t\t[%d] ", i);
                switch (sources.sources[i]) {
                    case DC1394_TRIGGER_SOURCE_0:
                        printf ("source: GPIO 0\n");
                        break;
                    case DC1394_TRIGGER_SOURCE_1:
                        printf ("source: GPIO 1\n");
                        break;
                    case DC1394_TRIGGER_SOURCE_2:
                        printf ("source: GPIO 2\n");
                        break;
                    case DC1394_TRIGGER_SOURCE_3:
                        printf ("source: GPIO 3\n");
                        break;
                    case DC1394_TRIGGER_SOURCE_SOFTWARE:
                        printf ("source: Software\n");
                        break;
                }
            }
        }
        else
            printf ("\terror reading sources supported\n");
    }

    fflush (stdout);
}

/** Open the given guid, or if -1, open the first camera available. **/
image_source_t *
image_source_dc1394_open (url_parser_t *urlp)
{
//    const char *protocol = url_parser_get_protocol(urlp);
    const char *location = url_parser_get_host (urlp);

    int64_t guid = 0;

    if (0==strlen (location)) {
        // use the first dc1394 camera in the system
        dc1394_t *dc1394 = dc1394_new ();
        if (dc1394 == NULL)
            return NULL;

        dc1394camera_list_t *list;

        if (dc1394_camera_enumerate (dc1394, &list)) {
            dc1394_free (dc1394);
            return NULL;
        }

        if (list->num > 0) {
            guid = list->ids[0].guid;
        }

        dc1394_camera_free_list (list);
    }
    else if (strto64 (location, strlen(location), &guid)) {
        printf ("image_source_open: dc1394 guid '%s' is not a valid integer.\n", location);
        return NULL;
    }

    image_source_t *isrc = calloc (1, sizeof(*isrc));
    impl_dc1394_t *impl = calloc (1, sizeof(*impl));

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

    impl->num_buffers = 2;

    impl->dc1394 = dc1394_new ();
    if (!impl->dc1394)
        goto fail;

    // now open our desired camera.
    impl->cam = dc1394_camera_new(impl->dc1394, guid);
    if (impl->cam == NULL)
        goto fail;

    dc1394format7modeset_t info;
    if (dc1394_format7_get_modeset (impl->cam, &info) != DC1394_SUCCESS)
        goto fail;

    for (int i = 0; i < DC1394_VIDEO_MODE_FORMAT7_NUM; i++) {

        dc1394format7mode_t *mode = info.mode + i;

        if (!info.mode[i].present)
            continue;

        for (int j = 0; j < mode->color_codings.num; j++) {

            impl->formats = realloc (impl->formats, (impl->nformats+1) * sizeof(struct format));

            impl->formats[impl->nformats].width = mode->max_size_x;
            impl->formats[impl->nformats].height = mode->max_size_y;
            const char *s = toformat (mode->color_codings.codings[j], mode->color_filter);
            strcpy (impl->formats[impl->nformats].format, s);

            impl->formats[impl->nformats].dc1394_mode = DC1394_VIDEO_MODE_FORMAT7_0 + i;
            impl->formats[impl->nformats].format7_mode_idx = i;
            impl->formats[impl->nformats].color_coding_idx = j;

            impl->nformats++;
        }
    }

    dc1394_feature_get_all (impl->cam, &impl->features);

    if (1) {
        // ptgrey cameras don't seem to have any cases of this...
        int reread = 0;

        for (int i = 0; i < DC1394_FEATURE_NUM; i++) {
            dc1394feature_info_t *f = &impl->features.feature[i];
            if (f->available && f->absolute_capable && !f->abs_control) {

                printf ("image_source_dc1394.c: automatically enabled absolute mode for dc1394 feature '%s'\n",
                        dc1394_feature_id_to_string(f->id));
                fflush (stdout);
                dc1394_feature_set_absolute_control (impl->cam, f->id, DC1394_ON);

                reread = 1;
            }
        }

        if (reread)
            dc1394_feature_get_all (impl->cam, &impl->features);
    }

    if (0) {
        // work around an intermittent bug where sometimes some
        // garbage causes the camera data to be offset by about a
        // third of a scanline.
        isrc->start(isrc);

        image_source_data_t *frmd = calloc (1, sizeof(*frmd));
        if (!isrc->get_frame(isrc, frmd)) {
            isrc->release_frame (isrc, frmd);
        }

        isrc->stop (isrc);
    }

    return isrc;

fail:
    printf ("image_source_dc1394_open: failure\n");
    free (isrc);
    return NULL;
}

void
image_source_enumerate_dc1394 (zarray_t *urls)
{
    dc1394_t *dc1394;
    dc1394camera_list_t *list;

    dc1394 = dc1394_new ();
    if (dc1394 == NULL)
        return;

    if (dc1394_camera_enumerate (dc1394, &list) < 0)
        goto exit;

    // display all cameras for convenience
    for (int i = 0; i < list->num; i++) {
        dc1394camera_t *cam = dc1394_camera_new (dc1394, list->ids[i].guid);
        if (cam == NULL)
            continue;

        char buf[1024];

        // other useful fields: cam->vendor, cam->model);
        snprintf (buf, 1024, "dc1394://%"PRIx64, list->ids[i].guid);
        char *p = strdup (buf);
        zarray_add (urls, &p);
        dc1394_camera_free (cam);
    }

    dc1394_camera_free_list (list);

exit:
    dc1394_free (dc1394);
    return;
}

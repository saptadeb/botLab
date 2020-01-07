#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>

#include "common/ssocket.h"
#include "common/string_util.h"
#include "common/url_parser.h"

#include "image_source.h"

#define IMPL_TYPE 0x33761723


typedef struct impl_tcp impl_tcp_t;
struct impl_tcp {
    char *hostname;
    int port;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int width, height;
    char format[IMAGE_SOURCE_MAX_FORMAT_LENGTH];
    uint32_t datalen;
    uint8_t *data;

    pthread_t reader;
};

static int64_t
utime_now (void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}

static int
read_u32 (FILE *f, uint32_t *v)
{
    uint8_t buf[4];

    int res = fread(buf, 1, 4, f);
    if (res != 4)
        return -1;

    *v = (buf[0]<<24) + (buf[1]<<16) + (buf[2]<<8) + buf[3];
    return 0;
}

static int
read_u64 (FILE *f, uint64_t *v)
{
    uint32_t h, l;

    if (read_u32(f, &h) || read_u32(f, &l))
        return -1;

    *v = (((uint64_t) h)<<32) + l;
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
    assert(isrc->impl_type == IMPL_TYPE);
    impl_tcp_t *impl = (impl_tcp_t*) isrc->impl;

    // wait for at least one frame....
    pthread_mutex_lock(&impl->mutex);
    while (impl->datalen == 0)
        pthread_cond_wait(&impl->cond, &impl->mutex);

    memset(fmt, 0, sizeof(image_source_format_t));
    fmt->width = impl->width;
    fmt->height = impl->height;
    strcpy(fmt->format, impl->format);

    pthread_mutex_unlock(&impl->mutex);
}

static int
get_current_format (image_source_t *isrc)
{
    return 0;
}

static int
set_format (image_source_t *isrc, int idx)
{
    return 0;
}

static int
set_named_format (image_source_t *isrc, const char *desired_format)
{
    return 0;
}

static int
num_features (image_source_t *isrc)
{
    return 0;
}

static const char *
get_feature_name (image_source_t *isrc, int idx)
{
    return NULL;
}

static char *
get_feature_type (image_source_t *isrc, int idx)
{
    return NULL;
}

static double
get_feature_value (image_source_t *isrc, int idx)
{
    return 0;
}

static int
set_feature_value (image_source_t *isrc, int idx, double v)
{
    return 0;
}

static int start(image_source_t *isrc)
{
    return 0;
}

static int
get_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    assert(isrc->impl_type == IMPL_TYPE);
    impl_tcp_t *impl = isrc->impl;

    memset(frmd, 0, sizeof(*frmd));

    pthread_mutex_lock(&impl->mutex);
    while (impl->data == NULL)
        pthread_cond_wait(&impl->cond, &impl->mutex);

    frmd->ifmt.width = impl->width;
    frmd->ifmt.height = impl->height;
    strcpy(frmd->ifmt.format, impl->format);
    frmd->data = impl->data;
    frmd->datalen = impl->datalen;
    frmd->utime = utime_now();
    impl->data = NULL;

    pthread_mutex_unlock(&impl->mutex);
    return 0;
}

static int
release_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    free(frmd->data);
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
    printf ("========================================\n");
    printf (" TCP Info\n");
    printf ("========================================\n");
}

static void *
reader_thread (void *arg)
{
    impl_tcp_t *impl = arg;

    while (1) {
        FILE *f = NULL;
        ssocket_t *sock = ssocket_create();

        if (ssocket_connect(sock, impl->hostname, impl->port))
            goto error;

        int fd = ssocket_get_fd(sock);
        f = fdopen(fd, "r");

        while(1) {
            uint64_t sync, utime;
            uint32_t width, height, formatlen;

            if (read_u64(f, &sync))
                break;

            if (sync != 0x17923349ab10ea9aL)
                continue;

            if (read_u64(f, &utime) ||
                read_u32(f, &width) ||
                read_u32(f, &height) ||
                read_u32(f, &formatlen))
                break;

            uint8_t *format = calloc(1, formatlen+1);
            if (fread(format, 1, formatlen, f) != formatlen)
                break;

            uint32_t datalen;
            if (read_u32(f, &datalen))
                break;

            uint8_t *data = malloc(datalen);
            if (fread(data, 1, datalen, f) != datalen)
                break;

            pthread_mutex_lock(&impl->mutex);
            impl->width = width;
            impl->height = height;
            impl->datalen = datalen;

            strncpy(impl->format, (char*) format, IMAGE_SOURCE_MAX_FORMAT_LENGTH);

            if (impl->data != NULL)
                free(impl->data);

            impl->data = data;

            pthread_cond_broadcast(&impl->cond);
            pthread_mutex_unlock(&impl->mutex);
        }

      error:
        ssocket_destroy(sock);

        printf("image_source_tcp connection lost.\n");
        fclose(f);
        sleep(1);
    }

    return NULL;
}

image_source_t *
image_source_tcp_open (url_parser_t *urlp)
{
    image_source_t *isrc = calloc (1, sizeof(*isrc));
    impl_tcp_t *impl = calloc (1, sizeof(*impl));

    isrc->impl_type = IMPL_TYPE;
    isrc->impl = impl;

    impl->hostname = strdup(url_parser_get_host(urlp));
    impl->port = url_parser_get_port(urlp);
    if (impl->port < 0)
        impl->port = 7701;

    strcpy(impl->format, "NONE");

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

    pthread_mutex_init(&impl->mutex, NULL);
    pthread_cond_init(&impl->cond, NULL);
    pthread_create(&impl->reader, NULL, reader_thread, impl);

    return isrc;
}


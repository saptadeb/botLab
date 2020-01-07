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
#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <assert.h>

#include "common/string_util.h"
#include "image_source.h"

/*
http://www.1394ta.org/press/WhitePapers/Firewire%20Reference%20Tutorial.pdf
http://damien.douxchamps.net/ieee1394/libdc1394/iidc/IIDC_1.31.pdf


libusb thread safety must be carefully managed. Our strategy: we use
synchronous calls for configuration/etc, and asynchronous calls for
streaming data. To support asynchronous calls, we spawn off an event
handling thread. Because this event-handling thread uses the same
poll/selectx mechanisms as the libusb synchronous calls, we must
ensure that we are never performing synchronous calls while the thread
is running.

*/
#define REQUEST_TIMEOUT_MS 100
#define TIMEOUT_MS 0

#define IMPL_TYPE 0x7123b65a

static int debug = 0;

struct feature {
    char   *name;
    int    (*is_available)(image_source_t *isrc, struct feature *f);
    char*  (*get_type)(image_source_t *isrc, struct feature *f);
    double (*get_value)(image_source_t *isrc, struct feature *f);
    int   (*set_value)(image_source_t *isrc, struct feature *f, double v);

    int value_lsb;
    int value_size;

    uint32_t user_addr;
    void   *user_ptr;

    uint32_t absolute_csr;
};

struct image_info {
    int status;

/** if 0, image is empty and available.
    if -1, image is being filled.
    if -2, image has been returned to user.
    if >=1, the frame is queued to be returned to the user, low numbers first.
**/

    uint8_t *buf;
};

struct transfer_info {
    struct libusb_transfer *transfer;
    uint8_t *buf;
};

struct format {
    int width, height;
    char format[IMAGE_SOURCE_MAX_FORMAT_LENGTH];

    uint32_t format7_mode_idx;
    uint32_t color_coding_idx;
    uint32_t csr;
};


typedef struct impl_pgusb impl_pgusb_t;
struct impl_pgusb {
    libusb_context *context;
    libusb_device *dev;
    libusb_device_handle *handle;

    int nformats;
    struct format *formats;
    int current_format_idx;

    // must add CONFIG_ROM_BASE to each of these.
    uint32_t unit_directory_offset;
    uint32_t unit_dependent_directory_offset;
    uint32_t command_regs_base;

    int bytes_per_frame; // how many bytes are actually used in each image?
    int bytes_transferred_per_frame; // how many bytes transferred per frame?
    int transfer_size;   // size of a single USB transaction

    int packet_size;
    int packets_per_image;

    int ntransfers;
    struct transfer_info *transfers;

    int nimages;
    struct image_info *images;

    pthread_t worker_thread;

    volatile int worker_exit_flag;

    // queue is used to access images (not transfers)
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;

    int current_frame_index; // which frame are we receiving from the camera?
    int current_frame_offset;

    int current_user_frame; // what frame is currently in the user's possession?

    pthread_mutex_t pending_transaction_mutex;
    volatile int transfers_submitted;

    volatile int consume_transfers_flag;


    struct feature *features;
    int nfeatures;

};

struct usb_vendor_product {
    uint16_t vendor;
    uint16_t product;
};

static struct usb_vendor_product vendor_products[] =
{
    { 0x1e10, 0x2000 }, // Point Grey Firefly MV Color
    { 0x1e10, 0x2001 }, // Point Grey Firefly MV Mono
    { 0x1e10, 0x2004 }, // Point Grey Chameleon Color
    { 0x1e10, 0x2005 }, // Point Grey Chameleon Mono
    { 0, 0 },
};

static const char* COLOR_MODES[] = { "GRAY", "YUV422", "UYVY", "IYU2", "RGB",
                                     "GRAY16", "RGB16", "GRAY16", "SRGB16", "RAW8", "RAW16" };
static const char* BAYER_MODES[] = { "BAYER_RGGB", "BAYER_GBRG", "BAYER_GRBG", "BAYER_BGGR" };

#define CONFIG_ROM_BASE             0xFFFFF0000000ULL

static int64_t
utime_now (void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
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
            printf ("%c", c); //return -1;
        acc = (acc<<4) + ic;
    }

    *ov = acc;
    return 0;
}

static int
strposat (const char *haystack, const char *needle, int haystackpos)
{
    int idx = haystackpos;
    int needlelen = strlen(needle);

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

// The high 16 bits of the IEEE 1394 address space are mapped to the
// request byte of USB control transfers.  Only a discrete set
// addresses are currently supported, as mapped by this function.
static int
address_to_request (uint64_t address)
{
    switch (address >> 32) {
        case 0xffff:
            return 0x7f;
        case 0xd000:
            return 0x80;
        case 0xd0001:
            return 0x81;
    }

    assert (0);
    return -1;
}

/** Find an image which we can write into. Returns -1 if no frame is
 * currently available; you'll have to drop data. Used by CALLBACK
 * thread. **/
static int
get_empty_frame (impl_pgusb_t *impl)
{
    int idx = -1;
    pthread_mutex_lock (&impl->queue_mutex);

    // disable this if low latency isn't your primary goal
    if (1) {
        // Do we have more than one "ready" frame? (I.e., has the user
        // fallen behind? If so, release all but the most recent frame,
        // and renumber the most recent frame to status=1.
        int max_ready = 0;
        for (int i = 0; i < impl->nimages; i++) {
            if (impl->images[i].status > max_ready)
                max_ready = impl->images[i].status;
        }

        if (max_ready > 0) {
            for (int i = 0; i < impl->nimages; i++) {
                if (impl->images[i].status > 0) {
                    if (impl->images[i].status == max_ready)
                        impl->images[i].status = 1;
                    else
                        impl->images[i].status = 0;
                }
            }
        }
    }


    // Otherwise, find an unused frame and return it.
    for (int i = 0; i < impl->nimages; i++) {
        if (impl->images[i].status == 0) {
            idx = i;
            impl->images[i].status = -1;
            break;
        }
    }

    pthread_mutex_unlock (&impl->queue_mutex);
    return idx;
}

/** Return a frame buffer to the pool. Called by release_frame **/
static void
put_empty_frame (impl_pgusb_t *impl, int idx)
{
    assert (idx >= 0);

    pthread_mutex_lock (&impl->queue_mutex);
    assert (impl->images[idx].status == -2);
    impl->images[idx].status = 0;
    pthread_mutex_unlock (&impl->queue_mutex);
}

/** An image is now ready for return to the user. **/
static void
put_ready_frame (impl_pgusb_t *impl, int idx)
{
    pthread_mutex_lock (&impl->queue_mutex);

    assert (idx >= 0);
    assert (impl->images[idx].status == -1);

    int maxstatus = 0;

    // where in the queue should it go?
    for (int i = 0; i < impl->nimages; i++) {
        if (impl->images[i].status > maxstatus)
            maxstatus = impl->images[i].status;
    }

    assert (maxstatus <= impl->nimages);
    impl->images[idx].status = maxstatus + 1;

    pthread_cond_broadcast (&impl->queue_cond);
    pthread_mutex_unlock (&impl->queue_mutex);
}

/** Retrieve an image for the user, waiting if necessary. **/
static int
get_ready_frame (impl_pgusb_t *impl)
{
    pthread_mutex_lock (&impl->queue_mutex);

    while (1) {
        // do we have a frame ready to go?
        for (int i = 0; i < impl->nimages; i++) {
            if (impl->images[i].status == 1) {
                // yes!
                impl->images[i].status = -2;

                // move any other frames up in the queue.
                for (int j = 0; j < impl->nimages; j++) {
                    if (impl->images[j].status >= 1)
                        impl->images[j].status--;
                }

                pthread_mutex_unlock (&impl->queue_mutex);
                return i;
            }
        }

        pthread_cond_wait (&impl->queue_cond, &impl->queue_mutex);
    }
}

static int
do_read (libusb_device_handle *handle, uint64_t address, uint32_t *quads, int num_quads)
{
    int request = address_to_request (address);
    if (request < 0)
        return -1;

    unsigned char buf[num_quads*4];

    // IEEE 1394 address reads are mapped to USB control transfers as
    // shown here.
retry: ;
    int ret = libusb_control_transfer (handle, 0xc0, request,
                                       address & 0xffff, (address >> 16) & 0xffff,
                                       buf, num_quads * 4, REQUEST_TIMEOUT_MS);

    if (ret == LIBUSB_ERROR_TIMEOUT) {
        printf ("pgusb timeout...\n");

//        libusb_reset_device(handle);
        goto retry;
    }

    if (ret < 0)
        return -1;

    int ret_quads = (ret + 3) / 4;

    // Convert from little-endian to host-endian
    for (int i = 0; i < ret_quads; i++) {
        quads[i] = (buf[4*i+3] << 24) | (buf[4*i+2] << 16)
            | (buf[4*i+1] << 8) | buf[4*i];
    }

    return ret_quads;
}

static int
do_write (libusb_device_handle *handle, uint64_t address, uint32_t *quads, int num_quads)
{
    int request = address_to_request (address);
    if (request < 0)
        return -1;

    if (debug)
        printf ("DO_WRITE %08" PRIx64 " := %08x\n", address, quads[0]);

    unsigned char buf[num_quads*4];

    // Convert from host-endian to little-endian
    for (int i = 0; i < num_quads; i++) {
        buf[4*i]   = quads[i] & 0xff;
        buf[4*i+1] = (quads[i] >> 8) & 0xff;
        buf[4*i+2] = (quads[i] >> 16) & 0xff;
        buf[4*i+3] = (quads[i] >> 24) & 0xff;
    }

    // IEEE 1394 address writes are mapped to USB control transfers as
    // shown here.
retry: ;

    int ret = libusb_control_transfer (handle, 0x40, request,
                                       address & 0xffff, (address >> 16) & 0xffff,
                                       buf, num_quads * 4, REQUEST_TIMEOUT_MS);

    if (ret == LIBUSB_ERROR_TIMEOUT) {
        printf ("pgusb timeout...\n");
        goto retry;
    }

    if (ret < 0)
        return -1;
    return ret / 4;
}

// returns number of quads actually read.
static int
read_config_rom (libusb_device *dev, uint32_t *quads, int nquads)
{
    libusb_device_handle *handle;
    if (libusb_open (dev, &handle) < 0) {
        printf("error");
        return -1;
    }

    int i = 0;
    for (i = 0; i < nquads; i++) {
        int ret = do_read (handle, CONFIG_ROM_BASE + 0x400 + 4*i, &quads[i], 1);
        if (ret < 1) {
            printf ("read_config_rom failed with ret=%d on word %d\n", ret, i);
            break;
        }
    }

    libusb_close (handle);
    return i;
}

static uint64_t
get_guid (libusb_device *dev)
{
    uint32_t config[5];

    if (read_config_rom (dev, config, 5) < 5) {
        printf ("error reading camera GUID\n");
        return -1;
    }

    // not an IIDC camera?
    if ((config[0] >> 24) != 0x4)
        return 0;

    return ((uint64_t) config[3] << 32) | config[4];
}

void
image_source_enumerate_pgusb (zarray_t *urls)
{
    libusb_context *context;

    if (libusb_init (&context) != 0) {
        printf ("Couldn't initialize libusb\n");
        return;
    }

    libusb_device **devs;
    int ndevs = libusb_get_device_list (context, &devs);

    for (int i = 0; i < ndevs; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor (devs[i], &desc) != 0) {
            printf ("couldn't get descriptor for device %d\n", i);
            continue;
        }

        int okay = 0;
        for (int j = 0; vendor_products[j].vendor != 0; j++) {
            if (desc.idVendor == vendor_products[j].vendor &&
                desc.idProduct == vendor_products[j].product) {
                okay = 1;
            }
        }

        if (okay) {
            uint64_t guid = get_guid (devs[i]);

            if (guid != 0xffffffffffffffffULL) {
                char buf[1024];
                snprintf (buf, 1024, "pgusb://%"PRIx64, guid);
                char *p = strdup (buf);
                zarray_add (urls, &p);
            }
        }
    }

    libusb_free_device_list (devs, 1);

    libusb_exit (context);
}


static int
num_formats (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

    return impl->nformats;
}

static void
get_format (image_source_t *isrc, int idx, image_source_format_t *fmt)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

    fmt->width = impl->formats[idx].width;
    fmt->height = impl->formats[idx].height;
    strcpy (fmt->format, impl->formats[idx].format);
}

static int
get_current_format(image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

    return impl->current_format_idx;
}

static int
set_format (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

    assert (idx>=0 && idx < impl->nformats);

    impl->current_format_idx = idx;

    return 0;
}

static int
set_named_format (image_source_t *isrc, const char *desired_format)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

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
        exit(EXIT_FAILURE);
    }

    impl->current_format_idx = fidx;

    free (format_name);
    return 0;
}

static int
num_features (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

    // don't forget: feature index starts at 0
    return impl->nfeatures;
}

static void
stop_callback (struct libusb_transfer *transfer)
{
    impl_pgusb_t *impl = (impl_pgusb_t*) transfer->user_data;

    for (int i = 0; i < impl->ntransfers; i++) {
        libusb_cancel_transfer (impl->transfers[i].transfer);
    }

    impl->consume_transfers_flag = 1;

    free (transfer->buffer);
    libusb_free_transfer (transfer);
}

static void
callback (struct libusb_transfer *transfer)
{
    image_source_t *isrc = transfer->user_data;
    assert(isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;
//    image_source_format_t *ifmt = get_format(isrc, get_current_format(isrc));

    if (impl->current_frame_index < 0)
        impl->current_frame_index = get_empty_frame (impl);

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED && impl->current_frame_index >= 0) {

        if (debug) {
            printf ("TRANSFER flags %03d, status %03d, length %5d, actual %5d, current_frame_offset %10d / %d\n",
                    transfer->flags, transfer->status, transfer->length,
                    transfer->actual_length, impl->current_frame_offset, impl->bytes_transferred_per_frame);
        }

        int cpysz = impl->bytes_transferred_per_frame - impl->current_frame_offset;
        if (cpysz > transfer->actual_length)
            cpysz = transfer->actual_length;

        memcpy (&impl->images[impl->current_frame_index].buf[impl->current_frame_offset], transfer->buffer, cpysz);
        impl->current_frame_offset += cpysz;

        if (impl->current_frame_offset == impl->bytes_transferred_per_frame) {
            put_ready_frame (impl, impl->current_frame_index);

            // If there's more data available, stuff it into the next
            // frame.  NOTE: We don't like to do this; this means that
            // the image was a multiple of 512, and as a consequence,
            // the camera didn't generate a short USB frame. It's the
            // short USB frames that help us recover camera
            // synchronization; without them, we can only guess that
            // we've maintained synchronization.
            impl->current_frame_index = get_empty_frame (impl);
            impl->current_frame_offset = 0;

            if (impl->current_frame_index >= 0) {
                memcpy (impl->images[impl->current_frame_index].buf, &transfer->buffer[cpysz], transfer->actual_length - cpysz);
                impl->current_frame_offset += transfer->actual_length - cpysz;
            }
        }

        if (transfer->actual_length != transfer->length) {
//        if (transfer->actual_length == 512) {
            // we lost sync. start over.
            if (impl->current_frame_offset != 0)
                printf ("sync: current_frame_offset %8d, transfer_actual %8d, transfer_length %8d\n",
                        impl->current_frame_offset, transfer->actual_length, transfer->length);

//            put_ready_frame(impl, impl->current_frame_index);
//            impl->current_frame_index = -1;
//            impl->current_frame_offset = (impl->current_frame_offset + 512) % impl->bytes_transferred_per_frame;
            impl->current_frame_offset = 0;
        }

    }
    else {
        if (impl->current_frame_index < 0)
            printf ("Ran out of frame buffers\n");
        else
            printf ("ERROR    flags %03d, status %03d, length %5d, actual %5d, current_frame_offset %10d / %d, current_frame_index %d\n",
                    transfer->flags, transfer->status, transfer->length, transfer->actual_length,
                    impl->current_frame_offset, impl->bytes_transferred_per_frame, impl->current_frame_index);
    }

    pthread_mutex_lock (&impl->pending_transaction_mutex);

    impl->transfers_submitted--;

    if (!impl->consume_transfers_flag) {
        // resubmit the transfer.
        libusb_fill_bulk_transfer (transfer, impl->handle,
                                   0x81, transfer->buffer, impl->transfer_size, callback, isrc, TIMEOUT_MS);

        if (libusb_submit_transfer (transfer) < 0) {
            printf ("***** UH OH ****** submit failed\n");
        }

        impl->transfers_submitted++;
    }
    pthread_mutex_unlock (&impl->pending_transaction_mutex);
}

static void *
worker_thread_proc (void *arg)
{
    image_source_t *isrc = arg;
    impl_pgusb_t *impl = isrc->impl;

    while (!impl->worker_exit_flag) {

        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 100000,
        };

        libusb_handle_events_timeout (impl->context, &tv);
    }

    return NULL;
}

static void
do_value_setting (image_source_t *isrc)
{
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;
    struct format *format = &impl->formats[get_current_format(isrc)];

    uint32_t quads[] = { 0x40000000 };

    if (do_write(impl->handle, CONFIG_ROM_BASE + format->csr + 0x7c, quads, 1) != 1)
        printf ("failed write: line %d\n", __LINE__);

    while (1) {
        uint32_t resp;

        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x7c, &resp, 1);

        if (debug)
            printf ("do_value_setting result: %08x\n", resp);

        if (resp & 0x80000000)
            break;
    }
}

static int
start(image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;
    struct format *format = &impl->formats[get_current_format(isrc)];

//    printf("CURRENT FORMAT %d\n", get_current_format(isrc));
//    printf("FORMAT_PRIV: %d %d %d\n", format_priv->format7_mode_idx, format_priv->color_coding_idx, format_priv->csr);

    int bytes_per_pixel = 1;
    if ((format->color_coding_idx >=5 && format->color_coding_idx <= 8) ||
        (format->color_coding_idx == 10)) {
        bytes_per_pixel = 2;
    }

    impl->bytes_per_frame = format->width * format->height * bytes_per_pixel;

    // set iso channel
    if (1) {
        uint32_t quads[] = { 0x02000000 }; //
        if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x60c, quads, 1) != 1)
            printf ("failed write: line %d\n", __LINE__);
    }
    do_value_setting (isrc);

    // shutter 81c = c3000212


    // gain 820 = c3000040


    // feature_hi? 834 = c0000000

    // feature_hi? 83c = c30001e0

    // set format 7  (608 = e0000000)
    if (1) {
        uint32_t quads[] = { 0xe0000000 }; // 7 << 29
        if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x608, quads, 1) != 1)
            printf ("failed write: line %d\n", __LINE__);
    }

    // set format 7 mode (604 = 00000000)
    if (1) {
        if (debug)
            printf ("FORMAT7_MODE_IDX %d\n", format->format7_mode_idx);

        uint32_t quads[] = { format->format7_mode_idx << 29 };
        if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x604, quads, 1) != 1)
            printf ("failed write: line %d\n", __LINE__);
    }

    // set iso channel (again?) 60c = 02000000 (UNNECESSARY?)

    // set image size a0c = 02f001e0
    if (1) {
        uint32_t quads[] = { format->height + (format->width<<16) };
        if (do_write (impl->handle, CONFIG_ROM_BASE + format->csr + 0x0c, quads, 1) != 1)
            printf ("failed write: line %d\n", __LINE__);
    }
    do_value_setting (isrc);

    // set image position a08 = 00000000
    if (1) {
        uint32_t quads[] = { 0 };
        if (do_write (impl->handle, CONFIG_ROM_BASE + format->csr + 0x08, quads, 1) != 1)
            printf ("failed write: line %d\n", __LINE__);
    }
    do_value_setting (isrc);

    // set format 7 color mode (a10 = 00000000)
    if (1) {
        uint32_t quads[] = { format->color_coding_idx<<24 };
        if (do_write (impl->handle, CONFIG_ROM_BASE + format->csr + 0x10, quads, 1) != 1)
            printf ("failed write: line %d\n", __LINE__);
    }

    // a7c = 40000000
    do_value_setting (isrc);

    if (1) {
        uint32_t pixels_per_frame;
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x34, &pixels_per_frame, 1);

        uint32_t total_bytes_hi, total_bytes_lo;
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x38, &total_bytes_hi, 1);
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x3c, &total_bytes_lo, 1);

        uint32_t packets_per_frame;
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x48, &packets_per_frame, 1);

        assert (total_bytes_hi == 0);

        printf ("pixels_per_frame %8d, total_bytes_lo %8d, packets_per_frame %8d\n",
                pixels_per_frame, total_bytes_lo, packets_per_frame);

    }

    do_value_setting (isrc);

    // a44 = 0bc00000
    if (1) {
        uint32_t tmp;

        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x40, &tmp, 1);
        uint32_t packet_unit = (tmp >> 16) & 0xffff;
        uint32_t packet_max = (tmp) & 0xffff;

        // set packet size
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x44, &tmp, 1);
        uint32_t previous_packet_size = (tmp >> 16) & 0xffff;
        uint32_t recommended_packet_size = (tmp) & 0xffff;

        printf ("previous_packet_size %5d, recommended_packet_size %5d, packet_unit %5d, packet_max %5d\n",
                previous_packet_size, recommended_packet_size, packet_unit, packet_max);

        // pick a packet size that guarantees that the
        // bytes_transferred_per_frame will NOT be a multiple of
        // 512. This is important, since it will allow us to reliably
        // detect end-of-frame conditions.

        // (the camera will send a zero-length frame when data is
        // lost. We will detect this as an "end of transfer",
        // resulting in a read of less than 16384 bytes.
        impl->packet_size = packet_max;
        while (1) {
            int npackets = (impl->bytes_per_frame + impl->packet_size - 1) / impl->packet_size;
            int total_bytes = npackets * impl->packet_size;
            int residual = total_bytes % 512;

            printf ("** Packet size %d, npackets %d, total bytes %d, residual %d\n",
                    impl->packet_size, npackets, total_bytes, residual);

            if (residual == 0)
                impl->packet_size -= packet_unit;
            else
                break;

            if (impl->packet_size <= 0) {
                printf ("Couldn't find a good packet size.\n");
                exit (1);
            }
        }
        assert (impl->packet_size % packet_unit == 0);

        printf ("Picking a packet size of %d (unit = %d, max = %d)\n", impl->packet_size, packet_unit, packet_max);

        uint32_t quads[] = { (impl->packet_size)<<16 };

        if (do_write (impl->handle, CONFIG_ROM_BASE + format->csr + 0x44, quads, 1) != 1)
            printf ("failed write: line %d\n", __LINE__);

        do_value_setting (isrc);

        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x44, &tmp, 1);
        uint32_t updated_packet_size = (tmp >> 16) & 0xffff;
//        uint32_t updated_recommended_packet_size = (tmp) & 0xffff;

        if (updated_packet_size != impl->packet_size) {
            printf ("WARNING: Wasn't able to change packet size from %d to %d; it's %d\n",
                    previous_packet_size, impl->packet_size, updated_packet_size);

            impl->packet_size = updated_packet_size;
        }
    }

    // check errorflag_2
//    do_value_setting(isrc);

   if (1) {
        uint32_t pixels_per_frame;
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x34, &pixels_per_frame, 1);

        uint32_t total_bytes_hi, total_bytes_lo;
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x38, &total_bytes_hi, 1);
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x3c, &total_bytes_lo, 1);

        uint32_t packets_per_frame;
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x48, &packets_per_frame, 1);

        uint32_t packet_tmp;
        do_read (impl->handle, CONFIG_ROM_BASE + format->csr + 0x44, &packet_tmp, 1);
        uint32_t packet_size = (packet_tmp >> 16) & 0xffff;

        if (debug) {
            printf ("READBACK: packet_size %8d, pixels_per_frame %8d, total_bytes_lo %8d, packets_per_frame %8d\n",
                    packet_size & 0xffff,
                    pixels_per_frame, total_bytes_lo, packets_per_frame);
        }

        assert ((packet_size&0xffff) == impl->packet_size);
    }

    // 614 = 80000000 (streaming = on)

    // set up USB transfers
    if (libusb_claim_interface (impl->handle, 0) < 0) {
        printf ("couldn't claim interface\n");
    }

    if (0) {
        for (int i = 0x0600; i <= 0x0630; i+=4) {
            uint32_t d;
            do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + i, &d, 1);
            printf ("%04x: %08x\n", i, d);
        }
    }

    if (0) {
        int nquads = 32;
        uint32_t quads[32];
        if (do_read (impl->handle, CONFIG_ROM_BASE + format->csr, quads, nquads) != nquads)
            return -1;

        for (int i = 0; i < 32; i++)
            printf (" %03x : %08x\n", i*4, quads[i]);
    }

    impl->ntransfers = 100;
    impl->transfers = calloc (impl->ntransfers, sizeof(struct transfer_info));

    uint32_t packets_per_frame = (impl->bytes_per_frame + impl->packet_size - 1) / impl->packet_size;
    impl->bytes_transferred_per_frame = packets_per_frame * impl->packet_size;

    impl->transfer_size = 16384; // 16384 is largest size not split up by libusb

    printf ("packet_size %d, bytes_per_frame %d, bytes_transferred_per_frame %d, packets_per_frame %d\n",
            impl->packet_size, impl->bytes_per_frame, impl->bytes_transferred_per_frame, packets_per_frame);

    impl->transfers_submitted = 0;

    for (int i = 0; i < impl->ntransfers; i++) {
        impl->transfers[i].transfer = libusb_alloc_transfer (0);
        impl->transfers[i].buf = malloc (impl->transfer_size);
        libusb_fill_bulk_transfer (impl->transfers[i].transfer, impl->handle,
                                   0x81, impl->transfers[i].buf, impl->transfer_size, callback, isrc, TIMEOUT_MS);

        impl->transfers[i].transfer->flags = 0; //LIBUSB_TRANSFER_SHORT_NOT_OK;

        impl->transfers_submitted++;

        if (libusb_submit_transfer (impl->transfers[i].transfer) < 0) {
            printf ("submit failed\n");
        }
    }

    impl->nimages = 10;
    impl->images = calloc(impl->nimages, sizeof(struct image_info));

    for (int i = 0; i < impl->nimages; i++)
        impl->images[i].buf = malloc (impl->bytes_transferred_per_frame);

    // we haven't lost any data yet.
    impl->current_frame_index = -1;
    impl->current_frame_offset = 0;

    pthread_mutex_lock (&impl->pending_transaction_mutex);

    impl->consume_transfers_flag = 0;
    impl->worker_exit_flag = 0;

    pthread_mutex_unlock (&impl->pending_transaction_mutex);

    // set transmission to ON
    if (1) {
        uint32_t quads[] = { 0x80000000UL };
        if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x614, quads, 1) != 1)
            printf ("transmission on: ack!\n");
    }

    if (pthread_create (&impl->worker_thread, NULL, worker_thread_proc, isrc) != 0) {
        perror ("pthread");
        return -1;
    }

    // we now are using the asynchronous libusb API.

    return 0;
}

static int
get_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

    memset (frmd, 0, sizeof(*frmd));

//    image_source_format_t *ifmt = get_format(isrc, get_current_format(isrc));

    int idx = get_ready_frame (impl);

    frmd->datalen = impl->bytes_per_frame;
    frmd->data = impl->images[idx].buf;
    frmd->utime = utime_now (); // XXX DO BETTER
    frmd->ifmt.width = impl->formats[impl->current_format_idx].width;
    frmd->ifmt.height = impl->formats[impl->current_format_idx].height;
    strcpy (frmd->ifmt.format, impl->formats[impl->current_format_idx].format);

    return 0;
}

static int
release_frame (image_source_t *isrc, image_source_data_t *frmd)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;

    void *imbuf = frmd->data;

    int idx = -1;
    for (int i = 0; i < impl->nimages; i++) {
        if (impl->images[i].buf == imbuf) {
            idx = i;
            break;
        }
    }

    if (idx < 0)
        printf ("%p\n", imbuf);

    assert (idx >= 0);

    put_empty_frame (impl, idx);

    return 0;
}

static int
stop (image_source_t *isrc)
{
    printf ("STOPPING\n");
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;

    // Stopping is a bit tricky; ptgrey camera can get wedged if we
    // don't pull data from it.  Strategy: submit a STOP command while
    // continuing to process data. Once the STOP is acknowledged,
    // cancel remaining URBs (that will happen in stop_callback). Once
    // all transfers have been either completed or cancelled, we're
    // done.
    if (1) {
        uint8_t *stop_buffer = calloc (1, LIBUSB_CONTROL_SETUP_SIZE + 4);
        // note: actual command is zero, so just call calloc.

        uint64_t address = CONFIG_ROM_BASE + impl->command_regs_base + 0x614;
        int request = address_to_request (address);

        struct libusb_transfer *stop_transfer = libusb_alloc_transfer (0);
        libusb_fill_control_setup (stop_buffer, 0x40, request, address & 0xffff, (address >> 16) & 0xffff, 4);

        libusb_fill_control_transfer (stop_transfer, impl->handle, stop_buffer, stop_callback, impl, 0);
        libusb_submit_transfer (stop_transfer);
    }

    // now wait for all transfers to be finished.
    while (1) {
        pthread_mutex_lock (&impl->pending_transaction_mutex);
        int cnt = impl->transfers_submitted;
        pthread_mutex_unlock (&impl->pending_transaction_mutex);

        if (impl->consume_transfers_flag && cnt == 0)
            break;

        printf ("waiting for transactions to complete: %d\n", cnt);

        usleep (50000);
    }

    // we can now stop the worker thread.
    impl->worker_exit_flag = 1;
    pthread_join (impl->worker_thread, NULL);

    printf ("image_source_pgusb: worker thread exited\n");

    // We're now using the synchronous API again.

    // set transmission to OFF.
    if (1) {
        uint32_t quads[] = { 0x00000000UL };
        if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x614, quads, 1) != 1)
            printf ("transmission off: ack!\n");
    }

    for (int i = 0; i < impl->ntransfers; i++) {
        libusb_free_transfer (impl->transfers[i].transfer);
        free (impl->transfers[i].buf);
    }

    free (impl->transfers);

    for (int i = 0; i < impl->nimages; i++) {
        free (impl->images[i].buf);
    }
    free (impl->images);

    libusb_release_interface (impl->handle, 0);

    return 0;
}

static int
my_close (image_source_t *isrc)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

    libusb_close (impl->handle);
    libusb_exit (impl->context);

    return 0;
}

static void
append_feature (impl_pgusb_t *impl, struct feature f)
{
    impl->nfeatures++;
    impl->features = realloc (impl->features, impl->nfeatures * sizeof(struct feature));
    impl->features[impl->nfeatures-1] = f;
}

/////////////////////////////////////////////////////////
// 0 : Off
// 1 : Auto
// 2 : Manual
// 3 : One-push ("one shot")
static int
simple_mode_is_available (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    if ((d & (1<<31)) == 0)
        return 0;

    if ((d & (1<<28)) || (d & (1<<26)) || (d & (1<<25)) || (d & (1<<24)))
        return 1;

    return 0;
}

static char *
simple_mode_get_type (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return NULL;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return NULL;

    // if the feature supports one-shot, on-off, auto, or manual, then
    // enable the feature-mode control.
    int oneshot = 0, onoff = 0, automode = 0, manual = 0;

    if (d & (1<<28))
        oneshot = 1;
    if (d & (1<<26))
        onoff = 1;
    if (d & (1<<25))
        automode = 1;
    if (d & (1<<24))
        manual = 1;

    char buf[1024];
    sprintf (buf, "c,%s%s%s%s",
             onoff ? "0=off," : "",
             automode ? "1=auto," : "",
             manual ? "2=manual," : "",
             oneshot ? "3=one-shot," : "");

    return strdup (buf);
}

static double
simple_mode_get_value (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    int onepush = (d & (1<<26)) ? 1 : 0;
    int onoff = (d & (1<<25)) ? 1 : 0;
    int automanual = (d & (1<<24)) ? 1 : 0;

    // this logic taken from IIDC spec.
    if (!onoff)
        return 0;
    if (automanual)
        return 1;
    if (!onepush)
        return 2;
    return 3;
}

static int
simple_mode_set_value (image_source_t *isrc, struct feature *f, double _v)
{
    impl_pgusb_t *impl = isrc->impl;
    int v = (int) _v;

    uint32_t d;

    if (v == 0) {
        // off
        d = 0;
    }
    else if (v == 1) {
        // auto
        d = (1<<25) | (1<<24);
    }
    else if (v == 2) {
        // manual
        d = (1<<25);
    }
    else if (v == 3) {
        // one-shot
        d = (1<<25) | (1<<26);
    }
    else
        return -1;

    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;


    return 0;
}

/////////////////////////////////////////////////////////
// BEWARE: IIDC 1.31 documentation has bit positions in wrong place
// register 0x500:          register 0x800
// 31: presence             31: presence
// 30: abs_control          30: abs_control
// 29                       26: one-push
// 28: one-push             25: onoff
// 27: read-out             24: automanual
// 26: on/off               0-11: value
// 25: auto_inq
// 24: manual
// 12-23: min value
// 0-11: max value
static int
simple_value_is_available (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    return (d & (1<<31)) ? 1 : 0;
}

static char *
simple_value_get_type (image_source_t *isrc, struct feature *f)
{
    if (!simple_value_is_available (isrc, f))
        return 0;

    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    // note: these offsets don't depend on value_lsb/value_size
    int min = (d>>12)&0xfff;
    int max = d & 0xfff;

    char buf[1024];
    sprintf (buf, "i,%d,%d", min, max);

    return strdup (buf);
}

static uint32_t
float_to_uint32 (float f)
{
    uint32_t *i = (uint32_t*) &f;
    return *i;
}

static double
uint32_to_float (uint32_t v)
{
    float *f = (float*) &v;
    return (double) *f;
}

static char *
simple_value_get_absolute_type (image_source_t *isrc, struct feature *f)
{
    assert (simple_value_is_available (isrc, f));

    impl_pgusb_t *impl = isrc->impl;
    uint32_t imin, imax;

    int res = do_read (impl->handle, CONFIG_ROM_BASE + f->absolute_csr, &imin, 1);
    assert (res==1);

    res = do_read (impl->handle, CONFIG_ROM_BASE + f->absolute_csr + 4, &imax, 1);
    assert (res==1);

    float min = uint32_to_float (imin);
    float max = uint32_to_float (imax);

    char buf[1024];
    sprintf (buf, "f,%.15f,%.15f", min, max);

    return strdup (buf);
}

static double
simple_value_get_absolute_value (image_source_t *isrc, struct feature *f)
{
    assert (simple_value_is_available (isrc, f));

    impl_pgusb_t *impl = isrc->impl;
    uint32_t ival;

    int res = do_read (impl->handle, CONFIG_ROM_BASE + f->absolute_csr + 8, &ival, 1);
    assert (res==1);

    return uint32_to_float (ival);
}

static int
simple_value_set_absolute_value (image_source_t *isrc, struct feature *f, double v)
{
    assert (simple_value_is_available (isrc, f));

    impl_pgusb_t *impl = isrc->impl;
    uint32_t iv = float_to_uint32 ((float) v);

    uint32_t d = (1 << 25) | (1 << 30); // manual control with absolute
    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return -1;

    int res = do_write (impl->handle, CONFIG_ROM_BASE + f->absolute_csr + 8, &iv, 1);
    assert (res == 1);

    return 0;
}

static double
simple_value_get_value (image_source_t *isrc, struct feature *f)
{
    if (!simple_value_is_available (isrc, f))
        return 0;

    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    uint32_t mask = ((1<<f->value_size) - 1) << f->value_lsb;

    return (d & mask) >> f->value_lsb;
}

static int
simple_value_set_value (image_source_t *isrc, struct feature *f, double v)
{
    if (!simple_value_is_available (isrc, f))
        return 0;

    impl_pgusb_t *impl = isrc->impl;
    uint32_t old;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &old, 1) != 1)
        return 0;

    uint32_t value_mask = ((1<<f->value_size) - 1) << f->value_lsb;

    uint32_t preserve_mask = 0x30ffffff ^ value_mask; // all reserved/value bits. preserve values here that we aren't rewriting.

    uint32_t d = (1 << 25) | (old & preserve_mask) | ((((int) v) << f->value_lsb) & value_mask);

    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return -1;

    return 0;
}

// addr should be around 0x500. Other related registers are computed
// by adding appropriate offsets.
void
add_simple_feature (image_source_t *isrc, const char *name, uint32_t addr)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + addr, &d, 1) != 1)
        return;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return;

    int absolute = (d & (1<<30)) ? 1 : 0;
    uint32_t absolute_csr;

    if (absolute) {
        if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + addr + 0x200, &absolute_csr, 1) != 1)
            return;

        absolute_csr *= 4;

        uint32_t foo;
        if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x480, &foo, 1) != 1)
            return;

        if (debug)
            printf ("ABSOLUTE %08x %08x\n", absolute_csr, foo);
    }

    // create a feature to control the mode
    if (1) {
        struct feature f;
        memset (&f, 0, sizeof f);

        char buf[128];
        sprintf (buf, "%s-mode", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = simple_mode_is_available;
        f.get_type = simple_mode_get_type;
        f.get_value = simple_mode_get_value;
        f.set_value = simple_mode_set_value;
        f.absolute_csr = absolute ? absolute_csr : 0;
        append_feature (impl, f);
    }

    // if it supports manual mode...
    if (addr==0x50c) {
        // hack for white balance, which has two features.

        struct feature f;
        memset(&f, 0, sizeof f);

        char buf[128];
        sprintf (buf, "%s-ub", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = simple_value_is_available;
        f.get_value = absolute ? simple_value_get_absolute_value : simple_value_get_value;
        f.set_value = absolute ? simple_value_set_absolute_value : simple_value_set_value;
        f.get_type = absolute ? simple_value_get_absolute_type : simple_value_get_type;
        f.absolute_csr = absolute_csr;
        f.value_lsb = 0;
        f.value_size = 12;
        append_feature (impl, f);

        sprintf (buf, "%s-vr", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = simple_value_is_available;
        f.get_value = absolute ? simple_value_get_absolute_value : simple_value_get_value;
        f.set_value = absolute ? simple_value_set_absolute_value : simple_value_set_value;
        f.get_type = absolute ? simple_value_get_absolute_type : simple_value_get_type;
        f.absolute_csr = absolute_csr;
        f.value_lsb = 12;
        f.value_size = 12;
        append_feature (impl, f);

    } else {

        if ((d & (1 << 24))) {
            struct feature f;
            memset (&f, 0, sizeof f);

            char buf[128];
            sprintf (buf, "%s", name);
            f.name = strdup (buf);
            f.user_addr = addr;
            f.is_available = simple_value_is_available;
            f.get_value = absolute ? simple_value_get_absolute_value : simple_value_get_value;
            f.set_value = absolute ? simple_value_set_absolute_value : simple_value_set_value;
            f.get_type = absolute ? simple_value_get_absolute_type : simple_value_get_type;
            f.absolute_csr = absolute_csr;
            f.value_lsb = 0;
            f.value_size = 12;
            append_feature (impl, f);
        }
    }

    if (debug) {
        printf ("FEATURE %-15s: %08x [%11s] [%6s] [%11s] [%10s] [%9s] [%7s] [%9s]\n",
                name, d,
                (d & (1<<31)) ? "present" : "not present",
                (d & (1<<30)) ? "abs": "no abs",
                (d & (1<<28)) ? "one-shot" : "no one-shot",
                (d & (1<<27)) ? "read" : "write-only",
                (d & (1<<26)) ? "on-off" : "no on-off",
                (d & (1<<25)) ? "auto" : "no auto",
                (d & (1<<24)) ? "manual" : "no manual"
            );
    }

//    printf("FEATURE %-15s: is_available: %1d, min %8.0f, max %8.0f, value %8.0f\n",
//           name, f.is_available(isrc, &f), f.get_min(isrc, &f), f.get_max(isrc, &f), f.get_value(isrc, &f));
    return;
}

/////////////////////////////////////////////////////////
// trigger
//
// BEWARE: IIDC 1.31 documentation has bit positions in wrong place
// register 0x530:          register 0x830
// 31: presence             31: presence
// 30: abs_control          30: abs_control
// 29:                      29-26:
// 28:                      25: onoff
// 27: read-out             24: polarity
// 26: on/off               23-21: source
// 25: polarity             20: raw signal value
// 24: value-read           19-16: mode
// 23: src 0 available      15-12:
// 22: src 1 available      11-0: parameter
// 21: src 2 available
// 20: src 3 available
// 19-17:
// 16: src sw available
// 15: mode  0 available
// 14: mode  1 available
// 13: mode  2 available
// 12: mode  3 available
// 11: mode  4 available
// 10: mode  5 available
// 9-2:
//  1: mode 14 available
//  0: mode 15 available

// trigger enabled
static int
trigger_enabled_is_available (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    // feature not present
    if ((d & (1<<31)) == 0)
        return 0;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return 0;

    int readout = (d & (1<<27)) ? 1 : 0;
    int onoff = (d & (1<<26)) ? 1 : 0;
    int polarity = (d & (1<<25)) ? 1 : 0;

    // we require readout, onoff and polarity for convenience
    if (readout && onoff && polarity)
        return 1;

    return 0;
}

static char *
trigger_enabled_get_type (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return NULL;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return NULL;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return NULL;

    int readout = (d & (1<<27)) ? 1 : 0;
    int onoff = (d & (1<<26)) ? 1 : 0;
    int polarity = (d & (1<<25)) ? 1 : 0;

    // we require readout, onoff and polarity for convenience
    if ((readout == 0) || (onoff == 0) || (polarity == 0))
        return NULL;

    char buf[1024];
    sprintf (buf, "c,0=off,1=on (active low),2=on (active high)");

    return strdup (buf);
}

static double
trigger_enabled_get_value (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    int absolute = (d & (1<<30)) ? 1 : 0;
    int onoff    = (d & (1<<25)) ? 1 : 0;
    int polarity = (d & (1<<24)) ? 1 : 0;

    if (absolute)
        return 0;
    // we require onoff and polarity for convenience
    if (!onoff)
        return 0; // off
    if (!polarity)
        return 1; // on, low
    return 2; // on, high
}

static int
trigger_enabled_set_value (image_source_t *isrc, struct feature *f, double _v)
{
    impl_pgusb_t *impl = isrc->impl;
    int v = (int) _v;

    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    if (v == 0) {
        // off
        d &= ~(1<<25); // unset off
    } else if (v == 1) {
        // low
        d |=  (1<<25); // set on
        d &= ~(1<<24); // unset polarity
    } else if (v == 2) {
        // high
        d |= (1<<25); // set on
        d |= (1<<24); // set polarity
    } else
        return -1;

    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    return 0;
}

// trigger source
static int
trigger_source_is_available (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    // feature not present
    if ((d & (1<<31)) == 0)
        return 0;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return 0;

    int readout = (d & (1<<27)) ? 1 : 0;
    int onoff = (d & (1<<26)) ? 1 : 0;
    int polarity = (d & (1<<25)) ? 1 : 0;

    // we require readout, onoff and polarity for convenience
    if (readout && onoff && polarity)
        return 1;

    return 0;
}

static char *
trigger_source_get_type (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return NULL;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return NULL;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return NULL;

    int readout = (d & (1<<27)) ? 1 : 0;
    int onoff = (d & (1<<26)) ? 1 : 0;
    int polarity = (d & (1<<25)) ? 1 : 0;

    // we require readout, onoff and polarity for convenience
    if ((readout == 0) || (onoff == 0) || (polarity == 0))
        return NULL;

    return sprintf_alloc ("c,%s%s%s%s%s",
                          (d & (1<<23)) ? "0=gpio0," : "",
                          (d & (1<<22)) ? "1=gpio1," : "",
                          (d & (1<<21)) ? "2=gpio2," : "",
                          (d & (1<<20)) ? "3=gpio3," : "",
                          (d & (1<<16)) ? "4=software-only," : "");
}

static double
trigger_source_get_value (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    int source = 0;
    source |= ((d >> 23) & 0x1) << 2;
    source |= ((d >> 22) & 0x1) << 1;
    source |= ((d >> 21) & 0x1) << 0;

    return source;
}

static int
trigger_source_set_value (image_source_t *isrc, struct feature *f, double _v)
{
    impl_pgusb_t *impl = isrc->impl;
    int v = (int) _v;

    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    // clear current source
    d &= ~(1<<23);
    d &= ~(1<<22);
    d &= ~(1<<21);

    // set source
    if (v >= 0 && v < 4) {
       d |= ((v >> 2) & 0x1) << 23;
       d |= ((v >> 1) & 0x1) << 22;
       d |= ((v >> 0) & 0x1) << 21;

    }
    else
        return -1;

    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    return 0;
}

// trigger mode
static int
trigger_mode_is_available (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    // feature not present
    if ((d & (1<<31)) == 0)
        return 0;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return 0;

    int readout = (d & (1<<27)) ? 1 : 0;
    int onoff = (d & (1<<26)) ? 1 : 0;
    int polarity = (d & (1<<25)) ? 1 : 0;

    // we require readout, onoff and polarity for convenience
    if (readout && onoff && polarity)
        return 1;

    return 0;
}

static char *
trigger_mode_get_type (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return NULL;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return NULL;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return NULL;

    int readout = (d & (1<<27)) ? 1 : 0;
    int onoff = (d & (1<<26)) ? 1 : 0;
    int polarity = (d & (1<<25)) ? 1 : 0;

    // we require readout, onoff and polarity for convenience
    if ((readout == 0) || (onoff == 0) || (polarity == 0))
        return NULL;

    char buf[1024];
    sprintf (buf, "c,%s%s%s%s%s%s%s%s",
             (d & (1<<15)) ? "0=standard," : "",
             (d & (1<<14)) ? "1=bulb shutter," : "",
             (d & (1<<13)) ? "2=multi-pulse trigger," : "",
             (d & (1<<12)) ? "3=skip frames," : "",
             (d & (1<<11)) ? "4=multiple exposure preset," : "",
             (d & (1<<10)) ? "5=multiple exposure pulse width," : "",
             (d & (1<< 1)) ? "14=overlapped exposure," : "",
             (d & (1<< 0)) ? "15=multi-shot trigger," : "");

    return strdup (buf);
}

static double
trigger_mode_get_value (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    int mode = 0;
    mode |= ((d >> 19) & 0x1) << 3;
    mode |= ((d >> 18) & 0x1) << 2;
    mode |= ((d >> 17) & 0x1) << 1;
    mode |= ((d >> 16) & 0x1) << 0;

    return mode;
}

static int
trigger_mode_set_value (image_source_t *isrc, struct feature *f, double _v)
{
    impl_pgusb_t *impl = isrc->impl;
    int v = (int) _v;

    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    // clear current mode
    d &= ~(1<<19);
    d &= ~(1<<18);
    d &= ~(1<<17);
    d &= ~(1<<16);

    // set mode
    if (v >= 0 && v < 16) {
       d |= ((v >> 3) & 0x1) << 19;
       d |= ((v >> 2) & 0x1) << 18;
       d |= ((v >> 1) & 0x1) << 17;
       d |= ((v >> 0) & 0x1) << 16;

    }
    else
        return -1;

    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr + 0x300, &d, 1) != 1)
        return 0;

    return 0;
}

// software trigger
static int
trigger_software_is_available (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    // feature not present
    if ((d & (1<<31)) == 0)
        return 0;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return 0;

    if (d & (1<<16))
        return 1;

    return 0;
}

static char *
trigger_software_get_type (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return NULL;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return NULL;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return NULL;

    // sw trigger not supported
    if ((d & (1<<16)) == 0)
        return NULL;

    char buf[1024];
    sprintf (buf, "b");

    return strdup (buf);
}

static double
trigger_software_get_value (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->absolute_csr, &d, 1) != 1)
        return 0;

    // ready
    if ((d & (1<<0)) == 0)
        return 0;
    // busy
    if ((d & (1<<31)) == 1)
        return 1;

    return 0;
}

static int
trigger_software_set_value (image_source_t *isrc, struct feature *f, double _v)
{
    impl_pgusb_t *impl = isrc->impl;
    int v = (int) _v;

    uint32_t d;
    if (v == 0)
        d = 0;
    else if (v == 1)
        d = (1<<31);
    else
        return -1;

    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->absolute_csr, &d, 1) != 1)
        return 0;

    return 0;
}

// addr should be 0x530.
void
add_trigger_feature (image_source_t *isrc, const char *name, uint32_t addr, uint32_t sw_addr)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + addr, &d, 1) != 1)
        return;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return;

    // absolute mode supported (never observed/tested)
    if ((d & (1<<30)) == 1)
        return;

    // can't switch on/off
    if ((d & (1<<26)) == 0)
        return;

    // can't change polarity
    if ((d & (1<<25)) == 0)
        return;

    // create a feature to control the mode AND polarity
    if (1) {
        struct feature f;
        memset(&f, 0, sizeof f);

        char buf[128];
        sprintf (buf, "%s-enabled", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = trigger_enabled_is_available;
        f.get_type = trigger_enabled_get_type;
        f.get_value = trigger_enabled_get_value;
        f.set_value = trigger_enabled_set_value;
        append_feature (impl, f);
    }

    // create a feature to control source
    if (1) {
        struct feature f;
        memset (&f, 0, sizeof f);

        char buf[128];
        sprintf (buf, "%s-source", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = trigger_source_is_available;
        f.get_value = trigger_source_get_value;
        f.set_value = trigger_source_set_value;
        f.get_type = trigger_source_get_type;
        append_feature (impl, f);
    }

    // create a feature to control mode
    if (1) {
        struct feature f;
        memset (&f, 0, sizeof f);

        char buf[128];
        sprintf (buf, "%s-mode", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = trigger_mode_is_available;
        f.get_value = trigger_mode_get_value;
        f.set_value = trigger_mode_set_value;
        f.get_type = trigger_mode_get_type;
        append_feature (impl, f);
    }

    // create a feature for software triggering
    if (1) {
        struct feature f;
        memset (&f, 0, sizeof f);

        char buf[128];
        sprintf (buf, "%s-software", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = trigger_software_is_available;
        f.get_value = trigger_software_get_value;
        f.set_value = trigger_software_set_value;
        f.get_type = trigger_software_get_type;
        f.absolute_csr = sw_addr; // XXX maybe make a new feature variable?
        append_feature (impl, f);
    }

    if (debug)
        printf ("FEATURE %-15s: %08x\n", name, d);

    return;
}

/////////////////////////////////////////////////////////
// frame info
//
// BEWARE: IIDC 1.31 documentation has bit positions in wrong place
// register 0x12F8
// 31: presence
// 30-10:
//  9: ROI position
//  8: gpio pin state
//  7: strobe pattern
//  6: frame counter
//  5: white balance
//  4: exposure
//  3: brightness
//  2: shutter
//  1: gain
//  0: timestamp

static int
bit_field_is_available (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    // feature not present
    if ((d & (1<<31)) == 0)
        return 0;

    return 1;
}

static char *
bit_field_get_type (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return NULL;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return NULL;

    char buf[1024];
    sprintf (buf, "b");

    return strdup (buf);
}

static double
bit_field_get_value (image_source_t *isrc, struct feature *f)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    if ((d & (1<<31)) == 0)
        return 0;

    uint32_t position = f->value_lsb;
    if ((d & (1<<position)) != 0)
        return 1;

    return 0;
}

static int
bit_field_set_value (image_source_t *isrc, struct feature *f, double _v)
{
    impl_pgusb_t *impl = isrc->impl;
    int v = (int) _v;

    if (v != 0 && v != 1)
        return -1;

    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    uint32_t position = f->value_lsb;

    if (v == 0)
        d &= ~(1<<position);
    else
        d |=  (1<<position);

    if (do_write (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + f->user_addr, &d, 1) != 1)
        return 0;

    return 0;
}

// addr should be 0x12F8
void add_bit_field_feature (image_source_t *isrc, const char *name, uint32_t addr, uint32_t position)
{
    impl_pgusb_t *impl = isrc->impl;
    uint32_t d;
    if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + addr, &d, 1) != 1)
        return;

    // feature not present.
    if ((d & (1<<31)) == 0)
        return;

    // create a feature to control all embedded frame info
    if (1) {
        struct feature f;
        memset (&f, 0, sizeof f);

        char buf[128];
        sprintf (buf, "%s", name);
        f.name = strdup (buf);
        f.user_addr = addr;
        f.is_available = bit_field_is_available;
        f.get_type = bit_field_get_type;
        f.get_value = bit_field_get_value;
        f.set_value = bit_field_set_value;
        f.value_lsb = position;
        f.value_size = 1;
        append_feature (impl, f);
    }

    if (debug) {
        printf ("FEATURE %-15s: %08x: %s\n",
                name, d,
                (d & (1<<position)) ? "enabled" : "disabled");
    }

    return;
}

////////////////////////////////////////////////////////////
static const char *
get_feature_name (image_source_t *isrc, int idx)
{
    assert(isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;

    assert(idx < impl->nfeatures);
    return impl->features[idx].name;
}

// static double get_feature_min(image_source_t *isrc, int idx)
// {
//     assert(isrc->impl_type == IMPL_TYPE);
//     impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

//     assert(idx < impl->nfeatures);
//     return impl->features[idx].get_min(isrc, &impl->features[idx]);
// }

// static double get_feature_max(image_source_t *isrc, int idx)
// {
//     assert(isrc->impl_type == IMPL_TYPE);
//     impl_pgusb_t *impl = (impl_pgusb_t*) isrc->impl;

//     assert(idx < impl->nfeatures);
//     return impl->features[idx].get_max(isrc, &impl->features[idx]);
// }

static int
is_feature_available (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;

    assert (idx < impl->nfeatures);
    return impl->features[idx].is_available(isrc, &impl->features[idx]);
}

static char *
get_feature_type (image_source_t *isrc, int idx)
{
    assert(isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;

    assert (idx < impl->nfeatures);
    return impl->features[idx].get_type(isrc, &impl->features[idx]);
}

static double
get_feature_value (image_source_t *isrc, int idx)
{
    assert (isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;

    assert (idx < impl->nfeatures);
    return impl->features[idx].get_value(isrc, &impl->features[idx]);
}

static int
set_feature_value (image_source_t *isrc, int idx, double v)
{
    assert(isrc->impl_type == IMPL_TYPE);
    impl_pgusb_t *impl = isrc->impl;

    assert (idx < impl->nfeatures);
    return impl->features[idx].set_value(isrc, &impl->features[idx], v);
}

image_source_t *
image_source_pgusb_open (url_parser_t *urlp)
{
    const char *location = url_parser_get_host (urlp);

    libusb_context *context;
    if (libusb_init (&context) != 0) {
        printf ("Couldn't initialize libusb\n");
        return NULL;
    }

    libusb_device **devs;
    int ndevs = libusb_get_device_list(context, &devs);

    int64_t guid = 0;
    if (strlen (location) > 0) {
        if (strto64 (location, strlen(location), &guid)) {
            printf ("image_source_open: pgusb guid '%s' is not a valid integer.\n", location);
            return NULL;
        }
    }

    // Look for a device whose guid matches what the user specified
    // (or, if the user didn't specify a guid, just pick the first
    // camera.
    libusb_device *dev = NULL;
    for (int i = 0; i < ndevs; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor (devs[i], &desc) != 0) {
            printf ("couldn't get descriptor for device %d\n", i);
            continue;
        }

        int okay = 0;
        for (int j = 0; vendor_products[j].vendor != 0; j++) {
            if (desc.idVendor == vendor_products[j].vendor &&
                desc.idProduct == vendor_products[j].product) {
                okay = 1;
            }
        }

        if (okay) {
            uint64_t this_guid = get_guid (devs[i]);

            if (guid == 0 || guid == this_guid) {
                dev = devs[i];
                break;
            }
        }
    }

    if (dev == NULL)
        return NULL;

    image_source_t *isrc = calloc (1, sizeof(*isrc));
    impl_pgusb_t *impl = calloc (1, sizeof(*impl));

    isrc->impl_type = IMPL_TYPE;
    isrc->impl = impl;

    impl->context = context;
    impl->dev = dev;


    pthread_cond_init (&impl->queue_cond, NULL);
    pthread_mutex_init (&impl->queue_mutex, NULL);
    pthread_mutex_init (&impl->pending_transaction_mutex, NULL);

    if (libusb_open (impl->dev, &impl->handle) < 0) {
        printf ("error\n");
        goto error;
    }

    if (libusb_set_configuration (impl->handle, 1) < 0) {
        printf ("error\n");
        goto error;
    }

    uint32_t magic;
    if (do_read (impl->handle, CONFIG_ROM_BASE + 0x404, &magic, 1) != 1)
        goto error;
    if (magic != 0x31333934)
        goto error;

    if (1) {
        uint32_t tmp;
        if (do_read (impl->handle, CONFIG_ROM_BASE + 0x424, &tmp, 1) != 1)
            goto error;

        assert ((tmp>>24)==0xd1);
        impl->unit_directory_offset = 0x424 + (tmp & 0x00ffffff)*4;

        if (debug)
            printf ("unit_directory_offset: %08x\n", impl->unit_directory_offset);
    }

    if (1) {
        uint32_t tmp;
        if (do_read (impl->handle, CONFIG_ROM_BASE + impl->unit_directory_offset + 0x0c, &tmp, 1) != 1)
            goto error;

        assert ((tmp>>24)==0xd4);
        impl->unit_dependent_directory_offset = impl->unit_directory_offset + 0x0c + (tmp & 0x00ffffff)*4;

        if (debug)
            printf ("unit_dependent_directory_offset: %08x\n", impl->unit_dependent_directory_offset);
    }

    if (1) {
        uint32_t tmp;
        if (do_read (impl->handle, CONFIG_ROM_BASE + impl->unit_dependent_directory_offset + 0x4, &tmp, 1) != 1)
            goto error;

        assert ((tmp>>24)==0x40);
        impl->command_regs_base = 4*(tmp&0x00ffffff);

        if (debug)
            printf ("command_regs_base: %08x\n", impl->command_regs_base);
    }

/*
    for (int i = 0x0400; i <= 0x0500; i+=4) {
        uint32_t d;
        do_read(impl->handle, CONFIG_ROM_BASE + i, &d, 1);
        printf("%04x: %08x\n", i, d);
    }
*/

    if (1) {
        // which modes are supported by format 7?

        uint32_t v_mode_inq_7;
        if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x019c, &v_mode_inq_7, 1) != 1)
            goto error;

        if (debug)
            printf ("v_mode_inq_7: %08x\n", v_mode_inq_7);

        v_mode_inq_7 = (v_mode_inq_7)>>24;

        for (int mode = 0; mode < 8; mode++) {
            if ((v_mode_inq_7 & (1<<(7-mode)))) {

                uint32_t mode_csr;
                if (do_read (impl->handle, CONFIG_ROM_BASE + impl->command_regs_base + 0x2e0 + mode*4, &mode_csr, 1) != 1)
                    goto error;

                mode_csr *= 4;

                if (debug)
                    printf ("mode %d csr %08x\n", mode, mode_csr);

                int nquads = 32;
                uint32_t quads[32];
                if (do_read (impl->handle, CONFIG_ROM_BASE + mode_csr, quads, nquads) != nquads)
                    goto error;

                uint32_t cmodes = quads[5];
                for (int cmode = 0; cmode < 11; cmode++) {
                    if (cmodes & (1<<(31-cmode))) {
                        if (debug)
                            printf (" %d %s\n", cmode, COLOR_MODES[cmode]);

                        impl->formats = realloc(impl->formats, (impl->nformats+1) * sizeof(struct format));
                        impl->formats[impl->nformats].height = quads[0] & 0xffff;
                        impl->formats[impl->nformats].width = quads[0] >> 16;

                        if (cmode == 9) {
                            // 8 bit bayer
                            int filter_mode = quads[22]>>24;
                            strcpy (impl->formats[impl->nformats].format, BAYER_MODES[filter_mode]);
                        }
                        else if (cmode == 10) {
                            // 16 bit bayer
                            int filter_mode = quads[22]>>24;
                            char buf[1024];
                            sprintf (buf, "%s16", BAYER_MODES[filter_mode]);
                            strcpy (impl->formats[impl->nformats].format, buf);
                        }
                        else {
                            // not a bayer
                            strcpy (impl->formats[impl->nformats].format, COLOR_MODES[cmode]);
                        }

                        impl->formats[impl->nformats].format7_mode_idx = mode;
                        impl->formats[impl->nformats].color_coding_idx = cmode;
                        impl->formats[impl->nformats].csr = mode_csr;

                        impl->nformats++;
                    }
                }
            }
        }
    }

    impl->nfeatures = 0;
    impl->features = NULL;
    add_simple_feature (isrc, "brightness",      0x500);
    add_simple_feature (isrc, "exposure",        0x504);
    add_simple_feature (isrc, "sharpness",       0x508);
    add_simple_feature (isrc, "white-balance",   0x50c);
    add_simple_feature (isrc, "hue",             0x510);
    add_simple_feature (isrc, "saturation",      0x514);
    add_simple_feature (isrc, "gamma",           0x518);
    add_simple_feature (isrc, "shutter",         0x51c);
    add_simple_feature (isrc, "gain",            0x520);
    add_simple_feature (isrc, "iris",            0x524);
    add_simple_feature (isrc, "focus",           0x528);
    add_simple_feature (isrc, "frame-rate",      0x53c);
    add_simple_feature (isrc, "zoom",            0x580);
    add_simple_feature (isrc, "pan",             0x584);
    add_simple_feature (isrc, "tilt",            0x588);
    add_simple_feature (isrc, "optical-filter",  0x58c);
    add_simple_feature (isrc, "capture-size",    0x5c0);
    add_simple_feature (isrc, "capture-quality", 0x5c4);

    add_trigger_feature (isrc, "trigger",        0x530, 0x62C); // XXX

    add_bit_field_feature (isrc, "embed-timestamp",      0x12F8, 0);
    add_bit_field_feature (isrc, "embed-gain",           0x12F8, 1);
    add_bit_field_feature (isrc, "embed-shutter",        0x12F8, 2);
    add_bit_field_feature (isrc, "embed-brightness",     0x12F8, 3);
    add_bit_field_feature (isrc, "embed-exposure",       0x12F8, 4);
    add_bit_field_feature (isrc, "embed-white-balance",  0x12F8, 5);
    add_bit_field_feature (isrc, "embed-frame-counter",  0x12F8, 6);
    add_bit_field_feature (isrc, "embed-strobe-pattern", 0x12F8, 7);
    add_bit_field_feature (isrc, "embed-gpio-pin-state", 0x12F8, 8);
    add_bit_field_feature (isrc, "embed-roi-position",   0x12F8, 9);


    isrc->num_formats = num_formats;
    isrc->get_format = get_format;
    isrc->get_current_format = get_current_format;
    isrc->set_format = set_format;
    isrc->set_named_format = set_named_format;

    isrc->num_features = num_features;
    isrc->get_feature_name = get_feature_name;
    isrc->get_feature_type = get_feature_type;
    isrc->is_feature_available = is_feature_available;
    isrc->get_feature_value = get_feature_value;
    isrc->set_feature_value = set_feature_value;

    isrc->start = start;
    isrc->get_frame = get_frame;
    isrc->release_frame = release_frame;

    isrc->stop = stop;
    isrc->close = my_close;

    return isrc;

  error:
    free (isrc);
    return NULL;
}

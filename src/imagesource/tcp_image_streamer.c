#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define SLEEP_US 33333 // 30FPS
#define MAGIC 0x17923349ab10ea9aL

static int64_t
utime_now (void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}

static void
write_i32 (uint8_t *buf, int32_t v)
{
    buf[0] = (v >> 24) & 0xFF;
    buf[1] = (v >> 16) & 0xFF;
    buf[2] = (v >>  8) & 0xFF;
    buf[3] = (v      ) & 0xFF;
}

static void
write_i64 (uint8_t *buf, int64_t v)
{
    uint32_t h = (uint32_t) (v >> 32);
    uint32_t l = (uint32_t) (v);

    write_i32(buf+0, h);
    write_i32(buf+4, l);
}

static uint8_t *
get_event_buffer (int *_len)
{
    int64_t magic = MAGIC;

    int64_t utime = utime_now();

    int32_t width  = 128;
    int32_t height =  64;

    char *format = "GRAY8";
    int32_t formatlen = strlen(format);

    int32_t imlen = width*height*sizeof(uint8_t);
    uint8_t *im = malloc(imlen);

    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            im[y*width+x] = rand() & 0xFF;

    int32_t buflen = 8 + 8 + 4 + 4 + 4 + formatlen + 4 + imlen;
    uint8_t *buf = calloc(buflen, sizeof(uint8_t));
    uint8_t *ptr = buf;

    write_i64(ptr, magic);          ptr += 8;
    write_i64(ptr, utime);          ptr += 8;
    write_i32(ptr, width);          ptr += 4;
    write_i32(ptr, height);         ptr += 4;
    write_i32(ptr, formatlen);      ptr += 4;
    memcpy(ptr, format, formatlen); ptr += formatlen;
    write_i32(ptr, imlen);          ptr += 4;
    memcpy(ptr, im, imlen);         ptr += imlen;

    free(im);

    *_len = buflen;
    return buf;
}

int
main (int argc, char *argv[])
{
    setlinebuf(stdout);
    setlinebuf(stderr);

    if (argc != 3) {
        printf("Usage: tcpstream <host> <port>\n");
        exit(1);
    }

    char *host = argv[1];
    int   port = atoi(argv[2]);

    printf("Connecting to '%s' on port %d\n", host, port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock  < 0) {
        printf("Error opening socket\n");
        perror("socket: ");
        goto cleanup;
    }

    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        printf("Error getting host by name\n");
        perror("gethostbyname: ");
        goto cleanup;
    }

    struct sockaddr_in serv_addr;
    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Error connecting to socket\n");
        perror("connect: ");
        goto cleanup;
    }

    printf("Connected\n");

    while (1)
    {
        int len = -1;
        uint8_t *buf = get_event_buffer(&len);

        int bytes = send(sock, buf, len, 0);

        free(buf);

        if (bytes != len) {
            printf("Tried to send %d bytes, sent %d\n", len, bytes);
            perror("send: ");
            goto cleanup;
        }

        usleep(SLEEP_US);
    }

    cleanup:

    if (sock >= 0)
        close(sock);

    printf("Done\n");
    exit(0);
}

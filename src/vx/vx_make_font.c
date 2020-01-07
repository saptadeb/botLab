#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "common/c5.h"
#include "common/string_util.h"
#include "imagesource/image_u8.h"

static long flength(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    return len;
}

void make_font(const char *basename)
{
    char pnmpath[1024];
    char bparampath[1024];
    char vxfpath[1024];

    sprintf(pnmpath, "%s.pnm", basename);
    sprintf(bparampath, "%s.bparam", basename);
    sprintf(vxfpath, "%s.vxf", basename);

    image_u8_t *im = image_u8_create_from_pnm(pnmpath);
    if (im == NULL) {
        printf("couldn't open pnm %s\n", pnmpath);
        exit(-1);
    }

    char *bparambuf = NULL;
    int bparambuflen;

    if (1) {
        FILE *f = fopen(bparampath, "r");
        if (f == NULL) {
            printf("Couldn't open file: %s\n", bparampath);
            exit(-1);
        }

        bparambuflen = flength(f);
        bparambuf = malloc(bparambuflen);
        int res = fread(bparambuf, 1, bparambuflen, f);

        if (res != bparambuflen) {
            printf("Couldn't read %d from file: %s, read %d instead\n",
                   bparambuflen, bparampath, res);
            exit(-1);
        }
        fclose(f);
    }

    int imbuflen = im->width * im->height;
    uint8_t *imbuf = malloc(imbuflen);
    for (int y = 0; y < im->height; y++)
        memcpy(&imbuf[y*im->width], &im->buf[y*im->width], im->width);

    int vxfbuflen = bparambuflen + imbuflen + 8;
    uint8_t *vxfbuf = malloc(vxfbuflen);
    int pos = 0;
    vxfbuf[pos++] = 0x0f; // write magic
    vxfbuf[pos++] = 0xed;
    vxfbuf[pos++] = 0x01;
    vxfbuf[pos++] = 0xae;

    memcpy(&vxfbuf[pos], bparambuf, bparambuflen);

    pos += bparambuflen;
    vxfbuf[pos++] = 0x00; // signifies c5 encoding
    vxfbuf[pos++] = 0x00;
    vxfbuf[pos++] = 0x00;
    vxfbuf[pos++] = 0x01;

    memcpy(&vxfbuf[pos], imbuf, imbuflen);

    int cvxfbuflen;
    uint8_t *cvxfbuf = malloc(vxfbuflen*2 + 1024);
    c5(vxfbuf, vxfbuflen, cvxfbuf, &cvxfbuflen);

    FILE *f = fopen(vxfpath, "w");
    if (f == NULL) {
        printf("couldn't open output file %s\n", vxfpath);
        exit(-1);
    }

    free(bparambuf);
    fwrite(cvxfbuf, 1, cvxfbuflen, f);
    fclose(f);
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        char *s = strdup(argv[i]);

        if (str_ends_with(s, ".bparam"))
            s[strlen(s) - 7] = 0;

        printf("%s\n", s);

        make_font(s);
    }
}


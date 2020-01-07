#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <pthread.h>

#include "common/c5.h"
#include "common/string_util.h"
#include "imagesource/image_u8.h"

#include "vx_codes.h"
#include "vx_program.h"
#include "vx_resc.h"
#include "vxo_chain.h"
#include "vxo_mat.h"
#include "vxp.h"
#include "vxo_text.h"

/** How to create a font:

    1. Export the font from Java, april.vis.VisFont.

    2. convert the png to a pnm
       convert serif__128.png serif__128.pnm

    3. run vx_make_font, passing it the name of the bparam file
       vx_make_font serif__128.bparam

    4. copy those fonts to the right place.

**/

// TODO
// - cache fonts
// - memory leaks
// - add color convenience formats
// - add dropshadow type effect
// - test margins
// - test fixed width

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static zhash_t *font_cache;

static long flength(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    return len;
}

static uint32_t decode32(uint8_t *buf, int *bufpos)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v = v << 8;
        v |= buf[(*bufpos)];
        (*bufpos)++;
    }
    return v;
}

// just let the vxchain do all the work.
static void append(vx_object_t * obj, zhash_t * resources, vx_code_output_stream_t * output)
{
    vxo_text_t *vt = (vxo_text_t*) obj;
    vt->chain->append(vt->chain, resources, output);
}

// just let the vxchain do all the work.
static void destroy(vx_object_t * obj)
{
    vxo_text_t *vt = (vxo_text_t*) obj;

    vt->chain->destroy(vt->chain);
    free(vt);
}

typedef struct {
    vx_resc_t *texture;
    int height, width; // size of the texture

    int ascii_min, ascii_max;
    int tile_width, tile_height, tile_dim;
    int *widths;
    int *advances;

    // info so that we can scale stuff correctly...
//    float requested_points; // how big did we ask for the font?
    float native_points;

} vxo_text_font_t;

typedef struct {
    int justification;
    zarray_t *fragments;
} vxo_text_line_t;

typedef struct {
    // when -1, the size is computed as a function of the string and
    // font. When not -1, the specified fixed width is used.
    int width;

    float rgba[4];
    vxo_text_font_t *font;
    char *s;

    float font_size;
} vxo_text_fragment_t;

static double vxo_text_font_get_width(vxo_text_font_t *vf, const char *s)
{
    int slen = strlen(s);

    double width = 0;

    for (int i = 0; i < slen; i++) {
        int c = s[i];
        if (c < vf->ascii_min || c >= vf->ascii_max)
            c = ' ';

        if (i+1 < slen)
            width += vf->advances[c - vf->ascii_min];
        else
            width += vf->widths[c - vf->ascii_min];
    }

    return width;
}

static int hexchar_to_int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    assert(0);
    return 0;
}

static double vxo_text_fragment_get_width(vxo_text_fragment_t *frag)
{
    if (frag->width >= 0)
        return frag->width;

    return vxo_text_font_get_width(frag->font, frag->s) * frag->font_size / frag->font->native_points;
}

static double vxo_text_line_get_height(vxo_text_line_t *line)
{
    double height = 0;

    for (int fragidx = 0; fragidx < zarray_size(line->fragments); fragidx++) {
        vxo_text_fragment_t *frag;
        zarray_get(line->fragments, fragidx, &frag);

        height = fmax(height, frag->font->tile_height * frag->font_size / frag->font->native_points);
    }

    return height;
}

static double vxo_text_line_get_width(vxo_text_line_t *line)
{
    double width = 0;

    for (int fragidx = 0; fragidx < zarray_size(line->fragments); fragidx++) {
        vxo_text_fragment_t *frag;
        zarray_get(line->fragments, fragidx, &frag);

        width += vxo_text_fragment_get_width(frag);
    }

    return width;
}


static vxo_text_font_t *font_create(const char *font_name, int style, float points)
{
    vxo_text_font_t *vf = calloc(1, sizeof(vxo_text_font_t));
    vf->native_points = points;

    char pnm_path[1024];
    char param_path[1024];
    char vxf_path[1024];

    char *font_path = getenv("VX_FONT_PATH");
    if (font_path == NULL) {
        printf("WARNING: No VX_FONT_PATH specified.\n");
        font_path = ".";
    }

    const char *style_string = "";
    if (style == VXO_TEXT_ITALIC)
        style_string = "i";
    if (style == VXO_TEXT_BOLD)
        style_string = "b";
    if (style == (VXO_TEXT_ITALIC | VXO_TEXT_BOLD))
        style_string = "bi";

    char *font_name_lower = str_tolowercase(strdup(font_name));
    sprintf(vxf_path, "%s/%s_%s_%.0f.vxf", font_path, font_name_lower, style_string, points);

    sprintf(pnm_path, "%s/%s_%s_%.0f.pnm", font_path, font_name_lower, style_string, points);
    sprintf(param_path, "%s/%s_%s_%.0f.param", font_path, font_name_lower, style_string, points);
    free(font_name_lower);

    /////////////////////////////////////////////////////////
    if (0) {
// Read font parameters
        FILE *f = fopen(param_path, "r");
        if (f == NULL) {
            printf("unable to open font parameter file: %s\n", param_path);
            exit(-1);
        }

        char line[1024];
        while (fgets(line, sizeof(line), f) != NULL) {
            zarray_t *toks = str_split(line, " ");

            if (zarray_size(toks) < 2)
                goto cleanup;

            char *t;
            char *v;
            zarray_get(toks, 0, &t);
            zarray_get(toks, 1, &v);

            if (!strcmp(t, "ascii_min"))
                vf->ascii_min = atoi(v);
            else if (!strcmp(t, "ascii_max"))
                vf->ascii_max = atoi(v);
            else if (!strcmp(t, "tile_width"))
                vf->tile_width = atoi(v);
            else if (!strcmp(t, "tile_height"))
                vf->tile_height = atoi(v);
            else if (!strcmp(t, "tile_dim"))
                vf->tile_dim = atoi(v);
            else if (!strcmp(t, "width"))
                vf->width = atoi(v);
            else if (!strcmp(t, "height"))
                vf->height = atoi(v);
            else if (!strcmp(t, "widths") || !strcmp(t, "advances")) {
                int *vs = calloc(zarray_size(toks)-1, sizeof(int));
                for (int i = 1; i < zarray_size(toks); i++) {
                    char *s;
                    zarray_get(toks, i, &s);
                    vs[i-1] = atoi(s);
                    if (!strcmp(t, "widths"))
                        vf->widths = vs;
                    else
                        vf->advances = vs;
                }
            } else
                assert(0);

          cleanup:
            zarray_vmap(toks, free);
            zarray_destroy(toks);
        }

        fclose(f);

        /////////////////////////////////////////////////////////
        // Read font data
        image_u8_t *im = image_u8_create_from_pnm(pnm_path);
        if (im == NULL) {
            printf("unable to open font file: %s\n", pnm_path);
            exit(-1);
        }

        // make sure there's no padding
        uint8_t *buf = malloc(im->width*im->height);
        for (int y = 0; y < im->height; y++)
            memcpy(&buf[y*im->width], &im->buf[y*im->width], im->width);

        vf->texture = vx_resc_copyub(buf, im->width*im->height);
    }

    if (1) {
        FILE *f = fopen(vxf_path, "r");
        if (f == NULL) {
            printf("Couldn't open font %s\n", vxf_path);
            exit(-1);
        }

        int cbuflen = flength(f);
        uint8_t *cbuf = malloc(cbuflen + C5_PAD);
        int res = fread(cbuf, 1, cbuflen, f);
        if (res != cbuflen) {
            printf("Unable to read %d bytes from %s, read %d instead\n", res, vxf_path, cbuflen);
            exit(-1);
        }
        fclose(f);

        uint8_t *buf = malloc(uc5_length(cbuf, cbuflen) + C5_PAD);
        int buflen;
        uc5(cbuf, cbuflen, buf,  &buflen);

        int pos = 0;

        uint32_t magic = decode32(buf, &pos);
        assert(magic == 0x0fed01ae);

        vf->ascii_min = decode32(buf, &pos);
        vf->ascii_max = decode32(buf, &pos);
        vf->tile_width = decode32(buf, &pos);
        vf->tile_height = decode32(buf, &pos);
        vf->tile_dim = decode32(buf, &pos);
        vf->width  = decode32(buf, &pos);
        vf->height = decode32(buf, &pos);

        int nwidths = decode32(buf, &pos);
        vf->widths = calloc(nwidths, sizeof(uint32_t));
        for (int i = 0; i < nwidths; i++)
            vf->widths[i] = decode32(buf, &pos) / 100.0;

        int nadvances = decode32(buf, &pos);
        vf->advances = calloc(nadvances, sizeof(uint32_t));
        for (int i = 0; i < nadvances; i++)
            vf->advances[i] = decode32(buf, &pos) / 100.0;

        uint32_t imformat = decode32(buf, &pos);
        assert(imformat == 0x00000001);

        vf->texture = vx_resc_copyub(&buf[pos], vf->width * vf->height);
        free(cbuf);
        free(buf);
    }

    assert(vf->ascii_min != 0);
    assert(vf->ascii_max != 0);
    assert(vf->tile_width != 0);
    assert(vf->tile_height != 0);
    assert(vf->tile_dim != 0);
    assert(vf->widths != NULL);
    assert(vf->advances != NULL);

    return vf;
}

static vxo_text_font_t *get_font(const char *font_name, int style)
{
    pthread_mutex_lock(&mutex);

    if (font_cache == NULL) {
        font_cache = zhash_create(sizeof(char*), sizeof(vxo_text_font_t*),
                                  zhash_str_hash, zhash_str_equals);
    }

    char *buf = malloc(1024);
    snprintf(buf, 1024, "%s-%d", font_name, style);

    vxo_text_font_t *font;
    if (!zhash_get(font_cache, &buf, &font)) {
        font = font_create(font_name, style, 128); // XXXXX Hard coded font size
        char *bufcpy = strdup(buf);
        zhash_put(font_cache, &bufcpy, &font, NULL, NULL);

        // we are maintaining a reference, so don't free these!
        vx_resc_inc_ref(font->texture);
    }

    free(buf);
    pthread_mutex_unlock(&mutex);

    return font;
}

static vx_program_t* vxo_text_fragment_make_program(vxo_text_fragment_t *frag)
{
    vxo_text_font_t *vf = frag->font;

    int slen = strlen(frag->s);
    float verts[12*slen], texcoords[12*slen];
    int nverts = 6*slen;
    float xpos = 0;

    for (int i = 0; i < slen; i++) {
        char c = frag->s[i];

        if (c < vf->ascii_min || c > vf->ascii_max)
            c = ' ';

        int idx = c - vf->ascii_min;
        int tile_y = idx / vf->tile_dim;
        int tile_x = idx - (tile_y*vf->tile_dim);

        float cwidth = vf->widths[idx];
        float advance = vf->advances[idx];

        verts[12*i+0]     = xpos;
        verts[12*i+1]     = 0;
        texcoords[12*i+0] = tile_x*vf->tile_width;
        texcoords[12*i+1] = (tile_y+1)*vf->tile_height;

        verts[12*i+2]     = xpos + cwidth;
        verts[12*i+3]     = 0;
        texcoords[12*i+2] = tile_x*vf->tile_width + cwidth;
        texcoords[12*i+3] = (tile_y+1)*vf->tile_height;

        verts[12*i+4]     = xpos;
        verts[12*i+5]     = vf->tile_height;
        texcoords[12*i+4] = tile_x*vf->tile_width;
        texcoords[12*i+5] = (tile_y+0)*vf->tile_height;

        verts[12*i+6]     = xpos;
        verts[12*i+7]     = vf->tile_height;
        texcoords[12*i+6] = tile_x*vf->tile_width;
        texcoords[12*i+7] = (tile_y+0)*vf->tile_height;

        verts[12*i+8]     = xpos + cwidth;
        verts[12*i+9]     = vf->tile_height;
        texcoords[12*i+8] = tile_x*vf->tile_width + cwidth;
        texcoords[12*i+9] = (tile_y+0)*vf->tile_height;

        verts[12*i+10]     = xpos + cwidth;
        verts[12*i+11]     = 0;
        texcoords[12*i+10] = tile_x*vf->tile_width + cwidth;
        texcoords[12*i+11] = (tile_y+1)*vf->tile_height;

        xpos += advance;
    }

    // normalize texture coordinates
    for (int i = 0; i < nverts; i++) {
        texcoords[2*i+0] /= vf->width;
        texcoords[2*i+1] /= vf->height;
    }

    /////////////////////////////////////////////////////////
    // Create program
    vx_program_t * program = vx_program_load_library("ucolor_texture_pm");

    vx_program_set_vertex_attrib(program, "position", vx_resc_copyf(verts, 2*nverts), 2);
    vx_program_set_vertex_attrib(program, "texcoord", vx_resc_copyf(texcoords, 2*nverts), 2);
    vx_program_set_texture(program, "texture", vf->texture, vf->width, vf->height, GL_LUMINANCE,
        VX_TEX_MIN_FILTER | VX_TEX_MAG_FILTER);
    vx_program_set_uniform4fv(program, "color", frag->rgba);
    vx_program_set_draw_array(program, nverts, GL_TRIANGLES);

    return program;
}

vx_object_t *vxo_text_create(int anchor, const char *alltext)
{
    assert(anchor >= VXO_TEXT_ANCHOR_TOP_LEFT && anchor <= VXO_TEXT_ANCHOR_BOTTOM_RIGHT);

    int justification = VXO_TEXT_JUSTIFY_LEFT;
    vxo_text_font_t *font = get_font("monospaced", VXO_TEXT_PLAIN);
    float font_size = 18;

    float rgba[4] = { 0, 0, 0, 1 };
    int width = -1;
    int margin = 0; // how many pixels should we pad on all sides?

    zarray_t *lines = zarray_create(sizeof(vxo_text_line_t*));

    zarray_t *ss = str_split(alltext, "\n");
    for (int ssidx = 0; ssidx < zarray_size(ss); ssidx++) {
        char *s;
        zarray_get(ss, ssidx, &s);
        int slen = strlen(s);

        int pos = 0;
        vxo_text_line_t *line = calloc(1, sizeof(vxo_text_line_t));
        line->fragments = zarray_create(sizeof(vxo_text_fragment_t*));

        line->justification = justification;
        zarray_add(lines, &line);

        while (pos >= 0 && pos < slen) {
            // relative to beginning of 's', find the index where << and >> begin.
            int fmtpos = str_indexof(&s[pos], "<<");
            int endfmtpos = str_indexof(&s[pos], ">>");
            if (fmtpos >= 0)
                fmtpos += pos;
            if (endfmtpos >= 0)
                endfmtpos += pos;

            if (fmtpos != pos || fmtpos < 0 || endfmtpos < 0) {
                // here's a block of text that is ready to be rendered.
                vxo_text_fragment_t *frag = calloc(1, sizeof(vxo_text_fragment_t));
                frag->font = font;
                frag->font_size = font_size;
                memcpy(frag->rgba, rgba, 4*sizeof(float));
                frag->width = width;
                width = -1;
                if (fmtpos < 0)
                    frag->s = str_substring(s, pos, -1); // the whole string
                else
                    frag->s = str_substring(s, pos, fmtpos); // just part of the string

                zarray_add(line->fragments, &frag);
                pos = fmtpos;
                continue;
            }

            // a format specifier begins at pos
            char *chunk = str_substring(s, fmtpos+2, endfmtpos);

            zarray_t *toks = str_split(chunk, ",");
            for (int tokidx = 0; tokidx < zarray_size(toks); tokidx++) {
                char *tok;
                zarray_get(toks, tokidx, &tok);
                str_trim(tok);
                str_tolowercase(tok);

                int tlen = strlen(tok);

                if (tok[0]=='#' && tlen==7) { // #RRGGBB
                    rgba[0] = ((hexchar_to_int(tok[1])<<4) + hexchar_to_int(tok[2])) / 255.0f;
                    rgba[1] = ((hexchar_to_int(tok[3])<<4) + hexchar_to_int(tok[4])) / 255.0f;
                    rgba[2] = ((hexchar_to_int(tok[5])<<4) + hexchar_to_int(tok[6])) / 255.0f;
                    rgba[3] = 1;
                    continue;
                }

                if (tok[0]=='#' && tlen==9) { // #RRGGBBAA
                    rgba[0] = ((hexchar_to_int(tok[1])<<4) + hexchar_to_int(tok[2])) / 255.0f;
                    rgba[1] = ((hexchar_to_int(tok[3])<<4) + hexchar_to_int(tok[4])) / 255.0f;
                    rgba[2] = ((hexchar_to_int(tok[5])<<4) + hexchar_to_int(tok[6])) / 255.0f;
                    rgba[3] = ((hexchar_to_int(tok[7])<<4) + hexchar_to_int(tok[8])) / 255.0f;
                    continue;
                }

                if (1) {
                    const char *font_names[] = {"serif", "sansserif", "monospaced", NULL };
                    int good = 0;

                    for (int k = 0; font_names[k] != NULL; k++) {
                        if (str_starts_with(tok, font_names[k])) {
                            int style = VXO_TEXT_PLAIN;

                            zarray_t *parts = str_split(tok, "-");
                            for (int tsidx = 1; tsidx < zarray_size(parts); tsidx++) {
                                char *part;
                                zarray_get(parts, tsidx, &part);
                                if (!strcmp(part, "bold"))
                                    style |= VXO_TEXT_BOLD;
                                else if (!strcmp(part, "italic"))
                                    style |= VXO_TEXT_ITALIC;
                                else if (isdigit(part[0]))
                                    font_size = atof(part);
                                else
                                    printf("unknown font specification %s\n", part);
                            }

                            font = get_font(font_names[k], style);

                            zarray_vmap(parts, free);
                            zarray_destroy(parts);
                            good = 1;
                            break;
                        }
                    }
                    if (good)
                        continue;
                }

                if (!strcmp(tok, "left")) {
                    justification = VXO_TEXT_JUSTIFY_LEFT;
                    line->justification = justification;
                    continue;
                }

                if (!strcmp(tok, "right")) {
                    justification = VXO_TEXT_JUSTIFY_RIGHT;
                    line->justification = justification;
                    continue;
                }

                if (!strcmp(tok, "center")) {
                    justification = VXO_TEXT_JUSTIFY_CENTER;
                    line->justification = justification;
                    continue;
                }

                if (str_starts_with(s, "width=")) {
                    width = atoi(&s[6]);
                    continue;
                }

                printf("vx_text_t: unknown format %s\n", tok);
            }

            free(chunk);
            zarray_vmap(toks, free);
            zarray_destroy(toks);

            // skip to end of format specifier.
            pos =  endfmtpos + 2;
        }
    }
    zarray_vmap(ss, free);
    zarray_destroy(ss);

    if (0) {
        // debug output
        for (int lineidx = 0; lineidx < zarray_size(lines); lineidx++) {
            vxo_text_line_t *line;
            zarray_get(lines, lineidx, &line);

            printf("LINE %d\n", lineidx);
            for (int fragidx = 0; fragidx < zarray_size(line->fragments); fragidx++) {
                vxo_text_fragment_t *frag;
                zarray_get(line->fragments, fragidx, &frag);

                printf(" [Frag %d]%s\n", fragidx, frag->s);
            }
        }
    }

    //////////////////////////////////////////////////////
    // it's render time.
    double total_height = 0;
    double total_width = 0;

    for (int lineidx = 0; lineidx < zarray_size(lines); lineidx++) {
        vxo_text_line_t *line;
        zarray_get(lines, lineidx, &line);

        double this_width = vxo_text_line_get_width(line);
        double this_height = vxo_text_line_get_height(line);

        total_width = fmax(total_width, this_width);
        total_height += this_height;
    }

    double anchorx = 0, anchory = 0;

    switch (anchor) {
        case VXO_TEXT_ANCHOR_TOP_LEFT:
        case VXO_TEXT_ANCHOR_LEFT:
        case VXO_TEXT_ANCHOR_BOTTOM_LEFT:
        case VXO_TEXT_ANCHOR_TOP_LEFT_ROUND:
        case VXO_TEXT_ANCHOR_LEFT_ROUND:
        case VXO_TEXT_ANCHOR_BOTTOM_LEFT_ROUND:
            anchorx = 0;
            break;

        case VXO_TEXT_ANCHOR_TOP_RIGHT:
        case VXO_TEXT_ANCHOR_RIGHT:
        case VXO_TEXT_ANCHOR_BOTTOM_RIGHT:
            anchorx = -total_width;
            break;

        case VXO_TEXT_ANCHOR_TOP_RIGHT_ROUND:
        case VXO_TEXT_ANCHOR_RIGHT_ROUND:
        case VXO_TEXT_ANCHOR_BOTTOM_RIGHT_ROUND:
            anchorx = round(-total_width);
            break;

        case VXO_TEXT_ANCHOR_TOP:
        case VXO_TEXT_ANCHOR_CENTER:
        case VXO_TEXT_ANCHOR_BOTTOM:
            anchorx = -total_width/2;
            break;

        case VXO_TEXT_ANCHOR_TOP_ROUND:
        case VXO_TEXT_ANCHOR_CENTER_ROUND:
        case VXO_TEXT_ANCHOR_BOTTOM_ROUND:
            anchorx = round(-total_width/2);
            break;
    }

    switch (anchor) {
        case VXO_TEXT_ANCHOR_TOP_LEFT:
        case VXO_TEXT_ANCHOR_TOP_RIGHT:
        case VXO_TEXT_ANCHOR_TOP:
            anchory = -total_height;
            break;

        case VXO_TEXT_ANCHOR_TOP_LEFT_ROUND:
        case VXO_TEXT_ANCHOR_TOP_RIGHT_ROUND:
        case VXO_TEXT_ANCHOR_TOP_ROUND:
            anchory = round(-total_height);
            break;

        case VXO_TEXT_ANCHOR_BOTTOM_LEFT:
        case VXO_TEXT_ANCHOR_BOTTOM_RIGHT:
        case VXO_TEXT_ANCHOR_BOTTOM:
        case VXO_TEXT_ANCHOR_BOTTOM_LEFT_ROUND:
        case VXO_TEXT_ANCHOR_BOTTOM_RIGHT_ROUND:
        case VXO_TEXT_ANCHOR_BOTTOM_ROUND:
            anchory = 0;
            break;

        case VXO_TEXT_ANCHOR_RIGHT:
        case VXO_TEXT_ANCHOR_LEFT:
        case VXO_TEXT_ANCHOR_CENTER:
            anchory = -total_height / 2;
            break;

        case VXO_TEXT_ANCHOR_RIGHT_ROUND:
        case VXO_TEXT_ANCHOR_LEFT_ROUND:
        case VXO_TEXT_ANCHOR_CENTER_ROUND:
            anchory = round(-total_height / 2);
            break;
    }

    // now actually construct the vx_object.
    vxo_text_t *vt = calloc(1, sizeof(vxo_text_t));
    vt->chain        = vxo_chain_create();
    vt->vxo.append   = append;
    vt->vxo.destroy  = destroy;
    vt->vxo.refcnt   = 0;
    vt->total_width  = total_width;
    vt->total_height = total_height;

    // draw drop shadow
    if (1) {
        // XXX
    }


    // draw text
    double y = total_height;

    for (int lineidx = 0; lineidx < zarray_size(lines); lineidx++) {
        vxo_text_line_t *line;
        zarray_get(lines, lineidx, &line);

        double line_width = 0;
        double x = 0;

        for (int fragidx = 0; fragidx < zarray_size(line->fragments); fragidx++) {
            vxo_text_fragment_t *frag;
            zarray_get(line->fragments, fragidx, &frag);

            line_width += vxo_text_fragment_get_width(frag);
        }

        switch (line->justification) {
            case VXO_TEXT_JUSTIFY_LEFT:
                break;
            case VXO_TEXT_JUSTIFY_RIGHT:
                x = total_width - line_width;
                break;
            case VXO_TEXT_JUSTIFY_CENTER:
                x = (total_width - line_width) / 2.0;
                break;
        }

        // move up a line.
        y -= vxo_text_line_get_height(line);

        for (int fragidx = 0; fragidx < zarray_size(line->fragments); fragidx++) {
            vxo_text_fragment_t *frag;
            zarray_get(line->fragments, fragidx, &frag);

            vxo_chain_add(vt->chain,
                          vxo_mat_push(),
                          vxo_mat_translate2(anchorx + x + margin, anchory + y - margin),
                          vxo_mat_scale(frag->font_size / frag->font->native_points),
                          vxo_text_fragment_make_program(frag)->super,
                          vxo_mat_pop(),
                          NULL);

            x += vxo_text_fragment_get_width(frag);
        }
    }

    //////////////////////////////////////////////////////
    // cleanup
    for (int lineidx = 0; lineidx < zarray_size(lines); lineidx++) {
        vxo_text_line_t *line;
        zarray_get(lines, lineidx, &line);

        for (int fragidx = 0; fragidx < zarray_size(line->fragments); fragidx++) {
            vxo_text_fragment_t *frag;
            zarray_get(line->fragments, fragidx, &frag);

            free(frag->s);
            free(frag);
        }

        zarray_destroy(line->fragments);
        free(line);
    }

    zarray_destroy(lines);

    return (vx_object_t*) vt;
}

double vxo_text_get_width(vx_object_t *vo)
{
    vxo_text_t *vt = (vxo_text_t*) vo;
    return vt->total_width;
}

double vxo_text_get_height(vx_object_t *vo)
{
    vxo_text_t *vt = (vxo_text_t*) vo;
    return vt->total_height;
}


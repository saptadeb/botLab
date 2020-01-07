#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "vx_codes.h"
#include "vx_viewport_mgr.h"


#define MODE_REL 1
#define MODE_ABS 2

// The strategy is to always store the current position as a relative

struct vx_viewport_mgr
{
    int mode;

    // The current position, always need to be in sync
    // so that we can switch modes if necessary
    float rel0[4];
    int   abs0[4];
    uint64_t mtime0;

    // Depending on the mode, we are trying to get to one of these two positions
    uint64_t mtime1;

    float rel1[4];

    int align_type1;
    int width1, height1;
    int offx1, offy1;
};

// compute a partial absolute viewport based on a relative viewport and
// the full 'viewport' (absolute)
static void absolute_viewport(int viewport[], float viewport_rel[],  int viewport_out[])
{
    assert(viewport[0] == 0.0f);
    assert(viewport[1] == 0.0f);

    viewport_out[0] = (int)(viewport[2]*viewport_rel[0]);
    viewport_out[1] = (int)(viewport[3]*viewport_rel[1]);
    viewport_out[2] = (int)(viewport[2]*viewport_rel[2]);
    viewport_out[3] = (int)(viewport[3]*viewport_rel[3]);
}

vx_viewport_mgr_t * vx_viewport_mgr_create(void)
{
    vx_viewport_mgr_t * mgr = calloc(1, sizeof(vx_viewport_mgr_t));
    mgr->mode = MODE_REL;

    // set viewport to fill screen by default
    mgr->rel0[0] = mgr->rel0[1] = 0.0f;
    mgr->rel0[2] = mgr->rel0[3] = 1.0f;
    mgr->mode = MODE_REL;
    memcpy(mgr->rel1, mgr->rel0, 4* sizeof(float));

    // XXX How to properly set abs0 properly? We don't know the actual
    // viewport size. So if we switch to ABS mode before we get queried
    // the first time, we won't be able to animate properly. Could use
    // mtime == 0 as a flag?

    return mgr;
}

static void get_abs_aligned(vx_viewport_mgr_t * mgr, int * fullviewport4, int * output4)
{

    int ww = mgr->width1;
    int wh = mgr->height1;

    double px0, px1, py0, py1;
    switch (mgr->align_type1)
    {
        case OP_ANCHOR_TOP_LEFT:
        case OP_ANCHOR_LEFT:
        case OP_ANCHOR_BOTTOM_LEFT:
            px0 = 0;
            px1 = ww;
            break;
        default:
        case OP_ANCHOR_TOP:
        case OP_ANCHOR_CENTER:
        case OP_ANCHOR_BOTTOM:
            px0 = (fullviewport4[0] + fullviewport4[2]) / 2 - ww / 2;
            px1 = px0 + ww;
            break;
        case OP_ANCHOR_TOP_RIGHT:
        case OP_ANCHOR_RIGHT:
        case OP_ANCHOR_BOTTOM_RIGHT:
            px1 = fullviewport4[2] - 1;
            px0 = px1 - ww;
            break;
    }
    switch (mgr->align_type1)
    {
        case OP_ANCHOR_TOP_LEFT:
        case OP_ANCHOR_TOP:
        case OP_ANCHOR_TOP_RIGHT:
            // remember that y is inverted: y=0 is at bottom
            // left in GL
            py0 = fullviewport4[3] - wh - 1;
            py1 = py0 + wh;
            break;
        default:
        case OP_ANCHOR_LEFT:
        case OP_ANCHOR_CENTER:
        case OP_ANCHOR_RIGHT:
            py0 = (fullviewport4[1] + fullviewport4[3]) / 2 - wh / 2;
            py1 = py0 + wh;
            break;
        case OP_ANCHOR_BOTTOM_LEFT:
        case OP_ANCHOR_BOTTOM:
        case OP_ANCHOR_BOTTOM_RIGHT:
            py0 = 0;
            py1 = py0 + wh;
            break;
    }
    output4[0] = mgr->offx1 + (int)px0;
    output4[1] = mgr->offy1 + (int)py0;
    output4[2] = (int)(px1-px0);
    output4[3] = (int)(py1-py0);
}

// Pass in the window size and current time, returns the viewport in
// pixels for this layer
int * vx_viewport_mgr_get_pos(vx_viewport_mgr_t *mgr, int *fullviewport4, uint64_t mtime)
{

    // On first run, we need to populate abs0
    if (mgr->mtime0 == 0) {

        if (mgr->mode == MODE_ABS) { // special case
            get_abs_aligned(mgr, fullviewport4, mgr->abs0);
        } else if (mgr->mode == MODE_REL) {
            absolute_viewport(fullviewport4, mgr->rel0, mgr->abs0);
        }
        mgr->mtime0 = mtime;
    }

    // this is the absolute viewport we will eventually return
    int * viewport = calloc(4, sizeof(int));
    int abs1[4] = {0};

    if (mgr->mode == MODE_REL) {

        // re-convert the relative viewports:
        absolute_viewport(fullviewport4, mgr->rel0, mgr->abs0);
        absolute_viewport(fullviewport4, mgr->rel1, abs1);
    }else if (mgr->mode == MODE_ABS) {
        get_abs_aligned(mgr, fullviewport4, abs1);
    } else {
        assert(0);
    }

    // Case 1: use the target
    if ( mtime >= mgr->mtime1) {
        memcpy(viewport, abs1, 4*sizeof(int));
    }
    // Case 2: use the source
    else if (mtime <= mgr->mtime0) {
        memcpy(viewport, mgr->abs0, 4*sizeof(int));
    }
    // Case 3: interpolate
    else {
        float alpha1 = (((double) mtime - mgr->mtime0) / (mgr->mtime1 - mgr->mtime0));
        float alpha0 = 1.0 - alpha1;


        for (int i = 0; i < 4; i++)
            viewport[i] = (int)roundf(alpha0*mgr->abs0[i] + alpha1*abs1[i]);
    }


    // Just before returning, copy the result back to rel0 and abs0 and mtime0:
    {
        mgr->mtime0 = mtime;
        memcpy(mgr->abs0, viewport, 4*sizeof(int));

        // recompute the relative0 viewport

        // X
        mgr->rel0[0] = ((float)viewport[0]) / fullviewport4[2];
        mgr->rel0[2] = ((float)viewport[2]) / fullviewport4[2];

        // Y
        mgr->rel0[1] = ((float)viewport[1]) / fullviewport4[3];
        mgr->rel0[3] = ((float)viewport[3]) / fullviewport4[3];
    }

    return viewport;
}


void vx_viewport_mgr_set_rel(vx_viewport_mgr_t *mgr, float * viewport4_rel, uint64_t mtime_goal)
{
    mgr->mode = MODE_REL;
    memcpy(mgr->rel1, viewport4_rel, 4*sizeof(float));
    mgr->mtime1 = mtime_goal;
}

void vx_viewport_mgr_set_abs(vx_viewport_mgr_t *mgr, int align_type,
                             int offx, int offy, int width, int height,
                             uint64_t mtime_goal)
{
    mgr->mode = MODE_ABS;
    mgr->align_type1 = align_type;
    mgr->offx1 = offx;
    mgr->offy1 = offy;
    mgr->width1 = width;
    mgr->height1 = height;

    mgr->mtime1 = mtime_goal;
}

void vx_viewport_mgr_destroy(vx_viewport_mgr_t *mgr)
{
    free(mgr);
}


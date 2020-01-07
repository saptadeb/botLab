#ifndef __VX_TEXT_H__
#define __VX_TEXT_H__

#include "vx_object.h"
#include "vxo_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VXO_TEXT_ANCHOR_TOP_LEFT = 1000, VXO_TEXT_ANCHOR_TOP_LEFT_ROUND,
    VXO_TEXT_ANCHOR_TOP, VXO_TEXT_ANCHOR_TOP_ROUND,
    VXO_TEXT_ANCHOR_TOP_RIGHT,VXO_TEXT_ANCHOR_TOP_RIGHT_ROUND,
    VXO_TEXT_ANCHOR_LEFT, VXO_TEXT_ANCHOR_LEFT_ROUND,
    VXO_TEXT_ANCHOR_CENTER, VXO_TEXT_ANCHOR_CENTER_ROUND,
    VXO_TEXT_ANCHOR_RIGHT, VXO_TEXT_ANCHOR_RIGHT_ROUND,
    VXO_TEXT_ANCHOR_BOTTOM_LEFT, VXO_TEXT_ANCHOR_BOTTOM_LEFT_ROUND,
    VXO_TEXT_ANCHOR_BOTTOM, VXO_TEXT_ANCHOR_BOTTOM_ROUND,
    VXO_TEXT_ANCHOR_BOTTOM_RIGHT, VXO_TEXT_ANCHOR_BOTTOM_RIGHT_ROUND };

enum {
    VXO_TEXT_JUSTIFY_LEFT = 2000,
    VXO_TEXT_JUSTIFY_RIGHT,
    VXO_TEXT_JUSTIFY_CENTER
};

enum {
    VXO_TEXT_PLAIN = 0,
    VXO_TEXT_ITALIC = 1,
    VXO_TEXT_BOLD = 2
};

typedef struct vxo_text vxo_text_t;
struct vxo_text
{
    vx_object_t  vxo; // must be first element.
    vx_object_t *chain;
    double       total_width;
    double       total_height;
};

/**
 * Create a text object. Pass in a string with markup in <<double-brackets>>.
 *
 * Format options:
 *    - Separate multiple options with commas ','
 *    - Colors. Specify in #RRGGBB or #RRGGBBAA format
 *    - Fonts. Options: 'serif', 'sansserif', 'monospaced'. Modify with '-bold', or '-italic'
 *    - Alignment. Options: 'left', 'right', 'center'
 *
 * Example:
 *    "<<monospaced-bold,left,#ff0000>>Example string\nSecond line"
 */
vx_object_t *vxo_text_create(int ANCHOR, const char *s);

/**
 * Get the width or height from a text object returned by vxo_text_create()
 */
double vxo_text_get_width(vx_object_t *vo);
double vxo_text_get_height(vx_object_t *vo);

#ifdef __cplusplus
}
#endif

#endif

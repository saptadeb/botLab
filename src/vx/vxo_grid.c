#include "vxo_grid.h"
#include "vx_global.h"
#include "vxo_lines.h"
#include "vx_codes.h"
#include "vx_colors.h"

// how many lines along each axis?
#define N_AXES_LINES 200
//#define N_AXES_LINES 500

static vx_resc_t * grid_vertices = NULL;

static void vxo_grid_init(int npts)
{

    // each iteration we make 2 points, along 2 axis in 2D, so stride = 8
    float all_lines[npts*8];

    for (int i = 0; i < npts/2; i++) {

        float x = i;
        float y = npts/2;


        // Vertical lines
        all_lines[ 16 * i + 0] =  x;
        all_lines[ 16 * i + 1] =  y;
        all_lines[ 16 * i + 2] =  x;
        all_lines[ 16 * i + 3] = -y;


        all_lines[ 16 * i + 4] = -x;
        all_lines[ 16 * i + 5] =  y;
        all_lines[ 16 * i + 6] = -x;
        all_lines[ 16 * i + 7] = -y;

        // Horizontal lines  (swap assignment of x->y)
        all_lines[ 16 * i + 8] =  y;
        all_lines[ 16 * i + 9] =  x;
        all_lines[ 16 * i + 10] = -y;
        all_lines[ 16 * i + 11] =  x;

        all_lines[ 16 * i + 12] =  y;
        all_lines[ 16 * i + 13] = -x;
        all_lines[ 16 * i + 14] = -y;
        all_lines[ 16 * i + 15] = -x;

    }

    grid_vertices = vx_resc_copyf(all_lines, npts*8);
    vx_resc_inc_ref(grid_vertices);
}


// will run when the program vx_global_destroy() is called by user at end of program
static void vxo_grid_destroy(void * ignored)
{
    vx_resc_dec_destroy(grid_vertices);
}

// 1M grid by default
vx_object_t * vxo_grid_colored(vx_style_t * style)
{
    // Make sure the static geometry is initialized safely, correctly, and quickly
    if (grid_vertices == NULL) {
        pthread_mutex_lock(&vx_convenience_mutex);
        if (grid_vertices == NULL) {
            vxo_grid_init(N_AXES_LINES);
            vx_global_register_destroy(vxo_grid_destroy, NULL);
        }
        pthread_mutex_unlock(&vx_convenience_mutex);
    }

    return vxo_lines(grid_vertices, N_AXES_LINES*4, GL_LINES, style);
}

vx_object_t * vxo_grid(void)
{
    return vxo_grid_colored(vxo_lines_style(vx_gray, 1.0f));
}

#ifndef _VX_CONSOLE_H
#define _VX_CONSOLE_H

#include "common/zarray.h"

#include "vx_world.h"
#include "vx_event_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _vx_console vx_console_t;

/**
 * Create a vx console. Must provide function pointers for a function to
 * receive commands, and a function to perform list possible commands during
 * tab completion. Callbacks occur in the GUI thread, in vx key event callbacks.
 *
 * write_buffer:    a vx_buffer where the console will actually be drawn
 * console_command: passed a full command
 * console_tab:     a tab-complete function. Passed the current command. Returns
 *                  a zarray of one string per possible completion. Spaces
 *                  should be used in the results when appropriate.
 * user:            a user pointer to pass to callback functions
 */
vx_console_t*
vx_console_create(vx_buffer_t *write_buffer,
                  void (*console_command)(vx_console_t *vc, const char *cmd, void *user),
                  zarray_t* (*console_tab)(vx_console_t *vc, const char *cmd, void *user),
                  void *user);

/**
 * Destroy a vx console.
 */
void vx_console_destroy(vx_console_t *vc);

/**
 * Get an event handler for this vx console. This call will return allocated memory,
 * which vx_layer is expected to assume control over through vx_layer_add_event_handler()
 */
vx_event_handler_t *vx_console_get_event_handler(vx_console_t *vc);

/**
 * Print to the vx console. String format should match vxo_text. Will be added
 * to the console output below previous lines and above the typing buffer.
 *
 * This function can safely be called from a separate thread.
 */
void vx_console_print(vx_console_t *vc, const char *str);

/**
 * Version of above function using a printf()-style format interface
 */
void vx_console_printf(vx_console_t *vc, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));


#ifdef __cplusplus
}
#endif

#endif


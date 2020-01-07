#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>

#include "common/string_util.h"

#include "vx_console.h"
#include "vx_key_codes.h"
#include "vx_codes.h"
#include "vxo_text.h"
#include "vxo_pix_coords.h"

#define VX_CONSOLE_MAX_LINES 5
#define VX_CONSOLE_COMMAND_HISTORY_SIZE 10
#define VX_CONSOLE_MAX_CHARS_WIDTH  80  // TODO: determine good value

#define ACTIVE_COMMAND_FORMAT         "<<left,#ffffff>>" // white text
#define CURSOR_FORMAT                 "<<left,#ffffff>>"
#define HISTORY_COMMAND_FORMAT        "<<left,#ffff00>>" // yellow text
#define TAB_COMPLETE_COMMAND_FORMAT   "<<left,#00ffff>>"

struct _vx_console
{
    vx_buffer_t *write_buffer;   // owned by vx
    void (*console_command)(vx_console_t *vc, const char *cmd, void *user);
    zarray_t* (*console_tab)(vx_console_t *vc, const char *cmd, void *user);
    void *user;

    int has_keyboard_focus;
    string_buffer_t *command;
    zarray_t *history;         // strings, owned by vx_console, oldest values first
    zarray_t *command_history; // only for up-arrow based completions
    int selected_cmd;          // if we are cycling through old commands

    pthread_mutex_t mutex;
};

vx_console_t*
vx_console_create(vx_buffer_t *write_buffer,
                  void (*console_command)(vx_console_t *vc, const char *cmd, void *user),
                  zarray_t* (*console_tab)(vx_console_t *vc, const char *cmd, void *user),
                  void *user)
{
    vx_console_t *vc = calloc(1, sizeof(vx_console_t));

    vc->write_buffer = write_buffer;
    vc->console_command = console_command;
    vc->console_tab = console_tab;
    vc->user = user;

    vc->has_keyboard_focus = 0;
    vc->history = zarray_create(sizeof(char*));
    vc->command_history = zarray_create(sizeof(char*));
    vc->command = string_buffer_create();
    vc->selected_cmd = -1;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&vc->mutex, &attr);

    pthread_mutexattr_destroy(&attr);

    return vc;
}

void vx_console_destroy(vx_console_t *vc)
{
    if (vc == NULL) return;

    zarray_vmap(vc->history, free);
    zarray_destroy(vc->history);

    zarray_vmap(vc->command_history, free);
    zarray_destroy(vc->command_history);

    string_buffer_destroy(vc->command);

    pthread_mutex_unlock(&vc->mutex);
    pthread_mutex_destroy(&vc->mutex);
    free(vc);
}

static void draw_console(vx_console_t *vc)
{
    // assemble a string for vxo_text in lower left
    string_buffer_t *sb = string_buffer_create();

    // render the current history - already have coloring/styles added
    for (int i = 0; i < zarray_size(vc->history); i++) {
        char *history_i;
        zarray_get(vc->history, i, &history_i);
        string_buffer_append_string(sb, history_i);
        if (i != zarray_size(vc->history)-1  || vc->has_keyboard_focus)
            string_buffer_append(sb, '\n'); // add newlines for each command
    }

    // render command if active
    if (vc->has_keyboard_focus) {
        char *command_str = string_buffer_to_string(vc->command);
        string_buffer_append_string(sb, ACTIVE_COMMAND_FORMAT);
        string_buffer_append_string(sb, ":");
        string_buffer_append_string(sb, command_str);
        string_buffer_append_string(sb, CURSOR_FORMAT);
        string_buffer_append_string(sb, "_");
        free(command_str);
    }

    // render console
    char *sb_str = string_buffer_to_string(sb);
    vx_object_t *vt = vxo_text_create(VXO_TEXT_ANCHOR_BOTTOM_LEFT, sb_str);
    free(sb_str);
    string_buffer_destroy(sb);
    vx_buffer_add_back(vc->write_buffer, vxo_pix_coords(VX_ORIGIN_BOTTOM_LEFT,vt));
    vx_buffer_set_draw_order(vc->write_buffer, 100);
    vx_buffer_swap(vc->write_buffer);
}

static int str_tail_overlap_indexof(const char *haystack, const char *needle)
{
    assert(haystack != NULL);
    assert(needle != NULL);

    int hlen = strlen(haystack);
    int nlen = strlen(needle);

    int max_overlap = nlen;
    if (nlen > hlen)
        max_overlap = hlen;
    for (int i = 0; i < nlen; i++) {
        char *hsub = str_substring(haystack, hlen-max_overlap+i, -1);
        char *nsub = str_substring(needle, 0, max_overlap-i);
        if (strcmp(hsub, nsub) == 0) {
            return hlen-max_overlap+i;
        }
    }
    return -1;
}

static void handle_tab_completions(vx_console_t *vc, const zarray_t *completions)
{
    if (zarray_size(completions) == 1) {
        // swap out the the command buffer
        // assume that tab completion function matches the last token
        char *command_str = string_buffer_to_string(vc->command);
        char *completion;
        zarray_get(completions, 0, &completion);

        // find overlap
        int tail_overlap = str_tail_overlap_indexof(command_str, completion);
        assert(tail_overlap >= 0);
        char *old_part = str_substring(command_str, 0, tail_overlap);
        string_buffer_reset(vc->command);
        string_buffer_appendf(vc->command, "%s%s", old_part, completion);
        free(old_part);
        free(command_str);
    } else {
        // flag for rendering as a single line
        string_buffer_t *sb = string_buffer_create();
        string_buffer_append_string(sb, TAB_COMPLETE_COMMAND_FORMAT);
        int nr_chars = 0;
        for (int i = 0; i < zarray_size(completions); i++) {
            char *tok;
            zarray_get(completions, i, &tok);
            int len = strlen(tok);
            if (len + nr_chars > VX_CONSOLE_MAX_CHARS_WIDTH) {
                string_buffer_appendf(sb, "\n%s ", tok);
                if (len > VX_CONSOLE_MAX_CHARS_WIDTH)
                    string_buffer_append(sb, '\n');
                nr_chars = 0;
            } else {
                string_buffer_append_string(sb, tok);
                string_buffer_append(sb, ' ');
                nr_chars += len + 1;
            }
        }
        char *tab_complete_str = string_buffer_to_string(sb);
        vx_console_print(vc, tab_complete_str);
        free(tab_complete_str);
    }
}

static void add_command_to_history(vx_console_t *vc, const char *command_str)
{
    // adds to both shown history and command history
    if (zarray_size(vc->history) == VX_CONSOLE_MAX_LINES)
        zarray_remove_index(vc->history, 0, 0);
    if (zarray_size(vc->command_history) == VX_CONSOLE_COMMAND_HISTORY_SIZE)
        zarray_remove_index(vc->command_history, 0, 0);

    char *formatted_command = str_concat(HISTORY_COMMAND_FORMAT, command_str);
    zarray_add(vc->history, &formatted_command);

    char *cmd = strdup(command_str);
    zarray_add(vc->command_history, &cmd);
}

static int vx_console_event_touch_event(vx_event_handler_t *vh, vx_layer_t *vl,
                                        vx_camera_pos_t *pos, vx_touch_event_t *mouse)
{
    return 0; // never used
}

static int vx_console_event_mouse_event(vx_event_handler_t *vh, vx_layer_t *vl,
                                        vx_camera_pos_t *pos, vx_mouse_event_t *mouse)
{
    return 0; // never used
}

static int vx_console_event_key_event(vx_event_handler_t *vh, vx_layer_t *vl, vx_key_event_t *key)
{
    // check whether we are already in console mode
    // lock out keyboard commands until hitting either enter or escape

    assert(vh->impl != NULL);
    vx_console_t *vc = vh->impl;
    int shift = key->modifiers & VX_SHIFT_MASK;

    // Do nothing on release
    if (key->released)
        return 0;

    int result = 0;
    pthread_mutex_lock(&vc->mutex);
    if (vc->has_keyboard_focus) {
        if (key->key_code == VX_KEY_ESC) {
            // break out of focus and clear line with escape
            string_buffer_reset(vc->command);
            vc->has_keyboard_focus = 0;
            vc->selected_cmd = -1;
        }
        else if (key->key_code == VX_KEY_TAB) {
            // call tab complete for tab
            char *command_str = string_buffer_to_string(vc->command);

            zarray_t *completions = (vc->console_tab)(vc, command_str, vc->user);
            handle_tab_completions(vc, completions);

            zarray_vmap(completions, free);
            zarray_destroy(completions);
            free(command_str);
        }
        else if (key->key_code == VX_KEY_ENTER) {
            // execute command for enter and release focus
            char *command_str = string_buffer_to_string(vc->command);
            add_command_to_history(vc, command_str);

            (vc->console_command)(vc, command_str, vc->user); // may change print statements here

            vc->has_keyboard_focus = 0;
            vc->selected_cmd = -1;

            string_buffer_reset(vc->command);
            free(command_str);
        }
        else if (key->key_code == VX_KEY_BACKSPACE) {
            // step back one character
            string_buffer_pop_back(vc->command);
        }
        else if (key->key_code == VX_KEY_UP) {
            // swap with previous command in buffer
            const int s = zarray_size(vc->command_history);
            ++vc->selected_cmd;
            if (vc->selected_cmd < s) {
                char *selected_command;
                zarray_get(vc->command_history,
                           s - 1 - vc->selected_cmd,
                           &selected_command);
                string_buffer_reset(vc->command);
                string_buffer_append_string(vc->command, selected_command);
            }
        }
        else if (key->key_code == VX_KEY_DOWN) {
            const int s = zarray_size(vc->command_history);
            // Go forward in history, or clear if already at bottom
            --vc->selected_cmd;
            if (vc->selected_cmd < 0) {
                string_buffer_reset(vc->command);
                vc->selected_cmd = -1;
            } else {
                char *selected_command;
                zarray_get(vc->command_history,
                        s - 1 - vc->selected_cmd,
                        &selected_command);
                string_buffer_reset(vc->command);
                string_buffer_append_string(vc->command, selected_command);
            }
        }
        else {
            // all other commands get appended to current line
            string_buffer_append(vc->command, key->key_code);
        }
        draw_console(vc); // draw current state
        result = 1;
    }
    else if (shift && (key->key_code == VX_KEY_SEMICOLON || key->key_code == VX_KEY_COLON)) {
        vc->has_keyboard_focus = 1;
        draw_console(vc);
        result = 1;
    }
    pthread_mutex_unlock(&vc->mutex);

    return result;
}

void vx_console_event_destroy(vx_event_handler_t * vh)
{
    free(vh); // console implementation performed separately
}

vx_event_handler_t *vx_console_get_event_handler(vx_console_t *vc)
{
    vx_event_handler_t *event_h = calloc(1, sizeof(vx_event_handler_t));
    event_h->mouse_event    = vx_console_event_mouse_event;
    event_h->key_event      = vx_console_event_key_event;
    event_h->touch_event    = vx_console_event_touch_event;
    event_h->destroy        = vx_console_event_destroy;
    event_h->impl           = vc;
    event_h->dispatch_order = 0; // force this to have first access to lock out other controls
    return event_h;
}

void vx_console_print(vx_console_t *vc, const char *str)
{
    pthread_mutex_lock(&vc->mutex);
    {
        if (zarray_size(vc->history) == VX_CONSOLE_MAX_LINES)
            zarray_remove_index(vc->history, 0, 0);

        char *copied_command = strdup(str);
        zarray_add(vc->history, &copied_command);
    }
    pthread_mutex_unlock(&vc->mutex);

    draw_console(vc);
}

void vx_console_printf(vx_console_t *vc, const char *fmt, ...) {
    assert(vc != NULL);
    assert(fmt != NULL);

    va_list args;

    va_start(args,fmt);
    char *buf = vsprintf_alloc(fmt, args);
    va_end(args);

    vx_console_print(vc, buf);
    free(buf);
}

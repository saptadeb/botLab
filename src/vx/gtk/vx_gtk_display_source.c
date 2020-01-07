#include <stdlib.h>
#include <gdk/gdkkeysyms.h>

#include "vx_gtk_display_source.h"
#include "gtk/gtk.h"
#include "gtkuimagepane.h"
#include "vx_gtk_display.h"
#include "vx/vx_event.h"
#include "vx/vx_codes.h"

struct vx_gtk_display_source
{
    vx_display_t * disp;
    vx_application_t * app;
    int button_mask;

    GtkuImagePane * imagePane; // we should make this be passed into the constructor
    GdkPixbuf * pixbuf;
    uint8_t * pixdata;
};


// Convert GTK modifiers to VX modifiers
static int gtk_to_vx_modifiers(int state)
{
    int modifiers = 0;

    // old, new
    int remap_bit[7][2] = {{0,0},
                           {1,4},
                           {2,1},
                           {3,3},
                           {4,5},
                           {5,-1},
                           {6,2}};

    for (int i = 0; i < 7; i++) {
        int outi = remap_bit[i][1];

        if (outi >= 0 && state >> i & 1)
            modifiers |= 1 << outi;
    }
    return modifiers;
}

// Convert GTK modifiers to VX key codes
static int gtk_to_vx_keycode(int code)
{


    switch(code) {
        case GDK_KEY_Escape:
            return VX_KEY_ESC;
        case GDK_KEY_Tab:
            return VX_KEY_TAB;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            return VX_KEY_ENTER;
        case GDK_KEY_BackSpace:
            return VX_KEY_BACKSPACE;
        case GDK_KEY_space:
            return VX_KEY_SPACE;
        case GDK_KEY_Shift_R:
        case GDK_KEY_Shift_L:
            return VX_KEY_SHIFT;
        case GDK_KEY_Control_R:
        case GDK_KEY_Control_L:
            return VX_KEY_CTRL;
        case GDK_KEY_Caps_Lock:
            return VX_KEY_CAPS_LOCK;
        case GDK_KEY_Num_Lock:
            return VX_KEY_NUM_LOCK;
        case GDK_KEY_Alt_R:
        case GDK_KEY_Alt_L:
            return VX_KEY_ALT;
        case GDK_KEY_Insert:
        case GDK_KEY_KP_Insert:
            return VX_KEY_INS;
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
            return VX_KEY_DEL;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            return VX_KEY_HOME;
        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            return VX_KEY_END;
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            return VX_KEY_PAGE_UP;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            return VX_KEY_PAGE_DOWN;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            return VX_KEY_LEFT;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            return VX_KEY_RIGHT;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            return VX_KEY_DOWN;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            return VX_KEY_UP;
        case GDK_KEY_asciitilde:
            return VX_KEY_TILDE;
        case GDK_KEY_quoteright:
            return VX_KEY_QUOTE_RIGHT;
        case GDK_KEY_quoteleft:
            return VX_KEY_QUOTE_LEFT;
        case GDK_KEY_quotedbl:
            return VX_KEY_QUOTE_DBL;
        case GDK_KEY_exclam:
            return VX_KEY_EXCLAMATION;
        case GDK_KEY_at:
            return VX_KEY_AT;
        case GDK_KEY_numbersign:
            return VX_KEY_HASH;
        case GDK_KEY_dollar:
            return VX_KEY_DOLLAR;
        case GDK_KEY_percent:
            return VX_KEY_PERCENT;
        case GDK_KEY_asciicircum:
            return VX_KEY_HAT;
        case GDK_KEY_ampersand:
            return VX_KEY_AMPERSAND;
        case GDK_KEY_asterisk:
        case GDK_KEY_KP_Multiply:
            return VX_KEY_ASTERISK;
        case GDK_KEY_parenleft:
            return VX_KEY_PAREN_LEFT;
        case GDK_KEY_parenright:
            return VX_KEY_PAREN_RIGHT;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            return VX_KEY_MINUS;
        case GDK_KEY_underscore:
            return VX_KEY_UNDERSCORE;
        case GDK_KEY_equal:
            return VX_KEY_EQUALS;
        case GDK_KEY_plus:
        case GDK_KEY_KP_Add:
            return VX_KEY_PLUS;
        case GDK_KEY_braceleft:
            return VX_KEY_BRACE_LEFT;
        case GDK_KEY_braceright:
            return VX_KEY_BRACE_RIGHT;
        case GDK_KEY_bracketleft:
            return VX_KEY_BRACKET_LEFT;
        case GDK_KEY_bracketright:
            return VX_KEY_BRACKET_RIGHT;
        case GDK_KEY_backslash:
            return VX_KEY_BACKSLASH;
        case GDK_KEY_bar:
            return VX_KEY_BAR;
        case GDK_KEY_semicolon:
            return VX_KEY_SEMICOLON;
        case GDK_KEY_colon:
            return VX_KEY_COLON;
        case GDK_KEY_comma:
            return VX_KEY_COMMA;
        case GDK_KEY_period:
            return VX_KEY_PERIOD;
        case GDK_KEY_less:
            return VX_KEY_LESS;
        case GDK_KEY_greater:
            return VX_KEY_GREATER;
        case GDK_KEY_slash:
        case GDK_KEY_KP_Divide:
            return VX_KEY_SLASH;
        case GDK_KEY_question:
            return VX_KEY_QUESTION;
        case GDK_KEY_A:
            return VX_KEY_A;
        case GDK_KEY_B:
            return VX_KEY_B;
        case GDK_KEY_C:
            return VX_KEY_C;
        case GDK_KEY_D:
            return VX_KEY_D;
        case GDK_KEY_E:
            return VX_KEY_E;
        case GDK_KEY_F:
            return VX_KEY_F;
        case GDK_KEY_G:
            return VX_KEY_G;
        case GDK_KEY_H:
            return VX_KEY_H;
        case GDK_KEY_I:
            return VX_KEY_I;
        case GDK_KEY_J:
            return VX_KEY_J;
        case GDK_KEY_K:
            return VX_KEY_K;
        case GDK_KEY_L:
            return VX_KEY_L;
        case GDK_KEY_M:
            return VX_KEY_M;
        case GDK_KEY_N:
            return VX_KEY_N;
        case GDK_KEY_O:
            return VX_KEY_O;
        case GDK_KEY_P:
            return VX_KEY_P;
        case GDK_KEY_Q:
            return VX_KEY_Q;
        case GDK_KEY_R:
            return VX_KEY_R;
        case GDK_KEY_S:
            return VX_KEY_S;
        case GDK_KEY_T:
            return VX_KEY_T;
        case GDK_KEY_U:
            return VX_KEY_U;
        case GDK_KEY_V:
            return VX_KEY_V;
        case GDK_KEY_W:
            return VX_KEY_W;
        case GDK_KEY_X:
            return VX_KEY_X;
        case GDK_KEY_Y:
            return VX_KEY_Y;
        case GDK_KEY_Z:
            return VX_KEY_Z;
        case GDK_KEY_a:
            return VX_KEY_a;
        case GDK_KEY_b:
            return VX_KEY_b;
        case GDK_KEY_c:
            return VX_KEY_c;
        case GDK_KEY_d:
            return VX_KEY_d;
        case GDK_KEY_e:
            return VX_KEY_e;
        case GDK_KEY_f:
            return VX_KEY_f;
        case GDK_KEY_g:
            return VX_KEY_g;
        case GDK_KEY_h:
            return VX_KEY_h;
        case GDK_KEY_i:
            return VX_KEY_i;
        case GDK_KEY_j:
            return VX_KEY_j;
        case GDK_KEY_k:
            return VX_KEY_k;
        case GDK_KEY_l:
            return VX_KEY_l;
        case GDK_KEY_m:
            return VX_KEY_m;
        case GDK_KEY_n:
            return VX_KEY_n;
        case GDK_KEY_o:
            return VX_KEY_o;
        case GDK_KEY_p:
            return VX_KEY_p;
        case GDK_KEY_q:
            return VX_KEY_q;
        case GDK_KEY_r:
            return VX_KEY_r;
        case GDK_KEY_s:
            return VX_KEY_s;
        case GDK_KEY_t:
            return VX_KEY_t;
        case GDK_KEY_u:
            return VX_KEY_u;
        case GDK_KEY_v:
            return VX_KEY_v;
        case GDK_KEY_w:
            return VX_KEY_w;
        case GDK_KEY_x:
            return VX_KEY_x;
        case GDK_KEY_y:
            return VX_KEY_y;
        case GDK_KEY_z:
            return VX_KEY_z;
        case GDK_KEY_0:
        case GDK_KEY_KP_0:
            return VX_KEY_0;
        case GDK_KEY_1:
        case GDK_KEY_KP_1:
            return VX_KEY_1;
        case GDK_KEY_2:
        case GDK_KEY_KP_2:
            return VX_KEY_2;
        case GDK_KEY_3:
        case GDK_KEY_KP_3:
            return VX_KEY_3;
        case GDK_KEY_4:
        case GDK_KEY_KP_4:
            return VX_KEY_4;
        case GDK_KEY_5:
        case GDK_KEY_KP_5:
            return VX_KEY_5;
        case GDK_KEY_6:
        case GDK_KEY_KP_6:
            return VX_KEY_6;
        case GDK_KEY_7:
        case GDK_KEY_KP_7:
            return VX_KEY_7;
        case GDK_KEY_8:
        case GDK_KEY_KP_8:
            return VX_KEY_8;
        case GDK_KEY_9:
        case GDK_KEY_KP_9:
            return VX_KEY_9;
        case GDK_KEY_F1:
            return VX_KEY_F1;
        case GDK_KEY_F2:
            return VX_KEY_F2;
        case GDK_KEY_F3:
            return VX_KEY_F3;
        case GDK_KEY_F4:
            return VX_KEY_F4;
        case GDK_KEY_F5:
            return VX_KEY_F5;
        case GDK_KEY_F6:
            return VX_KEY_F6;
        case GDK_KEY_F7:
            return VX_KEY_F7;
        case GDK_KEY_F8:
            return VX_KEY_F8;
        case GDK_KEY_F9:
            return VX_KEY_F9;
        case GDK_KEY_F10:
            return VX_KEY_F10;
        case GDK_KEY_F11:
            return VX_KEY_F11;
        case GDK_KEY_F12:
            return VX_KEY_F12;
        case GDK_KEY_F13:
            return VX_KEY_F13;
        case GDK_KEY_F14:
            return VX_KEY_F14;
        case GDK_KEY_F15:
            return VX_KEY_F15;
        case GDK_KEY_F16:
            return VX_KEY_F16;
        case GDK_KEY_F17:
            return VX_KEY_F17;
        case GDK_KEY_F18:
            return VX_KEY_F18;
        case GDK_KEY_F19:
            return VX_KEY_F19;
        case GDK_KEY_F20:
            return VX_KEY_F20;
        case GDK_KEY_F21:
            return VX_KEY_F21;
        case GDK_KEY_F22:
            return VX_KEY_F22;
        case GDK_KEY_F23:
            return VX_KEY_F23;
        case GDK_KEY_F24:
            return VX_KEY_F24;
        case GDK_KEY_Super_L: //Fallthrough
        case GDK_KEY_Super_R:
            return VX_KEY_WINDOWS;
        default:
            return VX_KEY_UNKNOWN;
    }



    return code;
}

static void update_button_by_id(vx_gtk_display_source_t * gtk, int button_id, int value)
{
    if (value) // button is down
        gtk->button_mask |= 1 << (button_id -1);
    else // button is up
        gtk->button_mask &= ~(1 << (button_id -1));
}

static void update_button_by_state(vx_gtk_display_source_t * gtk, int state)
{
    // based on GdkModifierType enum
    for (int i = 1; i <=5; i++) {
        int value = (state >> (i+7)) & 0x1;
        if (value) // button is down
            gtk->button_mask |= 1 << (i -1);
        else // button is up
            gtk->button_mask &= ~(1 << (i -1));
    }
}



// GTK event handlers
static gboolean
gtk_key_press (GtkWidget *widget, GdkEventKey *event, gpointer user)
{
    vx_gtk_display_source_t * gtk = (vx_gtk_display_source_t*)user;
    vx_key_event_t key;
    key.modifiers = gtk_to_vx_modifiers(event->state);
    key.key_code = gtk_to_vx_keycode(event->keyval);
    key.released = 0;

    return vx_gtk_display_dispatch_key(gtk->disp, &key);
}

static gboolean
gtk_key_release (GtkWidget *widget, GdkEventKey *event, gpointer user)
{
    vx_gtk_display_source_t * gtk = (vx_gtk_display_source_t*)user;
    vx_key_event_t key;
    key.modifiers = gtk_to_vx_modifiers(event->state);
    key.key_code = gtk_to_vx_keycode(event->keyval);
    key.released = 1;

    return vx_gtk_display_dispatch_key(gtk->disp, &key);
}

static gboolean
gtk_motion (GtkWidget *widget, GdkEventMotion *event, gpointer user)
{

    vx_gtk_display_source_t * gtk = (vx_gtk_display_source_t*)user;
    update_button_by_state(gtk, event->state);

    vx_mouse_event_t vxe; // Allocate on stack
    vxe.x = event->x;
    // Invert to match the vx convention
    vxe.y = gtku_image_pane_get_height(gtk->imagePane) - event->y;
    /* vxe.y = event->y; */
    vxe.button_mask = gtk->button_mask;
    vxe.scroll_amt = 0;
    vxe.modifiers = gtk_to_vx_modifiers(event->state);

    return vx_gtk_display_dispatch_mouse(gtk->disp, &vxe);
}

static gboolean
gtk_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user)
{
    vx_gtk_display_source_t * gtk = (vx_gtk_display_source_t*)user;
    update_button_by_id(gtk, event->button, 1);

    vx_mouse_event_t vxe; // Allocate on stack
    vxe.x = event->x;
    vxe.y = gtku_image_pane_get_height(gtk->imagePane) - event->y;
    /* vxe.y = event->y; */
    vxe.button_mask = gtk->button_mask;
    vxe.scroll_amt = 0;
    vxe.modifiers = gtk_to_vx_modifiers(event->state);

    if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3 && (event->state & GDK_CONTROL_MASK)) {
        vx_gtk_display_show_context_menu(gtk->disp, &vxe);
        return 1;
    }

    return vx_gtk_display_dispatch_mouse(gtk->disp, &vxe);
}

static gboolean
gtk_button_release (GtkWidget *widget, GdkEventButton *event, gpointer user)
{
    vx_gtk_display_source_t * gtk = (vx_gtk_display_source_t*)user;
    update_button_by_id(gtk, event->button, 0);

    vx_mouse_event_t vxe; // Allocate on stack
    vxe.x = event->x;
    vxe.y = gtku_image_pane_get_height(gtk->imagePane) - event->y;
    /* vxe.y = event->y; */
    vxe.button_mask = gtk->button_mask;
    vxe.scroll_amt = 0;
    vxe.modifiers = gtk_to_vx_modifiers(event->state);

    return vx_gtk_display_dispatch_mouse(gtk->disp, &vxe);
}

static gboolean
gtk_scroll (GtkWidget *widget, GdkEventScroll *event, gpointer user)
{
    vx_gtk_display_source_t * gtk = (vx_gtk_display_source_t*)user;

    vx_mouse_event_t vxe; // Allocate on stack
    vxe.x = event->x;
    vxe.y = gtku_image_pane_get_height(gtk->imagePane) - event->y;
    /* vxe.y = event->y; */
    vxe.button_mask = gtk->button_mask;
    switch(event->direction) {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
            vxe.scroll_amt = -1;
            break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT: // mapping right scrolling -- kind of hacky. could ignore?
            vxe.scroll_amt = 1;
            break;
    }
    vxe.modifiers = gtk_to_vx_modifiers(event->state);

    return vx_gtk_display_dispatch_mouse(gtk->disp, &vxe);
}

vx_gtk_display_source_t * vx_gtk_display_source_create(vx_application_t * app)
{
    return vx_gtk_display_source_create_toggle_mgr(app, 1);
}

vx_gtk_display_source_t * vx_gtk_display_source_create_toggle_mgr(vx_application_t * app, int use_resc_mgr)
{
    vx_gtk_display_source_t * gtk = calloc(1, sizeof(vx_gtk_display_source_t));
    gtk->imagePane = GTKU_IMAGE_PANE(gtku_image_pane_new());
    g_object_ref_sink(gtk->imagePane); // convert floating refernce to actual reference
    gtk->disp = vx_gtk_display_create(gtk->imagePane, use_resc_mgr);
    gtk->app = app;

    // It may eventually become useful to ensure that the user VX code runs
    // without the GDK lock set. This is because the gdk mutex is not
    // recursive, and any user's gtk code further down the pipeline
    // might not know what state the gdk lock is in when the user code
    // is called. See vx_gtk_manager for related issues.

    //gdk_threads_leave();

    gtk->app->display_started(gtk->app, gtk->disp);

    //gdk_threads_enter();

    // Connect signals:
    g_signal_connect (G_OBJECT (gtk->imagePane), "key_release_event",   G_CALLBACK (gtk_key_release),  gtk);
    g_signal_connect (G_OBJECT (gtk->imagePane), "key_press_event",     G_CALLBACK (gtk_key_press),    gtk);
    g_signal_connect (G_OBJECT (gtk->imagePane), "motion-notify-event", G_CALLBACK (gtk_motion),       gtk);

    g_signal_connect (G_OBJECT (gtk->imagePane), "button-press-event",  G_CALLBACK (gtk_button_press),   gtk);
    g_signal_connect (G_OBJECT (gtk->imagePane), "button-release-event",G_CALLBACK (gtk_button_release), gtk);
    g_signal_connect (G_OBJECT (gtk->imagePane), "scroll-event",        G_CALLBACK (gtk_scroll),         gtk);


    return gtk;
}

GtkWidget * vx_gtk_display_source_get_widget(vx_gtk_display_source_t * gtk)
{
    return GTK_WIDGET(gtk->imagePane);
}

void vx_gtk_display_source_destroy(vx_gtk_display_source_t* gtk)
{
    gtk->app->display_finished(gtk->app, gtk->disp);
    vx_gtk_display_destroy(gtk->disp);

    gtk_widget_destroy (GTK_WIDGET(gtk->imagePane));
    g_object_unref (gtk->imagePane);
    //vx_gtk_display_destroy(gtk->disp); // XXX need to notify app before this
    free(gtk);
}

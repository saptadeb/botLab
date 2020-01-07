#ifndef __PARAM_WIDGET_H__
#define __PARAM_WIDGET_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

// directly from libbot2, with namespace renamed

#ifdef __cplusplus
extern "C" {
#endif

G_BEGIN_DECLS

#define VX_GTK_TYPE_PARAM_WIDGET  vx_gtk_param_widget_get_type ()
#define VX_GTK_PARAM_WIDGET(obj)  (                                     \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), VX_GTK_TYPE_PARAM_WIDGET, VXGtkParamWidget) \
        )
#define VX_GTK_PARAM_WIDGET_CLASS(klass) (                              \
        G_TYPE_CHECK_CLASS_CAST ((klass), VX_GTK_TYPE_PARAM_WIDGET, VXGtkParamWidgetClass) \
        )

typedef struct _VXGtkParamWidget VXGtkParamWidget;
typedef struct _VXGtkParamWidgetClass VXGtkParamWidgetClass;

typedef enum {
    VX_GTK_PARAM_WIDGET_DEFAULTS = 0,

    VX_GTK_PARAM_WIDGET_ENTRY,

    // ui hints for integers
    VX_GTK_PARAM_WIDGET_SLIDER = 1,
    VX_GTK_PARAM_WIDGET_SPINBOX,

    // ui hints for enums
    VX_GTK_PARAM_WIDGET_MENU,

    // ui hints for booleans
    VX_GTK_PARAM_WIDGET_CHECKBOX,
    VX_GTK_PARAM_WIDGET_TOGGLE_BUTTON,
} VXGtkParamWidgetUIHint;

GType
vx_gtk_param_widget_get_type (void);

GtkWidget *
vx_gtk_param_widget_new (void);

int
vx_gtk_param_widget_add_enum (VXGtkParamWidget *pw,
                              const char *name, VXGtkParamWidgetUIHint ui_hints, int initial_value,
                              const char *string1, int value1, ...) __attribute__ ((sentinel));

int
vx_gtk_param_widget_add_enumv (VXGtkParamWidget *pw,
                               const char *name, VXGtkParamWidgetUIHint ui_hints, int initial_value,
                               int noptions, const char **names, const int *values);

int
vx_gtk_param_widget_add_int (VXGtkParamWidget *pw,
                             const char *name, VXGtkParamWidgetUIHint ui_hints,
                             int min, int max, int increment, int initial_value);

int
vx_gtk_param_widget_add_text_entry (VXGtkParamWidget *pw,
                                    const char *name, VXGtkParamWidgetUIHint ui_hints,
                                    const char *initial_value);

int
vx_gtk_param_widget_add_double (VXGtkParamWidget *pw,
                                const char *name, VXGtkParamWidgetUIHint ui_hints,
                                double min, double max, double increment, double initial_value);

int
vx_gtk_param_widget_add_booleans (VXGtkParamWidget *pw,
                                  VXGtkParamWidgetUIHint ui_hints,
                                  const char *name1, int initially_checked1,
                                  ...) __attribute__ ((sentinel));

int
vx_gtk_param_widget_add_buttons (VXGtkParamWidget *pw,
                                 const char *name1, ...)  __attribute__ ((sentinel));

void
vx_gtk_param_widget_add_separator (VXGtkParamWidget *pw,
                                   const char *text);

int vx_gtk_param_widget_get_int (VXGtkParamWidget *pw, const char *name);

double
vx_gtk_param_widget_get_double (VXGtkParamWidget *pw, const char *name);

const gchar *
vx_gtk_param_widget_get_text_entry (VXGtkParamWidget *pw, const char *name);

int
vx_gtk_param_widget_get_bool (VXGtkParamWidget *pw, const char *name);

int
vx_gtk_param_widget_get_enum (VXGtkParamWidget *pw, const char *name);

const char *
vx_gtk_param_widget_get_enum_str (VXGtkParamWidget *pw, const char *name);

void
vx_gtk_param_widget_set_int (VXGtkParamWidget *pw, const char *name, int val);

void
vx_gtk_param_widget_set_double (VXGtkParamWidget *pw, const char *name, double val);

void
vx_gtk_param_widget_set_bool (VXGtkParamWidget *pw, const char *name, int val);

void
vx_gtk_param_widget_set_enum (VXGtkParamWidget *pw, const char *name, int val);

void
vx_gtk_param_widget_set_enabled (VXGtkParamWidget *pw, const char *name, int enabled);

void
vx_gtk_param_widget_load_from_key_file (VXGtkParamWidget *pw,
                                        GKeyFile *keyfile, const char *group_name);

void
vx_gtk_param_widget_save_to_key_file (VXGtkParamWidget *pw,
                                      GKeyFile *keyfile, const char *group_name);

int
vx_gtk_param_widget_modify_int (VXGtkParamWidget *pw,
                                const char *name, int min, int max, int increment, int value);

int
vx_gtk_param_widget_modify_double (VXGtkParamWidget *pw,
                                   const char *name, double min, double max, double increment, double value);

int
vx_gtk_param_widget_modify_enum (VXGtkParamWidget *pw,
                                 const char *name, const char *label, const int value);

void
vx_gtk_param_widget_clear_enum (VXGtkParamWidget *pw, const char *name);

G_END_DECLS

#ifdef __cplusplus
}
#endif

#endif  //__PARAM_WIDGET_H__

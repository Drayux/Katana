#include "gui/app_window.h"
#include "settings/definitions.h"
#include <gtk/gtk.h>

extern void timer_start_split(LSAppWindow * win);
extern void timer_cancel_run(LSAppWindow * win);
extern void timer_skip(LSAppWindow * win);
extern void timer_unsplit(LSAppWindow * win);
extern void toggle_decorations(LSAppWindow * win);
extern void toggle_win_on_top(LSAppWindow * win);

gboolean ls_app_window_keypress(GtkEventControllerKey * controller,
	guint keyval, guint keycode, GdkModifierType state, gpointer data);

void keybind_start_split(GtkWidget * widget, LSAppWindow * win);

void keybind_stop_reset(char const * str, LSAppWindow * win);

void keybind_cancel(char const * str, LSAppWindow * win);

void keybind_skip(char const * str, LSAppWindow * win);

void keybind_unsplit(char const * str, LSAppWindow * win);

void keybind_toggle_decorations(char const * str, LSAppWindow * win);

void keybind_toggle_win_on_top(char const * str, LSAppWindow * win);

void bind_global_hotkeys(AppConfig cfg, LSAppWindow * win);

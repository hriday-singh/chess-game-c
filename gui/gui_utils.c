#include "gui_utils.h"

#include "gui_utils.h"

GtkWindow* gui_utils_get_root_window(GtkWidget *context_widget) {
    if (!context_widget) return NULL;

    GtkRoot *root = gtk_widget_get_root(context_widget);
    if (root && GTK_IS_WINDOW(root)) {
        GtkWindow *window = GTK_WINDOW(root);
        GtkWindow *parent = NULL;
        while ((parent = gtk_window_get_transient_for(window)) != NULL) {
            window = parent;
        }
        return window;
    }
    return NULL;
}

void gui_utils_focus_root(GtkWidget *context_widget) {
    if (!context_widget) return;

    GtkWindow *root = gui_utils_get_root_window(context_widget);
    if (root) {
        gtk_window_present(root);
    }
}

void gui_utils_focus_parent(GtkWidget *context_widget) {
    if (!context_widget) return;

    GtkRoot *root = gtk_widget_get_root(context_widget);
    if (!root || !GTK_IS_WINDOW(root)) return;

    GtkWindow *window = GTK_WINDOW(root);
    GtkWindow *parent = gtk_window_get_transient_for(window);
    
    if (parent) {
        gtk_window_present(parent);
    }
}
static void on_window_destroy_focus_parent(GtkWidget* widget, gpointer data) {
    (void)data;
    gui_utils_focus_parent(widget);
}

static gboolean on_window_close_request_focus_parent(GtkWindow* window, gpointer data) {
    (void)data;
    gui_utils_focus_parent(GTK_WIDGET(window));
    return FALSE; // Propagate (allow close)
}

void gui_utils_setup_auto_focus_restore(GtkWindow *window) {
    if (!window || !GTK_IS_WINDOW(window)) return;
    
    // Handle X button / Alt+F4
    g_signal_connect(window, "close-request", G_CALLBACK(on_window_close_request_focus_parent), NULL);
    
    // Handle programmatic destroy (if close-request wasn't triggered)
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy_focus_parent), NULL);
}

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

// Esc key controller
static gboolean on_esc_close_controller_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)controller; (void)keycode; (void)state;
    GtkWidget* window = (GtkWidget*)user_data;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_close(GTK_WINDOW(window));
        return TRUE;
    }
    return FALSE;
}

void gui_utils_add_esc_close(GtkWidget* window) {
    if (!window || !GTK_IS_WINDOW(window)) return;
    
    GtkEventController* controller = gtk_event_controller_key_new();
    g_signal_connect(controller, "key-pressed", G_CALLBACK(on_esc_close_controller_key_pressed), window);
    gtk_widget_add_controller(window, controller);
}

GtkWidget* gui_utils_create_loading_overlay(GtkOverlay* parent_overlay, GtkWidget** out_spinner, const char* title, const char* subtitle) {
    if (!parent_overlay) return NULL;

    /* ---------------- Loading Overlay ---------------- */
    GtkWidget* loading_overlay = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(loading_overlay, GTK_ALIGN_FILL);
    gtk_widget_set_valign(loading_overlay, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(loading_overlay, "overlay-dim");

    /* Center container */
    GtkWidget* center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(center_box, TRUE);
    gtk_widget_set_vexpand(center_box, TRUE);
    gtk_box_append(GTK_BOX(loading_overlay), center_box);

    /* Card / panel */
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_add_css_class(card, "loading-card");
    gtk_widget_set_margin_start(card, 48);
    gtk_widget_set_margin_end(card, 48);
    gtk_widget_set_margin_top(card, 36);
    gtk_widget_set_margin_bottom(card, 36);
    gtk_widget_set_size_request(card, 280, -1); // Minimum width
    gtk_box_append(GTK_BOX(center_box), card);

    /* Spinner */
    GtkWidget* spinner = gtk_spinner_new();
    gtk_widget_set_size_request(spinner, 80, 80);
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), spinner);
    
    if (out_spinner) *out_spinner = spinner;

    /* Title */
    if (title) {
        GtkWidget* title_lbl = gtk_label_new(title);
        gtk_widget_add_css_class(title_lbl, "loading-title");
        gtk_widget_set_halign(title_lbl, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(card), title_lbl);
    }

    /* Subtitle */
    if (subtitle) {
        GtkWidget* subtitle_lbl = gtk_label_new(subtitle);
        gtk_widget_add_css_class(subtitle_lbl, "loading-subtitle");
        gtk_widget_set_halign(subtitle_lbl, GTK_ALIGN_CENTER);
        gtk_label_set_wrap(GTK_LABEL(subtitle_lbl), TRUE);
        gtk_label_set_justify(GTK_LABEL(subtitle_lbl), GTK_JUSTIFY_CENTER);
        gtk_box_append(GTK_BOX(card), subtitle_lbl);
    }

    /* Add overlay */
    gtk_overlay_add_overlay(parent_overlay, loading_overlay);
    gtk_widget_set_visible(loading_overlay, FALSE);
    
    return loading_overlay;
}

void gui_utils_set_window_size_relative(GtkWindow* window, GtkWindow* relative_to, double w_factor, double h_factor) {
    if (!window) return;
    
    int w = 1200;
    int h = 900;
    
    if (relative_to && GTK_IS_WINDOW(relative_to)) {
        int rw = gtk_widget_get_width(GTK_WIDGET(relative_to));
        int rh = gtk_widget_get_height(GTK_WIDGET(relative_to));
        if (rw > 200 && rh > 200) {
            w = rw;
            h = rh;
        }
    }
    
    int target_w = (int)(w * w_factor);
    int target_h = (int)(h * h_factor);
    
    if (target_w < 400) target_w = 400;
    if (target_h < 300) target_h = 300;
    
    gtk_window_set_default_size(window, target_w, target_h);
}

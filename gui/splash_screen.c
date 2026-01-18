#include "splash_screen.h"
#include "theme_manager.h"
#include <math.h>

typedef struct {
    GtkWidget* icon;
    GtkWidget* status_label;
    GtkWidget* overlay;
    double scale;
    double time;
    GCallback on_finished;
    gpointer user_data;
    guint tick_id;
} SplashData;

static gboolean splash_tick(GtkWidget* widget, GdkFrameClock* frame_clock, gpointer user_data) {
    (void)widget;
    SplashData* data = (SplashData*)user_data;
    gint64 frame_time = gdk_frame_clock_get_frame_time(frame_clock);
    
    // Breathing effect: scale oscillates between 0.90 and 1.10
    // sin(time * speed) * amplitude + base
    double t = frame_time / 1000000.0; // seconds
    data->scale = sin(t * 3.0) * 0.10 + 1.0;
    
    // Apply scale to icon
    // In Gtk4, we can use a GtkFixed and transform, or just set scale request if it respect it (unlikely)
    // Better: use snapshot to scale. But for simplicity, let's use a wrapper that we scale.
    // Or just animate opacity? User said breathing effect.
    // Let's use CSS scale if possible or just adjust size request.
    
    // Actually, let's use a simpler approach: 
    // GtkCenterBox or Box and we use gtk_widget_set_size_request based on scale
    // Base size for icon: 256x256
    int base_size = 256;
    int size = (int)(base_size * data->scale);
    gtk_image_set_pixel_size(GTK_IMAGE(data->icon), size);
    
    return G_SOURCE_CONTINUE;
}

static void on_splash_destroy(gpointer data) {
    SplashData* sd = (SplashData*)data;
    if (sd->tick_id) {
        // Find the widget it was attached to? 
        // It's usually better to remove it in the "unrealize" or just rely on widget death.
    }
    g_free(sd);
}

GtkWidget* splash_screen_show(GtkWindow* parent_window) {
    SplashData* data = g_new0(SplashData, 1);
    
    // Create the overlay container that will cover everything
    data->overlay = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_hexpand(data->overlay, TRUE);
    gtk_widget_set_vexpand(data->overlay, TRUE);
    gtk_widget_set_halign(data->overlay, GTK_ALIGN_FILL);
    gtk_widget_set_valign(data->overlay, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(data->overlay, "splash-screen");
    
    // Add CSS for background and layout
    GtkCssProvider* provider = gtk_css_provider_new();
    
    const AppTheme* theme = theme_manager_get_current_theme();
    bool is_dark = theme_manager_is_dark();
    const AppThemeColors* colors = is_dark ? &theme->dark : &theme->light;
    
    char css[512];
    snprintf(css, sizeof(css), 
        ".splash-screen { background-color: %s; }"
        ".splash-icon { transition: all 0.1s ease-out; }"
        ".splash-status { color: %s; font-size: 24px; font-weight: 500; font-family: 'Inter', sans-serif; }",
        colors->base_bg, colors->base_fg);
    
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(data->overlay), 
                                               GTK_STYLE_PROVIDER(provider), 
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Centered content box
    GtkWidget* center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 32);
    gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(center_box, TRUE);
    gtk_widget_set_vexpand(center_box, TRUE);
    gtk_box_append(GTK_BOX(data->overlay), center_box);

    // Icon Container (Fixed size to prevent bobbing)
    GtkWidget* icon_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(icon_container, 300, 300);
    gtk_widget_set_halign(icon_container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon_container, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(center_box), icon_container);

    // Icon
    data->icon = gtk_image_new_from_file("assets/images/icon/icon.png");
    gtk_widget_add_css_class(data->icon, "splash-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(data->icon), 256);
    gtk_widget_set_halign(data->icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(data->icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_container), data->icon);

    // Status Container (Fixed height to prevent bobbing)
    GtkWidget* status_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(status_container, -1, 60);
    gtk_box_append(GTK_BOX(center_box), status_container);

    // Status Label
    data->status_label = gtk_label_new("Initializing...");
    gtk_widget_add_css_class(data->status_label, "splash-status");
    gtk_widget_set_valign(data->status_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(status_container), data->status_label);

    // Attach data to widget for cleanup and updates
    g_object_set_data_full(G_OBJECT(data->overlay), "splash-data", data, on_splash_destroy);

    // Add breathing animation
    data->tick_id = gtk_widget_add_tick_callback(data->overlay, splash_tick, data, NULL);

    // Add to window overlay
    // We need to find the GtkOverlay in the window child hierarchy
    GtkWidget* window_child = gtk_window_get_child(parent_window);
    if (GTK_IS_OVERLAY(window_child)) {
        gtk_overlay_add_overlay(GTK_OVERLAY(window_child), data->overlay);
    } else {
        // Fallback: if no overlay, just set it as child? NO, main.c has overlay.
        // If this is called BEFORE main.c sets up overlay, it might fail.
        // We will ensure main.c sets up overlay first.
        g_warning("Splash screen: Parent window has no GtkOverlay child!");
    }

    return data->overlay;
}

void splash_screen_update_status(GtkWidget* splash, const char* status) {
    SplashData* data = (SplashData*)g_object_get_data(G_OBJECT(splash), "splash-data");
    if (data && data->status_label) {
        gtk_label_set_text(GTK_LABEL(data->status_label), status);
    }
}

static gboolean fade_out_step(gpointer user_data) {
    GtkWidget* splash = (GtkWidget*)user_data;
    SplashData* data = (SplashData*)g_object_get_data(G_OBJECT(splash), "splash-data");
    if (!data) return G_SOURCE_REMOVE;

    double opacity = gtk_widget_get_opacity(splash);
    opacity -= 0.05;
    
    if (opacity <= 0.0) {
        gtk_widget_set_opacity(splash, 0.0);
        
        // Remove from parent overlay
        GtkWidget* parent = gtk_widget_get_parent(splash);
        if (GTK_IS_OVERLAY(parent)) {
            gtk_overlay_remove_overlay(GTK_OVERLAY(parent), splash);
        }
        
        // Trigger callback
        if (data->on_finished) {
            void (*cb)(gpointer) = (void (*)(gpointer))data->on_finished;
            cb(data->user_data);
        }
        
        return G_SOURCE_REMOVE;
    }
    
    gtk_widget_set_opacity(splash, opacity);
    return G_SOURCE_CONTINUE;
}

static gboolean start_fade_out(gpointer user_data) {
    GtkWidget* splash = (GtkWidget*)user_data;
    g_timeout_add(16, fade_out_step, splash);
    return G_SOURCE_REMOVE;
}

void splash_screen_finish(GtkWidget* splash, GCallback on_finished, gpointer user_data) {
    (void)on_finished; (void)user_data;
    SplashData* data = (SplashData*)g_object_get_data(G_OBJECT(splash), "splash-data");
    if (data) {
        data->on_finished = on_finished;
        data->user_data = user_data;
        
        // 800ms delay then start fade out
        g_timeout_add(800, start_fade_out, splash);
    }
}

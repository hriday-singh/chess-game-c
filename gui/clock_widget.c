#include "clock_widget.h"
#include "clock.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Internal structure
struct _ClockWidget {
    GtkWidget* main_container; // Parent box
    GtkWidget* name_pill;      // Separated name pill
    GtkWidget* clock_pill;     // Separated clock pill
    
    GtkWidget* icon_image;     // User/Bot icon
    GtkWidget* name_label;     // Player/Engine name
    
    GtkWidget* analog_area;    // Drawing area for analog clock
    GtkWidget* time_label;     // Digital time label
    
    Player side;               // Owner (White/Black)
    bool active;               // Is currently active
    bool disabled;             // Force zero/hidden state
    int64_t last_time_ms;      // Last known time (for change detection)
    int64_t initial_time_ms;    // Starting time for the match/period
    int64_t last_sync_system_us; // System time (monotonic) when last_time_ms was updated
    
    double current_scale;      // Current scale factor to prevent redundant resize queues
    guint tick_id;             // Animation tick callback ID
};

void clock_widget_set_scale(ClockWidget* clock, double scale) {
    if (!clock) return;
    
    // Bounds check scale
    if (scale < 0.5) scale = 0.5;
    if (scale > 2.0) scale = 2.0;
    
    // Check if changed (epsilon 0.001)
    if (fabs(clock->current_scale - scale) < 0.001) return;
    clock->current_scale = scale;
    
    // 1. Scale Icon
    int icon_size = (int)(18 * scale);
    gtk_image_set_pixel_size(GTK_IMAGE(clock->icon_image), icon_size);
    
    // 2. Scale Text (Pango Attributes) - DISABLED based on user feedback (too small)
    // Reverting to CSS-based sizing for now.
    /*
    PangoAttrList* attr_list = pango_attr_list_new();
    // Use Points instead of Absolute if enabled later: pango_attr_size_new(...)
    PangoAttribute* size_attr = pango_attr_size_new((int)(14 * scale * PANGO_SCALE));
    pango_attr_list_insert(attr_list, size_attr);
    
    gtk_label_set_attributes(GTK_LABEL(clock->name_label), attr_list);
    gtk_label_set_attributes(GTK_LABEL(clock->time_label), attr_list);
    pango_attr_list_unref(attr_list);
    */
    // Clear attributes to ensure CSS takes over
    gtk_label_set_attributes(GTK_LABEL(clock->name_label), NULL);
    gtk_label_set_attributes(GTK_LABEL(clock->time_label), NULL);
    
    // 3. Scale Analog Clock Widget Size
    int clock_size = (int)(18 * scale);
    gtk_widget_set_size_request(clock->analog_area, clock_size, clock_size);
    
    // 4. Adjust internal spacing/margins
    gtk_box_set_spacing(GTK_BOX(clock->name_pill), (int)(8 * scale));
    gtk_box_set_spacing(GTK_BOX(clock->clock_pill), (int)(12 * scale));
    
    // Force redraw
    gtk_widget_queue_resize(clock->main_container);
}

// Draw the discrete analog clock
static void draw_analog(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    ClockWidget* clock = (ClockWidget*)user_data;
    
    // Draw always, even if disabled (shows static clock at 12:00)
    // if (clock->disabled) return;
    
    // Outline (Circle) - Always visible
    GdkRGBA color;
    gtk_widget_get_color(GTK_WIDGET(area), &color);
    
    // If inactive, use a grey/dimmed color for the hand and circle
    if (!clock->active) {
        color.red = 0.5; color.green = 0.5; color.blue = 0.5;
    }

    cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
    cairo_set_line_width(cr, 1.8);
    cairo_arc(cr, width/2.0, height/2.0, width/2.0 - 2, 0, 2*M_PI);
    cairo_stroke(cr);
    
    // New Redesigned Animation:
    // 1 Full Revolution = 4 Seconds (90 degrees per second)
    // Formula: angle = ((initial - current) / 1000.0) * (M_PI / 2.0)
    
    double angle = 0;
    if (clock->last_time_ms != -1 && clock->initial_time_ms > 0) {
        int64_t current_logic_ms = clock->last_time_ms;
        
        // Interpolate for 60fps smoothness if active
        if (clock->active) {
            int64_t now_us = g_get_monotonic_time();
            int64_t system_delta_ms = (now_us - clock->last_sync_system_us) / 1000;
            if (system_delta_ms > 100) system_delta_ms = 100;
            if (system_delta_ms < 0) system_delta_ms = 0;
            current_logic_ms -= system_delta_ms;
        }

        // Calculate elapsed seconds since start of this match/period
        double elapsed_sec = (double)(clock->initial_time_ms - current_logic_ms) / 1000.0;
        // 90 degrees (M_PI/2) per second
        angle = elapsed_sec * (M_PI / 2.0);
    }
    
    // Draw Hand
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha); 
    
    double cx = width / 2.0;
    double cy = height / 2.0;
    double len = (width / 2.0) - 2.5;
    
    cairo_set_line_width(cr, 2.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + len * sin(angle), cy - len * cos(angle));
    cairo_stroke(cr);
}

static gboolean on_analog_tick(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data) {
    (void)widget; (void)frame_clock;
    ClockWidget* clock = (ClockWidget*)user_data;
    
    if (clock->active && !clock->disabled) {
        gtk_widget_queue_draw(clock->analog_area);
    }
    return G_SOURCE_CONTINUE;
}

ClockWidget* clock_widget_new(Player side) {
    ClockWidget* clock = g_new0(ClockWidget, 1);
    clock->side = side;
    clock->last_time_ms = -1;
    
    // Main Container (Center Box for strict Left/Right alignment)
    clock->main_container = gtk_center_box_new();
    gtk_widget_set_halign(clock->main_container, GTK_ALIGN_FILL); 
    gtk_widget_set_valign(clock->main_container, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(clock->main_container, "clock-widget-container");

    // 1. Name Pill
    clock->name_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_valign(clock->name_pill, GTK_ALIGN_CENTER); // Center Vertically
    gtk_widget_add_css_class(clock->name_pill, "clock-pill");
    gtk_widget_add_css_class(clock->name_pill, "name-pill");
    
    clock->icon_image = gtk_image_new_from_icon_name("avatar-default-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(clock->icon_image), 18);
    gtk_box_append(GTK_BOX(clock->name_pill), clock->icon_image);
    
    clock->name_label = gtk_label_new("");
    gtk_widget_add_css_class(clock->name_label, "clock-player-name");
    gtk_label_set_xalign(GTK_LABEL(clock->name_label), 0.0);
    gtk_box_append(GTK_BOX(clock->name_pill), clock->name_label);
    
    // Set as START widget (Left)
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(clock->main_container), clock->name_pill);

    // 2. Clock Pill
    clock->clock_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_valign(clock->clock_pill, GTK_ALIGN_CENTER); // Center Vertically
    gtk_widget_add_css_class(clock->clock_pill, "clock-pill");
    if (side == PLAYER_WHITE) {
        gtk_widget_add_css_class(clock->clock_pill, "clock-white");
    } else {
        gtk_widget_add_css_class(clock->clock_pill, "clock-black");
    }
    
    // Analog Clock Icon
    clock->analog_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(clock->analog_area, 18, 18);
    gtk_widget_set_valign(clock->analog_area, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(clock->analog_area), draw_analog, clock, NULL);
    gtk_box_append(GTK_BOX(clock->clock_pill), clock->analog_area);

    // Digital Label
    clock->time_label = gtk_label_new("00:00");
    gtk_widget_add_css_class(clock->time_label, "clock-time");
    gtk_label_set_xalign(GTK_LABEL(clock->time_label), 1.0); // Right align text
    gtk_box_append(GTK_BOX(clock->clock_pill), clock->time_label);
    
    // Set as END widget (Right)
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(clock->main_container), clock->clock_pill);

    // Register tick callback for analog hand updates
    clock->tick_id = gtk_widget_add_tick_callback(clock->analog_area, on_analog_tick, clock, NULL);
    
    return clock;
}

GtkWidget* clock_widget_get_widget(ClockWidget* clock) {
    return clock ? clock->main_container : NULL;
}

Player clock_widget_get_side(ClockWidget* clock) {
    return clock ? clock->side : PLAYER_WHITE;
}

void clock_widget_update(ClockWidget* clock, int64_t time_ms, int64_t initial_time_ms, bool is_active) {
    if (!clock || clock->disabled) return;
    
    // Safety check: Ensure widgets are still valid (prevents shutdown errors)
    if (!clock->time_label || !GTK_IS_LABEL(clock->time_label)) return;
    if (!clock->main_container || !GTK_IS_WIDGET(clock->main_container)) return;
    
    // Store initial time for rotation base
    clock->initial_time_ms = initial_time_ms;

    // Active State Change
    if (clock->active != is_active) {
        clock->active = is_active;
        if (is_active) {
            gtk_widget_add_css_class(clock->main_container, "active");
        } else {
            gtk_widget_remove_css_class(clock->main_container, "active");
        }
        // Force redraw to show/hide the whole drawing area contents
        if (clock->analog_area && GTK_IS_WIDGET(clock->analog_area)) {
            gtk_widget_queue_draw(clock->analog_area);
        }
    }
    
    // Use the same ceiling logic as clock_get_string to detect if the display string significantly changes
    int64_t current_display_sec = (time_ms + 999) / 1000;
    int64_t last_display_sec = (clock->last_time_ms + 999) / 1000;
    
    if (clock->last_time_ms == -1 || current_display_sec != last_display_sec) {
        char buf[32];
        clock_get_string(time_ms, buf, sizeof(buf));
        gtk_label_set_text(GTK_LABEL(clock->time_label), buf);
    }
    clock->last_time_ms = time_ms;
    clock->last_sync_system_us = g_get_monotonic_time();
}

void clock_widget_set_visible_state(ClockWidget* clock, bool visible) {
    if (!clock) return;
    gtk_widget_set_visible(clock->main_container, visible);
}

void clock_widget_set_name(ClockWidget* clock, const char* name) {
    if (!clock) return;
    gtk_label_set_text(GTK_LABEL(clock->name_label), name ? name : "");
    
    // Set Icon
    if (name) {
        if (strstr(name, "Engine") || strstr(name, "Stockfish") || strstr(name, "Bot")) {
            gtk_image_set_from_icon_name(GTK_IMAGE(clock->icon_image), "computer-symbolic");
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(clock->icon_image), "avatar-default-symbolic");
        }
    }
}

void clock_widget_set_disabled(ClockWidget* clock, bool disabled) {
    if (!clock) return;
    clock->disabled = disabled;
    if (disabled) {
        gtk_label_set_text(GTK_LABEL(clock->time_label), "00:00");
        gtk_widget_remove_css_class(clock->main_container, "active");
        clock->active = false;
    } else {
        // Reset last_time_ms to force next update to refresh display
        clock->last_time_ms = -1;
    }
    gtk_widget_queue_draw(clock->analog_area);
}

void clock_widget_free(ClockWidget* clock) {
    if (clock) {
        if (clock->tick_id > 0) {
            gtk_widget_remove_tick_callback(clock->analog_area, clock->tick_id);
        }
        g_free(clock);
    }
}

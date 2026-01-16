#include "clock_widget.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Internal structure
struct _ClockWidget {
    GtkWidget* container;   // The main pill box
    GtkWidget* analog_area; // Drawing area for analog clock
    GtkWidget* time_label;  // Digital time label
    
    Player side;            // Owner (White/Black)
    bool active;            // Is currently active
    bool disabled;          // Force zero/hidden state
    int64_t last_time_ms;   // Last known time (for change detection)
    int64_t initial_time_ms; // Starting time for the match/period
    int64_t last_sync_system_us; // System time (monotonic) when last_time_ms was updated
    
    guint tick_id;          // Animation tick callback ID
};

// Draw the discrete analog clock
static void draw_analog(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    ClockWidget* clock = (ClockWidget*)user_data;
    
    // If disabled, do NOT draw
    if (clock->disabled) return;
    
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
    
    // Container (Pill)
    clock->container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(clock->container, GTK_ALIGN_END); // Right aligned within its row
    gtk_widget_set_valign(clock->container, GTK_ALIGN_CENTER);
    
    // Styling classes
    gtk_widget_add_css_class(clock->container, "clock-pill");
    if (side == PLAYER_WHITE) {
        gtk_widget_add_css_class(clock->container, "clock-white");
    } else {
        gtk_widget_add_css_class(clock->container, "clock-black");
    }
    
    // Analog Clock Icon
    clock->analog_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(clock->analog_area, 18, 18);
    gtk_widget_set_valign(clock->analog_area, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(clock->analog_area), draw_analog, clock, NULL);
    gtk_box_append(GTK_BOX(clock->container), clock->analog_area);

    // Register tick callback for analog hand updates
    clock->tick_id = gtk_widget_add_tick_callback(clock->analog_area, on_analog_tick, clock, NULL);
    

    // Digital Label
    clock->time_label = gtk_label_new("00:00");
    gtk_widget_add_css_class(clock->time_label, "clock-time");
    gtk_label_set_xalign(GTK_LABEL(clock->time_label), 1.0); // Right align text within the 80px block
    gtk_box_append(GTK_BOX(clock->container), clock->time_label);
    
    return clock;
}

GtkWidget* clock_widget_get_widget(ClockWidget* clock) {
    return clock ? clock->container : NULL;
}

Player clock_widget_get_side(ClockWidget* clock) {
    return clock ? clock->side : PLAYER_WHITE;
}

void clock_widget_update(ClockWidget* clock, int64_t time_ms, int64_t initial_time_ms, bool is_active) {
    if (!clock || clock->disabled) return;
    
    // Store initial time for rotation base
    clock->initial_time_ms = initial_time_ms;

    // Active State Change
    if (clock->active != is_active) {
        clock->active = is_active;
        if (is_active) {
            gtk_widget_add_css_class(clock->container, "active");
        } else {
            gtk_widget_remove_css_class(clock->container, "active");
        }
        // Force redraw to show/hide the whole drawing area contents
        gtk_widget_queue_draw(clock->analog_area);
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
    gtk_widget_set_visible(clock->container, visible);
}

void clock_widget_set_disabled(ClockWidget* clock, bool disabled) {
    if (!clock) return;
    clock->disabled = disabled;
    if (disabled) {
        gtk_label_set_text(GTK_LABEL(clock->time_label), "00:00");
        gtk_widget_remove_css_class(clock->container, "active");
        clock->active = false;
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

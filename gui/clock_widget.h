#ifndef CLOCK_WIDGET_H
#define CLOCK_WIDGET_H

#include <gtk/gtk.h>
#include "../game/types.h" // For Player enum

typedef struct _ClockWidget ClockWidget;

// Create a new clock widget (the pill)
ClockWidget* clock_widget_new(Player side);

// Get the top-level GtkWidget (box) for packing
GtkWidget* clock_widget_get_widget(ClockWidget* clock);

// Get the side (Player) associated with this clock
Player clock_widget_get_side(ClockWidget* clock);

// Update time and state
// time_ms: current time in milliseconds
// initial_time_ms: starting time for calculating progress/period
// is_active: whether this clock is currently active (ticking)
void clock_widget_update(ClockWidget* clock, int64_t time_ms, int64_t initial_time_ms, bool is_active);

// Set overall visibility (for Tutorial/Puzzle modes)
void clock_widget_set_visible_state(ClockWidget* clock, bool visible);

// Set player/engine name on the left
void clock_widget_set_name(ClockWidget* clock, const char* name);

// Set "Disabled/Zero" state (for Tutorial/Puzzle modes)
void clock_widget_set_disabled(ClockWidget* clock, bool disabled);

// Destroy and free
void clock_widget_free(ClockWidget* clock);

#endif

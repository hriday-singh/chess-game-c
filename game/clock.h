#ifndef CLOCK_H
#define CLOCK_H

#include "types.h"
#include <stdint.h>
#include <stdbool.h>

// Clock state structure
typedef struct {
    int64_t white_time_ms;
    int64_t black_time_ms;
    int64_t initial_time_ms;
    int64_t increment_ms;
    
    // Tracks when the last tick occurred to calculate delta
    // Uses system monotonic time (or approximation)
    int64_t last_tick_time;
    
    bool active;       // Is clock running?
    bool enabled;      // Is clock feature enabled for this game?
    bool count_up_mode; // If true, clock counts UP (Stopwatch mode)
    
    // Who ran out of time?
    Player flagged_player; // PLAYER_NONE if no flag fall
} ClockState;

// Initialize clock
void clock_init(ClockState* clock, int minutes, int increment_sec);

// Reset clock
void clock_reset(ClockState* clock, int minutes, int increment_sec);

// Tick clock (should be called frequently, e.g. every frame or via timer)
// Returns true if a flag just fell
bool clock_tick(ClockState* clock, Player current_turn);

// Handle move press (add increment)
void clock_press(ClockState* clock, Player just_moved);

// Set custom time control
void clock_set(ClockState* clock, int64_t time_ms, int64_t inc_ms);

// Get formatted string (MM:SS)
void clock_get_string(int64_t time_ms, char* buf, size_t size);

// Get monotonic time in ms
int64_t clock_get_current_time_ms(void);

#endif // CLOCK_H

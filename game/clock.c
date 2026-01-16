#include "clock.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Helper to get current time in MS
static int64_t get_current_time_ms(void) {
#ifdef _WIN32
    static int64_t frequency = 0;
    if (frequency == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        frequency = freq.QuadPart;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (now.QuadPart * 1000) / frequency;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

void clock_init(ClockState* clock, int minutes, int increment_sec) {
    if (!clock) return;
    clock_reset(clock, minutes, increment_sec);
}

void clock_reset(ClockState* clock, int minutes, int increment_sec) {
    if (!clock) return;
    
    if (minutes <= 0) {
        clock->enabled = false;
        clock->white_time_ms = 0;
        clock->black_time_ms = 0;
        clock->initial_time_ms = 0;
        clock->increment_ms = 0;
        clock->active = false;
        clock->flagged_player = 0; // PLAYER_NONE (assuming 0 is neutral/none generally, but strictly should be explicit)
        // Check types.h for PLAYER_NONE? usually it's just 0 or handled carefully.
        // Assuming typical enum: PLAYER_WHITE=0, PLAYER_BLACK=1. We need a sentinel.
        // Let's assume the caller handles "flagged_player" check based on boolean result.
        // Actually, let's use a safe value. If Player is enum 0/1, we need -1 or similar.
        // But types.h usually has PLAYER_WHITE/BLACK. Let's just reset to -1.
        // Ideally we'd modify types.h to include PLAYER_NONE, but let's stick to existing code.
        // For now, flagged_player indicates *who lost*. 
        // We'll reset it to a non-player value.
        clock->flagged_player = -1; 
        return;
    }

    clock->enabled = true;
    clock->active = false; // Starts paused until first move (or explicit start)
    // Actually, usually white's clock starts running immediately? 
    // Or after first move? Standard rules: White's clock starts when game starts.
    // We'll let the controller start it.
    
    clock->initial_time_ms = (int64_t)minutes * 60 * 1000;
    clock->white_time_ms = clock->initial_time_ms;
    clock->black_time_ms = clock->initial_time_ms;
    clock->increment_ms = (int64_t)increment_sec * 1000;
    clock->last_tick_time = 0;
    clock->flagged_player = -1;
}

void clock_set(ClockState* clock, int64_t time_ms, int64_t inc_ms) {
    if (!clock) return;
    clock->enabled = true;
    clock->white_time_ms = time_ms;
    clock->black_time_ms = time_ms;
    clock->initial_time_ms = time_ms;
    clock->increment_ms = inc_ms;
    clock->active = false;
    clock->flagged_player = -1;
}

bool clock_tick(ClockState* clock, Player current_turn) {
    if (!clock || !clock->enabled || !clock->active) return false;
    
    int64_t now = get_current_time_ms();
    if (clock->last_tick_time == 0) {
        clock->last_tick_time = now;
        return false;
    }
    
    int64_t delta = now - clock->last_tick_time;
    clock->last_tick_time = now;
    
    if (current_turn == PLAYER_WHITE) {
        clock->white_time_ms -= delta;
        if (clock->white_time_ms <= 0) {
            clock->white_time_ms = 0;
            clock->flagged_player = PLAYER_WHITE;
            clock->active = false;
            return true;
        }
    } else {
        clock->black_time_ms -= delta;
        if (clock->black_time_ms <= 0) {
            clock->black_time_ms = 0;
            clock->flagged_player = PLAYER_BLACK;
            clock->active = false;
            return true;
        }
    }
    
    return false;
}

void clock_press(ClockState* clock, Player just_moved) {
    if (!clock || !clock->enabled) return;
    
    // Add increment to the player who just moved
    if (just_moved == PLAYER_WHITE) {
        clock->white_time_ms += clock->increment_ms;
    } else {
        clock->black_time_ms += clock->increment_ms;
    }
    
    // Ensure we don't restart tick calculation from a stale time
    // But actually, update loop handles tick.
    // Just ensure the delta doesn't jump.
    clock->last_tick_time = get_current_time_ms();
    
    // If not active (first move), activate it
    if (!clock->active) {
        clock->active = true;
    }
}

void clock_get_string(int64_t time_ms, char* buf, size_t size) {
    if (!buf || size == 0) return;
    
    if (time_ms < 0) time_ms = 0;
    
    // Chess clocks standard: ceiling for seconds, floor for minutes
    // Actually, simple: total_seconds = (ms + 999) / 1000
    int64_t total_sec = (time_ms + 999) / 1000;
    int32_t minutes = (int32_t)(total_sec / 60);
    int32_t seconds = (int32_t)(total_sec % 60);
    
    // Force standard MM:SS format. For > 99m it will still show e.g. 120:30
    snprintf(buf, size, "%02d:%02d", minutes, seconds);
}

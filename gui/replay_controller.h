#ifndef REPLAY_CONTROLLER_H
#define REPLAY_CONTROLLER_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include "../game/types.h"

// Forward declarations
typedef struct AppState AppState;
struct GameLogic;
typedef struct GameLogic GameLogic;

typedef struct ReplayController {
    GameLogic* logic;
    AppState* app_state;
    
    // Move Data
    char* full_san_history; // Copy of the full SAN string for the match
    Move** moves;           // Array of deep-copied moves for playback
    int total_moves;        // Total number of moves in the match
    int current_ply;        // Current ply index (0 to total_moves)
    
    // Playback State
    bool is_playing;
    int speed_ms;           // Delay in milliseconds
    guint timer_id;

    // Snapshots for efficient replay navigation
    PositionSnapshot* snapshots; // Contiguous array [0..total_moves]
    int snapshot_count;
    int snapshot_capacity;
    
} ReplayController;

// Lifecycle
ReplayController* replay_controller_new(GameLogic* logic, AppState* app_state);
void replay_controller_free(ReplayController* self);

// Core Functionality
// Load a match into the replay controller (populates internal state)
// start_fen can be NULL or empty for standard start
void replay_controller_load_match(ReplayController* self, const char* san_moves, const char* start_fen);
void replay_controller_start(ReplayController* self); // Enter replay mode, reset to start
void replay_controller_exit(ReplayController* self);  // Exit replay mode

// Playback Controls
void replay_controller_play(ReplayController* self);
void replay_controller_pause(ReplayController* self);
void replay_controller_toggle_play(ReplayController* self);
bool replay_controller_is_playing(ReplayController* self);

// Speed Control (Range: e.g., 200ms to 2000ms)
void replay_controller_set_speed(ReplayController* self, int ms);

// Navigation
void replay_controller_next(ReplayController* self);
void replay_controller_prev(ReplayController* self);
void replay_controller_seek(ReplayController* self, int ply);

// "Start From Here" Logic
// Returns true if successfully transitioned to live game
bool replay_controller_start_from_here(ReplayController* self, GameMode mode, Player side);

#endif // REPLAY_CONTROLLER_H

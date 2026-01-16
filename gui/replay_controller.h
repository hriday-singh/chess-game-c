#ifndef REPLAY_CONTROLLER_H
#define REPLAY_CONTROLLER_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include "../game/types.h"

// Forward declarations
typedef struct AppState AppState;
struct GameLogic;
typedef struct GameLogic GameLogic;
struct _AiAnalysisJob;
struct GameAnalysisResult;

typedef struct ReplayController {
    GameLogic* logic;
    AppState* app_state;
    
    // Move Data
    char* full_uci_history; // Copy of the full UCI string for the match
    Move** moves;           // Array of deep-copied moves for playback
    int total_moves;        // Total number of moves in the match
    int current_ply;        // Current ply index (0 to total_moves)
    
    // Playback State
    bool is_playing;
    guint timer_id;
    guint tick_timer_id; // For smooth clock animation

    // Match Metadata
    char result[16];        // e.g. "1-0"
    char result_reason[256]; // e.g. "Checkmate"

    // Emulation State
    int64_t move_start_time_monotonic; // When the current move animation started (for countdown)

    // Real-time Emulation
    int* think_times;       // Array of think times in ms
    int speed_ms;           // Delay in milliseconds
    double time_multiplier; // Playback speed multiplier (e.g. 1.0, 2.0, 0.5)
    int think_time_count;   
    bool use_think_times;   // If true, use think_times for delay instead of speed_ms

    // Clock Emulation
    bool clock_enabled;
    int clock_initial_ms;
    int clock_increment_ms;
    int64_t current_white_time;
    int64_t current_black_time;
    int64_t* precalc_white_time; // Array [total_moves+1]
    int64_t* precalc_black_time; // Array [total_moves+1]

    // Snapshots for efficient replay navigation
    PositionSnapshot* snapshots; // Contiguous array [0..total_moves]
    int snapshot_count;
    int snapshot_capacity;
    
    // AI Analysis
    struct _AiAnalysisJob* analysis_job;
    struct GameAnalysisResult* analysis_result;
    
} ReplayController;

// Lifecycle
ReplayController* replay_controller_new(GameLogic* logic, AppState* app_state);
void replay_controller_free(ReplayController* self);

// Core Actions
void replay_controller_enter_replay_mode(ReplayController* self);

// Load a match into the replay controller (populates internal state)
// start_fen can be NULL or empty for standard start
// think_times can be NULL. timestamps are for validation.
void replay_controller_load_match(ReplayController* self, const char* moves_uci, const char* start_fen, 
                                 const int* think_times, int think_time_count, 
                                 int64_t started_at, int64_t ended_at,
                                 bool clock_enabled, int initial_ms, int increment_ms);
void replay_controller_set_result(ReplayController* self, const char* result, const char* reason);
void replay_controller_exit(ReplayController* self);  // Exit replay mode

// Analysis
void replay_controller_analyze_match(ReplayController* self);
void replay_controller_cancel_analysis(ReplayController* self);
bool replay_controller_is_analyzing(ReplayController* self);
const struct GameAnalysisResult* replay_controller_get_analysis_result(ReplayController* self);

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

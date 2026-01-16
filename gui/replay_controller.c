#include "replay_controller.h"
#include "../game/gamelogic.h"
#include "app_state.h"
#include "../game/move.h"
#include "board_widget.h"
#include "clock_widget.h"
#include "right_side_panel.h"
#include "info_panel.h"
#include "ai_analysis.h"
#include "config_manager.h"
#include <string.h>

static bool debug_mode = true;

// Forward decl for internal timer
static gboolean replay_timer_callback(gpointer user_data);
static gboolean replay_tick_callback(gpointer user_data);

// Helper helper
static void refresh_clock_display(ReplayController* self);

static const char* get_name_for_config(MatchPlayerConfig cfg) {
    if (cfg.is_ai) {
        if (cfg.engine_type == 0) return "Inbuilt Stockfish Engine";
        return "Custom Engine";
    }
    return "Player";
}

ReplayController* replay_controller_new(GameLogic* logic, AppState* app_state) {
    ReplayController* self = g_new0(ReplayController, 1);
    self->logic = logic;
    self->app_state = app_state;
    self->speed_ms = 1000; // Default speed (1.0x base)
    self->time_multiplier = 1.0;
    return self;
}

static void sync_board_highlights(ReplayController* self) {
    if (!self || !self->app_state || !self->app_state->gui.board) return;

    // Highlight the move leading to the current state
    if (self->current_ply > 0 && self->moves && self->current_ply <= self->total_moves) {
        Move* m = self->moves[self->current_ply - 1]; // Move index is ply - 1
        int fr = m->from_sq / 8, fc = m->from_sq % 8;
        int tr = m->to_sq / 8, tc = m->to_sq % 8;
        board_widget_set_last_move(self->app_state->gui.board, fr, fc, tr, tc);
    } else {
        // Start of game (ply 0): Clear highlights
        board_widget_set_last_move(self->app_state->gui.board, -1, -1, -1, -1);
    }

    // Sync Right Side Panel Highlight
    if (self->app_state->gui.right_side_panel) {
        int hl = (self->current_ply > 0) ? (self->current_ply - 1) : -1;
        right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, hl);
    }
}

static void replay_ui_update(ReplayController* self) {
    if (!self || !self->app_state || !self->app_state->gui.info_panel) return;
    
    // 1. Sync Logic History for correct graveyard calc
    gamelogic_rebuild_history(self->logic, self->moves, self->current_ply);
    
    // 2. Refresh Status Checkmate/Stalemate
    gamelogic_update_game_state(self->logic);
    
    // 3. Update UI
    info_panel_update_replay_status(self->app_state->gui.info_panel, self->current_ply, self->total_moves);
    info_panel_refresh_graveyard(self->app_state->gui.info_panel);
    info_panel_refresh_graveyard(self->app_state->gui.info_panel);
    info_panel_update_status(self->app_state->gui.info_panel);
    
    // Check if at End of Match -> Show Result Status
    if (self->current_ply == self->total_moves) {
         // Force status message in Logic so info panel picks it up?
         // InfoPanel reads logic->statusMessage.
         // "Game Over - White Won (Checkmate)"
         // Parse result: "1-0" -> White Won
         const char* winner = "Draw";
         if (strcmp(self->result, "1-0") == 0) winner = "White Won";
         else if (strcmp(self->result, "0-1") == 0) winner = "Black Won";
         
         const char* reason = (self->result_reason[0]) ? self->result_reason : "Game Over";
         
         // Direct print with precision limits to avoid truncation warnings and overflow
         // Limit reason to 200 chars to ensure winner string fits in 256 bytes (statusMessage size)
         snprintf(self->logic->statusMessage, sizeof(self->logic->statusMessage), 
                  "%.200s - %s", reason, winner);
         
         self->logic->isGameOver = true; // Make sure panel shows it as finished
         info_panel_update_status(self->app_state->gui.info_panel);
    } 
    
    refresh_clock_display(self);
    sync_board_highlights(self);
}

// Separate function for clock updates (called by tick timer)
static void refresh_clock_display(ReplayController* self) {
    if (!self || !self->app_state) return;
    
    ClockWidget* clocks[2] = { self->app_state->gui.top_clock, self->app_state->gui.bottom_clock };

    if (clocks[0] && clocks[1]) {
        // Use precalculated times if available (always present when clock_enabled)
        // Otherwise disable the clocks
        if (!self->clock_enabled || !self->precalc_white_time || !self->precalc_black_time) {
            // Clock disabled or not initialized
            clock_widget_set_disabled(clocks[0], true);
            clock_widget_set_disabled(clocks[1], true);
        } else {
            // Use precalculated times for O(1) lookup
            int64_t w_time = self->precalc_white_time[self->current_ply];
            int64_t b_time = self->precalc_black_time[self->current_ply];
            
            clock_widget_set_disabled(clocks[0], false);
            clock_widget_set_disabled(clocks[1], false);
            
            // Active state
            Player turn_now = (Player)-1;
            if (self->current_ply < self->total_moves && self->snapshots) {
                turn_now = self->snapshots[self->current_ply].turn;
            }
            
            bool playing = self->is_playing && (turn_now != (Player)-1);
            
            for (int k=0; k<2; k++) {
                Player side = clock_widget_get_side(clocks[k]);
                int64_t t = (side == PLAYER_WHITE) ? w_time : b_time;
                bool is_active = playing && (side == turn_now);
                clock_widget_update(clocks[k], t, self->clock_initial_ms, is_active);
            }
        }
    }
}

static gboolean replay_tick_callback(gpointer user_data) {
    ReplayController* self = (ReplayController*)user_data;
    if (!self || !self->is_playing) return G_SOURCE_REMOVE;
    
    // Calculate progress
    int64_t now = g_get_monotonic_time(); // microseconds
    int64_t elapsed_us = now - (self->move_start_time_monotonic);
    int64_t elapsed_ms = elapsed_us / 1000;
    
    if (elapsed_ms < 0) elapsed_ms = 0;
    
    // Don't overshoot total delay for this move
    int total_delay = 0;
    if (self->use_think_times && self->think_times && self->current_ply < self->think_time_count) {
         total_delay = self->think_times[self->current_ply];
         if (total_delay < 100) total_delay = 100; // visual floor
    } else {
         total_delay = self->speed_ms;
    }
    
    // Scale elapsed time to virtual time
    elapsed_ms = (int64_t)(elapsed_ms * self->time_multiplier);
    
    if (elapsed_ms > total_delay) elapsed_ms = total_delay;

    // Determine whose turn it is
    Player turn_now = (Player)-1;
    if (self->current_ply < self->total_moves && self->snapshots) {
        turn_now = self->snapshots[self->current_ply].turn;
    }
    
    if (turn_now == (Player)-1) return G_SOURCE_CONTINUE;
    
    // Update Active Clock
    // Base time is precalc[current_ply].
    // Current time = Base - elapsed.
    
    ClockWidget* clocks[2] = { self->app_state->gui.top_clock, self->app_state->gui.bottom_clock };
    
    if (self->precalc_white_time && self->precalc_black_time) {
         int64_t w_time = self->precalc_white_time[self->current_ply];
         int64_t b_time = self->precalc_black_time[self->current_ply];
         
         if (turn_now == PLAYER_WHITE) {
             w_time -= elapsed_ms;
             if (w_time < 0) w_time = 0;
         } else {
             b_time -= elapsed_ms;
             if (b_time < 0) b_time = 0;
         }
         
         // Update widgets
         for (int k=0; k<2; k++) {
            Player side = clock_widget_get_side(clocks[k]);
            int64_t t = (side == PLAYER_WHITE) ? w_time : b_time;
            bool is_active = (side == turn_now); // Playing implies active
            clock_widget_update(clocks[k], t, self->clock_initial_ms, is_active);
         }
    }
    
    return G_SOURCE_CONTINUE;
}

void replay_controller_free(ReplayController* self) {
    if (!self) return;
    
    // Stop playback
    replay_controller_pause(self);
    
    // Free moves
    if (self->moves) {
        for (int i = 0; i < self->total_moves; i++) {
            move_free(self->moves[i]);
        }
        g_free(self->moves);
    }
    // Free snapshots
    if (self->snapshots) {
        g_free(self->snapshots);
    }
    
    g_free(self->full_uci_history);
    
    if (self->think_times) {
        g_free(self->think_times);
        self->think_times = NULL;
    }
    
    if (self->analysis_job) {
        ai_analysis_cancel(self->analysis_job);
        // Note: Thread might still be running. Ideally we join or wait.
        // For now, assume cancel acts quickly or we leak small struct until app exit.
        // ai_analysis_free(self->analysis_job); // Unsafe if thread running
    }
    
    if (self->analysis_result) {
        ai_analysis_result_unref(self->analysis_result);
    }

    if (self->tick_timer_id > 0) {
        g_source_remove(self->tick_timer_id);
    }
    if (self->precalc_white_time) g_free(self->precalc_white_time);
    if (self->precalc_black_time) g_free(self->precalc_black_time);
    
    g_free(self);
}

void replay_controller_load_match(ReplayController* self, const char* moves_uci, const char* start_fen, 
                                  const int* think_times, int think_time_count, 
                                  int64_t started_at, int64_t ended_at,
                                  bool clock_enabled, int initial_ms, int increment_ms,
                                  MatchPlayerConfig white, MatchPlayerConfig black) {
    if (!self) return;
    
    self->white_config = white;
    self->black_config = black;
    
    (void)started_at; (void)ended_at; // Validation logic optional/removed for now

    if(debug_mode) {
        printf("[ReplayController] Loading match:\n");
        printf("  Moves UCI: %s\n", moves_uci ? moves_uci : "NULL");
        printf("  Think Times Count: %d\n", think_time_count);
        printf("  Start FEN: %s\n", start_fen ? start_fen : "Initial");
        printf("  Timestamps: Started=%lld, Ended=%lld\n", (long long)started_at, (long long)ended_at);
        printf("  Clock: %s (Initial: %d ms, Increment: %d ms)\n", 
               clock_enabled ? "ENABLED" : "DISABLED", initial_ms, increment_ms);
        printf("  White: %s (ELO=%d, Depth=%d, MoveTime=%d, Engine=%d, Path=%s)\n",
               white.is_ai ? "AI" : "Human", white.elo, white.depth, white.movetime, 
               white.engine_type, white.engine_path[0] != '\0' ? white.engine_path : "N/A");
        printf("  Black: %s (ELO=%d, Depth=%d, MoveTime=%d, Engine=%d, Path=%s)\n",
               black.is_ai ? "AI" : "Human", black.elo, black.depth, black.movetime, 
               black.engine_type, black.engine_path[0] != '\0' ? black.engine_path : "N/A");
    }

    replay_controller_pause(self);
    
    // clock setup
    self->clock_enabled = clock_enabled;
    self->clock_initial_ms = initial_ms;
    self->clock_increment_ms = increment_ms;
    
    // Clear existing snapshots
    if (self->snapshots) {
        g_free(self->snapshots);
        self->snapshots = NULL;
        self->snapshot_count = 0;
        self->snapshot_capacity = 0;
    }

    // We no longer store full SAN history as a string from source
    g_free(self->full_uci_history);
    self->full_uci_history = NULL;
    
    // Clear think times
    if (self->think_times) {
        g_free(self->think_times);
        self->think_times = NULL;
    }
    self->think_time_count = 0;
    self->use_think_times = false; // Default to false until validated
    
    self->total_moves = 0;
    self->current_ply = 0;

    self->total_moves = 0;
    self->current_ply = 0;
    
    if (self->precalc_white_time) g_free(self->precalc_white_time);
    if (self->precalc_black_time) g_free(self->precalc_black_time);
    self->precalc_white_time = NULL;
    self->precalc_black_time = NULL;
    
    memset(self->result, 0, sizeof(self->result));
    memset(self->result_reason, 0, sizeof(self->result_reason));

    // 1. Load Logic (UCI Only)
    if (moves_uci && strlen(moves_uci) > 0) {
        gamelogic_load_from_uci_moves(self->logic, moves_uci, start_fen);
    } else {
        // No moves to load
        if (debug_mode) printf("[ReplayController] No UCI moves provided, match loaded empty.\n");
    }
    
    // 2. Extract Moves & Snapshots
    self->total_moves = gamelogic_get_move_count(self->logic);
    if (self->total_moves > 0) {
        self->moves = g_new0(Move*, self->total_moves);
        for (int i = 0; i < self->total_moves; i++) {
            Move m = gamelogic_get_move_at(self->logic, i);
            self->moves[i] = move_copy(&m);
        }
    }

    // 3. Validation & Think Time Setup
    if (think_times && think_time_count == self->total_moves) {
        // Basic Integrity Pass
        bool valid = true;
        int64_t total_think = 0;
        
        self->think_times = g_new0(int, think_time_count);
        self->think_time_count = think_time_count;
        
        for (int i=0; i<think_time_count; i++) {
            if (think_times[i] < 0) { 
                valid = false; 
                break; 
            }
            // Clamp for sanity (e.g. 100ms min, 60s max for replay flow?)
            // User asked for "exact", but 0ms moves are weird. 
            // We just store raw for now, logic below handles min delay.
            self->think_times[i] = think_times[i];
            total_think += think_times[i];
        }
        (void)total_think; // Silence unused warning if debug_mode is off
        
        // If clock is enabled, assume timestamps/duration checks are secondary to explicit think times
        if (valid) {
             self->use_think_times = true;
             if (debug_mode) printf("[Replay] Real-time emulation enabled. Count: %d, Total Think: %lld ms\n", think_time_count, total_think);
        } else {
             if (debug_mode) printf("[Replay] Think times invalid, fallback to speed_ms.\n");
        }
    } else if (think_times && think_time_count == self->total_moves + 1 && think_times[think_time_count-1] == 0) {
        // Auto-fix for "Trailing Zero" corruption (caused by previous parser bug)
        if (debug_mode) printf("[Replay] Detected trailing zero corruption. Trimming last element.\n");

        self->think_times = g_new0(int, self->total_moves);
        self->think_time_count = self->total_moves;
        
        bool valid = true;
        int64_t total_think = 0;
        
        for (int i=0; i<self->total_moves; i++) {
            if (think_times[i] < 0) { valid = false; break; }
            self->think_times[i] = think_times[i];
            total_think += think_times[i];
        }
        
        if (valid) {
             self->use_think_times = true;
             if (debug_mode) printf("[Replay] Real-time emulation enabled (Sanitized). Count: %d, Total Think: %lld ms\n", self->think_time_count, total_think);
        }
    } else {
        if (debug_mode) {
             printf("[Replay] Think times not provided or count mismatch. (Provided: %p, Count: %d, Total Moves: %d)\n", 
                    (void*)think_times, think_time_count, self->total_moves);
             if (think_time_count != self->total_moves && think_time_count > 0) {
                 printf("[Replay] WARNING: Think time count (%d) != Move count (%d). This usually means gamelogic didn't pop think time on undo.\n", 
                        think_time_count, self->total_moves);
             }
        }
    }

    // Allocate snapshots (ply 0 to ply N)
    self->snapshot_count = self->total_moves + 1;
    self->snapshot_capacity = self->snapshot_count;
    self->snapshots = g_new0(PositionSnapshot, self->snapshot_count);

    // Re-run to capture snapshots
    if (start_fen && start_fen[0] != '\0') gamelogic_load_fen(self->logic, start_fen);
    else gamelogic_reset(self->logic);

    gamelogic_create_snapshot(self->logic, &self->snapshots[0]);
    for (int i = 0; i < self->total_moves; i++) {
        gamelogic_perform_move(self->logic, self->moves[i]);
        gamelogic_create_snapshot(self->logic, &self->snapshots[i+1]);
    }
    
    // Reset board to start position for Replay
    gamelogic_restore_snapshot(self->logic, &self->snapshots[0]);
    self->current_ply = 0;

    if (debug_mode) {
        printf("[ReplayController] Match Loaded:\n");
        printf("  Total Moves: %d\n", self->total_moves);
        printf("  Snapshots: %d\n", self->snapshot_count);
        printf("  Move 0 (Start) FEN: %s\n", self->logic->start_fen);
        // Debug stack pointer indirectly via move count or internal inspection
        int current_count = gamelogic_get_move_count(self->logic);
        printf("  Move 0 Stack Count: %d (Should be 0)\n", current_count);
    }
    
    // Auto-Set Perspective
    bool flip = false;
    if (!self->white_config.is_ai && self->black_config.is_ai) flip = false;     // Human is White
    else if (self->white_config.is_ai && !self->black_config.is_ai) flip = true; // Human is Black
    
    if (self->app_state && self->app_state->gui.board) {
        board_widget_set_flipped(self->app_state->gui.board, flip);
    }
    if (self->app_state && self->app_state->gui.right_side_panel) {
        right_side_panel_set_flipped(self->app_state->gui.right_side_panel, flip);
    }
    
    // Set Clock Names and Icons
    if (self->app_state && self->app_state->gui.top_clock && self->app_state->gui.bottom_clock) {
        ClockWidget* white_clk = flip ? self->app_state->gui.top_clock : self->app_state->gui.bottom_clock;
        ClockWidget* black_clk = flip ? self->app_state->gui.bottom_clock : self->app_state->gui.top_clock;
        
        // Use the existing helper (I'll need to define it or find it)
        // Actually I'll use the logic from get_name_for_config
        char w_name[64], b_name[64];
        if (self->white_config.is_ai) {
             snprintf(w_name, sizeof(w_name), "%s", self->white_config.engine_type == 1 ? "Custom Engine" : "Inbuilt STOCKFISH ENGINE");
        } else {
             snprintf(w_name, sizeof(w_name), "Player");
        }
        
        if (self->black_config.is_ai) {
             snprintf(b_name, sizeof(b_name), "%s", self->black_config.engine_type == 1 ? "Custom Engine" : "Inbuilt STOCKFISH ENGINE");
        } else {
             snprintf(b_name, sizeof(b_name), "Player");
        }
        
        clock_widget_set_name(white_clk, w_name);
        clock_widget_set_name(black_clk, b_name);
        
        clock_widget_set_disabled(white_clk, !self->clock_enabled);
        clock_widget_set_disabled(black_clk, !self->clock_enabled);

        // Explicitly set initial times
        clock_widget_update(white_clk, self->clock_initial_ms, self->clock_initial_ms, false);
        clock_widget_update(black_clk, self->clock_initial_ms, self->clock_initial_ms, false);
    }
    
    // 3. Update UI: Regenerate SAN from Logic history
    if (self->app_state) {
        board_widget_refresh(self->app_state->gui.board);
        if (self->app_state->gui.right_side_panel) {
             right_side_panel_clear_history(self->app_state->gui.right_side_panel);
             
             // Use a temp logic to replay and generate accurate SAN context (checks/mates/disambiguation)
             GameLogic* temp_logic = gamelogic_create();
             if (temp_logic) {
                 if (start_fen && start_fen[0] != '\0') gamelogic_load_fen(temp_logic, start_fen);
                 else gamelogic_reset(temp_logic);
                 
                 temp_logic->isSimulation = true; // fast mode
                 
                 Player p = temp_logic->turn;
                 int m_num = 1;

                 // Accumulate UCI string for export/debug if needed
                 GString* full_uci = g_string_new("");

                 for (int i = 0; i < self->total_moves; i++) {
                     Move* m = self->moves[i];
                     
                     // Get piece type from current board state before move
                     int r = m->from_sq / 8;
                     int c = m->from_sq % 8;
                     PieceType p_type = NO_PIECE;
                     if (temp_logic->board[r][c] != NULL) {
                         p_type = temp_logic->board[r][c]->type;
                     }

                     char uci[16];
                     move_to_uci(m, uci); // Keep UCI for the full_uci_history string
                     
                     // Advance temp logic FIRST, then get SAN (which works on the updated state)
                     gamelogic_perform_move(temp_logic, m);

                     char san[16];
                     gamelogic_get_move_san(temp_logic, m, san, sizeof(san));

                     right_side_panel_add_move_notation(self->app_state->gui.right_side_panel, san, p_type, m_num, p);
                     
                     if (full_uci->len > 0) g_string_append_c(full_uci, ' ');
                     g_string_append(full_uci, uci);
                     
                     if (p == PLAYER_BLACK) m_num++;
                     p = (p == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
                 }
                 gamelogic_free(temp_logic);
                 
                 // Store reconstructed UCI history
                 self->full_uci_history = g_string_free(full_uci, FALSE);

                 // Ensure we scroll to top after bulk load as requested by user
                 right_side_panel_scroll_to_top(self->app_state->gui.right_side_panel);
             }
        }
    }
    
    // Pre-calculate Clock Times for every ply
    // This allows O(1) clock display during jumps and precise countdowns.
    if (self->clock_enabled) {
        self->precalc_white_time = g_new0(int64_t, self->total_moves + 1);
        self->precalc_black_time = g_new0(int64_t, self->total_moves + 1);
        
        int64_t w = self->clock_initial_ms;
        int64_t b = self->clock_initial_ms;
        
        // Ply 0
        self->precalc_white_time[0] = w;
        self->precalc_black_time[0] = b;
        
        for (int i=0; i < self->total_moves; i++) {
             // Calculate time AFTER ply i (which leads to state i+1)
             // But wait, "precalc[i]" means Clock Time AT START of ply i.
             // So precalc[0] is initial.
             // Ply 0 (White about to move). 
             // Move 0 happens. It takes T time.
             // End of Move 0 (Start of Ply 1): W_new = W_old - T + Inc. B_new = B_old.
             // So precalc[i+1] is derived from precalc[i] and think_times[i].
             
             int duration = 0;
             if (self->think_times && i < self->think_time_count) duration = self->think_times[i];
             // Clamp negative duration if corrupt?
             if (duration < 0) duration = 0;
             
             Player mover = self->snapshots[i].turn; // Current turn at ply i
             
             if (mover == PLAYER_WHITE) {
                 w -= duration;
                 if (w < 0) w = 0;
                 w += self->clock_increment_ms;
             } else {
                 b -= duration;
                 if (b < 0) b = 0;
                 b += self->clock_increment_ms;
             }
             
             self->precalc_white_time[i+1] = w;
             self->precalc_black_time[i+1] = b;
        }
    }
    
    // Copy result if available (passed from entry usually? No, entry passed fields.)
    // Wait, load_match doesn't take result string.
    // Need to update signature or header?
    // User requested "Show Result Status".
    // I can't update signature easily without breaking callers (main.c).
    // Let's assume the caller will set the result fields manually after load?
    // OR we infer from logic? Logic doesn't know "Timeout".
    // I will read result from app_state->gui.history_dialog selection? No.
    // MatchHistoryEntry pointer is safer.
    // I will modify main.c to set these fields on the controller directly after load!

    // Initialize highlighting to first move (ply 0)
    if (self->app_state && self->app_state->gui.right_side_panel) {
        // Ensure replay lock is ON so we can scroll to top
        right_side_panel_set_replay_lock(self->app_state->gui.right_side_panel, true);
        right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, -1);
    }
    
    // Update UI immediately to show correct move count
    if(debug_mode) printf("[ReplayController] Updating UI\n");
    replay_ui_update(self);
}

// Set result metadata (Called by app logic after load)
void replay_controller_set_result(ReplayController* self, const char* result, const char* reason) {
    if (!self) return;
    if (result) snprintf(self->result, sizeof(self->result), "%s", result);
    if (reason) snprintf(self->result_reason, sizeof(self->result_reason), "%s", reason);
}

void replay_controller_start(ReplayController* self) {
    if (!self) return;
    self->current_ply = 0;
    if (self->snapshots) {
        gamelogic_restore_snapshot(self->logic, &self->snapshots[0]);
    } else {
        gamelogic_reset(self->logic);
    }
    if (self->app_state) {
         board_widget_reset_selection(self->app_state->gui.board);
         board_widget_refresh(self->app_state->gui.board);
         // Highlights synced in replay_ui_update
    }
    replay_ui_update(self);
}

void replay_controller_exit(ReplayController* self) {
    if (!self) return;
    replay_controller_pause(self);
    // Cleanup is handled by caller usually setting is_replaying = false
    if (self->app_state && self->app_state->gui.right_side_panel) {
        right_side_panel_set_replay_lock(self->app_state->gui.right_side_panel, false);
    }
}

void replay_controller_play(ReplayController* self) {
    if (!self || self->is_playing) return;

    if (self->app_state && self->app_state->gui.right_side_panel) {
        right_side_panel_set_replay_lock(self->app_state->gui.right_side_panel, true);
    }

    // If at end, restart from beginning
    if (self->current_ply >= self->total_moves) {
        replay_controller_start(self);
    }
    
    self->is_playing = true;
    
    // Determine initial delay
    int nominal_delay = self->speed_ms;
    if (self->use_think_times && self->think_times && self->current_ply < self->think_time_count) {
        nominal_delay = self->think_times[self->current_ply];
        if (nominal_delay < 100) nominal_delay = 100; // Minimum visual floor
    }
    
    // Apply speed multiplier
    int effective_delay = (int)(nominal_delay / self->time_multiplier);
    if (effective_delay < 10) effective_delay = 10; // Safety floor

    self->timer_id = g_timeout_add(effective_delay, replay_timer_callback, self);
    self->move_start_time_monotonic = g_get_monotonic_time();
    
    if (self->tick_timer_id == 0) {
        self->tick_timer_id = g_timeout_add(50, replay_tick_callback, self); // 20fps for clock
    }
    
    // Update UI Play/Pause button state (will be handled by signal/caller or direct UI update if we had ref)
    // Ideally, we fire a callback or update a linked UI. 
    // for now, pure logic.
    if (self->app_state && self->app_state->gui.info_panel) {
        info_panel_show_replay_controls(self->app_state->gui.info_panel, TRUE);
    }
}

void replay_controller_pause(ReplayController* self) {
    if (!self || !self->is_playing) return;

    if (self->app_state && self->app_state->gui.right_side_panel) {
        right_side_panel_set_replay_lock(self->app_state->gui.right_side_panel, false);
    }
 
    self->is_playing = false;
    if (self->timer_id > 0) {
        g_source_remove(self->timer_id);
        self->timer_id = 0;
    }
    if (self->tick_timer_id > 0) {
        g_source_remove(self->tick_timer_id);
        self->tick_timer_id = 0;
    }
    // Update clock one last time to snap to exact values
    refresh_clock_display(self);
    if (self->app_state && self->app_state->gui.info_panel) {
        info_panel_show_replay_controls(self->app_state->gui.info_panel, TRUE);
    }
}

void replay_controller_toggle_play(ReplayController* self) {
    if (self->is_playing) replay_controller_pause(self);
    else replay_controller_play(self);
}

bool replay_controller_is_playing(ReplayController* self) {
    return self ? self->is_playing : false;
}

void replay_controller_set_speed(ReplayController* self, int ms) {
    if (!self) return;
    if (ms < 10) ms = 10; // Safety
    
    // Derived from UI logic: Base(1.0x) = 1000ms.
    // ms parameter is "target delay for 1.0x move if manual".
    // multiplier = 1000 / ms.
    if (ms <= 0) ms = 1000;
    double new_multiplier = 1000.0 / (double)ms;
    
    // If checking against current, we can skip? No, always apply.
    
    // To assume smooth clock transition:
    // CurrentVirtual = (Now - Start) * OldMult
    // NewStart = Now - (CurrentVirtual / NewMult)
    if (self->is_playing) {
        int64_t now = g_get_monotonic_time();
        int64_t elapsed_us = now - self->move_start_time_monotonic;
        double current_virtual_us = elapsed_us * self->time_multiplier;
        
        int64_t virtual_start_offset = (int64_t)(current_virtual_us / new_multiplier);
        self->move_start_time_monotonic = now - virtual_start_offset;
    }

    self->time_multiplier = new_multiplier;
    self->speed_ms = 1000; // Fixed base for manual operations
    // NOTE: We do NOT disable use_think_times anymore, allowing scaling of think times!
    // self->use_think_times = false; 
    
    // If playing, update timer
    if (self->is_playing) {
        if (self->timer_id > 0) g_source_remove(self->timer_id);
        
        // Calculate remaining delay for current move
        // Nominal (Unscaled) Total Delay
        int nominal_total = self->speed_ms;
        if (self->use_think_times && self->think_times && self->current_ply < self->think_time_count) {
            nominal_total = self->think_times[self->current_ply];
            if (nominal_total < 100) nominal_total = 100;
        }

        // How much Virtual Time already happened?
        int64_t now = g_get_monotonic_time();
        int64_t current_virtual_ms = (int64_t)((now - self->move_start_time_monotonic) / 1000.0 * self->time_multiplier);
        
        int virtual_remaining = nominal_total - (int)current_virtual_ms;
        if (virtual_remaining < 0) virtual_remaining = 0;
        
        // Convert remaining virtual to real time
        int real_remaining = (int)(virtual_remaining / self->time_multiplier);
        if (real_remaining < 10) real_remaining = 10; // fire soon
        
        self->timer_id = g_timeout_add(real_remaining, replay_timer_callback, self);
    }
}

void replay_controller_next(ReplayController* self, bool from_timer) {
    if (!self) return;
    
    // Skip Delay: Only pause if this is manual navigation (not from timer)
    if (self->is_playing && !from_timer) {
        replay_controller_pause(self);
    }

    if (self->current_ply >= self->total_moves) return;
    
    Move* next_move = self->moves[self->current_ply];
    
    // Animate move on board BEFORE logic update (so piece is still at source)
    if (self->app_state && self->app_state->gui.board) {
        board_widget_animate_move(self->app_state->gui.board, next_move);
    }
    
    // Note: gamelogic_perform_move is NOW called to ensure logic state (turn, checkmate status) updates
    // The animation system will call execute_move_with_updates when animation completes, 
    // BUT we need immediate logic state update for the UI status to be correct.
    // Double execution is prevented because Replay Controller manages the logic, not the Board Widget callbacks in this mode.
    gamelogic_perform_move(self->logic, next_move);
    
    self->current_ply++;
    
    // Auto-pause if we've reached the end
    if (self->current_ply >= self->total_moves) {
        replay_controller_pause(self);
    }
    
    // Clear board selection/highlights
    // Highlights synced in replay_ui_update
    replay_ui_update(self);
}

void replay_controller_prev(ReplayController* self, bool from_timer) {
    if (!self) return;

    // Skip Delay: Only pause if this is manual navigation (not from timer)
    if (self->is_playing && !from_timer) {
        replay_controller_pause(self);
    }

    if (self->current_ply <= 0) return;
    
    self->current_ply--;
    if (self->snapshots) {
        gamelogic_restore_snapshot(self->logic, &self->snapshots[self->current_ply]);
    } else {
        gamelogic_undo_move(self->logic);
    }

    if (self->app_state) {
        board_widget_refresh(self->app_state->gui.board);
        // Highlights synced in replay_ui_update
    }
    replay_ui_update(self);
}

void replay_controller_seek(ReplayController* self, int ply) {
    if (!self) return;
    if (ply < 0) ply = 0;
    if (ply >= self->snapshot_count) ply = self->snapshot_count - 1;
    
    replay_controller_pause(self);
    
    if (self->app_state) {
        board_widget_reset_selection(self->app_state->gui.board);
        // Highlights synced in replay_ui_update
    }

    if (self->snapshots) {
        gamelogic_restore_snapshot(self->logic, &self->snapshots[ply]);
        self->current_ply = ply;
    } else {
        // Fallback for missing snapshots (reset and walk)
        if (ply < self->current_ply) {
            while (self->current_ply > ply) {
                gamelogic_undo_move(self->logic);
                self->current_ply--;
            }
        } else {
            while (self->current_ply < ply) {
                Move* next_move = self->moves[self->current_ply];
                gamelogic_perform_move(self->logic, next_move);
                self->current_ply++;
            }
        }
    }
    
    if (self->app_state) {
        board_widget_refresh(self->app_state->gui.board);
        // Highlights synced in replay_ui_update
    }
    replay_ui_update(self);
}

// "Start From Here" Logic
bool replay_controller_start_from_here(ReplayController* self, GameMode mode, Player side) {
    if (!self || !self->logic || !self->app_state) return false;
    
    // 1. Stop Replay
    replay_controller_pause(self);
    
    // 2. Truncate/Rebuild History
    // The gamelogic currently holds the state at `current_ply`.
    // We need to ensure the undo stack matches exactly moves[0..current_ply-1].
    // `gamelogic_rebuild_history` does exactly this: clears stack and pushes the provided array.
    if (self->moves && self->current_ply > 0) {
        gamelogic_rebuild_history(self->logic, self->moves, self->current_ply);
    } else {
        // If at start (ply 0), clear history
        gamelogic_rebuild_history(self->logic, NULL, 0);
    }
    
    // 3. Sync Clock
    if (self->clock_enabled && self->precalc_white_time && self->precalc_black_time) {
        // Restore time from precalculated values for the CURRENT ply (start of this turn)
        int64_t w_time = self->precalc_white_time[self->current_ply];
        int64_t b_time = self->precalc_black_time[self->current_ply];
        
        // Use custom clock setter to ensure precise restoration
        gamelogic_set_custom_clock(self->logic, 0, 0); // Reset internal
        self->logic->clock.white_time_ms = w_time;
        self->logic->clock.black_time_ms = b_time;
        self->logic->clock.initial_time_ms = self->clock_initial_ms;
        self->logic->clock.increment_ms = self->clock_initial_ms; // Wait, increment field?
        // logic struct has separate increment field
        self->logic->clock_initial_ms = self->clock_initial_ms;
        self->logic->clock_increment_ms = self->clock_increment_ms;
        
        self->logic->clock.enabled = true;
        self->logic->clock.active = false; // Will activate on first move interaction
        self->logic->clock.flagged_player = PLAYER_NONE;
        self->logic->clock.last_tick_time = 0;
        
        // Also update widgets immediately to be sure
        if (self->app_state->gui.top_clock && self->app_state->gui.bottom_clock) {
             ClockWidget* clocks[2] = { self->app_state->gui.top_clock, self->app_state->gui.bottom_clock };
             for(int k=0; k<2; k++) {
                 Player clk_side = clock_widget_get_side(clocks[k]);
                 int64_t t = (clk_side == PLAYER_WHITE) ? w_time : b_time;
                 clock_widget_update(clocks[k], t, self->clock_initial_ms, false);
                 clock_widget_set_disabled(clocks[k], false);
             }
        }
    } else {
        // No clock or clock disabled
        self->logic->clock.enabled = false;
        self->logic->clock.active = false;
        if (self->app_state->gui.top_clock) clock_widget_set_disabled(self->app_state->gui.top_clock, true);
        if (self->app_state->gui.bottom_clock) clock_widget_set_disabled(self->app_state->gui.bottom_clock, true);
    }
    
    // 4. Set Gameplay State
    self->logic->gameMode = mode;
    self->logic->playerSide = side; 
    
    if (self->app_state->gui.info_panel) {
        info_panel_set_game_mode(self->app_state->gui.info_panel, mode);
        // We'll set the side once UI is ready to avoid double logic triggers
    }
    
    // Sync Game State: Re-calculate check/mate/status flags for the new position
    gamelogic_update_game_state(self->logic);
    
    // Sync Think Times: Ensure logic has history of think times up to current ply
    if (self->think_times && self->current_ply > 0) {
        // Ensure capacity
        if (self->logic->think_time_capacity < self->current_ply) {
            self->logic->think_time_capacity = self->current_ply + 32;
            self->logic->think_times = g_realloc(self->logic->think_times, self->logic->think_time_capacity * sizeof(int));
        }
        
        // Copy times
        if (self->logic->think_times) {
            // Safely copy available think times. 
            // If current_ply > think_time_count (e.g. if partial data), fill with 0 or limit.
            int count_to_copy = self->current_ply;
            if (count_to_copy > self->think_time_count) count_to_copy = self->think_time_count;
            
            memcpy(self->logic->think_times, self->think_times, count_to_copy * sizeof(int));
            
            // Fill remainder with 0 if any
            for (int i = count_to_copy; i < self->current_ply; i++) {
                self->logic->think_times[i] = 0;
            }
            
            self->logic->think_time_count = self->current_ply;
        }
    } else {
        // No think times or at start
        self->logic->think_time_count = 0;
    }
    
    // 5. Exit Replay Mode (Set flag early to allow standard UI behaviors)
    self->app_state->is_replaying = false;
    self->app_state->match_saved = false; 
    
    // Free and detach replay match ID to prevent overwriting original file
    if (self->app_state->replay_match_id) {
        g_free(self->app_state->replay_match_id);
    }
    self->app_state->replay_match_id = NULL; 
    
    // 6. UI Updates
    if (self->app_state->gui.board) {
        board_widget_set_interactive(self->app_state->gui.board, TRUE);
        
        // Perspective: Flip board if playing as Black
        board_widget_set_flipped(self->app_state->gui.board, side == PLAYER_BLACK);
        
        // Visual continuity: Highlight the move that just happened before this position
        if (self->current_ply > 0) {
            Move* last = self->moves[self->current_ply - 1];
            board_widget_set_last_move(self->app_state->gui.board, 
                                       last->from_sq / 8, last->from_sq % 8, 
                                       last->to_sq / 8, last->to_sq % 8);
        } else {
             board_widget_set_last_move(self->app_state->gui.board, -1, -1, -1, -1);
        }
    }

    if (self->app_state->gui.info_panel) {
        info_panel_show_replay_controls(self->app_state->gui.info_panel, FALSE);
        info_panel_update_status(self->app_state->gui.info_panel);
        info_panel_refresh_graveyard(self->app_state->gui.info_panel);
        
        // Sync side dropdown with the new active side (human player)
        // This is now safe as signals are blocked in info_panel.c
        info_panel_set_player_side(self->app_state->gui.info_panel, side);
        
        // Restore CvC UI if needed
        if (mode == GAME_MODE_CVC) {
            info_panel_set_cvc_state(self->app_state->gui.info_panel, CVC_STATE_STOPPED);
        }
    }
    
    // Explicitly refresh board to show pieces at the new position
    if (self->app_state->gui.board) {
        board_widget_refresh(self->app_state->gui.board);
    }
    
    if (self->app_state->gui.right_side_panel) {
        // Sync panel perspective
        right_side_panel_set_flipped(self->app_state->gui.right_side_panel, side == PLAYER_BLACK);

        // Unlock history interactions
        right_side_panel_set_replay_lock(self->app_state->gui.right_side_panel, false);
        
        // Truncate the visual history in the right panel to match current ply
        right_side_panel_clear_history(self->app_state->gui.right_side_panel);
        
        // Re-populate from logic history
        GameLogic* temp = gamelogic_create(); 
        if (temp) {
             if (self->logic->start_fen[0]) gamelogic_load_fen(temp, self->logic->start_fen);
             else gamelogic_reset(temp);
             
             Player p = temp->turn;
             int m_num = temp->fullmoveNumber;

             for (int i = 0; i < self->current_ply; i++) {
                 Move* m = self->moves[i];
                 int r = m->from_sq/8; 
                 int c = m->from_sq%8;
                 PieceType pt = PIECE_PAWN;
                 if (temp->board[r][c]) pt = temp->board[r][c]->type;
                 
                 char san[16];
                 gamelogic_get_move_san(temp, m, san, sizeof(san));
                 right_side_panel_add_move_notation(self->app_state->gui.right_side_panel, san, pt, m_num, p);
                 gamelogic_perform_move(temp, m);
                 
                 if (p == PLAYER_BLACK) m_num++;
                 p = (p == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
             }
             gamelogic_free(temp);
        }
        
        right_side_panel_scroll_to_bottom(self->app_state->gui.right_side_panel);
    }
    
    // 7. Trigger Logic Resume
    if (self->clock_enabled) {
        self->logic->clock.active = true;
        self->logic->clock.last_tick_time = clock_get_current_time_ms();
    }
    
    // Trigger update callback to sync UI and trigger AI if it's the computer's turn
    if (self->logic->updateCallback) {
        self->logic->updateCallback();
    }
    
    return true;
}

static gboolean replay_timer_callback(gpointer user_data) {
    ReplayController* self = (ReplayController*)user_data;
    if (!self || !self->is_playing) return G_SOURCE_REMOVE;
    
    if (self->current_ply >= self->total_moves) {
        self->is_playing = false;
        self->timer_id = 0;
        // Notify UI?
        return G_SOURCE_REMOVE;
    }
    
    replay_controller_next(self, true);  // from_timer = true
    
    // Critical: Reset clock animation anchor for the NEW move we just started
    self->move_start_time_monotonic = g_get_monotonic_time();
    
    // Schedule next
    if (self->is_playing && self->current_ply < self->total_moves) {
         int nominal_delay = self->speed_ms;
         if (self->use_think_times && self->think_times && self->current_ply < self->think_time_count) {
             nominal_delay = self->think_times[self->current_ply];
             // Clamp visual floor
             if (nominal_delay < 200) nominal_delay = 200; 
         }
         
         if (debug_mode) {
             printf("[Replay] Ply %d -> Nominal: %d ms, Multiplier: %.2f\n", 
                    self->current_ply, nominal_delay, self->time_multiplier);
         }
         
         int effective_delay = (int)(nominal_delay / self->time_multiplier);
         if (effective_delay < 10) effective_delay = 10;

         self->timer_id = g_timeout_add(effective_delay, replay_timer_callback, self);
    } else {
         self->timer_id = 0;
         self->is_playing = false;
         // Ensure clock snaps to final
         refresh_clock_display(self);
    }
    
    return G_SOURCE_REMOVE; // We manually re-scheduled above with variable delay
}

/* --- AI Analysis Integration --- */

typedef struct {
    ReplayController* controller;
    int ply_done;
    int total;
} AnalysisProgressData;

static gboolean on_analysis_progress_idle(gpointer user_data) {
    AnalysisProgressData* data = (AnalysisProgressData*)user_data;
    if (data->controller && data->controller->app_state) {
        // We aren't using a granular progress bar in the new UI, just a spinner/overlay.
        // But we could update the loading label text if we exposed a function for it.
        // For now, just logging or no-op.
        // right_side_panel_set_loading_text(...); // Optional future enhancement
    }
    g_free(data);
    return FALSE;
}

static void on_analysis_progress(int ply_done, int total, void* user_data) {
    ReplayController* self = (ReplayController*)user_data;
    if (!self) return;
    
    AnalysisProgressData* data = g_new0(AnalysisProgressData, 1);
    data->controller = self;
    data->ply_done = ply_done;
    data->total = total;
    
    g_idle_add(on_analysis_progress_idle, data);
}

typedef struct {
    ReplayController* controller;
    GameAnalysisResult* result;
} AnalysisCompleteData;

static gboolean on_analysis_complete_idle(gpointer user_data) {
    AnalysisCompleteData* data = (AnalysisCompleteData*)user_data;
    ReplayController* self = data->controller;
    
    if (self && !self->analysis_result) { // Only set if not already set (or handle overwrite)
         self->analysis_result = data->result; // Transfer ownership (ref count)
         
         if (debug_mode) {
             printf("[Replay] Analysis Result Stored: %d plies\n", self->analysis_result->total_plies);
         }
         
         // Notify UI
         if (self->app_state) {
             // Update Right Side Panel (Move List Annotations)
             right_side_panel_set_analysis_result(self->app_state->gui.right_side_panel, self->analysis_result);
             
             // Update Info Panel (Status / Button State)
             info_panel_update_replay_status(self->app_state->gui.info_panel, self->current_ply, self->total_moves);
             
             // Hide progress / Stop loading overlay
             right_side_panel_set_analyzing_state(self->app_state->gui.right_side_panel, false);
         }
         
    } else {
        // Collision or cancelled? Unref payload
        ai_analysis_result_unref(data->result);
    }
    
    // Cleanup job reference since it's finished
    if (self && self->analysis_job) {
        ai_analysis_free(self->analysis_job);
        self->analysis_job = NULL;
    }

    g_free(data);
    return FALSE; 
}

// Callback for the RightSidePanel "Analyze Game" button
static void on_replay_analyze_clicked_cb(GtkButton* btn, gpointer user_data) {
    (void)btn; // Unused
    ReplayController* self = (ReplayController*)user_data;
    printf("[ReplayController] Analyze button clicked. Controller: %p\n", (void*)self);
    
    if (!self) return;
    
    if (replay_controller_is_analyzing(self)) {
        printf("[ReplayController] Cancelling analysis...\n");
        replay_controller_cancel_analysis(self);
        // UI update for cancel happens immediately or via state check?
        // Let's force UI update
        if (self->app_state) {
            right_side_panel_set_analyzing_state(self->app_state->gui.right_side_panel, false);
        }
    } else {
        printf("[ReplayController] Starting analysis...\n");
        replay_controller_analyze_match(self);
        // Start loading overlay
        if (self->app_state) {
            right_side_panel_set_analyzing_state(self->app_state->gui.right_side_panel, true);
        }
    }
}

void replay_controller_enter_replay_mode(ReplayController* self) {
    if (!self || !self->app_state) {
        printf("[ReplayController] Error: Enter replay mode called with NULL self or app_state\n");
        return;
    }
    printf("[ReplayController] Entering replay mode...\n");
    
    // Wire up the new Analyze button in RightSidePanel
    if (self->app_state->gui.right_side_panel) {
        right_side_panel_set_analyze_callback(self->app_state->gui.right_side_panel, G_CALLBACK(on_replay_analyze_clicked_cb), self);
        // FORCE replay lock so scroll behavior works
        right_side_panel_set_replay_lock(self->app_state->gui.right_side_panel, true);
    } else {
        printf("[ReplayController] Warning: RightSidePanel is NULL in AppState\n");
    }
    
    // Reset analysis UI state
    right_side_panel_set_analyzing_state(self->app_state->gui.right_side_panel, false);
    right_side_panel_set_analysis_result(self->app_state->gui.right_side_panel, self->analysis_result); // Show existing result if any

    // Set Clock Names
    if (self->app_state->gui.top_clock && self->app_state->gui.bottom_clock) {
        bool flipped = board_widget_is_flipped(self->app_state->gui.board);
        ClockWidget* white_clk = flipped ? self->app_state->gui.top_clock : self->app_state->gui.bottom_clock;
        ClockWidget* black_clk = flipped ? self->app_state->gui.bottom_clock : self->app_state->gui.top_clock;
        
        clock_widget_set_name(white_clk, get_name_for_config(self->white_config));
        clock_widget_set_name(black_clk, get_name_for_config(self->black_config));
    }
}

static void on_analysis_complete(GameAnalysisResult* result, void* user_data) {
    ReplayController* self = (ReplayController*)user_data;
    if (!self) return;
    
    // Ref the result to pass to main thread
    ai_analysis_result_ref(result);
    
    AnalysisCompleteData* data = g_new0(AnalysisCompleteData, 1);
    data->controller = self;
    data->result = result;
    
    g_idle_add(on_analysis_complete_idle, data);
}

void replay_controller_analyze_match(ReplayController* self) {
    if (!self || !self->full_uci_history) {
        printf("[Replay] Cannot analyze: No moves.\n");
        return;
    }
    
    if (self->analysis_job) {
        printf("[Replay] Analysis already in progress.\n");
        return;
    }
    
    if (self->analysis_result) {
        ai_analysis_result_unref(self->analysis_result);
        self->analysis_result = NULL;
    }
    
    // Config
    AppConfig* app_config = config_get();
    AnalysisConfig cfg = {0};
    cfg.threads = 1;
    cfg.hash_size = 64; // Small default
    cfg.multipv = 3;
    
    if (app_config->analysis_use_custom && strlen(app_config->custom_engine_path) > 0) {
        cfg.engine_path = app_config->custom_engine_path;
        cfg.move_time_pass1 = app_config->custom_movetime;
    } else {
        // Internal/Stockfish
        // TODO: Is there a unified getter? For now assume standard path or config path
        // Using "stockfish" or configured path for internal if available
        // Ideally `ai_engine` knows the internal path.
        // For now, let's try strict path or assume in path.
        cfg.engine_path = "stockfish"; 
    }
    
    // Override for high accuracy if requested, or just use config
    // cfg.move_time_pass1 = 1000; 
    
    // Split moves
    char** moves = g_strsplit(self->full_uci_history, " ", -1);
    int count = g_strv_length(moves);
    
    if (debug_mode) printf("[Replay] Starting analysis on %d moves...\n", count);
    
    self->analysis_job = ai_analysis_start(NULL, // startpos
                                           moves, 
                                           count, 
                                           cfg, 
                                           on_analysis_progress, 
                                           on_analysis_complete, 
                                           self);
                                           
    g_strfreev(moves);
}

void replay_controller_cancel_analysis(ReplayController* self) {
    if (!self || !self->analysis_job) return;
    
    ai_analysis_cancel(self->analysis_job);
    // Job free happens in callback or we wait? 
    // `ai_analysis_cancel` just flags. 
    // We should probably rely on callback to clean up `analysis_job` ptr.
}

bool replay_controller_is_analyzing(ReplayController* self) {
    return (self && self->analysis_job != NULL);
}

const GameAnalysisResult* replay_controller_get_analysis_result(ReplayController* self) {
    return self ? self->analysis_result : NULL;
}

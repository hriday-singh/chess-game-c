#include "replay_controller.h"
#include "../game/gamelogic.h"
#include "app_state.h"
#include "../game/move.h"
#include "board_widget.h"
#include "right_side_panel.h"
#include "info_panel.h"
#include "ai_analysis.h"
#include "config_manager.h"
#include <string.h>

static bool debug_mode = true;

// Forward decl for internal timer
static gboolean replay_timer_callback(gpointer user_data);

ReplayController* replay_controller_new(GameLogic* logic, AppState* app_state) {
    ReplayController* self = g_new0(ReplayController, 1);
    self->logic = logic;
    self->app_state = app_state;
    self->speed_ms = 800; // Default speed
    return self;
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
    info_panel_update_status(self->app_state->gui.info_panel);
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
    
    if (self->analysis_job) {
        ai_analysis_cancel(self->analysis_job);
        // Note: Thread might still be running. Ideally we join or wait.
        // For now, assume cancel acts quickly or we leak small struct until app exit.
        // ai_analysis_free(self->analysis_job); // Unsafe if thread running
    }
    
    if (self->analysis_result) {
        ai_analysis_result_unref(self->analysis_result);
    }
    
    g_free(self);
}

void replay_controller_load_match(ReplayController* self, const char* moves_uci, const char* start_fen) {
    if (!self) return;
    
    replay_controller_pause(self);
    
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
    
    self->total_moves = 0;
    self->current_ply = 0;

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
    
    // Initialize highlighting to first move (ply 0)
    if (self->app_state && self->app_state->gui.right_side_panel) {
        right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, -1);
    }
    
    // Update UI immediately to show correct move count
    if(debug_mode) printf("[ReplayController] Updating UI\n");
    replay_ui_update(self);
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
         board_widget_set_last_move(self->app_state->gui.board, -1, -1, -1, -1); // Clear highlights at start
         board_widget_refresh(self->app_state->gui.board);
         if (self->app_state->gui.right_side_panel) {
            int hl = (self->current_ply > 0) ? (self->current_ply - 1) : -1;
             right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, hl);
         }
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
    self->timer_id = g_timeout_add(self->speed_ms, replay_timer_callback, self);
    
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
    if (ms < 350) ms = 350; // Minimum speed limit
    if (ms > 5000) ms = 5000;
    
    self->speed_ms = ms;
    
    // If playing, update timer
    if (self->is_playing) {
        if (self->timer_id > 0) g_source_remove(self->timer_id);
        self->timer_id = g_timeout_add(self->speed_ms, replay_timer_callback, self);
    }
}

void replay_controller_next(ReplayController* self) {
    if (!self || self->current_ply >= self->total_moves) {
        replay_controller_pause(self);
        return;
    }
    
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
    if (self->app_state && self->app_state->gui.board) {
        board_widget_reset_selection(self->app_state->gui.board);
        // Set yellow highlight for the move we just made
        if (next_move) {
            int fromRow = next_move->from_sq / 8, fromCol = next_move->from_sq % 8;
            int toRow = next_move->to_sq / 8, toCol = next_move->to_sq % 8;
            board_widget_set_last_move(self->app_state->gui.board, fromRow, fromCol, toRow, toCol);
        }
    }
    
    // Update Sync
    int hl = (self->current_ply > 0) ? (self->current_ply - 1) : -1;
    right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, hl);
    replay_ui_update(self);
}

void replay_controller_prev(ReplayController* self) {
    if (self->current_ply <= 0) return;
    
    self->current_ply--;
    if (self->app_state) {
        board_widget_reset_selection(self->app_state->gui.board);
        // Set yellow highlight for position we are GOING to
        if (self->current_ply > 0 && self->moves && self->current_ply <= self->total_moves) {
            Move* currentMove = self->moves[self->current_ply - 1];
            int fromRow = currentMove->from_sq / 8, fromCol = currentMove->from_sq % 8;
            int toRow = currentMove->to_sq / 8, toCol = currentMove->to_sq % 8;
            board_widget_set_last_move(self->app_state->gui.board, fromRow, fromCol, toRow, toCol);
        } else {
            // At start, clear highlights
            board_widget_set_last_move(self->app_state->gui.board, -1, -1, -1, -1);
        }
    }

    if (self->snapshots) {
        gamelogic_restore_snapshot(self->logic, &self->snapshots[self->current_ply]);
    } else {
        gamelogic_undo_move(self->logic);
    }
    
    if (self->app_state) {
        board_widget_refresh(self->app_state->gui.board);
        if (self->app_state->gui.right_side_panel) {
            int hl = (self->current_ply > 0) ? (self->current_ply - 1) : -1;
            right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, hl);
        }
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
        // Set yellow highlight for position we are GOING to
        if (ply > 0 && self->moves && ply <= self->total_moves) {
            Move* currentMove = self->moves[ply - 1];
            int fromRow = currentMove->from_sq / 8, fromCol = currentMove->from_sq % 8;
            int toRow = currentMove->to_sq / 8, toCol = currentMove->to_sq % 8;
            board_widget_set_last_move(self->app_state->gui.board, fromRow, fromCol, toRow, toCol);
        } else {
            // At start, clear highlights
            board_widget_set_last_move(self->app_state->gui.board, -1, -1, -1, -1);
        }
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
        if (self->app_state->gui.right_side_panel) {
            int hl = (self->current_ply > 0) ? (self->current_ply - 1) : -1;
            right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, hl);
        }
    }
    replay_ui_update(self);
}

// "Start From Here" Logic
bool replay_controller_start_from_here(ReplayController* self, GameMode mode, Player side) {
    if (!self || !self->logic) return false;
    
    replay_controller_pause(self);
    
    // 1. Logic State is already at self->current_ply.
    // We just need to discard future moves and switch mode.
    
    self->logic->gameMode = mode;
    self->logic->playerSide = side;
    
    // In CvC, side doesn't matter as much, but logic->playerSide stores main POV.
    
    // 2. Clear history stacks in gamelogic?
    // NO. We want to KEEP history up to this point so undo works!
    // But GameLogic doesn't have a "future" stack once we start playing new moves.
    // So simply by playing a NEW move, the branching happens naturally if logic supports it.
    // Our GameLogic `perform_move` pushes to stack. It doesn't clear "future" because there is no future stack in GameLogic (only Undo stack).
    // So we are good!
    
    // 3. UI Updates handled by caller (exit replay mode visuals)
    
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
    
    replay_controller_next(self);
    
    return G_SOURCE_CONTINUE;
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
    } else {
        printf("[ReplayController] Warning: RightSidePanel is NULL in AppState\n");
    }
    
    // Reset analysis UI state
    right_side_panel_set_analyzing_state(self->app_state->gui.right_side_panel, false);
    right_side_panel_set_analysis_result(self->app_state->gui.right_side_panel, self->analysis_result); // Show existing result if any
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

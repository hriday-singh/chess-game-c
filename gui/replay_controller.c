#include "replay_controller.h"
#include "../game/gamelogic.h"
#include "app_state.h"
#include "../game/move.h"
#include "board_widget.h"
#include "right_side_panel.h"
#include "info_panel.h"
#include <stdlib.h>
#include <string.h>

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
    info_panel_update_replay_status(self->app_state->gui.info_panel, self->current_ply, self->total_moves);
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
    
    g_free(self->full_san_history);
    
    g_free(self);
}

void replay_controller_load_match(ReplayController* self, const char* san_moves, const char* start_fen) {
    if (!self || !san_moves) return;
    
    replay_controller_pause(self);
    
    // Clear existing snapshots
    if (self->snapshots) {
        g_free(self->snapshots);
        self->snapshots = NULL;
        self->snapshot_count = 0;
        self->snapshot_capacity = 0;
    }

    g_free(self->full_san_history);
    self->full_san_history = g_strdup(san_moves);
    self->total_moves = 0;
    self->current_ply = 0;
    
    // 2. Temporarily load into GameLogic to extract Moves
    GString* clean_moves = g_string_new(NULL);
    char* temp_cur = g_strdup(san_moves);
    char* next_tok = NULL;
    char* tok = strtok_s(temp_cur, " ", &next_tok);
    while (tok) {
        size_t len = strlen(tok);
        if (len > 0 && tok[len-1] != '.') {
            if (clean_moves->len > 0) g_string_append_c(clean_moves, ' ');
            g_string_append(clean_moves, tok);
        }
        tok = strtok_s(NULL, " ", &next_tok);
    }
    g_free(temp_cur);

    // Use cleaned string with start_fen
    gamelogic_load_from_san_moves(self->logic, clean_moves->str, start_fen);
    
    // 3. Extract moves and Generate Snapshots
    self->total_moves = gamelogic_get_move_count(self->logic);
    if (self->total_moves > 0) {
        self->moves = g_new0(Move*, self->total_moves);
        for (int i = 0; i < self->total_moves; i++) {
            Move* m = gamelogic_get_move_at(self->logic, i);
            if (m) self->moves[i] = move_copy(m);
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
    
    // Update UI initial state
    if (self->app_state) {
        board_widget_refresh(self->app_state->gui.board);
        if (self->app_state->gui.right_side_panel) {
             right_side_panel_clear_history(self->app_state->gui.right_side_panel);
             char* copy = g_strdup(clean_moves->str);
             char* next_copy = NULL;
             char* token = strtok_s(copy, " ", &next_copy);
             int m_num = 1;
             Player p = self->logic->turn; 
             
             while (token) {
                 right_side_panel_add_san_move(self->app_state->gui.right_side_panel, token, m_num, p);
                 if (p == PLAYER_BLACK) m_num++;
                 p = (p == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
                 token = strtok_s(NULL, " ", &next_copy);
             }
             g_free(copy);
        }
    }
    g_string_free(clean_moves, TRUE);
    
    // Initialize highlighting to first move (ply 0)
    if (self->app_state && self->app_state->gui.right_side_panel && self->total_moves > 0) {
        right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, 0);
    }
    
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
         board_widget_refresh(self->app_state->gui.board);
         if (self->app_state->gui.right_side_panel) {
             right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, -1);
         }
    }
    replay_ui_update(self);
}

void replay_controller_exit(ReplayController* self) {
    if (!self) return;
    replay_controller_pause(self);
    // Cleanup is handled by caller usually setting is_replaying = false
}

void replay_controller_play(ReplayController* self) {
    if (!self || self->is_playing) return;
    
    // If at end, wrap to start? No, just stop or stay.
    if (self->current_ply >= self->total_moves) {
        // Option: Auto-restart? Or just do nothing.
        // Let's restart if at end?
        // replay_controller_seek(self, 0); 
        // User prefers manual control likely.
        return;
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
    if (ms < 250) ms = 250; // Limit
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
    
    // Note: gamelogic_perform_move is NOT called here to avoid double execution
    // The animation system will call execute_move_with_updates when animation completes
    // For replay, we just update the ply counter
    self->current_ply++;
    
    // Auto-pause if we've reached the end
    if (self->current_ply >= self->total_moves) {
        replay_controller_pause(self);
    }
    
    // Update Sync
    if (self->app_state && self->app_state->gui.right_side_panel) {
        right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, self->current_ply - 1);
    }
    replay_ui_update(self);
}

void replay_controller_prev(ReplayController* self) {
    if (self->current_ply <= 0) return;
    
    self->current_ply--;
    if (self->snapshots) {
        gamelogic_restore_snapshot(self->logic, &self->snapshots[self->current_ply]);
    } else {
        gamelogic_undo_move(self->logic);
    }
    
    if (self->app_state) {
        board_widget_refresh(self->app_state->gui.board);
        if (self->app_state->gui.right_side_panel) {
            right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, self->current_ply - 1);
        }
    }
    replay_ui_update(self);
}

void replay_controller_seek(ReplayController* self, int ply) {
    if (!self) return;
    if (ply < 0) ply = 0;
    if (ply >= self->snapshot_count) ply = self->snapshot_count - 1;
    
    replay_controller_pause(self);
    
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
            right_side_panel_highlight_ply(self->app_state->gui.right_side_panel, self->current_ply - 1);
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
    
    // While animating, we don't want to trigger next immediately?
    // `board_widget_animate_move` is async/visual.
    // If speed is fast, animations might overlap.
    // `animate_move` usually cancels previous animation.
    
    return G_SOURCE_CONTINUE;
}

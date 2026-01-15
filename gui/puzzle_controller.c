#include "puzzle_controller.h"
#include "puzzles.h"
#include "sound_engine.h"
#include "settings_dialog.h"
#include "board_widget.h"
#include "info_panel.h"
#include <stdio.h>
#include <string.h>
#include "gamelogic.h"

#include "right_side_panel.h"

static void on_puzzle_list_item_selected(int idx, gpointer user_data);

void puzzle_controller_init(AppState* state) {
    if (!state) return;
    puzzle_controller_refresh_list(state);
}

void puzzle_controller_cleanup(AppState* state) {
    // Nothing specific to clean up beyond what main cleanup handles
    (void)state;
}

void puzzle_controller_start(AppState* state, int puzzle_idx) {
    if (!state) return;
    if (puzzle_idx < 0 || puzzle_idx >= puzzles_get_count()) return;
    
    const Puzzle* puzzle = puzzles_get_at(puzzle_idx);
    if (!puzzle) return;
    
    state->puzzle.current_idx = puzzle_idx;
    state->puzzle.move_idx = 0;
    state->puzzle.last_processed_move = 0;
    state->puzzle.wait = false;
    state->logic->gameMode = GAME_MODE_PUZZLE;
    
    // IMPORTANT: Reset the game state first to clear move history and turn
    gamelogic_reset(state->logic);
    
    // Then load the puzzle's FEN position
    gamelogic_load_fen(state->logic, puzzle->fen);
    if (state->gui.board) board_widget_refresh(state->gui.board);
    
    // Update Info Panel
    if (state->gui.info_panel) {
        info_panel_set_puzzle_mode(state->gui.info_panel, true);
        info_panel_update_puzzle_info(state->gui.info_panel, puzzle->title, puzzle->description, "Your turn! (White to Move)", true);
        
        // Connect callbacks
        info_panel_set_puzzle_callbacks(state->gui.info_panel, G_CALLBACK(on_puzzle_reset_clicked), G_CALLBACK(on_puzzle_next_clicked), state);
        info_panel_set_puzzle_exit_callback(state->gui.info_panel, G_CALLBACK(on_puzzle_exit_clicked), state);
        
        info_panel_highlight_puzzle(state->gui.info_panel, puzzle_idx);
    }
    
    // Unlock the board for the new puzzle
    if (state->gui.board) {
        board_widget_set_nav_restricted(state->gui.board, false, -1, -1, -1, -1);
        board_widget_reset_selection(state->gui.board);
        gtk_widget_grab_focus(state->gui.board);
    }
    
    // Hide Right Side Panel for immersion
    if (state->gui.right_side_panel) right_side_panel_set_visible(state->gui.right_side_panel, false);

    // Explicitly grab focus to main window
    if (state->gui.window) gtk_window_present(state->gui.window);
}


static gboolean on_auto_play_opponent_move(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || state->logic->gameMode != GAME_MODE_PUZZLE) return FALSE;
    
    const Puzzle* puzzle = puzzles_get_at(state->puzzle.current_idx);
    if (!puzzle || state->puzzle.move_idx >= puzzle->solution_length) return FALSE;
    
    const char* uci = puzzle->solution_moves[state->puzzle.move_idx];
    if (!uci) return FALSE;
    
    // Parse UCI
    Move m = {0};
    m.promotionPiece = NO_PROMOTION; // Default to no promotion
    m.capturedPieceType = NO_PIECE;  // Default to no capture (logic will fill if needed, but safer)
    
    int f1 = uci[0] - 'a';
    int r1 = 8 - (uci[1] - '0');
    int f2 = uci[2] - 'a';
    int r2 = 8 - (uci[3] - '0');
    
    m.from_sq = r1 * 8 + f1;
    m.to_sq = r2 * 8 + f2;
    
    if (strlen(uci) > 4) {
        char p = uci[4];
        if (p == 'q') m.promotionPiece = PIECE_QUEEN;
        else if (p == 'r') m.promotionPiece = PIECE_ROOK;
        else if (p == 'b') m.promotionPiece = PIECE_BISHOP;
        else if (p == 'n') m.promotionPiece = PIECE_KNIGHT;
    }
    
    // Animate (this will trigger logical move upon completion)
    if (state->gui.board) {
        state->puzzle.wait = false; 
        board_widget_animate_move(state->gui.board, &m);
    }
    
    return FALSE; // One-shot
}

void puzzle_controller_check_move(AppState* state) {
    if (!state || state->logic->gameMode != GAME_MODE_PUZZLE || state->puzzle.wait) return;

    const Puzzle* puzzle = puzzles_get_at(state->puzzle.current_idx);
    if (!puzzle) return;
    
    // Don't validate if puzzle is already complete
    if (state->puzzle.move_idx >= puzzle->solution_length) {
        return;
    }
    
    // Check if we have a new move to validate
    int current_move_count = gamelogic_get_move_count(state->logic);
    if (current_move_count <= state->puzzle.last_processed_move) {
        return;
    }

    int count = gamelogic_get_move_count(state->logic);
    Move last_move = gamelogic_get_last_move(state->logic);
    if (count > 0) {
        // Convert to UCI format (e.g., "e2e4" or "e7e8q" for promotion)
        char move_uci[32]; 
        int r1 = last_move.from_sq / 8, c1 = last_move.from_sq % 8;
        int r2 = last_move.to_sq / 8, c2 = last_move.to_sq % 8;
        snprintf(move_uci, sizeof(move_uci), "%c%d%c%d",
            'a' + c1, 8 - r1,
            'a' + c2, 8 - r2);
        
        // Check if it matches expected move
        const char* expected = puzzle->solution_moves[state->puzzle.move_idx];
        
        if (expected && strcmp(move_uci, expected) == 0) {
            // Correct move!
            
            // Mark this move as processed
            state->puzzle.last_processed_move = current_move_count;
            state->puzzle.move_idx++;
            
            // Check if puzzle is complete
            if (state->puzzle.move_idx >= puzzle->solution_length) {
                if (state->gui.info_panel) info_panel_update_puzzle_info(state->gui.info_panel, NULL, NULL, "Puzzle Solved! Great job!", true);
                sound_engine_play(SOUND_PUZZLE_CORRECT);
                // Disable board interaction
                if (state->gui.board) board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            } else {
                sound_engine_play(SOUND_PUZZLE_CORRECT_2);
                     // Check if next move is opponent's response (Odd index = Opponent)
                if (state->puzzle.move_idx % 2 != 0) {
                     state->puzzle.wait = true;
                     if (state->gui.board) board_widget_set_interactive(state->gui.board, FALSE);
                     if (state->gui.info_panel) info_panel_update_puzzle_info(state->gui.info_panel, NULL, NULL, "Correct! Opponent is responding...", false);
                     g_timeout_add(500, on_auto_play_opponent_move, state);
                } else {
                     // It is Player's turn again
                     state->puzzle.wait = false;
                     if (state->gui.board) board_widget_set_interactive(state->gui.board, TRUE);
                     if (state->gui.info_panel) info_panel_update_puzzle_info(state->gui.info_panel, NULL, NULL, "Your turn! Make the winning move.", true);
                }
            }
        } else {
            // Wrong move - undo it
            sound_engine_play(SOUND_PUZZLE_WRONG); // Play error sound for wrong move
            gamelogic_undo_move(state->logic);
            if (state->gui.board) board_widget_refresh(state->gui.board);
            if (state->gui.info_panel) info_panel_update_puzzle_info(state->gui.info_panel, NULL, NULL, "Try again! That's not the solution.", true);
            // Do NOT update puzzle_last_processed_move, so the retry (which increments count again) will be processed.
        }
    }
}

void puzzle_controller_reset(AppState* state) {
    if (!state) return;
    puzzle_controller_start(state, state->puzzle.current_idx);
}

void puzzle_controller_next(AppState* state) {
    if (!state) return;
    int next_idx = (state->puzzle.current_idx + 1) % puzzles_get_count();
    puzzle_controller_start(state, next_idx);
}

void puzzle_controller_exit(AppState* state) {
    if (!state) return;
    
    // Exit puzzle mode
    state->logic->gameMode = GAME_MODE_PVC;
    state->puzzle.current_idx = -1;
    state->puzzle.move_idx = 0;
    state->puzzle.wait = false;
    
    // Reset the board to standard position
    gamelogic_reset(state->logic);
    if (state->gui.board) board_widget_refresh(state->gui.board);
    
    // Hide puzzle UI and show standard controls
    if (state->gui.info_panel) info_panel_set_puzzle_mode(state->gui.info_panel, false);
    
    // Update the dropdown to show PvC
    if (state->gui.info_panel) info_panel_set_game_mode(state->gui.info_panel, GAME_MODE_PVC);
    
    if (state->gui.info_panel) info_panel_rebuild_layout(state->gui.info_panel);
    
    // Restore RSP
    if (state->gui.right_side_panel) right_side_panel_set_visible(state->gui.right_side_panel, true);
}

void puzzle_controller_refresh_list(AppState* state) {
    if (!state || !state->gui.info_panel) return;
    info_panel_clear_puzzle_list(state->gui.info_panel);
    for (int i = 0; i < puzzles_get_count(); i++) {
        const Puzzle* p = puzzles_get_at(i);
        if (p) {
            info_panel_add_puzzle_to_list(state->gui.info_panel, p->title, i);
        }
    }
    // Connect proper callback for list selection
    info_panel_set_puzzle_list_callback(state->gui.info_panel, G_CALLBACK(on_puzzle_list_item_selected), state);
}

// --- Signal Handlers ---

void on_puzzle_reset_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    puzzle_controller_reset((AppState*)user_data);
}

void on_puzzle_next_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    puzzle_controller_next((AppState*)user_data);
}

void on_puzzle_exit_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    puzzle_controller_exit((AppState*)user_data);
}

void on_start_puzzle_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action;
    AppState* state = (AppState*)user_data;
    int puzzle_idx = g_variant_get_int32(parameter);
    
    // Close settings if open
    if (state->gui.settings_dialog && settings_dialog_get_window(state->gui.settings_dialog)) {
        gtk_widget_set_visible(GTK_WIDGET(settings_dialog_get_window(state->gui.settings_dialog)), FALSE);
    }
    
    puzzle_controller_start(state, puzzle_idx);
}

void on_puzzles_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    if (state) {
        // Trigger the open-settings action defined in main.c
        GApplication* app = g_application_get_default();
        if (app) {
            g_action_group_activate_action(G_ACTION_GROUP(app), "open-settings", g_variant_new_string("puzzles"));
        }
    }
}

typedef struct {
    AppState* state;
    int puzzle_index;
} PendingPuzzleData;

static gboolean start_puzzle_idle(gpointer user_data) {
    PendingPuzzleData* data = (PendingPuzzleData*)user_data;
    if (data && data->state) {
        puzzle_controller_start(data->state, data->puzzle_index);
    }
    g_free(data);
    return FALSE;
}

void on_panel_puzzle_selected_safe(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
    (void)box;
    AppState* state = (AppState*)user_data;
    if (!row) return;
    
    int idx = gtk_list_box_row_get_index(row);
    if (idx < 0) return;
    
    // Use idle to safely switch context
    PendingPuzzleData* data = g_new0(PendingPuzzleData, 1);
    data->state = state;
    data->puzzle_index = idx;
    g_idle_add(start_puzzle_idle, data);
}

static void on_puzzle_list_item_selected(int idx, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (idx < 0) return;
    
    PendingPuzzleData* data = g_new0(PendingPuzzleData, 1);
    data->state = state;
    data->puzzle_index = idx;
    g_idle_add(start_puzzle_idle, data);
}

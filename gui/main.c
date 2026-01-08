#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "gamelogic.h"
#include "board_widget.h"
#include "info_panel.h"
#include "sound_engine.h"
#include "theme_data.h"
#include "board_theme_dialog.h"
#include "piece_theme_dialog.h"
#include "settings_dialog.h"
#include "ai_dialog.h"
#include "move.h"
#include "ai_engine.h"
#include "types.h"
#include "puzzles.h"

#include "app_state.h"
#include "tutorial.h"
#include "dark_mode_button.h"

// Globals
static AppState* g_app_state = NULL;
static int g_ai_move_delay_ms = 250;

// Forward declarations
static void request_ai_move(AppState* state);
static gboolean check_trigger_ai_idle(gpointer user_data);
static gboolean ai_think_thread_timeout(gpointer user_data);

// --- CvC Orchestration ---

static void on_cvc_control_action(CvCMatchState action, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return;
    
    state->cvc_match_state = action;
    
    // If stopped, just pause the match state (no reset)
    if (action == CVC_STATE_STOPPED) {
         // gamelogic_reset(state->logic); // Removed: Keep board state
         board_widget_refresh(state->board);
    }
    
    // Update Info Panel Status
    // CRITICAL: Set CvC state FIRST to prevent infinite recursion
    // (info_panel_update_status checks state, if mismatch it calls this callback again!)
    info_panel_set_cvc_state(state->info_panel, action);
    info_panel_update_status(state->info_panel);
    
    // Trigger generic idle check
    if (action == CVC_STATE_RUNNING) {
        g_idle_add(check_trigger_ai_idle, state);
    }
}

// --- AI Settings Orchestration ---

static void on_settings_destroyed(GtkWidget *w, gpointer data) {
    (void)w;
    AppState* app = (AppState*)data;
    app->settings_dialog = NULL;
}

static void ensure_settings_dialog(AppState* app) {
    if (!app->settings_dialog) {
        app->settings_dialog = settings_dialog_new(app);
        GtkWindow* w = settings_dialog_get_window(app->settings_dialog);
        if (w) {
            g_signal_connect(w, "destroy", G_CALLBACK(on_settings_destroyed), app);
        }
    }
}

static void open_settings_page(AppState* app, const char* page) {
    ensure_settings_dialog(app);
    if (app->settings_dialog) {
        settings_dialog_open_page(app->settings_dialog, page);
    }
}

static void show_ai_settings_dialog(int tab_index, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (state->ai_dialog) {
        ai_dialog_show_tab(state->ai_dialog, tab_index);
        open_settings_page(state, "ai");
    }
}

static void on_edit_ai_settings_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    open_settings_page(state, "ai");
}

// show_message_dialog moved to tutorial.c

// --- UI Callbacks ---

// Forward declarations
static void request_ai_move(AppState* state);
static void on_puzzle_reset(GtkButton* btn, gpointer user_data);
static void on_puzzle_next(GtkButton* btn, gpointer user_data);

static void refresh_puzzle_list(AppState* state);
// Removed unused forward declaration

// Callback from game logic when state changes (e.g. move made)
static void update_ui_callback(void) {
    if (!g_app_state) return;
    AppState* state = g_app_state;
    
    // Check game over state
    if (g_app_state->logic->isGameOver) {
        // Ensure AI thinking is off
        g_app_state->ai_thinking = FALSE;
        
        // Stop CvC if running
        if (g_app_state->cvc_match_state == CVC_STATE_RUNNING) {
            g_app_state->cvc_match_state = CVC_STATE_STOPPED;
            
            // Re-enable controls immediately
             if (g_app_state->info_panel) {
                // We need to force update controls
                // info_panel_update_controls(g_app_state->info_panel); // If available
            }
        }
    } else {
        // Only trigger AI if game NOT over
        g_idle_add(check_trigger_ai_idle, g_app_state);
    }
    
    // Refresh UI
    if (g_app_state->board) {
        board_widget_refresh(g_app_state->board);
    }
    
    if (g_app_state->info_panel) {
        info_panel_update_status(g_app_state->info_panel);
    }

    // Update Puzzle Status (Turn Indicator)
    if (state->logic->gameMode == GAME_MODE_PUZZLE && !state->puzzle_wait) {
        // Only update if not solved (solved message is static)
        const Puzzle* p = puzzles_get_at(state->current_puzzle_idx);
        if (p && state->puzzle_move_idx < p->solution_length) {
            const char* turn_str = (state->logic->turn == PLAYER_WHITE) ? "Your turn! (White to Move)" : "Your turn! (Black to Move)";
            // We pass NULL for title/desc to avoid overwriting them?
            // info_panel_update_puzzle_info usage: if title is NULL, it's ignored.
            info_panel_update_puzzle_info(state->info_panel, NULL, NULL, turn_str, true);
        }
    }
    
    // Tutorial Check
    if (state->tutorial_step != TUT_OFF) {
        tutorial_check_progress(state);
    }
    
    // Puzzle Check
    if (state->logic->gameMode == GAME_MODE_PUZZLE && !state->puzzle_wait) {
        const Puzzle* puzzle = puzzles_get_at(state->current_puzzle_idx);
        if (!puzzle) return;
        
        // Don't validate if puzzle is already complete
        if (state->puzzle_move_idx >= puzzle->solution_length) {
            return;
        }
        
        // Check if we have a new move to validate
        int current_move_count = gamelogic_get_move_count(state->logic);
        if (current_move_count <= state->puzzle_last_processed_move) {
            return;
        }

        // Get the last move
        Move* last_move = gamelogic_get_last_move(state->logic);
        if (last_move) {
            // Convert to UCI format (e.g., "e2e4" or "e7e8q" for promotion)
            char move_uci[32];  // Large buffer to silence compiler warning
            snprintf(move_uci, sizeof(move_uci), "%c%d%c%d",
                'a' + last_move->startCol, 8 - last_move->startRow,
                'a' + last_move->endCol, 8 - last_move->endRow);
            
            // Check if it matches expected move
            const char* expected = puzzle->solution_moves[state->puzzle_move_idx];
            
            // Debug output
            printf("[PUZZLE DEBUG] Move #%d: Expected='%s', Got='%s'\n", 
                   state->puzzle_move_idx + 1, expected ? expected : "NULL", move_uci);
            
            if (expected && strcmp(move_uci, expected) == 0) {
                // Correct move!
                printf("[PUZZLE DEBUG] ✓ Move is CORRECT!\n");
                
                // Mark this move as processed
                state->puzzle_last_processed_move = current_move_count;
                state->puzzle_move_idx++;
                
                sound_engine_play(SOUND_MOVE); // Play move sound for correct move
                
                // Check if puzzle is complete
                if (state->puzzle_move_idx >= puzzle->solution_length) {
                    printf("[PUZZLE DEBUG] ★ Puzzle SOLVED! (%d/%d moves)\n", 
                           state->puzzle_move_idx, puzzle->solution_length);
                    info_panel_update_puzzle_info(state->info_panel, NULL, NULL, "Puzzle Solved! Great job!", true);
                    sound_engine_play(SOUND_WIN);
                    // Disable board interaction
                    board_widget_set_nav_restricted(state->board, true, -1, -1, -1, -1);
                } else {
                    // Check if next move is opponent's response
                    state->puzzle_wait = true;
                    info_panel_update_puzzle_info(state->info_panel, NULL, NULL, "Correct! Keep going...", false);
                    
                    // Auto-play opponent response after delay
                    // TODO: Implement auto-play of opponent move
                    state->puzzle_wait = false;
                }
            } else {
                // Wrong move - undo it
                printf("[PUZZLE DEBUG] ✗ Move is WRONG! Undoing...\n");
                sound_engine_play(SOUND_ERROR); // Play error sound for wrong move
                gamelogic_undo_move(state->logic);
                board_widget_refresh(state->board);
                info_panel_update_puzzle_info(state->info_panel, NULL, NULL, "Try again! That's not the solution.", true);
                // Do NOT update puzzle_last_processed_move, so the retry (which increments count again) will be processed.
            }
        }
    }
}





// Forward declaration
static gboolean sync_ai_settings_to_panel(gpointer user_data);

static void on_ai_settings_changed(void* user_data) {
    AppState* state = (AppState*)user_data;
    // Reset board only if not in tutorial
    if (state->tutorial_step == TUT_OFF) {
        // If playing against AI, reset the game to apply new difficulty/engine
        if (state->logic->gameMode == GAME_MODE_PVC || state->logic->gameMode == GAME_MODE_CVC) {
             gamelogic_reset(state->logic);
             board_widget_refresh(state->board);
             if (state->info_panel) info_panel_rebuild_layout(state->info_panel);
        }
    }
    // Force immediate sync to panel
    sync_ai_settings_to_panel(state);
}


// --- Puzzle Logic ---

static void on_puzzle_exit(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* state = (AppState*)user_data;
    
    // Exit puzzle mode
    state->logic->gameMode = GAME_MODE_PVC;
    state->current_puzzle_idx = -1;
    state->puzzle_move_idx = 0;
    state->puzzle_wait = false;
    
    // Reset the board to standard position
    gamelogic_reset(state->logic);
    board_widget_refresh(state->board);
    
    // Hide puzzle UI and show standard controls
    info_panel_set_puzzle_mode(state->info_panel, false);
    
    // Update the dropdown to show PvC
    info_panel_set_game_mode(state->info_panel, GAME_MODE_PVC);
    
    info_panel_rebuild_layout(state->info_panel);
}

static void start_puzzle(AppState* state, int puzzle_idx) {
    if (puzzle_idx < 0 || puzzle_idx >= puzzles_get_count()) return;
    
    const Puzzle* puzzle = puzzles_get_at(puzzle_idx);
    if (!puzzle) return;
    
    state->current_puzzle_idx = puzzle_idx;
    state->puzzle_move_idx = 0;
    state->puzzle_last_processed_move = 0;
    state->puzzle_wait = false;
    state->logic->gameMode = GAME_MODE_PUZZLE;
    
    // IMPORTANT: Reset the game state first to clear move history and turn
    gamelogic_reset(state->logic);
    
    // Then load the puzzle's FEN position
    gamelogic_load_fen(state->logic, puzzle->fen);
    board_widget_refresh(state->board);
    
    // Update Info Panel
    info_panel_set_puzzle_mode(state->info_panel, true);
    info_panel_update_puzzle_info(state->info_panel, puzzle->title, puzzle->description, "Your turn! (White to Move)", true);
    
    // Connect callbacks (do this every time to ensure they're connected)
    // Connect callbacks (do this every time to ensure they're connected)
    info_panel_set_puzzle_callbacks(state->info_panel, G_CALLBACK(on_puzzle_reset), G_CALLBACK(on_puzzle_next), state);
    info_panel_set_puzzle_exit_callback(state->info_panel, G_CALLBACK(on_puzzle_exit), state);
    
    // Unlock the board for the new puzzle
    board_widget_set_nav_restricted(state->board, false, -1, -1, -1, -1);
    board_widget_reset_selection(state->board);
    gtk_widget_grab_focus(state->board);

    // Highlight in list
    info_panel_highlight_puzzle(state->info_panel, puzzle_idx);
    
    // Explicitly grab focus to main window and board (fixes Settings focus bug)
    if (state->window) gtk_window_present(state->window);
    if (state->board) gtk_widget_grab_focus(state->board);
}

static void on_puzzle_reset(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* state = (AppState*)user_data;
    start_puzzle(state, state->current_puzzle_idx);
}

static void on_puzzle_next(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* state = (AppState*)user_data;
    int next_idx = (state->current_puzzle_idx + 1) % puzzles_get_count();
    start_puzzle(state, next_idx);
}

static void refresh_puzzle_list(AppState* state) {
    if (!state->info_panel) return;
    info_panel_clear_puzzle_list(state->info_panel);
    for (int i = 0; i < puzzles_get_count(); i++) {
        const Puzzle* p = puzzles_get_at(i);
        if (p) {
            info_panel_add_puzzle_to_list(state->info_panel, p->title, i);
        }
    }
}

typedef struct {
    AppState* state;
    int puzzle_index;
} PendingPuzzleData;

// Idle callback to start puzzle safely outside listbox signal handlers
static gboolean start_puzzle_idle(gpointer user_data) {
    PendingPuzzleData* data = (PendingPuzzleData*)user_data;
    if (data && data->state) {
        start_puzzle(data->state, data->puzzle_index);
    }
    g_free(data);
    return FALSE; 
}

// Unused wrapper removed

// Corrected callback signature for the custom puzzle list callback
static void on_panel_puzzle_selected_safe(int index, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    
    // Use idle add to defer execution to avoid modifying list while handling signal
    PendingPuzzleData* data = g_new0(PendingPuzzleData, 1);
    data->state = state;
    data->puzzle_index = index;
    g_idle_add(start_puzzle_idle, data);
}

#include "puzzle_editor.h"

static void on_puzzles_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    open_settings_page(state, "puzzles");
}

// on_puzzle_select_btn_clicked removed


// Action handler for starting a puzzle by index
static void on_start_puzzle_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action;
    AppState* state = (AppState*)user_data;
    int idx = g_variant_get_int32(parameter);
    start_puzzle(state, idx);
    // Ensure we close settings if open? SettingsDialog handles its own close?
    // If we start a puzzle, we probably want to show the board.
    // So we should close the Settings Dialog.
    if (state->settings_dialog && settings_dialog_get_window(state->settings_dialog)) {
        gtk_widget_set_visible(GTK_WIDGET(settings_dialog_get_window(state->settings_dialog)), FALSE);
    }
}

// on_tutorial_exit moved to tutorial.c

static void on_edit_board_theme(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    open_settings_page(state, "board");
}

static void on_edit_piece_theme(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    open_settings_page(state, "piece");
}

// tutorial_setup functions moved to tutorial.c

// Helper to grab focus after a short delay to ensure dialog is gone


// Wrapper to show popover in timeout
static gboolean popup_popover_delayed(gpointer user_data) {
    GtkPopover* popover = GTK_POPOVER(user_data);
    if (popover && GTK_IS_POPOVER(popover)) {
        gtk_popover_popup(popover);
    }
    return FALSE;
}

// Wrapper to activate action
// Wrapper to activate action
static void activate_tutorial_action(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    AppState* state = (AppState*)user_data;
    if (state) {
       // Call the action handler directly
       on_tutorial_action(NULL, NULL, state);
    }
}

// Callback for when a step instruction dialog is closed (OK accepted)
// static void on_step_instruction_closed(GtkWidget* dialog, int response_id, gpointer user_data) {
//    ... removed ...
// }

// on_tutorial_next and on_tutorial_action moved to tutorial.c

static void on_about_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    open_settings_page(state, "about");
}

static void on_open_settings_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; 
    AppState* state = (AppState*)user_data;
    
    // Check if a specific page was requested via parameter
    if (parameter) {
        const char* req_page = g_variant_get_string(parameter, NULL);
        if (req_page && req_page[0] != '\0') {
            open_settings_page(state, req_page);
            return;
        }
    }
    
    // Default / Last Page Logic (if parameter is empty string or NULL)
    const char* page = "ai"; // Default

    // Validate if last_settings_page is valid
    const char* valid_pages[] = {"ai", "board", "piece", "puzzles", "tutorial", "about"};
    bool is_valid = false;
    for (int i = 0; i < 6; i++) {
        if (state->last_settings_page[0] != '\0' && strcmp(state->last_settings_page, valid_pages[i]) == 0) {
            is_valid = true;
            break;
        }
    }

    if (is_valid) {
        page = state->last_settings_page;
    } else {
        // Clear invalid garbage
        state->last_settings_page[0] = '\0';
    }
    
    open_settings_page(state, page);
}

typedef struct {
    AppState* state;
    char* fen;
    AiDifficultyParams params;
    EngineHandle* engine;
    char* nnue_path;
    bool nnue_enabled;
} AiTaskData;

static gboolean apply_ai_move_idle(gpointer user_data) {
    AiTaskData* data = (AiTaskData*)user_data;
    AppState* state = data->state;
    char* bestmove_str = ai_engine_wait_for_bestmove(data->engine);
    char current_fen[256];
    gamelogic_generate_fen(state->logic, current_fen, sizeof(current_fen));
    if (strcmp(current_fen, data->fen) != 0) {
        if (bestmove_str) ai_engine_free_response(bestmove_str);
        state->ai_thinking = FALSE;
        g_free(data->fen);
        if (data->nnue_path) g_free(data->nnue_path);
        g_free(data);
        return FALSE;
    }
    if (bestmove_str) {
        const char* move_ptr = bestmove_str + 9;
        if (strlen(move_ptr) >= 4) {
            int c1 = move_ptr[0] - 'a', r1 = 8 - (move_ptr[1] - '0');
            int c2 = move_ptr[2] - 'a', r2 = 8 - (move_ptr[3] - '0');
            Move* m = move_create(r1, c1, r2, c2);
            if (strlen(move_ptr) >= 5) {
                switch (move_ptr[4]) {
                    case 'q': m->promotionPiece = PIECE_QUEEN; break;
                    case 'r': m->promotionPiece = PIECE_ROOK; break;
                    case 'b': m->promotionPiece = PIECE_BISHOP; break;
                    case 'n': m->promotionPiece = PIECE_KNIGHT; break;
                }
            }
            board_widget_animate_move(state->board, m);
        }
        ai_engine_free_response(bestmove_str);
    }
    state->ai_thinking = FALSE;
    g_free(data->fen);
    if (data->nnue_path) g_free(data->nnue_path);
    g_free(data);
    return FALSE;
}

static gpointer ai_think_thread(gpointer user_data) {
    AiTaskData* data = (AiTaskData*)user_data;
    ai_engine_send_command(data->engine, "ucinewgame");
    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", data->fen);
    ai_engine_send_command(data->engine, pos_cmd);
    if (data->nnue_enabled && data->nnue_path) {
        ai_engine_set_option(data->engine, "Use NNUE", "true");
        ai_engine_set_option(data->engine, "EvalFile", data->nnue_path);
    }
    if (data->params.skill_level >= 0) {
        char opt_cmd[128];
        snprintf(opt_cmd, sizeof(opt_cmd), "setoption name Skill Level value %d", data->params.skill_level);
        ai_engine_send_command(data->engine, opt_cmd);
    }
    char go_cmd[128];
    if (data->params.depth > 0) snprintf(go_cmd, sizeof(go_cmd), "go depth %d", data->params.depth);
    else snprintf(go_cmd, sizeof(go_cmd), "go movetime %d", data->params.move_time_ms);
    ai_engine_send_command(data->engine, go_cmd);
    g_idle_add(apply_ai_move_idle, data);
    return NULL;
}

static void request_ai_move(AppState* state) {
    if (state->ai_thinking || state->logic->isGameOver) return;
    GameMode mode = gamelogic_get_game_mode(state->logic);
    Player current_turn = gamelogic_get_turn(state->logic);
    if (mode == GAME_MODE_PVP) return;
    if (mode == GAME_MODE_PUZZLE) return; // Don't trigger AI in puzzle mode
    if (mode == GAME_MODE_CVC && state->cvc_match_state != CVC_STATE_RUNNING) return;
    if (mode == GAME_MODE_PVC && !gamelogic_is_computer(state->logic, current_turn)) return;
    
    // CRITICAL: Don't request AI move if board is still animating previous move
    // This prevents race conditions in CvC where logic hasn't updated yet
    if (board_widget_is_animating(state->board)) return;

    state->ai_thinking = TRUE;
    bool is_black = (current_turn == PLAYER_BLACK);
    bool use_custom = info_panel_is_custom_selected(state->info_panel, is_black);
    AiTaskData* data = g_new0(AiTaskData, 1);
    data->state = state;
    data->fen = g_malloc(256);
    gamelogic_generate_fen(state->logic, data->fen, 256);
    if (use_custom) {
        const char* path = ai_dialog_get_custom_path(state->ai_dialog);
        if (!state->custom_engine) state->custom_engine = ai_engine_init_external(path);
        data->engine = state->custom_engine;
    } else {
        if (!state->internal_engine) state->internal_engine = ai_engine_init_internal();
        data->engine = state->internal_engine;
    }
    if (ai_dialog_is_advanced_enabled(state->ai_dialog, use_custom)) {
        data->params.skill_level = -1;
        data->params.depth = ai_dialog_get_depth(state->ai_dialog, use_custom);
        data->params.move_time_ms = ai_dialog_get_movetime(state->ai_dialog, use_custom);
    } else {
        int elo = info_panel_get_elo(state->info_panel, is_black);
        data->params = ai_get_difficulty_params(elo);
    }
    const char* nn_path = ai_dialog_get_nnue_path(state->ai_dialog, &data->nnue_enabled);
    if (nn_path) data->nnue_path = g_strdup(nn_path);
    
    // Use the configurable delay
    g_timeout_add(g_ai_move_delay_ms, (GSourceFunc)ai_think_thread_timeout, data);
}

static gboolean ai_think_thread_timeout(gpointer user_data) {
    g_thread_new("ai-think", ai_think_thread, user_data);
    return FALSE;
}

static gboolean check_trigger_ai_idle(gpointer user_data) {
    request_ai_move((AppState*)user_data);
    return FALSE;
}

static void on_app_shutdown(GApplication* app, gpointer user_data) {
    (void)app;
    AppState* state = (AppState*)user_data;
    if (state) {
        if (state->internal_engine) ai_engine_cleanup(state->internal_engine);
        if (state->custom_engine) ai_engine_cleanup(state->custom_engine);
        if (state->ai_dialog) ai_dialog_free(state->ai_dialog);
        if (state->theme_dialog) board_theme_dialog_free(state->theme_dialog);
        if (state->piece_theme_dialog) piece_theme_dialog_free(state->piece_theme_dialog);
        theme_data_free(state->theme);
        gamelogic_free(state->logic);
        g_free(state);
    }
    puzzles_cleanup(); // Free dynamic puzzles
    sound_engine_cleanup();
    
    if (state && state->settings_timer_id > 0) {
        g_source_remove(state->settings_timer_id);
        state->settings_timer_id = 0;
    }
}

static gboolean sync_ai_settings_to_panel(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->ai_dialog || !state->info_panel) return G_SOURCE_CONTINUE;
    
    // Sync Advanced Mode visibility / Labels
    // Check if Custom Engine is selected for White/Black to fetch correct settings
    bool white_uses_custom = info_panel_is_custom_selected(state->info_panel, false); // false = white
    bool black_uses_custom = info_panel_is_custom_selected(state->info_panel, true);  // true = black

    bool w_adv = ai_dialog_is_advanced_enabled(state->ai_dialog, white_uses_custom);
    int w_depth = ai_dialog_get_depth(state->ai_dialog, white_uses_custom);
    int w_time = ai_dialog_get_movetime(state->ai_dialog, white_uses_custom);
    
    bool b_adv = ai_dialog_is_advanced_enabled(state->ai_dialog, black_uses_custom);
    int b_depth = ai_dialog_get_depth(state->ai_dialog, black_uses_custom); 
    int b_time = ai_dialog_get_movetime(state->ai_dialog, black_uses_custom);
    
    info_panel_update_ai_settings(state->info_panel, w_adv, w_depth, w_time, b_adv, b_depth, b_time);
    
    // Sync Custom Engine Availability
    bool has_custom = ai_dialog_has_valid_custom_engine(state->ai_dialog);
    info_panel_set_custom_available(state->info_panel, has_custom);

    return G_SOURCE_CONTINUE;
}

static void on_main_window_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    AppState* state = (AppState*)user_data;
    
    // Stop Settings Timer Immediately
    if (state->settings_timer_id > 0) {
        g_source_remove(state->settings_timer_id);
        state->settings_timer_id = 0;
    }
    
    // Nullify widget pointers to prevent access in any remaining callbacks
    state->window = NULL;
    state->board = NULL;
    state->info_panel = NULL;
    // Note: ai_dialog/etc might be freed by GTK hierarchy or separate cleanup, 
    // but we shouldn't access them anymore.
}

static void on_app_activate(GtkApplication* app, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    state->window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(state->window, "HAL :) Chess");
    gtk_window_set_default_size(state->window, 1020, 780);
    
    GtkCssProvider* cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(cssProvider,
        "window { border-radius: 12px; }"
        "headerbar { border-radius: 12px 12px 0 0; }"
        ".info-panel { background: #f5f5f5; border-right: 1px solid #ddd; border-radius: 0 0 0 12px; }" 
        ".board-frame { border: 2px solid #333; border-radius: 12px; margin: 10px; }"
        ".capture-box { background: #e0e0e0; border-radius: 4px; padding: 4px; min-height: 32px; }"
        ".undo-button { color: #fff; background: #2196F3; border-radius: 4px; padding: 4px 12px; }"
        ".reset-button { color: #fff; background: #f44336; border-radius: 4px; padding: 4px 12px; }"
        ".suggested-action { background: #1976D2; color: white; }"
        ".destructive-action { background: #D32F2F; color: white; }"
        ".capture-count { font-size: 10px; font-weight: bold; color: #555; margin-left: 2px; }"
    );
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    // Initialize AI Dialog as embedded (View managed by SettingsDialog)
    state->ai_dialog = ai_dialog_new_embedded();
    ai_dialog_set_settings_changed_callback(state->ai_dialog, on_ai_settings_changed, state);
    sound_engine_init();
    state->theme = theme_data_new();

    GtkWidget* header = gtk_header_bar_new();
    // Replacement for Menu: Settings Button
    GtkWidget* settings_btn = gtk_button_new_from_icon_name("open-menu-symbolic");
    gtk_widget_set_tooltip_text(settings_btn, "Settings");
    
    // Register "open-settings" action with string parameter (optional page name)
    // Note: We use G_VARIANT_TYPE_STRING so we can pass specific pages like "piece" from tutorial
    GSimpleAction* act_settings = g_simple_action_new("open-settings", G_VARIANT_TYPE_STRING);
    g_signal_connect(act_settings, "activate", G_CALLBACK(on_open_settings_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_settings));
    
    gtk_actionable_set_action_name(GTK_ACTIONABLE(settings_btn), "app.open-settings");
    // Set default target to empty string (will trigger logic to use last page)
    gtk_actionable_set_action_target(GTK_ACTIONABLE(settings_btn), "s", "");
    
    // Create Dark Mode Toggle Button (UI Only)
    GtkWidget* dark_mode_btn = dark_mode_button_new();
    gtk_widget_set_valign(dark_mode_btn, GTK_ALIGN_CENTER);
    // Pack it next to the settings button (end)
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), dark_mode_btn);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), settings_btn);
    
    // Exit Tutorial Button
    GtkWidget* exit_tut = gtk_button_new_with_label("Exit Tutorial");
    gtk_widget_add_css_class(exit_tut, "destructive-action");
    gtk_widget_set_visible(exit_tut, FALSE);
    g_signal_connect(exit_tut, "clicked", G_CALLBACK(on_tutorial_exit), state);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), exit_tut);
    state->tutorial_exit_btn = exit_tut;
    
    gtk_window_set_titlebar(state->window, header);

    GSimpleAction* act_ai = g_simple_action_new("edit-ai-settings", NULL);
    g_signal_connect(act_ai, "activate", G_CALLBACK(on_edit_ai_settings_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_ai));

    GSimpleAction* act_bt = g_simple_action_new("edit-board-theme", NULL);
    g_signal_connect(act_bt, "activate", G_CALLBACK(on_edit_board_theme), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_bt));

    GSimpleAction* act_pt = g_simple_action_new("edit-piece-theme", NULL);
    g_signal_connect(act_pt, "activate", G_CALLBACK(on_edit_piece_theme), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_pt));

    GSimpleAction* act_about = g_simple_action_new("about", NULL);
    g_signal_connect(act_about, "activate", G_CALLBACK(on_about_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_about));

    GSimpleAction* act_tut = g_simple_action_new("tutorial", NULL);
    g_signal_connect(act_tut, "activate", G_CALLBACK(on_tutorial_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_tut));

    GSimpleAction* act_puzzles = g_simple_action_new("puzzles", NULL);
    g_signal_connect(act_puzzles, "activate", G_CALLBACK(on_puzzles_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_puzzles));
    
    // Register start-puzzle action (with int parameter)
    GSimpleAction* act_start_puzzle = g_simple_action_new("start-puzzle", G_VARIANT_TYPE_INT32);
    g_signal_connect(act_start_puzzle, "activate", G_CALLBACK(on_start_puzzle_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_start_puzzle));
    
    printf("DEBUG: App Activate complete. Actions connected.\n");

    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    state->board = board_widget_new(state->logic);
    board_widget_set_theme(state->board, state->theme);
    
    state->info_panel = info_panel_new(state->logic, state->board, state->theme);
    info_panel_set_cvc_callback(state->info_panel, on_cvc_control_action, state);
    info_panel_set_ai_settings_callback(state->info_panel, (GCallback)show_ai_settings_dialog, state);
    info_panel_set_puzzle_list_callback(state->info_panel, G_CALLBACK(on_panel_puzzle_selected_safe), state);
    refresh_puzzle_list(state);
    gtk_widget_set_size_request(state->info_panel, 300, -1);
    gtk_widget_set_hexpand(state->info_panel, FALSE);
    gtk_box_append(GTK_BOX(main_box), state->info_panel);
    
    GtkWidget* aspect_frame = gtk_aspect_frame_new(0.5, 0.5, 1.0, FALSE);
    gtk_widget_set_hexpand(aspect_frame, TRUE);
    gtk_widget_set_vexpand(aspect_frame, TRUE);
    GtkWidget* board_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(board_container, "board-frame");
    gtk_box_append(GTK_BOX(board_container), state->board);
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(aspect_frame), board_container);
    gtk_box_append(GTK_BOX(main_box), aspect_frame);
    gtk_window_set_child(state->window, main_box);

    // Onboarding Bubble
    gboolean show_onboarding = TRUE; // Could save/load this pref
    if (show_onboarding) {
        GtkPopover* popover = GTK_POPOVER(gtk_popover_new());
        // Parent to header instead of menu button to avoid "children left" warning on button finalization
        // But pointing to the button
        gtk_widget_set_parent(GTK_WIDGET(popover), GTK_WIDGET(header));
        
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_top(box, 12);
        gtk_widget_set_margin_bottom(box, 12);
        gtk_widget_set_margin_start(box, 12);
        gtk_widget_set_margin_end(box, 12);
        
        GtkWidget* lbl = gtk_label_new("New to chess?\nTry the tutorial!");
        gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
        gtk_box_append(GTK_BOX(box), lbl);
        
        // Start Button
        GtkWidget* btn_start = gtk_button_new_with_label("Start Tutorial");
        gtk_widget_add_css_class(btn_start, "suggested-action");
        // Connect actions
        g_signal_connect(btn_start, "clicked", G_CALLBACK(activate_tutorial_action), state);
        g_signal_connect_swapped(btn_start, "clicked", G_CALLBACK(gtk_popover_popdown), popover);
        g_signal_connect_swapped(btn_start, "clicked", G_CALLBACK(gtk_popover_popdown), popover);
        // Ensure focus returns to board (although starting tutorial will likely handle its own focus via dialogs)
        // But for completeness:
        // (Assuming tutorial start shows a dialog, which takes focus, so this might be redundant but safe)
        
        gtk_box_append(GTK_BOX(box), btn_start);
        
        // Close Button
        GtkWidget* btn_close = gtk_button_new_with_label("Close"); // Or use "X"
        // Close popover
        g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_popover_popdown), popover);
        // Grab focus on board
        g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_grab_focus), state->board);
        gtk_box_append(GTK_BOX(box), btn_close);
        
        gtk_popover_set_child(popover, box);
        gtk_popover_set_position(popover, GTK_POS_BOTTOM);
        gtk_popover_set_autohide(popover, TRUE);
        
        // Point to the menu button
        // We need to set pointing_to relative to the parent (header)
        // Since MB is in header, we can try to get its allocation? 
        // Or simpler: Just keep parenting to MB but handle destroy?
        // Actually parenting to header is safer. We won't set pointing_to for now, 
        // it will point to header (top left) or center? 
        // Let's set pointing to the menu button allocation if possible.
        // For simplicity in GTK4, if we want it to point to MB, it's best to be child of MB.
        // But to fix valid warning: we must unparent it before MB is finalized?
        // Let's rely on weak ref or signal to unparent.
        // Or just suppress warning by unparenting on popdown? NO.
        
        // Alternative: Use gtk_widget_set_parent(popover, mb) but add cleanup.
        // Cleanup: g_signal_connect(mb, "destroy", G_CALLBACK(gtk_widget_unparent), popover);
        // But mb destroys children automatically. Why warning?
        // Because GtkMenuButton destroys its internal popover? 
        // Maybe it doesn't like extra children.
        
        // Let's try parenting to header and moving on.
        // If we parent to header, it will pop up relative to header.
        
        // Show after a delay
        g_timeout_add_seconds(1, popup_popover_delayed, popover);
        
        // Clean up popover when header is destroyed to avoid "children left" warning
        g_signal_connect_swapped(header, "destroy", G_CALLBACK(gtk_widget_unparent), popover);
    }

    gamelogic_set_callback(state->logic, update_ui_callback);
    state->settings_timer_id = g_timeout_add(500, sync_ai_settings_to_panel, state);
    
    // Connect destroy signal to stop timers immediately
    g_signal_connect(state->window, "destroy", G_CALLBACK(on_main_window_destroy), state);
    
    gtk_window_present(state->window);
    
    // Ensure board widget has focus on startup
    gtk_widget_grab_focus(state->board);
}
int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("com.hriday.chessc", G_APPLICATION_DEFAULT_FLAGS);
    AppState* state = g_new0(AppState, 1);
    g_app_state = state;
    state->logic = gamelogic_create();
    state->cvc_match_state = CVC_STATE_STOPPED;
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), state);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_app_shutdown), state);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

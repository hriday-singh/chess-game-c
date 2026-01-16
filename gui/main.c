#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "../game/move.h"
#include "config_manager.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "theme_manager.h"
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
#include "ai_engine.h"
#include "types.h"
#include "puzzles.h"
#include "app_state.h"
#include "tutorial.h"
#include "dark_mode_button.h"
#include "ai_controller.h"
#include "right_side_panel.h"
#include "puzzle_controller.h"
#include "replay_controller.h"
#include "clock_widget.h"

static bool debug_mode = true;
static int app_height = 1035;
static int app_width = 1475;

// Globals
#include "history_dialog.h"
static AppState* g_app_state = NULL;

// Forward declarations
static gboolean check_trigger_ai_idle(gpointer user_data);
static gboolean grab_board_focus_idle(gpointer user_data);
static void request_ai_move(AppState* state);
static void on_ai_move_ready(Move* move, gpointer user_data);
static void sync_live_analysis(AppState* state);
static void on_history_clicked(GSimpleAction* action, GVariant* parameter, gpointer user_data);
static void on_start_replay_action(GSimpleAction* action, GVariant* parameter, gpointer user_data);
static void on_exit_replay(GSimpleAction* action, GVariant* parameter, gpointer user_data);
static void record_match_history(AppState* state, const char* reason);

// Rule 3: AI triggered from EXACTLY ONE place
static gboolean check_trigger_ai_idle(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (state) state->ai_trigger_id = 0; // Clear ID as it's running
    request_ai_move(state);
    return FALSE;
}

// Ensure the window is fully mapped before presenting to fix activation issues
static void on_window_mapped_notify(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.window) return;

    // When window is actually mapped, request activation.
    if (gtk_widget_get_mapped(GTK_WIDGET(state->gui.window))) {
        gtk_window_present(state->gui.window);

        // Disconnect so it runs only once
        g_signal_handlers_disconnect_by_func(obj, G_CALLBACK(on_window_mapped_notify), user_data);

        // Now focus a widget inside the active window
        g_idle_add(grab_board_focus_idle, state);
    }
}

// --- CvC Orchestration ---

static void on_cvc_control_action(CvCMatchState action, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return;
    
    state->cvc_match_state = action;
    if (debug_mode) printf("[Main] CvC: State changed to %d\n", action);
    
    // If stopped, just pause the match state (no reset)
    if (action == CVC_STATE_STOPPED) {
         if (state->gui.board) board_widget_refresh(state->gui.board);
         if (debug_mode) printf("[Main] CvC: Match stopped. AI thinking flag reset.\n");
         if (state->ai_controller) ai_controller_stop(state->ai_controller);
         if (state->ai_trigger_id > 0) {
             g_source_remove(state->ai_trigger_id);
             state->ai_trigger_id = 0;
             if (debug_mode) printf("[Main] CvC: AI trigger ID cleared.\n");
         }
    }
    
    // Update Info Panel Status
    // CRITICAL: Set CvC state FIRST to prevent infinite recursion
    // (info_panel_update_status checks state, if mismatch it calls this callback again!)
    if (state->gui.info_panel) info_panel_set_cvc_state(state->gui.info_panel, action);
    if (state->gui.info_panel) info_panel_update_status(state->gui.info_panel);
    
    // Trigger generic idle check
    if (action == CVC_STATE_RUNNING) {
        if (state->ai_trigger_id == 0) {
            state->ai_trigger_id = g_idle_add(check_trigger_ai_idle, state);
            if (debug_mode) printf("[Main] CvC: Match running. AI trigger scheduled.\n");
        }
    } else {
        if (state->ai_controller) ai_controller_stop(state->ai_controller);
    }
}

// --- AI Settings Orchestration ---

static void on_settings_destroyed(GtkWidget *w, gpointer data) {
    (void)w;
    AppState* app = (AppState*)data;
    app->gui.settings_dialog = NULL; // Fixed access
}

static void ensure_settings_dialog(AppState* app) {
    if (!app->gui.settings_dialog) {
        app->gui.settings_dialog = settings_dialog_new(app);
        GtkWindow* w = settings_dialog_get_window(app->gui.settings_dialog);
        if (w) {
            g_signal_connect(w, "destroy", G_CALLBACK(on_settings_destroyed), app);
        }
    }
}

static void open_settings_page(AppState* app, const char* page) {
    ensure_settings_dialog(app);
    if (app->gui.settings_dialog) {
        settings_dialog_open_page(app->gui.settings_dialog, page);
    }
}

static void show_ai_settings_dialog(int tab_index, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (state->gui.ai_dialog) {
        ai_dialog_show_tab(state->gui.ai_dialog, tab_index);
        open_settings_page(state, "ai");
    }
}

static void on_edit_ai_settings_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    open_settings_page(state, "ai");
}

// --- UI Callbacks ---

// Forward declarations
static void request_ai_move(AppState* state);

// Clock Tick Callback
static gboolean clock_tick_callback(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->logic) return G_SOURCE_CONTINUE;
    
    // Only tick if game is not over
    if (state->logic->isGameOver) return G_SOURCE_CONTINUE;

    // CvC Mode: Only tick if RUNNING
    if (state->logic->gameMode == GAME_MODE_CVC && state->cvc_match_state != CVC_STATE_RUNNING) {
        // Skip tick but keep timer alive
        state->logic->clock.last_tick_time = 0; // Avoid huge jump on resume
        return G_SOURCE_CONTINUE;
    }

    ClockState* clock = &state->logic->clock;
    bool flagged = gamelogic_tick_clock(state->logic);
    
    // Update Display
    // Update Display
    if (state->gui.right_side_panel) {
        // Legacy call removed or updated if stats needed
        // right_side_panel_update_clock(state->gui.right_side_panel);
    }
    
    // Update Clock Widgets
    if (state->gui.top_clock && state->gui.bottom_clock) {
        // Determine active side
        Player turn = gamelogic_get_turn(state->logic);
        
        // Orientation-aware updates
        // If flipped (Black at bottom): Top=White, Bottom=Black
        // If normal (White at bottom): Top=Black, Bottom=White
        
        bool flipped = state->gui.board ? board_widget_is_flipped(state->gui.board) : false;
        
        ClockWidget* white_clk = flipped ? state->gui.top_clock : state->gui.bottom_clock;
        ClockWidget* black_clk = flipped ? state->gui.bottom_clock : state->gui.top_clock;
        
        clock_widget_update(white_clk, clock->white_time_ms, clock->initial_time_ms, turn == PLAYER_WHITE);
        clock_widget_update(black_clk, clock->black_time_ms, clock->initial_time_ms, turn == PLAYER_BLACK);
    }
    
    if (flagged) {
        // Handle Game Over
        state->logic->isGameOver = true;
        
        Player loser = clock->flagged_player;
        if (loser == PLAYER_WHITE) {
             snprintf(state->logic->statusMessage, sizeof(state->logic->statusMessage), "Black won on time");
             // Determine sound based on user side if pertinent, or generic
             sound_engine_play(SOUND_WIN); // Generic end sound?
        } else {
             snprintf(state->logic->statusMessage, sizeof(state->logic->statusMessage), "White won on time");
             sound_engine_play(SOUND_WIN);
        }
        
        // Refresh UI to show game over state
        if (state->gui.board) gtk_widget_queue_draw(state->gui.board);
        if (state->gui.info_panel) info_panel_update_status(state->gui.info_panel);
    }
    
    return G_SOURCE_CONTINUE;
}


static void on_right_panel_nav(const char* action, int ply_index, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->logic) return;
    
    if (strcmp(action, "goto_ply") == 0) {
        if (state->is_replaying && state->replay_controller) {
            replay_controller_seek(state->replay_controller, ply_index + 1); // ply_index is 0-based index of move, seek expects target ply count?
            // Wait, logic says ply_index + 1. 
            // ply_index 0 -> Move 1 (White). Count should be 1.
            // ply_index 1 -> Move 1 (Black). Count should be 2.
            // So ply_index + 1 seems correct target ply count.
        } else {
            // Only allow jumping back in history
            while (gamelogic_get_move_count(state->logic) > ply_index + 1) {
                gamelogic_undo_move(state->logic);
            }
        }
    } else if (strcmp(action, "prev") == 0) {
        if (state->is_replaying && state->replay_controller) {
            replay_controller_prev(state->replay_controller);
        } else {
            gamelogic_undo_move(state->logic);
        }
    } else if (strcmp(action, "next") == 0) {
        if (state->is_replaying && state->replay_controller) {
            replay_controller_next(state->replay_controller);
        }
        // Normal mode has no redo
    } else if (strcmp(action, "start") == 0) {
        if (state->is_replaying && state->replay_controller) {
            replay_controller_seek(state->replay_controller, 0); // Goto start
        } else {
            while (gamelogic_get_move_count(state->logic) > 0) {
                gamelogic_undo_move(state->logic);
            }
        }
    }
}

static void record_match_history(AppState* state, const char* reason) {
    if (!state || !state->logic || state->match_saved) return;
    int plies = gamelogic_get_move_count(state->logic);
    // User requested: games that ended with some result OR matches that went over 5 pairs (10 plies)
    if (state->is_replaying) return;
    bool is_result = (strcmp(reason, "Checkmate") == 0 || strcmp(reason, "Stalemate") == 0);
    // Generalize 10-ply (5 pairs) rule for ALL non-result saves (Reset, Shutdown, etc.)
    if (!is_result && plies < 10) return;

    MatchHistoryEntry entry = {0};
    snprintf(entry.id, sizeof(entry.id), "m_%ld", (long)time(NULL));
    entry.timestamp = (int64_t)time(NULL);
    entry.game_mode = (int)state->logic->gameMode;
    
    AppConfig* cfg = config_get();
    
    // Players Metadata
    entry.white.is_ai = gamelogic_is_computer(state->logic, PLAYER_WHITE);
    if (entry.white.is_ai) {
        bool custom = (state->logic->gameMode == GAME_MODE_CVC) ? true : cfg->analysis_use_custom; // Approximation
        entry.white.elo = cfg->int_elo; 
        entry.white.depth = cfg->int_depth;
        entry.white.movetime = cfg->int_movetime;
        entry.white.engine_type = custom ? 1 : 0;
        if (custom) {
            strncpy(entry.white.engine_path, cfg->custom_engine_path, sizeof(entry.white.engine_path) - 1);
            entry.white.engine_path[sizeof(entry.white.engine_path) - 1] = '\0';
        }
    }

    entry.black.is_ai = gamelogic_is_computer(state->logic, PLAYER_BLACK);
    if (entry.black.is_ai) {
        bool custom = (state->logic->gameMode == GAME_MODE_CVC) ? true : cfg->analysis_use_custom;
        entry.black.elo = cfg->custom_elo;
        entry.black.depth = cfg->custom_depth;
        entry.black.movetime = cfg->custom_movetime;
        entry.black.engine_type = custom ? 1 : 0;
        if (custom) {
            strncpy(entry.black.engine_path, cfg->custom_engine_path, sizeof(entry.black.engine_path) - 1);
            entry.black.engine_path[sizeof(entry.black.engine_path) - 1] = '\0';
        }
    }

    // Result Logic
    strncpy(entry.result_reason, reason, sizeof(entry.result_reason)-1);
    if (strcmp(reason, "Checkmate") == 0) {
        // Current turn is the one who just LOST (king is in checkmate)
        strncpy(entry.result, (state->logic->turn == PLAYER_BLACK) ? "1-0" : "0-1", 15);
    } else if (strcmp(reason, "Stalemate") == 0) {
        strncpy(entry.result, "1/2-1/2", 15);
    } else {
        strncpy(entry.result, "*", 15);
    }

    // Start FEN
    snprintf(entry.start_fen, sizeof(entry.start_fen), "%s", state->logic->start_fen);
    
    // UCI Generation
    GString* uci_str = g_string_new("");
    for (int i = 0; i < plies; i++) {
        if (uci_str->len > 0) g_string_append_c(uci_str, ' ');
        Move historical_move = gamelogic_get_move_at(state->logic, i);
        char uci[8];
        move_to_uci(&historical_move, uci);
        g_string_append(uci_str, uci);
    }
    entry.moves_uci = uci_str->str;

    entry.move_count = plies;
    
    // Capture Start FEN
    snprintf(entry.start_fen, sizeof(entry.start_fen), "%s", state->logic->start_fen);
    
    gamelogic_generate_fen(state->logic, entry.final_fen, sizeof(entry.final_fen));

    match_history_add(&entry);
    g_string_free(uci_str, TRUE);
    state->match_saved = true;
}

// CENTRAL DISPATCHER: Orchestrates UI updates and AI triggers
static void update_ui_callback(void) {
    if (!g_app_state) return;
    AppState* state = g_app_state;
    if (!state->gui.board || !state->gui.info_panel || !state->logic) return;
    
    // 1. Logic State Check
    if (state->logic->isGameOver) {
        if (state->ai_controller) ai_controller_stop(state->ai_controller);
        if (state->cvc_match_state == CVC_STATE_RUNNING) {
            state->cvc_match_state = CVC_STATE_STOPPED;
        }

        // AUTO-SAVE Match History on result
        // Note: gamelogic_update_game_state already set isGameOver and statusMessage
        if (!state->match_saved && !state->tutorial.step && state->logic->gameMode != GAME_MODE_PUZZLE) {
            // Use the status message to determine the result type
            const char* status = state->logic->statusMessage;
            if (strstr(status, "Checkmate")) {
                record_match_history(state, "Checkmate");
            } else if (strstr(status, "Stalemate")) {
                record_match_history(state, "Stalemate");
            } else {
                record_match_history(state, "Game Over");
            }
        }
    }
    
    // 2. UI Persistence & Dashboards
    if (state->gui.info_panel) {
        info_panel_update_status(state->gui.info_panel);
    }
    
    // Update Right Side Panel
    if (state->gui.right_side_panel) {
        int count = gamelogic_get_move_count(state->logic);
        if (count > 0) {
            Move m = gamelogic_get_last_move(state->logic);
            char uci[32];
            gamelogic_get_move_uci(state->logic, &m, uci, sizeof(uci));
            int m_num = (count + 1) / 2;
            Player p = (count % 2 == 1) ? PLAYER_WHITE : PLAYER_BLACK;
            // This now handles truncation automatically if we are in the past
            if (!state->is_replaying && state->logic->gameMode != GAME_MODE_PUZZLE && state->tutorial.step == TUT_OFF) {
                if (debug_mode) printf("[Main] update_ui: adding move, count=%d\n", count);
                right_side_panel_add_move(state->gui.right_side_panel, m, m_num, p);
            }
        } else if (count == 0 && !state->is_replaying) {
            if (debug_mode) printf("[Main] update_ui: clearing history, count is 0\n");
            right_side_panel_clear_history(state->gui.right_side_panel);
        }
        
        // Detect move change for rating
        if (count > state->last_move_count) {
            state->last_move_count = count;
            state->match_saved = false; // New move made, any previous "saved" state for this match is potentially stale
        }

        bool is_live_match = !state->logic->isGameOver && !state->tutorial.step && !state->is_replaying;
        // bool in_playing_mode = (state->logic->gameMode == GAME_MODE_PVC || state->logic->gameMode == GAME_MODE_CVC);
        // bool should_hide_nav = is_live_match && in_playing_mode;

        right_side_panel_set_interactive(state->gui.right_side_panel, !is_live_match);
        // right_side_panel_set_nav_visible(state->gui.right_side_panel, !should_hide_nav); // Removed
        
        // Suppress highlight updates during replay (controller handles it)
        if (!state->is_replaying) {
            right_side_panel_highlight_ply(state->gui.right_side_panel, count - 1);
        }
    }

    // 5. Board Grid Refresh
    if (state->gui.board) {
        board_widget_refresh(state->gui.board);
    }
    
    // 6. AI & Match Orchestration
    // Only trigger if not game over and NOT currently animating
    if (!state->logic->isGameOver && !board_widget_is_animating(state->gui.board) && !state->is_replaying) {
        if (state->ai_controller && !ai_controller_is_thinking(state->ai_controller)) {
            // Trigger AI if it's currently a computer turn in PvC or we are in CvC
            GameMode mode = state->logic->gameMode;
            Player turn = state->logic->turn;
            bool is_ai_turn = (mode == GAME_MODE_CVC && state->cvc_match_state == CVC_STATE_RUNNING) ||
                              (mode == GAME_MODE_PVC && gamelogic_is_computer(state->logic, turn));
            
            if (is_ai_turn) {
                if (state->ai_trigger_id == 0) {
                    state->ai_trigger_id = g_idle_add(check_trigger_ai_idle, state);
                }
            }
        }
    }

    // 7. Analysis Re-sync (Always after move/undo)
    sync_live_analysis(state);

    // 8. Extension Logic (Tutorials, Puzzles)
    if (state->tutorial.step != TUT_OFF) {
        tutorial_check_progress(state);
    }
    if (state->logic->gameMode == GAME_MODE_PUZZLE) {
        puzzle_controller_check_move(state);
    }
}

// Forward declaration
static gboolean sync_ai_settings_to_panel(gpointer user_data);

static void on_ai_settings_changed(void* user_data) {
    AppState* state = (AppState*)user_data;
    if (debug_mode) printf("[Main] ConfigManager: AI Settings Changed callback fired.\n");
    
    // Force immediate sync to panel
    sync_live_analysis(state);
    sync_ai_settings_to_panel(state);
}
static void sync_live_analysis(AppState* state) {
    if (!state || !state->ai_controller) return;
    
    AppConfig* cfg = config_get();
    
    // Sync Right Panel Console Visibility & Configuration
    if (state->gui.right_side_panel) {
        right_side_panel_sync_config(state->gui.right_side_panel, cfg);
    }

    if (!cfg || !cfg->enable_live_analysis || (state->logic && state->logic->isGameOver) || state->is_replaying) {
        return;
    }
    
    // Determine engine for analysis
    bool use_custom = cfg->analysis_use_custom;
    const char* custom_path = NULL;
    
    if (use_custom) {
        custom_path = cfg->custom_engine_path;
        if (!custom_path || strlen(custom_path) == 0) {
            use_custom = false;
        }
    }
}

static void on_undo_move(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return;

    if (debug_mode) printf("[Main] Move undone. Invalidating analysis.\n");

    // Clear stale analysis UI state immediately
    if (state->gui.right_side_panel) {
        right_side_panel_set_mate_warning(state->gui.right_side_panel, 0);
    }

    // Force re-analysis of the restored position
    sync_live_analysis(state);
}

// Callback for game reset
static void on_game_reset(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    
    if (state->ai_controller) ai_controller_stop(state->ai_controller);
    state->cvc_match_state = CVC_STATE_STOPPED;
    if (debug_mode) printf("[Main] Game reset. CvC state -> STOPPED\n"); 
    
    // Cancel AI trigger on reset to prevent old moves trigger
    if (state->ai_trigger_id > 0) {
        g_source_remove(state->ai_trigger_id);
        state->ai_trigger_id = 0;
    }
    // Save history if significant plies and game NOT OVER (game over already saved)
    if (!state->match_saved && !state->logic->isGameOver && !state->tutorial.step && state->logic->gameMode != GAME_MODE_PUZZLE && !state->is_replaying) {
        record_match_history(state, "Reset");
    }

    gamelogic_reset(state->logic);
    state->match_saved = false;
    // Sync flip with player side (fix for Play as Black/Random)
    bool flip = (state->logic->playerSide == PLAYER_BLACK);
    board_widget_set_flipped(state->gui.board, flip);
    board_widget_refresh(state->gui.board);
    
    // Clear Analysis UI state
    if (state->gui.right_side_panel) {
        right_side_panel_update_stats(state->gui.right_side_panel, 0.0, false);
        right_side_panel_set_mate_warning(state->gui.right_side_panel, 0);
        right_side_panel_set_hanging_pieces(state->gui.right_side_panel, 0, 0);
        right_side_panel_clear_history(state->gui.right_side_panel);
    }
    sync_live_analysis(state);
    if (state->gui.info_panel) { // Added NULL check
        info_panel_update_status(state->gui.info_panel);
        info_panel_set_cvc_state(state->gui.info_panel, CVC_STATE_STOPPED);
    }
    
    // If in tutorial, reset tutorial state
    if (state->tutorial.step != TUT_OFF) {
        on_tutorial_exit(NULL, state);
    }
    
    // If in puzzle mode, exit it
    if (state->logic->gameMode == GAME_MODE_PUZZLE) {
        puzzle_controller_exit(state);
    }
    
    // Explicitly grab focus to main window and board (fixes Settings focus bug)
    if (state->gui.window) gtk_window_present(state->gui.window);
    if (state->gui.board) gtk_widget_grab_focus(state->gui.board); // Added NULL check
    
    // Note: AI is now triggered centrally via the logic update callback (update_ui_callback)
    // which is called inside gamelogic_reset.
}

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

// Wrapper to show popover in timeout
static gboolean popup_popover_delayed(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (state && state->gui.onboarding_popover && GTK_IS_POPOVER(state->gui.onboarding_popover)) {
        gtk_popover_popup(GTK_POPOVER(state->gui.onboarding_popover));
        
        // Now that it's shown, we can focus the button inside it safely
        GtkWidget* tutorial_btn = (GtkWidget*)g_object_get_data(G_OBJECT(state->gui.window), "tutorial-start-btn");
        if (tutorial_btn) gtk_widget_grab_focus(tutorial_btn);
    }
    // Clear timer ID as it's about to be removed automatically
    if (state) state->onboarding_timer_id = 0;
    return FALSE;
}

// Wrapper to activate action
static void activate_tutorial_action(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    AppState* state = (AppState*)user_data;
    if (state) {
       // Call the action handler directly
       on_tutorial_action(NULL, NULL, state);
    }
}

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

static void on_ai_move_ready(Move* move, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.board || !state->logic || !move) return;
    
    if (debug_mode) {
        printf("[Main] AI: Move Ready. Applying to board.\n");
    }

    // Add visual delay effect is handled in controller
    board_widget_animate_move(state->gui.board, move);
}

static void request_ai_move(AppState* state) {
    if(debug_mode) printf("[Main] AI: Requesting move from system...\n");
    if (!state->ai_controller) return;
    
    if (ai_controller_is_thinking(state->ai_controller)) return;
    if (state->logic->isGameOver) return;
    
    GameMode mode = gamelogic_get_game_mode(state->logic);
    Player current_turn = gamelogic_get_turn(state->logic);
    
    if (mode == GAME_MODE_PUZZLE || mode == GAME_MODE_PVP) return;
    if (mode == GAME_MODE_CVC && state->cvc_match_state != CVC_STATE_RUNNING) return;
    if (mode == GAME_MODE_PVC && !gamelogic_is_computer(state->logic, current_turn)) return;
    if (board_widget_is_animating(state->gui.board)) return;
    
    bool is_black = (current_turn == PLAYER_BLACK);
    bool use_custom = info_panel_is_custom_selected(state->gui.info_panel, is_black);
    
    AiDifficultyParams params;
    if (ai_dialog_is_advanced_enabled(state->gui.ai_dialog, use_custom)) {
        params.depth = ai_dialog_get_depth(state->gui.ai_dialog, use_custom);
        params.move_time_ms = ai_dialog_get_movetime(state->gui.ai_dialog, use_custom);
    } else {
        int elo = info_panel_get_elo(state->gui.info_panel, is_black);
        params = ai_get_difficulty_params(elo);
    }
    
    const char* path = use_custom ? ai_dialog_get_custom_path(state->gui.ai_dialog) : NULL;
    
    ai_controller_request_move(state->ai_controller, use_custom, params, path, on_ai_move_ready, state);
}

static void on_dismiss_onboarding(GtkWidget* btn, gpointer user_data) {
    (void)btn; 
    AppState* state = (AppState*)user_data;
    if (state && state->gui.onboarding_popover) {
        gtk_popover_popdown(GTK_POPOVER(state->gui.onboarding_popover));
    }
    // Update config
    AppConfig* cfg = config_get();
    if (cfg) {
        cfg->show_tutorial_dialog = false;
        config_save();
    }
}

static void on_app_shutdown(GApplication* app, gpointer user_data) {
    config_save(); // Persist any pending changes (e.g. Dark Mode)
    (void)app;
    AppState* state = (AppState*)user_data;
    if (state) {
        // Critical Fix: Explicitly destroy settings dialog FIRST while state is valid.
        if (state->gui.settings_dialog) {
            GtkWindow* w = settings_dialog_get_window(state->gui.settings_dialog);
            if (w) gtk_window_destroy(w);
        }
        if (state->gui.history_dialog) {
            GtkWindow* w = history_dialog_get_window(state->gui.history_dialog);
            if (w) gtk_window_destroy(w);
        }

        // Stop all timers explicitly
        if (state->settings_timer_id > 0) {
            g_source_remove(state->settings_timer_id);
            state->settings_timer_id = 0;
        }
        if (state->onboarding_timer_id > 0) {
            g_source_remove(state->onboarding_timer_id);
            state->onboarding_timer_id = 0;
        }
        if (state->ai_trigger_id > 0) {
            g_source_remove(state->ai_trigger_id);
            state->ai_trigger_id = 0;
        }

        if (state->ai_controller) ai_controller_free(state->ai_controller);
        if (state->replay_controller) replay_controller_free(state->replay_controller);
        
        // Final background save attempt if window closed mid-game
        if (!state->match_saved && !state->tutorial.step && state->logic->gameMode != GAME_MODE_PUZZLE) {
            record_match_history(state, "App Shutdown");
        }

        if (state->gui.right_side_panel) right_side_panel_free(state->gui.right_side_panel);
        
        if (state->gui.ai_dialog) ai_dialog_free(state->gui.ai_dialog);
        if (state->gui.theme_dialog) board_theme_dialog_free(state->gui.theme_dialog);
        if (state->gui.piece_theme_dialog) piece_theme_dialog_free(state->gui.piece_theme_dialog);
        theme_data_free(state->theme);
        gamelogic_free(state->logic);
        g_free(state);
        g_app_state = NULL; // CRITICAL: Prevent callbacks from accessing freed state
    }
    puzzles_cleanup(); // Free dynamic puzzles
    sound_engine_cleanup();
}

static gboolean sync_ai_settings_to_panel(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.ai_dialog || !state->gui.info_panel) return G_SOURCE_CONTINUE;
    
    // Sync Advanced Mode visibility / Labels
    // Check if Custom Engine is selected for White/Black to fetch correct settings
    bool white_uses_custom = info_panel_is_custom_selected(state->gui.info_panel, false); // false = white
    bool black_uses_custom = info_panel_is_custom_selected(state->gui.info_panel, true);  // true = black

    bool w_adv = ai_dialog_is_advanced_enabled(state->gui.ai_dialog, white_uses_custom);
    int w_depth = ai_dialog_get_depth(state->gui.ai_dialog, white_uses_custom);
    int w_time = ai_dialog_get_movetime(state->gui.ai_dialog, white_uses_custom);
    
    bool b_adv = ai_dialog_is_advanced_enabled(state->gui.ai_dialog, black_uses_custom);
    int b_depth = ai_dialog_get_depth(state->gui.ai_dialog, black_uses_custom); 
    int b_time = ai_dialog_get_movetime(state->gui.ai_dialog, black_uses_custom);
    
    info_panel_update_ai_settings(state->gui.info_panel, w_adv, w_depth, w_time, b_adv, b_depth, b_time);
    
    // Sync Custom Engine Availability
    bool has_custom = ai_dialog_has_valid_custom_engine(state->gui.ai_dialog);
    info_panel_set_custom_available(state->gui.info_panel, has_custom);

    return G_SOURCE_CONTINUE;
}

static gboolean on_window_close_request(GtkWindow* window, gpointer user_data) {
    (void)window;
    AppState* state = (AppState*)user_data;
    
    // Stop Settings Timer Immediately
    if (state->settings_timer_id > 0) {
        g_source_remove(state->settings_timer_id);
        state->settings_timer_id = 0;
    }

    // Stop Onboarding Popover Timer
    if (state->onboarding_timer_id > 0) {
        g_source_remove(state->onboarding_timer_id);
        state->onboarding_timer_id = 0;
    }

    // Explicitly unparent popover cleanly BEFORE destruction
    if (state->gui.onboarding_popover) {
        if (GTK_IS_POPOVER(state->gui.onboarding_popover)) {
            gtk_popover_popdown(GTK_POPOVER(state->gui.onboarding_popover));
        }
        // Force unparent now to ensure it disconnects from the window structure
        gtk_widget_unparent(state->gui.onboarding_popover);
        state->gui.onboarding_popover = NULL;
    }

    // Destroy Settings Dialog if open, ensuring it cleans up its shared state refs
    if (state->gui.settings_dialog) {
        GtkWindow* w = settings_dialog_get_window(state->gui.settings_dialog);
        if (w) gtk_window_destroy(w);
        // The destroy signal handler will nullify state->gui.settings_dialog
        // But we can enable extra safety:
        state->gui.settings_dialog = NULL; 
    }

    // Destroy History Dialog if open
    if (state->gui.history_dialog) {
        GtkWindow* w = history_dialog_get_window(state->gui.history_dialog);
        if (w) gtk_window_destroy(w);
        state->gui.history_dialog = NULL;
    }

    // Cancel AI Trigger
    if (state->ai_trigger_id > 0) {
        g_source_remove(state->ai_trigger_id);
        state->ai_trigger_id = 0;
    }
    
    // Nullify widget pointers
    state->gui.window = NULL;
    state->gui.board = NULL;
    state->gui.info_panel = NULL;
    
    return FALSE; // Allow close to proceed
}

// Callback from BoardWidget (before human move execution)
static void on_board_before_move(const char* move_uci, void* user_data) {
    (void)move_uci;
    AppState* state = (AppState*)user_data;
    if (state && state->ai_controller && state->logic) {
    }
}

// --- History Dialog Callbacks ---
// Helper to grab focus after dialog is fully destroyed
static gboolean restore_board_focus_idle(gpointer user_data) {
    AppState* app = (AppState*)user_data;
    if (app && app->gui.window) {
        gtk_window_present(app->gui.window); // Ensure main window is active
        if (app->gui.board) {
            gtk_widget_grab_focus(app->gui.board);
        }
    }
    return FALSE;
}

static void on_history_dialog_destroyed(GtkWidget *w, gpointer data) {
    (void)w;
    AppState* app = (AppState*)data;
    app->gui.history_dialog = NULL;
    
    // Schedule focus restoration for next idle cycle
    // This allows the window manager to finish closing the modal dialog
    g_idle_add(restore_board_focus_idle, app);
}

static void on_history_clicked(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;

    if (!state->gui.history_dialog) {
        state->gui.history_dialog = history_dialog_new(state->gui.window);
        GtkWindow* w = history_dialog_get_window(state->gui.history_dialog);
        if (w) {
            g_signal_connect(w, "destroy", G_CALLBACK(on_history_dialog_destroyed), state);
            history_dialog_set_replay_callback(state->gui.history_dialog, G_CALLBACK(on_start_replay_action), state);
        }
    }
    history_dialog_show(state->gui.history_dialog);
}

static void on_start_replay_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action;
    AppState* state = (AppState*)user_data;
    if (!state || !state->logic || !parameter) return;

    // Get the match ID from the parameter
    const char* match_id = g_variant_get_string(parameter, NULL);
    if (!match_id) return;

    MatchHistoryEntry* entry = match_history_find_by_id(match_id);
    if (!entry) {
        g_warning("Match with ID %s not found for replay.", match_id);
        return;
    }

    // 1. Save current game state before reset
    state->pre_replay_mode = state->logic->gameMode;
    state->pre_replay_side = state->logic->playerSide;

    // 2. Reset current game state
    on_game_reset(state); 

    // Initialize Replay Controller
    if (state->replay_controller) {
        replay_controller_free(state->replay_controller);
    }
    state->replay_controller = replay_controller_new(state->logic, state);
    
    // Wire up UI
    replay_controller_enter_replay_mode(state->replay_controller);
    
    // Initial Seek to Start (Ply 0)
    replay_controller_seek(state->replay_controller, 0);
    if (state->gui.info_panel) {
        info_panel_show_replay_controls(state->gui.info_panel, TRUE);
    }
    
    // 4. Load Match (Controller will trigger UI updates which now find initialized widgets)
    state->is_replaying = true;
    state->replay_match_id = g_strdup(match_id);
    replay_controller_load_match(state->replay_controller, entry->moves_uci, entry->start_fen);
    
    // 5. Update Side Panel visuals
    if (state->gui.right_side_panel) {
        right_side_panel_set_analysis_visible(state->gui.right_side_panel, FALSE);
    }

    // Disable board interaction and clear any selection
    if (state->ai_controller) ai_controller_stop(state->ai_controller);
    board_widget_reset_selection(state->gui.board);
    board_widget_set_interactive(state->gui.board, FALSE);

    // Close history dialog
    if (state->gui.history_dialog) {
        gtk_window_destroy(history_dialog_get_window(state->gui.history_dialog));
    }
    
    if (debug_mode) printf("[Main] Started replay for match ID: %s\n", match_id);
    
    // Ensure board has focus so arrow keys work immediately
    if (state->gui.board) {
        gtk_widget_grab_focus(state->gui.board);
    }
}

// Bridge callback for InfoPanel exit button
static void on_replay_exit_requested(gpointer user_data) {
    on_exit_replay(NULL, NULL, user_data);
}

static void on_exit_replay(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    if (!state) return;

    // cleanup calls
    if (state->replay_controller) {
        replay_controller_exit(state->replay_controller);
    }
    g_free(state->replay_match_id);
    state->replay_match_id = NULL;

    if (state->gui.info_panel) {
        info_panel_show_replay_controls(state->gui.info_panel, FALSE);
    }

    // Mark match as saved to prevent on_game_reset from saving the replay
    state->match_saved = true;

    // Clear yellow highlights from replay
    board_widget_set_last_move(state->gui.board, -1, -1, -1, -1);

    // Reset game to initial state
    on_game_reset(state);

    // Restore Game Mode & Side
    state->logic->gameMode = state->pre_replay_mode;
    state->logic->playerSide = state->pre_replay_side;
    
    // Sync UI with restored state
    board_widget_set_flipped(state->gui.board, state->logic->playerSide == PLAYER_BLACK);
    board_widget_refresh(state->gui.board);

    // Restore UI to normal mode
    board_widget_set_interactive(state->gui.board, TRUE); // Re-enable board interaction

    if (state->gui.right_side_panel) {
        right_side_panel_set_analysis_visible(state->gui.right_side_panel, TRUE);
        right_side_panel_clear_history(state->gui.right_side_panel);
    }
    
    // Trigger AI if restored mode requires it (PvC computer turn)
    if (state->logic->gameMode == GAME_MODE_PVC && gamelogic_is_computer(state->logic, state->logic->turn)) {
         request_ai_move(state); 
    }

    // Now safe to clear replay flag
    state->is_replaying = false;
    
    if (debug_mode) printf("[Main] Exited replay mode.\n");
}


static void on_app_activate(GtkApplication* app, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    state->gui.window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(state->gui.window, "HAL :) Chess");
    gtk_window_set_default_size(state->gui.window, app_width, app_height);
    
    gtk_window_set_default_size(state->gui.window, app_width, app_height);
    
    config_set_app_param("HAL Chess");
    config_init(); // Initialize config from persistent storage
    
    // Apply saved config to Theme Manager
    AppConfig* cfg = config_get();
    
    // Init match history
    match_history_init();

    if (cfg) {
        theme_manager_set_dark(cfg->is_dark_mode);
        if (cfg->theme[0] != '\0' && strcmp(cfg->theme, "default") != 0) {
             theme_manager_set_theme_id(cfg->theme);
        }
    }
    
    theme_manager_init(); // Initialize global theme manager
    
    // Initialize AI Dialog as embedded (View managed by SettingsDialog)
    state->gui.ai_dialog = ai_dialog_new_embedded();
    if (cfg) ai_dialog_load_config(state->gui.ai_dialog, cfg);

    ai_dialog_set_settings_changed_callback(state->gui.ai_dialog, on_ai_settings_changed, state);
    sound_engine_init();
    state->theme = theme_data_new();
    if (cfg) theme_data_load_config(state->theme, cfg);
    
    state->ai_controller = ai_controller_new(state->logic, state->gui.ai_dialog);

    GtkWidget* header = gtk_header_bar_new();
    // Replacement for Menu: Settings Button
    GtkWidget* settings_btn = gtk_button_new_from_icon_name("open-menu-symbolic");
    gtk_widget_add_css_class(settings_btn, "header-button");
    gtk_widget_set_tooltip_text(settings_btn, "Settings");
    state->gui.settings_btn = settings_btn; // Store reference
    
    // Register "open-settings" action with string parameter (optional page name)
    // Note: We use G_VARIANT_TYPE_STRING so we can pass specific pages like "piece" from tutorial
    GSimpleAction* act_settings = g_simple_action_new("open-settings", G_VARIANT_TYPE_STRING);
    g_signal_connect(act_settings, "activate", G_CALLBACK(on_open_settings_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_settings));
    
    gtk_actionable_set_action_name(GTK_ACTIONABLE(settings_btn), "app.open-settings");
    // Set default target to empty string (will trigger logic to use last page)
    gtk_actionable_set_action_target(GTK_ACTIONABLE(settings_btn), "s", "");
    
    // Create Dark Mode Toggle Button
    GtkWidget* dark_mode_btn = dark_mode_button_new();
    gtk_widget_set_valign(dark_mode_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_focusable(dark_mode_btn, FALSE);
    state->gui.dark_mode_btn = dark_mode_btn; // Store reference

    // History Button
    GtkWidget* history_btn = gtk_button_new_from_icon_name("document-open-recent-symbolic");
    gtk_widget_set_valign(history_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(history_btn, "Game History");
    // Ensure it uses the transparent header button class
    gtk_widget_add_css_class(history_btn, "header-button");
    state->gui.history_btn = history_btn;

    // Exit Replay Button (Initially Hidden)
    GtkWidget* exit_replay_btn = gtk_button_new_with_label("Exit Replay");
    gtk_widget_add_css_class(exit_replay_btn, "destructive-action");
    gtk_widget_set_valign(exit_replay_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(exit_replay_btn, FALSE);
    state->gui.exit_replay_btn = exit_replay_btn;

    // Pack buttons: [Exit Replay] [History] [Dark Mode] [Settings]
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), settings_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), dark_mode_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), history_btn);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), exit_replay_btn);
    
    gtk_window_set_titlebar(state->gui.window, header);

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

    GSimpleAction* act_puzzles = g_simple_action_new("open-puzzles", NULL);
    g_signal_connect(act_puzzles, "activate", G_CALLBACK(on_puzzles_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_puzzles));
    
    GSimpleAction* act_history = g_simple_action_new("open-history", NULL);
    g_signal_connect(act_history, "activate", G_CALLBACK(on_history_clicked), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_history));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(history_btn), "app.open-history");

    GSimpleAction* act_exit_replay = g_simple_action_new("exit-replay", NULL);
    g_signal_connect(act_exit_replay, "activate", G_CALLBACK(on_exit_replay), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_exit_replay));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(exit_replay_btn), "app.exit-replay");

    GSimpleAction* act_start_puzzle = g_simple_action_new("start-puzzle", G_VARIANT_TYPE_INT32);
    g_signal_connect(act_start_puzzle, "activate", G_CALLBACK(on_start_puzzle_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_start_puzzle));

    GSimpleAction* act_start_replay = g_simple_action_new("start-replay", G_VARIANT_TYPE_STRING);
    g_signal_connect(act_start_replay, "activate", G_CALLBACK(on_start_replay_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_start_replay));
    
    printf("[Main] DEBUG: App Activate complete. Actions connected.\n");

    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    
    if (debug_mode) printf("[Main] Creating BoardWidget with logic %p\n", (void*)state->logic);
    state->gui.board = board_widget_new(state->logic);
    
    // Register Pre-Move Callback for Rating Accuracy
    board_widget_set_pre_move_callback(state->gui.board, on_board_before_move, state);
    
    board_widget_set_theme(state->gui.board, state->theme);
    
    state->gui.info_panel = info_panel_new(state->logic, state->gui.board, state->theme);
    g_object_set_data(G_OBJECT(state->gui.info_panel), "app_state", state); // Attach State for Replay UI callbacks
    info_panel_set_cvc_callback(state->gui.info_panel, on_cvc_control_action, state);
    info_panel_set_ai_settings_callback(state->gui.info_panel, (GCallback)show_ai_settings_dialog, state);
    info_panel_set_puzzle_list_callback(state->gui.info_panel, G_CALLBACK(on_panel_puzzle_selected_safe), state);
    
    // Set game reset callback to trigger AI after reset/side changes
    info_panel_set_game_reset_callback(state->gui.info_panel, on_game_reset, state);
    
    // Connect tutorial callbacks to info panel (Moved here to ensure panel exists)
    info_panel_set_tutorial_callbacks(state->gui.info_panel, G_CALLBACK(tutorial_reset_step), G_CALLBACK(on_tutorial_exit), state);
    
    // Connect undo callback to handle analysis state
    info_panel_set_undo_callback(state->gui.info_panel, on_undo_move, state);
    
    // Connect Replay Exit Callback
    info_panel_set_replay_exit_callback(state->gui.info_panel, G_CALLBACK(on_replay_exit_requested), state);
    
    puzzle_controller_refresh_list(state);
    gtk_widget_set_size_request(state->gui.info_panel, 290, -1);
    gtk_widget_set_hexpand(state->gui.info_panel, FALSE);
    gtk_box_append(GTK_BOX(main_box), state->gui.info_panel);
    
    // Board Area (Center)
    GtkWidget* board_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); 
    gtk_widget_set_hexpand(board_area, TRUE);
    gtk_widget_set_vexpand(board_area, TRUE);
    // Reduced side margins from 20 to 8
    gtk_widget_set_margin_start(board_area, 8);
    gtk_widget_set_margin_end(board_area, 8);

    // Setup Clocks
    state->gui.top_clock = clock_widget_new(PLAYER_BLACK);
    state->gui.bottom_clock = clock_widget_new(PLAYER_WHITE);

    // Top Clock Row
    GtkWidget* top_clk = clock_widget_get_widget(state->gui.top_clock);
    gtk_widget_set_margin_top(top_clk, 12);     // Padding from window top/header
    gtk_widget_set_margin_bottom(top_clk, 0);   // Lose bottom padding (touch board)
    gtk_widget_set_halign(top_clk, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(board_area), top_clk);

    // Aspect Frame - Keeps board square
    GtkWidget* board_aspect = gtk_aspect_frame_new(0.5, 0.5, 1.0, FALSE);
    gtk_widget_set_hexpand(board_aspect, TRUE);
    gtk_widget_set_vexpand(board_aspect, TRUE);
    
    gtk_widget_add_css_class(state->gui.board, "board-frame");
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(board_aspect), state->gui.board);
    gtk_box_append(GTK_BOX(board_area), board_aspect);
    
    // Bottom Clock Row
    GtkWidget* bot_clk = clock_widget_get_widget(state->gui.bottom_clock);
    gtk_widget_set_margin_top(bot_clk, 0);      // Lose top padding (touch board)
    gtk_widget_set_margin_bottom(bot_clk, 12);  // Padding from window bottom
    gtk_widget_set_halign(bot_clk, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(board_area), bot_clk);
    
    gtk_box_append(GTK_BOX(main_box), board_area);

    // Right Side Panel
    if (debug_mode) printf("[Main] Creating RightSidePanel with logic %p\n", (void*)state->logic);
    state->gui.right_side_panel = right_side_panel_new(state->logic, state->theme);
    right_side_panel_set_nav_callback(state->gui.right_side_panel, on_right_panel_nav, state);
    gtk_box_append(GTK_BOX(main_box), right_side_panel_get_widget(state->gui.right_side_panel));

    gtk_window_set_child(state->gui.window, main_box);

    // Onboarding Bubble
    gboolean show_onboarding = cfg ? cfg->show_tutorial_dialog : TRUE;
    if (show_onboarding) {
        GtkPopover* popover = GTK_POPOVER(gtk_popover_new());
        gtk_popover_set_has_arrow(popover, FALSE);
        gtk_widget_set_parent(GTK_WIDGET(popover), GTK_WIDGET(header));
        
        GtkWidget* main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(main_vbox, 16);
        gtk_widget_set_margin_bottom(main_vbox, 16);
        gtk_widget_set_margin_start(main_vbox, 16);
        gtk_widget_set_margin_end(main_vbox, 16);
        
        GtkWidget* lbl = gtk_label_new("New to chess?\nTry the tutorial!");
        gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
        gtk_box_append(GTK_BOX(main_vbox), lbl);
        
        // Start Button
        GtkWidget* btn_start = gtk_button_new_with_label("Start Tutorial");
        gtk_widget_add_css_class(btn_start, "suggested-action");
        // Connect actions
        g_signal_connect(btn_start, "clicked", G_CALLBACK(activate_tutorial_action), state);
        g_signal_connect(btn_start, "clicked", G_CALLBACK(on_dismiss_onboarding), state);
        
        // Store button reference for focus grabbing on startup
        g_object_set_data(G_OBJECT(state->gui.window), "tutorial-start-btn", btn_start);
        
        gtk_box_append(GTK_BOX(main_vbox), btn_start);
        
        // Close Button
        GtkWidget* btn_close = gtk_button_new_with_label("Close");
        g_signal_connect(btn_close, "clicked", G_CALLBACK(on_dismiss_onboarding), state);
        g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_grab_focus), state->gui.board);
        gtk_box_append(GTK_BOX(main_vbox), btn_close);
        
        gtk_popover_set_child(popover, main_vbox);
        gtk_popover_set_position(popover, GTK_POS_BOTTOM);
        gtk_popover_set_autohide(popover, TRUE);
        
        // Show after a delay - GTK will handle cleanup when header is destroyed
        // Store ID to cancel on destroy
        state->gui.onboarding_popover = GTK_WIDGET(popover);
        state->onboarding_timer_id = g_timeout_add_seconds(1, popup_popover_delayed, state);
    }


    gamelogic_set_callback(state->logic, update_ui_callback);
    state->settings_timer_id = g_timeout_add(500, sync_ai_settings_to_panel, state);
    
    // Initial analysis sync
    sync_live_analysis(state);
    
    // Initial side/state sync (Fix for "Play as Black" on startup)
    on_game_reset(state);
    
    // Connect close-request signal to cleanup BEFORE destruction
    g_signal_connect(state->gui.window, "close-request", G_CALLBACK(on_window_close_request), state);
    
    // Enable focus visibility
    gtk_window_set_focus_visible(state->gui.window, TRUE);
    
    // Present window only when mapped to ensure correct activation
    g_signal_connect(state->gui.window, "notify::mapped", G_CALLBACK(on_window_mapped_notify), state);
    // Verify initial clock state
    if (debug_mode) printf("[Main] Initializing Clock...\n");
    // Clock tick timer (100ms precision)
    g_timeout_add(100, clock_tick_callback, state);

    gtk_widget_set_visible(GTK_WIDGET(state->gui.window), TRUE);
}

// Helper to grab focus after window is fully realized
static gboolean grab_board_focus_idle(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return FALSE;

    // Default to board focus for standard keyboard navigation
    if (state->gui.board) gtk_widget_grab_focus(state->gui.board);
    return FALSE;
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

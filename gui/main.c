#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "../game/move.h"
#include "config_manager.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>
#include <stdlib.h>
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
#include "gui_utils.h"
#include "splash_screen.h"

static bool debug_mode = false;
static int app_ratio_height = 1075;
static int app_ratio_width = 1560;

// HELPER: Calculate ideal height based on width (60% board + clocks + padding)
static int gui_layout_calculate_target_height(int width) {
    // Current design: board is 60% of total width.
    // To keep it square, board_h = board_w = width * 0.6.
    // Clock 'bits' add ~15% overhead to the board height.
    double aspect_factor = (double)app_ratio_height / app_ratio_width; // ~0.689
    return (int)(width * aspect_factor);
}

// Starting size for 720p screen with 70% as failsafe
static int app_height = 490;
static int app_width = 682;

static void on_window_default_width_notify(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.window) return;

    // We only enforce ratio if not in fullscreen/maximized
    if (gtk_window_is_maximized(GTK_WINDOW(state->gui.window)) || 
        gtk_window_is_fullscreen(GTK_WINDOW(state->gui.window))) return;

    int width = 0, height = 0;
    gtk_window_get_default_size(GTK_WINDOW(state->gui.window), &width, &height);
    
    int target_h = gui_layout_calculate_target_height(width);
    
    // Use a small static guard to prevent recursive loops if GTK triggers notifies immediately
    static bool in_rescale = false;
    if (in_rescale) return;
    
    if (abs(height - target_h) > 15) {
        in_rescale = true;
        gtk_window_set_default_size(GTK_WINDOW(state->gui.window), width, target_h);
        in_rescale = false;
    }
}

// Globals
#include "history_dialog.h"
AppState* g_app_state = NULL;

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

    // Do NOT tick clock during Replay Mode
    if (state->is_replaying) return G_SOURCE_CONTINUE;

    // CvC Mode: Only tick if RUNNING
    if (state->logic->gameMode == GAME_MODE_CVC && state->cvc_match_state != CVC_STATE_RUNNING) {
        // Skip tick but keep timer alive
        state->logic->clock.last_tick_time = 0; // Avoid huge jump on resume
        return G_SOURCE_CONTINUE;
    }

    ClockState* clock = &state->logic->clock;
    bool flagged = gamelogic_tick_clock(state->logic);
    
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
            replay_controller_prev(state->replay_controller, false);  // Manual navigation
        } else {
            gamelogic_undo_move(state->logic);
        }
    } else if (strcmp(action, "next") == 0) {
        if (state->is_replaying && state->replay_controller) {
            replay_controller_next(state->replay_controller, false);  // Manual navigation
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
    bool is_result = (strcmp(reason, "Checkmate") == 0 || strcmp(reason, "Stalemate") == 0 || strcmp(reason, "Timeout") == 0);
    // Generalize 10-ply (5 pairs) rule for ALL non-result saves (Reset, Shutdown, etc.)
    if (!is_result && plies < 10) return;

    MatchHistoryEntry entry = {0};
    snprintf(entry.id, sizeof(entry.id), "m_%ld", (long)time(NULL));
    entry.timestamp = (int64_t)time(NULL);
    // New Timestamps
    entry.created_at_ms = state->logic->created_at_ms;
    entry.started_at_ms = state->logic->started_at_ms;
    entry.ended_at_ms = (int64_t)time(NULL) * 1000;

    entry.game_mode = (int)state->logic->gameMode;
    
    // Clock Snapshot
    entry.clock.enabled = state->logic->clock.enabled;
    entry.clock.initial_ms = state->logic->clock_initial_ms;     // Saved from config setting
    entry.clock.increment_ms = state->logic->clock_increment_ms; // Saved from config setting
    
    AppConfig* cfg = config_get();
    
    // Players Metadata
    entry.white.is_ai = gamelogic_is_computer(state->logic, PLAYER_WHITE);
    if (entry.white.is_ai) {
        bool custom = (state->logic->gameMode == GAME_MODE_CVC) ? true : cfg->analysis_use_custom; // Approximation
        entry.white.elo = cfg->int_elo; 
        entry.white.depth = cfg->int_depth;
        entry.white.engine_type = custom ? 1 : 0;
        if (custom) {
            snprintf(entry.white.engine_path, sizeof(entry.white.engine_path), "%s", cfg->custom_engine_path);
        }
    }

    entry.black.is_ai = gamelogic_is_computer(state->logic, PLAYER_BLACK);
    if (entry.black.is_ai) {
        bool custom = (state->logic->gameMode == GAME_MODE_CVC) ? true : cfg->analysis_use_custom;
        entry.black.elo = cfg->custom_elo;
        entry.black.depth = cfg->custom_depth;
        entry.black.engine_type = custom ? 1 : 0;
        if (custom) {
            snprintf(entry.black.engine_path, sizeof(entry.black.engine_path), "%s", cfg->custom_engine_path);
        }
    }

    // Result Logic
    snprintf(entry.result_reason, sizeof(entry.result_reason), "%s", reason);
    if (strcmp(reason, "Checkmate") == 0) {
        // Current turn is the one who just LOST (king is in checkmate)
        snprintf(entry.result, sizeof(entry.result), "%s", (state->logic->turn == PLAYER_BLACK) ? "1-0" : "0-1");
    } else if (strcmp(reason, "Timeout") == 0) {
        // Flagged player is the loser
        snprintf(entry.result, sizeof(entry.result), "%s", (state->logic->clock.flagged_player == PLAYER_BLACK) ? "1-0" : "0-1");
    } else if (strcmp(reason, "Stalemate") == 0) {
        snprintf(entry.result, sizeof(entry.result), "%s", "1/2-1/2");
    } else {
        snprintf(entry.result, sizeof(entry.result), "%s", "*");
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

    // Think Times
    if (state->logic->think_time_count > 0 && state->logic->think_times) {
        // Allocate space for copy
        entry.think_time_count = state->logic->think_time_count;
        entry.think_time_ms = malloc(entry.think_time_count * sizeof(int));
        if (entry.think_time_ms) {
            memcpy(entry.think_time_ms, state->logic->think_times, entry.think_time_count * sizeof(int));
        }
    }

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
            } else if (strstr(status, "on time")) {
                record_match_history(state, "Timeout");
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
            // Only invalidate save status if the game is still ongoing.
            // If the game ended, the auto-save logic (above) handled it, and we shouldn't revert that.
            if (!state->logic->isGameOver) {
                state->match_saved = false;
            }
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
        
        // Sync Last Move Highlight
        int count = gamelogic_get_move_count(state->logic);
        if (count > 0) {
            Move m = gamelogic_get_last_move(state->logic);
            // Assuming get_last_move returns valid move
            int from_r = m.from_sq / 8;
            int from_c = m.from_sq % 8;
            int to_r   = m.to_sq / 8;
            int to_c   = m.to_sq % 8;
            board_widget_set_last_move(state->gui.board, from_r, from_c, to_r, to_c);
        } else {
            // No moves (Start or Reset) - Clear highlights
            board_widget_set_last_move(state->gui.board, -1, -1, -1, -1);
        }
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
    
    // Explicitly update clock widgets to show restored time immediately
    if (state->logic && state->gui.top_clock && state->gui.bottom_clock) {
         bool flipped = board_widget_is_flipped(state->gui.board);
         ClockWidget* white_clk = flipped ? state->gui.top_clock : state->gui.bottom_clock;
         ClockWidget* black_clk = flipped ? state->gui.bottom_clock : state->gui.top_clock;
         
         clock_widget_update(white_clk, state->logic->clock.white_time_ms, state->logic->clock.initial_time_ms, false);
         clock_widget_update(black_clk, state->logic->clock.black_time_ms, state->logic->clock.initial_time_ms, false);
    }
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

    // Apply Clock Settings from Config
    AppConfig* cfg = config_get();
    if (cfg->clock_minutes > 0 || cfg->clock_increment > 0) {
        gamelogic_set_clock(state->logic, cfg->clock_minutes, cfg->clock_increment);
    } else {
        // Ensure clock is disabled if set to 0/No Clock
        clock_reset(&state->logic->clock, 0, 0);
    }

    // Force immediate UI update for clock display
    if (state->gui.top_clock && state->gui.bottom_clock) {
        ClockState* clk = &state->logic->clock;
        bool flipped = board_widget_is_flipped(state->gui.board);
        ClockWidget* white_clk = flipped ? state->gui.top_clock : state->gui.bottom_clock;
        ClockWidget* black_clk = flipped ? state->gui.bottom_clock : state->gui.top_clock;
        
        Player turn = gamelogic_get_turn(state->logic);
        clock_widget_update(white_clk, clk->white_time_ms, clk->initial_time_ms, turn == PLAYER_WHITE);
        clock_widget_update(black_clk, clk->black_time_ms, clk->initial_time_ms, turn == PLAYER_BLACK);
        
        clock_widget_set_disabled(white_clk, !clk->enabled);
        clock_widget_set_disabled(black_clk, !clk->enabled);
    }

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
    // Inserting clock resets here:
    if (state->gui.top_clock && state->gui.bottom_clock) {
        int64_t init_time = state->logic ? state->logic->clock_initial_ms : 0;
        // White starts, so nobody is "active" until first move/click in many modes, 
        // or White is active if auto-start. logic->clock.active tells us.
        // Usually reset means active=false.
        clock_widget_update(state->gui.top_clock, init_time, init_time, false);
        clock_widget_update(state->gui.bottom_clock, init_time, init_time, false);

        // Set Names based on perspective
        AppConfig* cfg = config_get();
        clock_widget_set_name(state->gui.bottom_clock, "Player"); // Default human perspective
        if (state->logic->gameMode == GAME_MODE_PVC) {
            clock_widget_set_name(state->gui.top_clock, cfg->analysis_use_custom ? "Custom Engine" : "Inbuilt Stockfish Engine");
        } else if (state->logic->gameMode == GAME_MODE_CVC) {
            clock_widget_set_name(state->gui.bottom_clock, "Inbuilt Stockfish Engine");
            clock_widget_set_name(state->gui.top_clock, "Custom Engine");
        } else {
            clock_widget_set_name(state->gui.top_clock, "Player");
        }
    }

    if (state->gui.info_panel) { 
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
    
    if (state->is_replaying) return;
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
        params.target_elo = 0; // Advanced mode: no skill level
    } else {
        int elo = info_panel_get_elo(state->gui.info_panel, is_black);
        params = ai_get_difficulty_params(elo);
    }
    
    const char* path = use_custom ? ai_dialog_get_custom_path(state->gui.ai_dialog) : NULL;
    
    // NEW: Get clock state from logic
    int64_t wtime = state->logic->clock.white_time_ms;
    int64_t btime = state->logic->clock.black_time_ms;
    int64_t winc = state->logic->clock.increment_ms;
    int64_t binc = state->logic->clock.increment_ms;
    bool enabled = state->logic->clock.enabled;

    // NEW: Ensure timer is started/synced when AI is requested
    // This handles the case where AI is White (first move) or Black (after White)
    if (enabled) {
         // Sync turn start time to NOW
         state->logic->turn_start_time = get_monotonic_time_ms();
         
         // Force clock to active state so it ticks immediately
         state->logic->clock.active = true;
         if (state->logic->clock.last_tick_time == 0) {
             state->logic->clock.last_tick_time = state->logic->turn_start_time;
         }
    }

    ai_controller_request_move(state->ai_controller, use_custom, params, path, 
                               wtime, btime, winc, binc, enabled,
                               on_ai_move_ready, state);
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
    bool b_adv = ai_dialog_is_advanced_enabled(state->gui.ai_dialog, black_uses_custom);
    int b_depth = ai_dialog_get_depth(state->gui.ai_dialog, black_uses_custom); 
    
    info_panel_update_ai_settings(state->gui.info_panel, w_adv, w_depth, b_adv, b_depth);
    
    // Sync Custom Engine Availability
    bool has_custom = ai_dialog_has_valid_custom_engine(state->gui.ai_dialog);
    info_panel_set_custom_available(state->gui.info_panel, has_custom);

    return G_SOURCE_CONTINUE;
}

static gboolean on_window_close_request(GtkWindow* window, gpointer user_data) {
    (void)window;
    AppState* state = (AppState*)user_data;
    
    // SAVE WINDOW STATE
    if (state->gui.window) {
        AppConfig* cfg = config_get();
        if (cfg) {
            // Check for fullscreen & maximized
            GtkNative* native = GTK_NATIVE(state->gui.window);
            GdkSurface* surface = gtk_native_get_surface(native);
            bool is_full = false;
            bool is_max = false;
            
            if (surface) {
                GdkToplevel* toplevel = GDK_TOPLEVEL(surface);
                GdkToplevelState state_flags = gdk_toplevel_get_state(toplevel);
                is_full = (state_flags & GDK_TOPLEVEL_STATE_FULLSCREEN) != 0;
                is_max = (state_flags & GDK_TOPLEVEL_STATE_MAXIMIZED) != 0;
            } else {
                is_full = gtk_window_is_fullscreen(state->gui.window);
                is_max = gtk_window_is_maximized(state->gui.window);
            }

            cfg->is_fullscreen = is_full;
            cfg->is_maximized = is_max;

            // Logic to save sizes:
            // gtk_window_get_default_size returns the "restored" size (user resized size)
            // It returns 0,0 if the user hasn't resized it yet.
            int def_w = 0, def_h = 0;
            gtk_window_get_default_size(state->gui.window, &def_w, &def_h);

            if (def_w > 0 && def_h > 0) {
                 // We have a stored "restored" size, trust it even if currently maximized
                 cfg->window_width = def_w;
                 cfg->window_height = def_h;
            } else if (!is_max && !is_full) {
                 // No stored default size, but we are in normal mode. Use current size.
                 cfg->window_width = gtk_widget_get_width(GTK_WIDGET(state->gui.window));
                 cfg->window_height = gtk_widget_get_height(GTK_WIDGET(state->gui.window));
            }
            // Else (Maximized/Full AND no default size): Change NOTHING. Keep old config values.
            if (debug_mode) printf("[Main] Saving window state: %dx%d, Fullscreen: %d, Maximized: %d\n", 
                                   cfg->window_width, cfg->window_height, cfg->is_fullscreen, cfg->is_maximized);
        }
    }
    
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

typedef struct {
    AppState* state;
    char* match_id;
} ReplayLoadContext;

static gboolean delayed_replay_load_task(gpointer user_data) {
    ReplayLoadContext* ctx = (ReplayLoadContext*)user_data;
    AppState* state = ctx->state;
    const char* match_id = ctx->match_id;

    MatchHistoryEntry* entry = match_history_find_by_id(match_id);
    if (!entry) {
        g_warning("Match with ID %s not found for replay.", match_id);
        goto cleanup;
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
    
    // 4. Load Match
    state->is_replaying = true;
    if (state->replay_match_id) free(state->replay_match_id);
    state->replay_match_id = _strdup(match_id);
    replay_controller_load_match(state->replay_controller, entry->moves_uci, entry->start_fen,
                                 entry->think_time_ms, entry->think_time_count,
                                 entry->started_at_ms, entry->ended_at_ms,
                                 entry->clock.enabled, entry->clock.initial_ms, entry->clock.increment_ms,
                                 &entry->white, &entry->black);
                                 
    // Pass result metadata
    replay_controller_set_result(state->replay_controller, entry->result, entry->result_reason);
    
    // 5. Update Side Panel visuals
    if (state->gui.right_side_panel) {
        right_side_panel_set_analysis_visible(state->gui.right_side_panel, FALSE);
    }

    // Disable board interaction
    if (state->ai_controller) ai_controller_stop(state->ai_controller);
    board_widget_reset_selection(state->gui.board);
    board_widget_set_interactive(state->gui.board, FALSE);

    // Close history dialog
    if (state->gui.history_dialog) {
        gtk_window_destroy(history_dialog_get_window(state->gui.history_dialog));
        state->gui.history_dialog = NULL; // Already handling via destroy callback but safe here
    }
    
    if (debug_mode) printf("[Main] Started replay for match ID: %s\n", match_id);
    
    if (state->gui.board) {
        gtk_widget_grab_focus(state->gui.board);
    }

cleanup:
    // Hide overlay
    if (state->gui.loading_overlay) {
        gtk_widget_set_visible(state->gui.loading_overlay, FALSE);
        if (state->gui.loading_spinner) gtk_spinner_stop(GTK_SPINNER(state->gui.loading_spinner));
    }
    free(ctx->match_id);
    free(ctx);
    return FALSE;
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

    const char* match_id = g_variant_get_string(parameter, NULL);
    if (!match_id) return;

    // Show loading overlay
    if (state->gui.loading_overlay) {
        gtk_widget_set_visible(state->gui.loading_overlay, TRUE);
        if (state->gui.loading_spinner) gtk_spinner_start(GTK_SPINNER(state->gui.loading_spinner));
    }

    // Defer the heavy work so the UI can render the overlay
    ReplayLoadContext* ctx = (ReplayLoadContext*)malloc(sizeof(ReplayLoadContext));
    ctx->state = state;
    ctx->match_id = _strdup(match_id);
    g_timeout_add(50, delayed_replay_load_task, ctx);
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
    
    // Stop any piece animations immediately
    board_widget_cancel_animation(state->gui.board);
    
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

// --- Custom Layout Logic for Board + Clocks ---

static void board_layout_measure(GtkWidget *widget,
                                 GtkOrientation orientation, int for_size,
                                 int *minimum, int *natural,
                                 int *minimum_baseline, int *natural_baseline) {
    (void)for_size; (void)minimum_baseline; (void)natural_baseline;
    GtkWidget *child;
    int min_size = 0;
    int nat_size = 0;

    // Robust measure: Find children by name
    for (child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        
        if (!gtk_widget_should_layout(child)) continue;

        int child_min = 0, child_nat = 0;
        gtk_widget_measure(child, orientation, for_size,
                           &child_min, &child_nat, NULL, NULL);

        if (orientation == GTK_ORIENTATION_VERTICAL) {
            min_size += child_min;
            nat_size += child_nat;
        } else {
            // Horizontal: Max of widths
            if (child_min > min_size) min_size = child_min;
            if (child_nat > nat_size) nat_size = child_nat;
        }
    }
    
    if (minimum) *minimum = min_size;
    if (natural) *natural = nat_size;
}

static void board_layout_allocate(GtkWidget *widget,
                                  int width, int height, int baseline) {
    (void)baseline;
    GtkWidget *top_clock = NULL;
    GtkWidget *board = NULL;
    GtkWidget *bot_clock = NULL;

    // Robust allocation: Find children by name
    // This supports scenarios where clocks are hidden (Tutorial)
    GtkWidget *child;
    for (child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
         
         const char* name = gtk_widget_get_name(child);
         if (g_strcmp0(name, "layout_top_clock") == 0) top_clock = child;
         else if (g_strcmp0(name, "layout_board") == 0) board = child;
         else if (g_strcmp0(name, "layout_bot_clock") == 0) bot_clock = child;
    }

    // Must have board at minimum
    if (!board) return;

    // --- PASS 1: Preliminary Measure ---
    int top_min = 0, top_nat = 0;
    if (top_clock && gtk_widget_should_layout(top_clock)) {
        gtk_widget_measure(top_clock, GTK_ORIENTATION_VERTICAL, width, &top_min, &top_nat, NULL, NULL);
    }
    
    int bot_min = 0, bot_nat = 0;
    if (bot_clock && gtk_widget_should_layout(bot_clock)) {
        gtk_widget_measure(bot_clock, GTK_ORIENTATION_VERTICAL, width, &bot_min, &bot_nat, NULL, NULL);
    }

    int clocks_h = top_nat + bot_nat;
    int avail_h = height - clocks_h;
    if (avail_h < 0) avail_h = 0;
    
    int s_est = MIN(width, avail_h);
    if (s_est < 100) s_est = 100;
    
    // --- Scale Update ---
    double scale = (double)s_est / 800.0;
    
    if (top_clock) {
        ClockWidget* top_ptr = (ClockWidget*)g_object_get_data(G_OBJECT(top_clock), "clock-owner");
        if (top_ptr) clock_widget_set_scale(top_ptr, scale);
    }
    
    if (bot_clock) {
        ClockWidget* bot_ptr = (ClockWidget*)g_object_get_data(G_OBJECT(bot_clock), "clock-owner");
        if (bot_ptr) clock_widget_set_scale(bot_ptr, scale);
    }
    
    // --- PASS 2: Final Measure (after scale update) ---
    // Measure using the estimated width 's_est' to be precise
    if (top_clock && gtk_widget_should_layout(top_clock)) {
        gtk_widget_measure(top_clock, GTK_ORIENTATION_VERTICAL, s_est, &top_min, &top_nat, NULL, NULL);
    }
    if (bot_clock && gtk_widget_should_layout(bot_clock)) {
        gtk_widget_measure(bot_clock, GTK_ORIENTATION_VERTICAL, s_est, &bot_min, &bot_nat, NULL, NULL);
    }
    
    clocks_h = top_nat + bot_nat;
    avail_h = height - clocks_h;
    if (avail_h < 0) avail_h = 0;
    
    int s = MIN(width, avail_h);
    if (s < 100) s = 100;

    // Center the group
    int total_group_w = s;
    int total_group_h = top_nat + s + bot_nat;

    int offset_x = (width - total_group_w) / 2;
    int offset_y = (height - total_group_h) / 2;
    if (offset_x < 0) offset_x = 0;
    if (offset_y < 0) offset_y = 0;

    GtkAllocation alloc;

    // 1. Top Clock allocation (if visible)
    if (top_clock && gtk_widget_should_layout(top_clock)) {
        alloc.x = offset_x;
        alloc.y = offset_y;
        alloc.width = s;          // MATCH BOARD WIDTH
        alloc.height = top_nat;
        gtk_widget_size_allocate(top_clock, &alloc, -1);
    }

    // 2. Board allocation (Always visible usually)
    if (board && gtk_widget_should_layout(board)) {
        alloc.x = offset_x;
        alloc.y = offset_y + top_nat; // Even if top_clock hidden, top_nat is 0 so this works
        alloc.width = s;
        alloc.height = s;         // SQUARE
        gtk_widget_size_allocate(board, &alloc, -1);
    }
    
    // 3. Bottom Clock allocation (if visible)
    if (bot_clock && gtk_widget_should_layout(bot_clock)) {
        alloc.x = offset_x;
        alloc.y = offset_y + top_nat + s;
        alloc.width = s;          // MATCH BOARD WIDTH
        alloc.height = bot_nat;
        gtk_widget_size_allocate(bot_clock, &alloc, -1);
    }
}

static void apply_dynamic_resolution(GtkWindow* window) {
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(window));
    if (!display) display = gdk_display_get_default();
    if (!display) return;

    GListModel *monitors = gdk_display_get_monitors(display);
    if (g_list_model_get_n_items(monitors) > 0) {
        GdkMonitor *monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
        GdkRectangle geom;
        gdk_monitor_get_geometry(monitor, &geom);
        
        // Target area: 70% of screen width and height
        double max_w = geom.width * 0.7;
        double max_h = geom.height * 0.7;
        
        // Calculate dimensions to fit 70% bounds
        app_width = (int)max_w;
        app_height = gui_layout_calculate_target_height(app_width);
        
        if (app_height > max_h) {
            app_height = (int)max_h;
            // Reverse map width: width = height / aspect_factor
            double aspect_factor = (double)app_ratio_height / app_ratio_width;
            app_width = (int)(app_height / aspect_factor);
        }
        
        if (debug_mode) {
            double aspect = (double)app_ratio_width / app_ratio_height;
            printf("[Main] Dynamic Resolution: Screen %dx%d -> App %dx%d (70%% scale, aspect %.4f)\n", 
                   geom.width, geom.height, app_width, app_height, aspect);
        }
    }
}

// Debug helper for layout investigation
// Force ratio enforcement on startup
// Stateful layout enforcement
typedef struct {
    int attempts;
    int stage; // 0=Wait, 1=Force, 2=Relax
} LayoutEnforcerState;

static gboolean force_layout_ratios(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.window) return G_SOURCE_REMOVE;

    // Persist state across calls using static or object data
    // Simpler: use static variable since app is singleton-ish, or attached data
    // We'll attach a small struct to the window to track state
    LayoutEnforcerState* les = (LayoutEnforcerState*)g_object_get_data(G_OBJECT(state->gui.window), "layout_enforcer");
    if (!les) {
        les = g_new0(LayoutEnforcerState, 1);
        g_object_set_data_full(G_OBJECT(state->gui.window), "layout_enforcer", les, g_free);
    }

    int w_total = gtk_widget_get_width(GTK_WIDGET(state->gui.window));
    if (w_total <= 200) return G_SOURCE_CONTINUE; // Wait for real window
    
    // Widgets
    GtkWidget* w_info = state->gui.info_panel;
    GtkWidget* w_board = state->gui.board;
    GtkWidget* w_right = right_side_panel_get_widget(state->gui.right_side_panel);
    
    // Paneds
    GtkPaned* root = GTK_PANED(g_object_get_data(G_OBJECT(state->gui.window), "root_paned"));
    GtkPaned* right_split = GTK_PANED(g_object_get_data(G_OBJECT(state->gui.window), "right_split_paned"));

    if (!root || !right_split) return G_SOURCE_REMOVE;

    // Targets: 20% - 60% - 20%
    int t_info = (int)(w_total * 0.20);
    int t_board = (int)(w_total * 0.60);
    int t_right = w_total - t_info - t_board;
    int t_height = gui_layout_calculate_target_height(w_total);

    if (les->stage == 0) {
        // Wait for mapping
        if (!gtk_widget_get_mapped(GTK_WIDGET(state->gui.window))) return G_SOURCE_CONTINUE;
        les->stage = 1;
        return G_SOURCE_CONTINUE;
    }
    
    if (les->stage == 1) { 
        // STAGE 1: FORCE
        // We set MINIMUM sizes to the targets to force the Paned to comply
        gtk_widget_set_size_request(w_info, t_info, -1);
        gtk_widget_set_size_request(w_board, t_board, -1);
        gtk_widget_set_size_request(w_right, t_right, -1);
        
        // Force window height to match the ratio
        gtk_window_set_default_size(GTK_WINDOW(state->gui.window), w_total, t_height);
        
        // Also set pane positions as hints
        gtk_paned_set_position(root, t_info);
        gtk_paned_set_position(right_split, t_board);
        
        if(debug_mode) printf("[Layout] STAGE 1: FORCED Constraints -> Info: %d, Board: %d, Right: %d, Target Height: %d\n", 
               t_info, t_board, t_right, t_height);
        
        les->stage = 2;
        return G_SOURCE_CONTINUE; // Come back next tick to relax
    }
    
    if (les->stage == 2) {
        // STAGE 2: RELAX
        // Restore resizability
        gtk_widget_set_size_request(w_info, 100, -1);
        gtk_widget_set_size_request(w_board, 100, -1);
        gtk_widget_set_size_request(w_right, 100, -1);
        
        // Verify final result
        int act_info = gtk_widget_get_width(w_info);
        int act_board = gtk_widget_get_width(w_board);
        int act_right = gtk_widget_get_width(w_right);
        
        if(debug_mode){
            printf("\n=== LAYOUT FINAL ===\n");
            printf("Window: %d\n", w_total);
            printf("Info:   %d px (Target: %d)\n", act_info, t_info);
            printf("Board:  %d px (Target: %d)\n", act_board, t_board);
            printf("Right:  %d px (Target: %d)\n", act_right, t_right);
        }
        
        return G_SOURCE_REMOVE; // Be free
    }

    return G_SOURCE_REMOVE;
}

static gboolean startup_step_idle(gpointer user_data);

typedef struct {
    AppState* state;
    GtkWidget* splash;
    int step;
    int64_t total_start;
} StartupState;

// Delayed focus grabber to ensure window manager is ready
static gboolean delayed_focus_grab(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (state && state->gui.board) {
        if (debug_mode) printf("[Main] Executing delayed focus grab on board\n");
        gtk_widget_grab_focus(state->gui.board);
    }
    return G_SOURCE_REMOVE;
}

static void on_startup_finished(gpointer user_data) {
    StartupState* ss = (StartupState*)user_data;
    if (!ss) return;

    if (debug_mode) printf("[Main] Splash Screen: Fade out complete. Application ready.\n");
    
    // Ensure window has focus
    if (ss->state && ss->state->gui.window) {
         gtk_window_present(ss->state->gui.window);
    }
     
    // Schedule focus grab for next idle cycle to ensure window state is finalized
    if (ss->state) {
        g_idle_add(delayed_focus_grab, ss->state);
    }

    g_free(ss);
}

static gboolean startup_step_idle(gpointer user_data) {
    StartupState* ss = (StartupState*)user_data;
    AppState* state = ss->state;
    AppConfig* cfg = config_get();

    switch (ss->step) {
        case 0:
            splash_screen_update_status(ss->splash, "Loading History...");
            match_history_init();
            ss->step++;
            g_timeout_add(16, startup_step_idle, ss);
            return G_SOURCE_REMOVE;

        case 1:
            splash_screen_update_status(ss->splash, "Initializing AI...");
            state->gui.ai_dialog = ai_dialog_new_embedded();
            if (cfg) ai_dialog_load_config(state->gui.ai_dialog, cfg);
            ai_dialog_set_settings_changed_callback(state->gui.ai_dialog, on_ai_settings_changed, state);
            ss->step++;
            g_timeout_add(16, startup_step_idle, ss);
            return G_SOURCE_REMOVE;

        case 2:
            splash_screen_update_status(ss->splash, "Loading Components...");
            sound_engine_init();
            state->theme = theme_data_new();
            if (cfg) theme_data_load_config(state->theme, cfg);
            state->ai_controller = ai_controller_new(state->logic, state->gui.ai_dialog);
            gui_utils_init_icon_theme();
            ss->step++;
            g_timeout_add(16, startup_step_idle, ss);
            return G_SOURCE_REMOVE;

        case 3: {
            splash_screen_update_status(ss->splash, "Building UI...");
            // Now run the heavy UI building part
            // (We essentially move the rest of on_app_activate here)
            // But we need to be careful: some things might need to be in the main thread.
            // Gtk works in main thread, so idle is fine.
            
            GtkApplication* app = gtk_window_get_application(state->gui.window);
            
            GtkWidget* header = gtk_header_bar_new();
            GtkWidget* settings_btn = gui_utils_new_button_from_system_icon("emblem-system-symbolic");
            gtk_widget_add_css_class(settings_btn, "header-button");
            gtk_widget_set_tooltip_text(settings_btn, "Settings");
            state->gui.settings_btn = settings_btn;

            GSimpleAction* act_settings = g_simple_action_new("open-settings", G_VARIANT_TYPE_STRING);
            g_signal_connect(act_settings, "activate", G_CALLBACK(on_open_settings_action), state);
            g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_settings));
            gtk_actionable_set_action_name(GTK_ACTIONABLE(settings_btn), "app.open-settings");
            gtk_actionable_set_action_target(GTK_ACTIONABLE(settings_btn), "s", "");

            GtkWidget* dark_mode_btn = dark_mode_button_new();
            gtk_widget_set_valign(dark_mode_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_focusable(dark_mode_btn, FALSE);
            state->gui.dark_mode_btn = dark_mode_btn;

            GtkWidget* history_btn = gui_utils_new_button_from_system_icon("open-menu-symbolic");
            gtk_widget_set_valign(history_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text(history_btn, "Game History");
            gtk_widget_add_css_class(history_btn, "header-button");
            state->gui.history_btn = history_btn;

            GtkWidget* exit_replay_btn = gtk_button_new_with_label("Exit Replay");
            gtk_widget_add_css_class(exit_replay_btn, "destructive-action");
            gtk_widget_set_valign(exit_replay_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_visible(exit_replay_btn, FALSE);
            state->gui.exit_replay_btn = exit_replay_btn;

            gtk_header_bar_pack_end(GTK_HEADER_BAR(header), settings_btn);
            gtk_header_bar_pack_end(GTK_HEADER_BAR(header), dark_mode_btn);
            gtk_header_bar_pack_end(GTK_HEADER_BAR(header), history_btn);
            gtk_header_bar_pack_start(GTK_HEADER_BAR(header), exit_replay_btn);
            gtk_window_set_titlebar(state->gui.window, header);

            // Actions setup
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

            // Paned Hierarchy
            GtkWidget* root_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
            int w_total = gtk_widget_get_width(GTK_WIDGET(state->gui.window)); // Use current or default?
            if (w_total == 0) w_total = app_width;
            int info_width_target = w_total * 0.2;
            int board_width_target = w_total * 0.6;
            gtk_paned_set_position(GTK_PANED(root_paned), info_width_target);

            state->gui.board = board_widget_new(state->logic);
            board_widget_set_pre_move_callback(state->gui.board, on_board_before_move, state);
            board_widget_set_theme(state->gui.board, state->theme);
            
            state->gui.info_panel = info_panel_new(state->logic, state->gui.board, state->theme);
            g_object_set_data(G_OBJECT(state->gui.info_panel), "app_state", state);
            info_panel_set_cvc_callback(state->gui.info_panel, on_cvc_control_action, state);
            info_panel_set_ai_settings_callback(state->gui.info_panel, (GCallback)show_ai_settings_dialog, state);
            info_panel_set_puzzle_list_callback(state->gui.info_panel, G_CALLBACK(on_panel_puzzle_selected_safe), state);
            info_panel_set_game_reset_callback(state->gui.info_panel, on_game_reset, state);
            info_panel_set_tutorial_callbacks(state->gui.info_panel, G_CALLBACK(tutorial_reset_step), G_CALLBACK(on_tutorial_exit), state);
            info_panel_set_undo_callback(state->gui.info_panel, on_undo_move, state);
            info_panel_set_replay_exit_callback(state->gui.info_panel, G_CALLBACK(on_replay_exit_requested), state);
            
            puzzle_controller_refresh_list(state);
            gtk_widget_set_size_request(state->gui.info_panel, 100, -1);
            gtk_paned_set_start_child(GTK_PANED(root_paned), state->gui.info_panel);
            gtk_paned_set_resize_start_child(GTK_PANED(root_paned), TRUE);
            gtk_paned_set_shrink_start_child(GTK_PANED(root_paned), TRUE);

            GtkWidget* right_split_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_paned_set_position(GTK_PANED(right_split_paned), board_width_target);
            gtk_paned_set_resize_start_child(GTK_PANED(right_split_paned), TRUE);
            gtk_paned_set_shrink_start_child(GTK_PANED(right_split_paned), TRUE);
            gtk_paned_set_end_child(GTK_PANED(root_paned), right_split_paned);
            gtk_paned_set_resize_end_child(GTK_PANED(root_paned), TRUE);
            gtk_paned_set_shrink_end_child(GTK_PANED(root_paned), TRUE);

            GtkWidget* board_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            GtkLayoutManager* layout = gtk_custom_layout_new(NULL, board_layout_measure, board_layout_allocate);
            gtk_widget_set_layout_manager(board_area, layout);
            gtk_widget_set_hexpand(board_area, TRUE);
            gtk_widget_set_vexpand(board_area, TRUE);
            gtk_widget_set_margin_start(board_area, 8);
            gtk_widget_set_margin_end(board_area, 8);

            state->gui.top_clock = clock_widget_new(PLAYER_BLACK);
            state->gui.bottom_clock = clock_widget_new(PLAYER_WHITE);

            GtkWidget* top_clk = clock_widget_get_widget(state->gui.top_clock);
            g_object_set_data(G_OBJECT(top_clk), "clock-owner", state->gui.top_clock);
            gtk_widget_set_name(top_clk, "layout_top_clock");
            gtk_box_append(GTK_BOX(board_area), top_clk);

            gtk_widget_add_css_class(state->gui.board, "board-frame");
            gtk_widget_set_name(state->gui.board, "layout_board");
            gtk_box_append(GTK_BOX(board_area), state->gui.board);
            
            GtkWidget* bot_clk = clock_widget_get_widget(state->gui.bottom_clock);
            g_object_set_data(G_OBJECT(bot_clk), "clock-owner", state->gui.bottom_clock);
            gtk_widget_set_name(bot_clk, "layout_bot_clock");
            gtk_box_append(GTK_BOX(board_area), bot_clk);
            
            gtk_paned_set_start_child(GTK_PANED(right_split_paned), board_area);
            gtk_paned_set_resize_start_child(GTK_PANED(right_split_paned), TRUE);
            gtk_paned_set_shrink_start_child(GTK_PANED(right_split_paned), FALSE);

            // IMMEDIATE FOCUS GRAB: Don't wait for startup sequence to finish
            if (state->gui.board && gtk_widget_get_focusable(state->gui.board)) {
                if(debug_mode) printf("[Main] Step 3: IMMEDIATE Focus Grab on Board\n");
                gtk_widget_grab_focus(state->gui.board);
            }

            state->gui.right_side_panel = right_side_panel_new(state->logic, state->theme);
            right_side_panel_set_nav_callback(state->gui.right_side_panel, on_right_panel_nav, state);
            GtkWidget* right_widget = right_side_panel_get_widget(state->gui.right_side_panel);
            gtk_widget_set_size_request(right_widget, 100, -1);
            gtk_paned_set_end_child(GTK_PANED(right_split_paned), right_widget);
            gtk_paned_set_resize_end_child(GTK_PANED(right_split_paned), TRUE);
            gtk_paned_set_shrink_end_child(GTK_PANED(right_split_paned), TRUE);

            g_object_set_data(G_OBJECT(state->gui.window), "root_paned", root_paned);
            g_object_set_data(G_OBJECT(state->gui.window), "right_split_paned", right_split_paned);

            // Re-connect overlay
            GtkWidget* main_overlay = gtk_widget_get_parent(ss->splash);
            // We need to insert root_paned BELOW the splash in the overlay
            gtk_overlay_set_child(GTK_OVERLAY(main_overlay), root_paned);

            // Loading overlay for replays
            state->gui.loading_overlay = gui_utils_create_loading_overlay(
                GTK_OVERLAY(main_overlay), 
                &state->gui.loading_spinner, 
                "Loading Replay", 
                "Preparing game state..."
            );

            // Onboarding Setup
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
                
                GtkWidget* btn_start = gtk_button_new_with_label("Start Tutorial");
                gtk_widget_add_css_class(btn_start, "suggested-action");
                g_signal_connect(btn_start, "clicked", G_CALLBACK(activate_tutorial_action), state);
                g_signal_connect(btn_start, "clicked", G_CALLBACK(on_dismiss_onboarding), state);
                g_object_set_data(G_OBJECT(state->gui.window), "tutorial-start-btn", btn_start);
                gtk_box_append(GTK_BOX(main_vbox), btn_start);
                
                GtkWidget* btn_close = gtk_button_new_with_label("Close");
                g_signal_connect(btn_close, "clicked", G_CALLBACK(on_dismiss_onboarding), state);
                g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_grab_focus), state->gui.board);
                gtk_box_append(GTK_BOX(main_vbox), btn_close);
                
                gtk_popover_set_child(popover, main_vbox);
                gtk_popover_set_position(popover, GTK_POS_BOTTOM);
                gtk_popover_set_autohide(popover, TRUE);
                
                state->gui.onboarding_popover = GTK_WIDGET(popover);
                state->onboarding_timer_id = g_timeout_add_seconds(1, popup_popover_delayed, state);
            }

            gamelogic_set_callback(state->logic, update_ui_callback);
            state->settings_timer_id = g_timeout_add(500, sync_ai_settings_to_panel, state);
            sync_live_analysis(state);
            
            on_game_reset(state);
            
            g_signal_connect(state->gui.window, "close-request", G_CALLBACK(on_window_close_request), state);
            gtk_window_set_focus_visible(state->gui.window, TRUE);
            g_signal_connect(state->gui.window, "notify::mapped", G_CALLBACK(on_window_mapped_notify), state);
            
            g_timeout_add(100, clock_tick_callback, state);
            g_timeout_add(100, force_layout_ratios, state);

            splash_screen_update_status(ss->splash, "Ready!");
            splash_screen_finish(ss->splash, G_CALLBACK(on_startup_finished), ss);
            
            int64_t now = g_get_monotonic_time();
            if (debug_mode) printf("[Startup Profile] Total Boot Time: %.2f ms\n", (now - ss->total_start) / 1000.0);
            
            return G_SOURCE_REMOVE;
        }
    }
    return G_SOURCE_REMOVE;
}

static void on_app_activate(GtkApplication* app, gpointer user_data) {
    int64_t total_start = g_get_monotonic_time();
    
    config_set_app_param("HalChess");
    config_init(); 
    
    AppState* state = (AppState*)user_data;
    state->gui.window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(state->gui.window, "HAL :) Chess");
    
    AppConfig* cfg = config_get();

    // Size initialization (Synchronous)
    if (cfg && cfg->window_width > 0 && cfg->window_height > 0) {
        gtk_window_set_default_size(state->gui.window, cfg->window_width, cfg->window_height);
    } else {
        apply_dynamic_resolution(state->gui.window);
        gtk_window_set_default_size(state->gui.window, app_width, app_height);
        if (cfg) {
            cfg->window_width = app_width;
            cfg->window_height = app_height;
        }
    }    

    if (cfg) {
        if (cfg->is_fullscreen) gtk_window_fullscreen(state->gui.window);
        else if (cfg->is_maximized) gtk_window_maximize(state->gui.window);
    }

    // Theme initialization (Synchronous)
    if (cfg) {
        theme_manager_set_dark(cfg->is_dark_mode);
        if (cfg->theme[0] != '\0' && strcmp(cfg->theme, "default") != 0) {
             theme_manager_set_theme_id(cfg->theme);
        }
    }
    theme_manager_init();

    // Create Root Overlay NOW so we can show splash
    GtkWidget* main_overlay = gtk_overlay_new();
    gtk_window_set_child(state->gui.window, main_overlay);

    // Show Splash Screen
    GtkWidget* splash = splash_screen_show(state->gui.window);
    
    // Setup continuous ratio enforcement via property notifications
    g_signal_connect(state->gui.window, "notify::default-width", G_CALLBACK(on_window_default_width_notify), state);

    // Show window immediately so user sees the splash
    gtk_window_present(state->gui.window);

    // Schedule rest of initialization
    StartupState* ss = g_new0(StartupState, 1);
    ss->state = state;
    ss->splash = splash;
    ss->step = 0;
    ss->total_start = total_start;
    
    g_idle_add(startup_step_idle, ss);
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
    // Force OpenGL renderer to avoid Vulkan drivers issues on some PCs
    _putenv("GSK_RENDERER=gl");
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

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "config_manager.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
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

static bool debug_mode = false;
static int app_height = 960;
static int app_width = 1530;

// Globals
static AppState* g_app_state = NULL;

// Forward declarations
static gboolean check_trigger_ai_idle(gpointer user_data);
static gboolean grab_board_focus_idle(gpointer user_data);
static void request_ai_move(AppState* state);
static void on_ai_move_ready(Move* move, gpointer user_data);
static void sync_live_analysis(AppState* state);

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

// Explicit callback for animation finished (Debug requirements)
static gboolean on_animation_finished(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (debug_mode) printf("[DEBUG] Turn complete (animation finished).\n");
    if (debug_mode) printf("[DEBUG] Requesting AI now.\n");
    // Directly request AI move - the check_trigger_ai_idle does the same thing
    request_ai_move(state);
    return FALSE;
}

// --- CvC Orchestration ---

static void on_cvc_control_action(CvCMatchState action, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return;
    
    state->cvc_match_state = action;
    if (debug_mode) printf("[CvC] State changed to %d\n", action);
    
    // If stopped, just pause the match state (no reset)
    if (action == CVC_STATE_STOPPED) {
         if (state->gui.board) board_widget_refresh(state->gui.board);
         if (debug_mode) printf("[CvC] Match stopped. AI thinking flag reset.\n");
         if (state->ai_controller) ai_controller_stop(state->ai_controller);
         if (state->ai_trigger_id > 0) {
             g_source_remove(state->ai_trigger_id);
             state->ai_trigger_id = 0;
             if (debug_mode) printf("[CvC] AI trigger ID cleared.\n");
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
            if (debug_mode) printf("[CvC] Match running. AI trigger scheduled.\n");
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


static void on_right_panel_nav(const char* action, int ply_index, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->logic) return;
    
    if (strcmp(action, "goto_ply") == 0) {
        // Only allow jumping back in history
        while (gamelogic_get_move_count(state->logic) > ply_index + 1) {
            gamelogic_undo_move(state->logic);
        }
    } else if (strcmp(action, "prev") == 0) {
        gamelogic_undo_move(state->logic);
    } else if (strcmp(action, "next") == 0) {
        // Currently no redo, but next is possible if we navigated to the past
        // Actually, REDO was removed, so next is only if we've undone.
        // Rule: Restricted to "undo exclusively for live matches".
        // If the user wants to jump forward again, they'd need redo.
        // User said: "remove all redo history functionality ... so i want all the four buttons though"
        // This is contradictory. If redo is gone, 'Next' and 'End' don't work unless we keep a hidden redo stack.
        // I will assume 'Next' and 'End' only work if the user is currently "in the past" due to undos.
    } else if (strcmp(action, "start") == 0) {
        while (gamelogic_get_move_count(state->logic) > 0) {
            gamelogic_undo_move(state->logic);
        }
    }
}

// Callback from game logic when state changes (e.g. move made)
static void update_ui_callback(void) {
    if (!g_app_state) return;
    AppState* state = g_app_state;
    if (!state->gui.board || !state->gui.info_panel || !state->logic) return;
    
    if (state->logic->isGameOver) {
        if (state->ai_controller) ai_controller_stop(state->ai_controller);
        if (state->cvc_match_state == CVC_STATE_RUNNING) {
            state->cvc_match_state = CVC_STATE_STOPPED;
        }
    } else {
        if (state->ai_controller && !ai_controller_is_thinking(state->ai_controller)) {
            if (state->ai_trigger_id == 0) {
                state->ai_trigger_id = g_idle_add(check_trigger_ai_idle, state);
            }
        }
    }
    
    // Update Right Side Panel
    if (state->gui.right_side_panel) {
        int count = gamelogic_get_move_count(state->logic);
        Move* last = gamelogic_get_last_move(state->logic);
        if (last) {
            int m_num = (count + 1) / 2;
            Player p = (count % 2 == 1) ? PLAYER_WHITE : PLAYER_BLACK;
            // This now handles truncation automatically if we are in the past
            right_side_panel_add_move(state->gui.right_side_panel, last, m_num, p);
        } else if (count == 0) {
            right_side_panel_clear_history(state->gui.right_side_panel);
        }
        
        // Detect move change for rating
        if (count > state->last_move_count) {
            ai_controller_set_rating_pending(state->ai_controller, true);
            state->last_move_count = count;
        }

        bool is_live_match = !state->logic->isGameOver && !state->tutorial.step;
        bool in_playing_mode = (state->logic->gameMode == GAME_MODE_PVC || state->logic->gameMode == GAME_MODE_CVC);
        bool should_hide_nav = is_live_match && in_playing_mode;

        right_side_panel_set_interactive(state->gui.right_side_panel, !is_live_match);
        right_side_panel_set_nav_visible(state->gui.right_side_panel, !should_hide_nav);
        right_side_panel_highlight_ply(state->gui.right_side_panel, count - 1);
    }

    sync_live_analysis(state);
    
    // Refresh UI
    if (state->gui.board) { // Added NULL check
        board_widget_refresh(state->gui.board);
    }
    
    if (state->gui.info_panel) { // Added NULL check
        info_panel_update_status(state->gui.info_panel);
    }

    // Update Puzzle Status (Turn Indicator)
    if (state->logic->gameMode == GAME_MODE_PUZZLE && !state->puzzle.wait) {
        // Only update if not solved (solved message is static)
        const Puzzle* p = puzzles_get_at(state->puzzle.current_idx);
        if (p && state->puzzle.move_idx < p->solution_length) {
            const char* turn_str = (state->logic->turn == PLAYER_WHITE) ? "Your turn! (White to Move)" : "Your turn! (Black to Move)";
            // We pass NULL for title/desc to avoid overwriting them?
            // info_panel_update_puzzle_info usage: if title is NULL, it's ignored.
            if (state->gui.info_panel) info_panel_update_puzzle_info(state->gui.info_panel, NULL, NULL, turn_str, true); // Added NULL check
        }
    }
    
    // Tutorial Check
    if (state->tutorial.step != TUT_OFF) {
        tutorial_check_progress(state);
    }
    
    // Puzzle Check
    if (state->logic->gameMode == GAME_MODE_PUZZLE) {
        puzzle_controller_check_move(state);
    }
}

// Forward declaration
static gboolean sync_ai_settings_to_panel(gpointer user_data);

static void on_ai_settings_changed(void* user_data) {
    AppState* state = (AppState*)user_data;
    if (debug_mode) printf("[DEBUG] AI Settings Changed callback fired.\n");
    
    // DECISION: Do NOT reset the game automatically. 
    // This was causing games to restart unexpectedly (pieces disappearing) if this callback fired
    // due to UI events (e.g. initial sync, or slight adjustments).
    // If the user wants a new game with the new settings, they can press Reset manually.
    
    /* 
    if (state->tutorial.step == TUT_OFF) {
        if (state->logic->gameMode == GAME_MODE_PVC || state->logic->gameMode == GAME_MODE_CVC) {
             gamelogic_reset(state->logic);
             if (state->gui.board) board_widget_refresh(state->gui.board);
             if (state->gui.info_panel) info_panel_rebuild_layout(state->gui.info_panel);
        }
    }
    */

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

    if (!cfg || !cfg->enable_live_analysis || (state->logic && state->logic->isGameOver)) {
        ai_controller_stop_analysis(state->ai_controller, false);
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
    
    ai_controller_start_analysis(state->ai_controller, use_custom, custom_path);
}

static void on_undo_move(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return;

    if (debug_mode) printf("[Undo] Move undone. Invalidating analysis.\n");

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
    if (debug_mode) printf("[Reset] Game reset. CvC state -> STOPPED\n"); 
    
    // Cancel AI trigger on reset to prevent old moves trigger
    if (state->ai_trigger_id > 0) {
        g_source_remove(state->ai_trigger_id);
        state->ai_trigger_id = 0;
    }

    gamelogic_reset(state->logic);
    if (state->gui.board) { // Added NULL check
        // Sync flip with player side (fix for Play as Black/Random)
        bool flip = (state->logic->playerSide == PLAYER_BLACK);
        board_widget_set_flipped(state->gui.board, flip);
        board_widget_refresh(state->gui.board);
    }
    
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
    
    // Trigger AI if it's its turn (e.g. Play as Black)
    if (state->ai_trigger_id == 0) {
        state->ai_trigger_id = g_idle_add(check_trigger_ai_idle, state);
    }
}

static void on_toggle_panel_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.right_side_panel) return;
    
    GtkWidget* panel_widget = right_side_panel_get_widget(state->gui.right_side_panel);
    bool visible = gtk_widget_get_visible(panel_widget);
    bool target = !visible;
    
    // UI update for button state
    if (state->gui.header_right_panel_btn) {
        if (target) {
            gtk_widget_add_css_class(state->gui.header_right_panel_btn, "active");
            gtk_widget_set_tooltip_text(state->gui.header_right_panel_btn, "Hide History");
        } else {
            gtk_widget_remove_css_class(state->gui.header_right_panel_btn, "active");
            gtk_widget_set_tooltip_text(state->gui.header_right_panel_btn, "Show History");
        }
    }
    
    // Safe toggle: Only call set_visible if necessary
    if (gtk_widget_get_visible(panel_widget) != target) {
        gtk_widget_set_visible(panel_widget, target);
    }
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

static void on_ai_eval_update(const AiStats* stats, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.right_side_panel || !stats) return;
    
    AppConfig* cfg = config_get();
    if (!cfg) return;

    // Rating Toast
    if (stats->rating_label && cfg->show_move_rating) {
        right_side_panel_show_rating_toast(state->gui.right_side_panel, 
             stats->rating_label, stats->rating_reason, stats->move_number);
    }
    
    // Eval Bar
    if (cfg->show_advantage_bar) {
        double eval_val = (double)stats->score / 100.0;
        right_side_panel_update_stats(state->gui.right_side_panel, eval_val, stats->is_mate);
    }
    
    // Mate Warning
    if (cfg->show_mate_warning) {
        int mate_dist = stats->is_mate ? abs(stats->mate_distance) : 0;
        right_side_panel_set_mate_warning(state->gui.right_side_panel, mate_dist);
    }
    
    // Hanging Pieces
    if (cfg->show_hanging_pieces) {
        right_side_panel_set_hanging_pieces(state->gui.right_side_panel, 
              stats->white_hanging, stats->black_hanging);
    }
}

static void on_ai_move_ready(Move* move, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state || !state->gui.board || !state->logic || !move) return;
    
    // Add visual delay effect is handled in controller
    board_widget_animate_move(state->gui.board, move);
}

static void request_ai_move(AppState* state) {
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

static void on_app_activate(GtkApplication* app, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    state->gui.window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(state->gui.window, "HAL :) Chess");
    gtk_window_set_default_size(state->gui.window, app_width, app_height);
    
    config_set_app_param("HAL Chess");
    config_init(); // Initialize config from persistent storage
    
    // Apply saved config to Theme Manager
    AppConfig* cfg = config_get();
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
    ai_controller_set_eval_callback(state->ai_controller, on_ai_eval_update, state);

    GtkWidget* header = gtk_header_bar_new();
    // Replacement for Menu: Settings Button
    GtkWidget* settings_btn = gtk_button_new_from_icon_name("open-menu-symbolic");
    gtk_widget_add_css_class(settings_btn, "header-button");
    gtk_widget_set_tooltip_text(settings_btn, "Settings");
    
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

    // Create Panel Toggle Button
    state->gui.header_right_panel_btn = gtk_button_new_from_icon_name("view-list-symbolic");
    gtk_widget_add_css_class(state->gui.header_right_panel_btn, "header-button");
    gtk_widget_add_css_class(state->gui.header_right_panel_btn, "active"); // Default visible
    gtk_widget_set_tooltip_text(state->gui.header_right_panel_btn, "Hide History");
    g_signal_connect(state->gui.header_right_panel_btn, "clicked", G_CALLBACK(on_toggle_panel_clicked), state);
    
    // Pack buttons: [History] [Dark Mode] [Settings]
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), settings_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), dark_mode_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), state->gui.header_right_panel_btn);
    
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
    
    GSimpleAction* act_start_puzzle = g_simple_action_new("start-puzzle", G_VARIANT_TYPE_INT32);
    g_signal_connect(act_start_puzzle, "activate", G_CALLBACK(on_start_puzzle_action), state);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_start_puzzle));
    
    printf("DEBUG: App Activate complete. Actions connected.\n");

    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    state->gui.board = board_widget_new(state->logic);
    
    // Fix AI Latency: Register animation finish callback to trigger AI immediately
    // state->gui.board is the frame, we need the grid child where the callback is invoked
    GtkWidget* grid_child = gtk_frame_get_child(GTK_FRAME(state->gui.board));
    if (grid_child) {
        g_object_set_data(G_OBJECT(grid_child), "anim-finish-cb", (gpointer)on_animation_finished);
        g_object_set_data(G_OBJECT(grid_child), "anim-finish-data", state);
    }

    board_widget_set_theme(state->gui.board, state->theme);
    
    state->gui.info_panel = info_panel_new(state->logic, state->gui.board, state->theme);
    info_panel_set_cvc_callback(state->gui.info_panel, on_cvc_control_action, state);
    info_panel_set_ai_settings_callback(state->gui.info_panel, (GCallback)show_ai_settings_dialog, state);
    info_panel_set_puzzle_list_callback(state->gui.info_panel, G_CALLBACK(on_panel_puzzle_selected_safe), state);
    
    // Set game reset callback to trigger AI after reset/side changes
    info_panel_set_game_reset_callback(state->gui.info_panel, on_game_reset, state);
    
    // Connect tutorial callbacks to info panel (Moved here to ensure panel exists)
    info_panel_set_tutorial_callbacks(state->gui.info_panel, G_CALLBACK(tutorial_reset_step), G_CALLBACK(on_tutorial_exit), state);
    
    // Connect undo callback to handle analysis state
    info_panel_set_undo_callback(state->gui.info_panel, on_undo_move, state);
    
    puzzle_controller_refresh_list(state);
    gtk_widget_set_size_request(state->gui.info_panel, 240, -1);
    gtk_widget_set_hexpand(state->gui.info_panel, FALSE);
    gtk_box_append(GTK_BOX(main_box), state->gui.info_panel);
    
    GtkWidget* aspect_frame = gtk_aspect_frame_new(0.5, 0.5, 1.0, FALSE);
    gtk_widget_set_hexpand(aspect_frame, TRUE);
    gtk_widget_set_vexpand(aspect_frame, TRUE);
    GtkWidget* board_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(board_container, "board-frame");
    gtk_box_append(GTK_BOX(board_container), state->gui.board);
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(aspect_frame), board_container);
    gtk_box_append(GTK_BOX(main_box), aspect_frame);

    // Right Side Panel
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
        g_signal_connect(btn_start, "clicked", G_CALLBACK(on_dismiss_onboarding), state);
        
        // Store button reference for focus grabbing on startup
        g_object_set_data(G_OBJECT(state->gui.window), "tutorial-start-btn", btn_start);
        
        gtk_box_append(GTK_BOX(box), btn_start);
        
        // Close Button
        GtkWidget* btn_close = gtk_button_new_with_label("Close");
        g_signal_connect(btn_close, "clicked", G_CALLBACK(on_dismiss_onboarding), state);
        g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_widget_grab_focus), state->gui.board);
        gtk_box_append(GTK_BOX(box), btn_close);
        
        gtk_popover_set_child(popover, box);
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
    
    // Connect close-request signal to cleanup BEFORE destruction
    g_signal_connect(state->gui.window, "close-request", G_CALLBACK(on_window_close_request), state);
    
    // Enable focus visibility
    gtk_window_set_focus_visible(state->gui.window, TRUE);
    
    // Present window only when mapped to ensure correct activation
    g_signal_connect(state->gui.window, "notify::mapped", G_CALLBACK(on_window_mapped_notify), state);
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

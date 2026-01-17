#ifndef INFO_PANEL_H
#define INFO_PANEL_H

#include <gtk/gtk.h>
#include "theme_data.h"

typedef enum {
    CVC_STATE_STOPPED,
    CVC_STATE_RUNNING,
    CVC_STATE_PAUSED
} CvCMatchState;

typedef void (*CvCControlCallback)(CvCMatchState action, gpointer user_data);

// Create the info panel widget
GtkWidget* info_panel_new(GameLogic* logic, GtkWidget* board_widget, ThemeData* theme);

// Update the status display
void info_panel_update_status(GtkWidget* info_panel);

// Rebuild the layout (for when game mode changes, etc.)
void info_panel_rebuild_layout(GtkWidget* info_panel);

void info_panel_set_cvc_callback(GtkWidget* info_panel, CvCControlCallback callback, gpointer user_data);
void info_panel_set_cvc_state(GtkWidget* info_panel, CvCMatchState state);
void info_panel_set_custom_available(GtkWidget* info_panel, bool available);
void info_panel_set_ai_settings_callback(GtkWidget* info_panel, GCallback callback, gpointer user_data);
bool info_panel_is_custom_selected(GtkWidget* info_panel, bool for_black);
void info_panel_show_ai_settings(GtkWidget* info_panel);

void info_panel_update_ai_settings(GtkWidget* info_panel, bool white_adv, int white_depth, bool black_adv, int black_depth);
int info_panel_get_elo(GtkWidget* info_panel, bool for_black);

void info_panel_set_sensitive(GtkWidget* info_panel, bool sensitive);
void info_panel_set_elo(GtkWidget* info_panel, int elo, bool is_black);

// Puzzle Mode
void info_panel_set_puzzle_mode(GtkWidget* info_panel, bool enabled);
void info_panel_update_puzzle_info(GtkWidget* info_panel, const char* title, const char* description, const char* status, bool show_next_btn);
void info_panel_set_puzzle_callbacks(GtkWidget* info_panel, GCallback on_reset, GCallback on_next, gpointer user_data);
void info_panel_set_puzzle_exit_callback(GtkWidget* info_panel, GCallback on_exit, gpointer user_data);

// Puzzle List Management
void info_panel_clear_puzzle_list(GtkWidget* info_panel);
void info_panel_add_puzzle_to_list(GtkWidget* info_panel, const char* title, int index);
void info_panel_highlight_puzzle(GtkWidget* info_panel, int index);
void info_panel_set_puzzle_list_callback(GtkWidget* info_panel, GCallback on_selected, gpointer user_data);

// Refresh captured pieces (graveyard) display
void info_panel_refresh_graveyard(GtkWidget* info_panel);

void info_panel_set_game_mode(GtkWidget* info_panel, GameMode mode);
void info_panel_set_player_side(GtkWidget* info_panel, Player side);

// Callback for game reset/side change to trigger AI
typedef void (*GameResetCallback)(gpointer user_data);
void info_panel_set_game_reset_callback(GtkWidget* info_panel, GameResetCallback callback, gpointer user_data);

// Callback for move undo
typedef void (*UndoCallback)(gpointer user_data);
void info_panel_set_undo_callback(GtkWidget* info_panel, UndoCallback callback, gpointer user_data);

// Tutorial Mode Control
void info_panel_set_tutorial_mode(GtkWidget* info_panel, bool enabled);
void info_panel_update_tutorial_info(GtkWidget* info_panel, const char* instruction, const char* learning_objective);
void info_panel_set_tutorial_callbacks(GtkWidget* info_panel, GCallback on_reset, GCallback on_exit, gpointer user_data);

// Replay Mode UI
void info_panel_show_replay_controls(GtkWidget* panel, gboolean visible);
void info_panel_update_replay_status(GtkWidget* info_panel, int current_ply, int total_plies);
void info_panel_set_replay_exit_callback(GtkWidget* info_panel, GCallback callback, gpointer user_data);

#endif // INFO_PANEL_H

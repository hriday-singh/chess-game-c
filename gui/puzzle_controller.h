#ifndef PUZZLE_CONTROLLER_H
#define PUZZLE_CONTROLLER_H

#include <gtk/gtk.h>
#include "app_state.h"

// Initialize puzzle controller resources (if any)
void puzzle_controller_init(AppState* state);

// Cleanup resources
void puzzle_controller_cleanup(AppState* state);

// Start a specific puzzle
void puzzle_controller_start(AppState* state, int puzzle_idx);

// Check if a move matches the puzzle solution
// Returns true if the move was a valid puzzle move (correct or incorrect)
// and handles the UI updates/sounds accordingly.
void puzzle_controller_check_move(AppState* state);

// Reset the current puzzle
void puzzle_controller_reset(AppState* state);

// Move to the next puzzle
void puzzle_controller_next(AppState* state);

// Exit puzzle mode logic
void puzzle_controller_exit(AppState* state);

// Refresh the list of puzzles in the UI
void puzzle_controller_refresh_list(AppState* state);

// Signal handler wrappers
void on_puzzle_reset_clicked(GtkButton* btn, gpointer user_data);
void on_puzzle_next_clicked(GtkButton* btn, gpointer user_data);
void on_puzzle_exit_clicked(GtkButton* btn, gpointer user_data);
void on_start_puzzle_action(GSimpleAction* action, GVariant* parameter, gpointer user_data);
void on_puzzles_action(GSimpleAction* action, GVariant* parameter, gpointer user_data);
void on_panel_puzzle_selected_safe(GtkListBox* box, GtkListBoxRow* row, gpointer user_data);

#endif // PUZZLE_CONTROLLER_H

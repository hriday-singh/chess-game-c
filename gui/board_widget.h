#ifndef BOARD_WIDGET_H
#define BOARD_WIDGET_H

#include <gtk/gtk.h>
#include "theme_data.h"

// Create a simple 8x8 board grid widget.
GtkWidget* board_widget_new(GameLogic* logic);

// Set theme data for board colors
void board_widget_set_theme(GtkWidget* board_widget, ThemeData* theme);

// Set interaction mode: true = drag-and-drop, false = click-to-move
void board_widget_set_drag_mode(GtkWidget* board_widget, bool drag_mode);

// Get current interaction mode
bool board_widget_get_drag_mode(GtkWidget* board_widget);

// Refresh the board display
void board_widget_refresh(GtkWidget* board_widget);

// Reset selection (clear selected piece)
void board_widget_reset_selection(GtkWidget* board_widget);

// Set last move for yellow highlight (for replay mode)
void board_widget_set_last_move(GtkWidget* board_widget, int fromRow, int fromCol, int toRow, int toCol);

// Set board orientation (flip board for black's perspective)
void board_widget_set_flipped(GtkWidget* board_widget, bool flipped);

// Get board orientation
bool board_widget_get_flipped(GtkWidget* board_widget);

// Set animations enabled
void board_widget_set_animations_enabled(GtkWidget* board_widget, bool enabled);

// Get animations enabled
bool board_widget_get_animations_enabled(GtkWidget* board_widget);

// Set hints mode: true = dots, false = squares
void board_widget_set_hints_mode(GtkWidget* board_widget, bool use_dots);

// Get hints mode: true = dots, false = squares
bool board_widget_get_hints_mode(GtkWidget* board_widget);

/**
 * Animates a move on the board widget.
 * This is useful for AI moves to show smooth transition.
 * Returns: TRUE if animation started, FALSE if invalid input or widget error.
 */
gboolean board_widget_animate_move(GtkWidget* board_widget, Move* move);

// Restrict navigation (for tutorial mode)
void board_widget_set_nav_restricted(GtkWidget* board_widget, bool restricted, int r1, int c1, int r2, int c2);

/**
 * Check if board is currently animating a move.
 */
bool board_widget_is_animating(GtkWidget* board_widget);

// Invalid move callback (for tutorial feedback)
typedef void (*BoardInvalidMoveCallback)(void* user_data);
void board_widget_set_invalid_move_callback(GtkWidget* board_widget, BoardInvalidMoveCallback cb, void* data);

// Pre-move callback (fired before a human move is committed)
typedef void (*BoardPreMoveCallback)(const char* move_uci, void* user_data);
void board_widget_set_pre_move_callback(GtkWidget* board_widget, BoardPreMoveCallback cb, void* data);

// Enable/Disable board interaction (D&D and clicks)
void board_widget_set_interactive(GtkWidget* board_widget, bool interactive);

#endif // BOARD_WIDGET_H



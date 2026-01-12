#ifndef APP_STATE_H
#define APP_STATE_H

#include <gtk/gtk.h>
#include "info_panel.h"
#include "board_theme_dialog.h"
#include "piece_theme_dialog.h"
#include "ai_dialog.h"

// Tutorial Enums
enum {
    TUT_OFF = 0,
    TUT_INTRO = 1,
    TUT_PAWN = 2,
    TUT_ROOK = 3,
    TUT_BISHOP = 4,
    TUT_KNIGHT = 5,
    TUT_QUEEN = 6,
    TUT_CHECK = 7,
    TUT_ESCAPE = 8,
    TUT_CASTLING = 9,
    TUT_MATE = 10,
    TUT_DONE = 11
};

typedef struct GuiState {
    // Widgets
    GtkWidget* info_panel;
    GtkWidget* board;
    GtkWindow* window;
    GtkWidget* tutorial_msg; 
    GtkWidget* tutorial_exit_btn;
    GtkWidget* onboarding_popover;
    GtkWidget* header_right_panel_btn;

    // Dialogs & Panels
    BoardThemeDialog* theme_dialog;
    PieceThemeDialog* piece_theme_dialog;
    AiDialog* ai_dialog;
    struct _SettingsDialog* settings_dialog;
    struct _RightSidePanel* right_side_panel;
} GuiState;

typedef struct TutorialState {
    int step; 
    int next_step; // For delayed transition
    gboolean message_shown; 
    gboolean wait; // To prevent rapid progression
} TutorialState;

typedef struct PuzzleState {
    int current_idx;
    int move_idx;
    int last_processed_move; // Track processed move count from game logic
    gboolean wait; // Waiting for opponent response
} PuzzleState;

typedef struct AppState {
    // Core Logic & Data
    GameLogic* logic;
    struct _AiController* ai_controller;
    ThemeData* theme;
    
    // Sub-states
    GuiState gui;
    TutorialState tutorial;
    PuzzleState puzzle;

    // Session State
    CvCMatchState cvc_match_state;
    char last_settings_page[32];
    
    // Analysis/Rating State
    int last_move_count;

    // Timers
    guint settings_timer_id;
    guint onboarding_timer_id;
    guint ai_trigger_id;
} AppState;

#endif

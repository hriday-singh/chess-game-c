#ifndef APP_STATE_H
#define APP_STATE_H

#include <gtk/gtk.h>
#include "gamelogic.h"
#include "info_panel.h"
#include "board_theme_dialog.h"
#include "piece_theme_dialog.h"
#include "ai_dialog.h"
#include "ai_engine.h"
// #include "types.h" // If needed

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

typedef struct AppState {
    GameLogic* logic;
    GtkWidget* info_panel;
    GtkWidget* board;
    GtkWindow* window;
    ThemeData* theme;
    BoardThemeDialog* theme_dialog;
    PieceThemeDialog* piece_theme_dialog;
    AiDialog* ai_dialog;
    struct _SettingsDialog* settings_dialog;
    
    gboolean ai_thinking;
    EngineHandle* internal_engine;
    EngineHandle* custom_engine;
    
    CvCMatchState cvc_match_state;

    // Tutorial State
    int tutorial_step; 
    
    // Timers
    guint settings_timer_id;
    int tutorial_next_step; // For delayed transition
    GtkWidget* tutorial_msg; 
    GtkWidget* tutorial_exit_btn;
    gboolean tutorial_message_shown; 
    gboolean tutorial_wait; // To prevent rapid progression

    // Puzzle State
    int current_puzzle_idx;
    int puzzle_move_idx;
    int puzzle_last_processed_move; // Track processed move count from game logic
    gboolean puzzle_wait; // Waiting for opponent response
    
    // Settings State
    char last_settings_page[32];
    
    // Onboarding Timer (to cancel on destroy)
    guint onboarding_timer_id;
    GtkWidget* onboarding_popover;
} AppState;

#endif

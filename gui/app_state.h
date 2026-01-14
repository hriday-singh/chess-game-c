#ifndef APP_STATE_H
#define APP_STATE_H

#include <gtk/gtk.h>
#include "info_panel.h"
#include "board_theme_dialog.h"
#include "piece_theme_dialog.h"
#include "ai_dialog.h"
#include "config_manager.h"
#include "replay_controller.h"

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
    GtkWidget* history_btn;
    GtkWidget* exit_replay_btn;
    GtkWidget* settings_btn;
    GtkWidget* dark_mode_btn;

    // Dialogs & Panels
    BoardThemeDialog* theme_dialog;
    PieceThemeDialog* piece_theme_dialog;
    AiDialog* ai_dialog;
    struct _SettingsDialog* settings_dialog;
    struct _RightSidePanel* right_side_panel;
    struct _HistoryDialog* history_dialog;
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

// Add a match to history and save to disk
void match_history_add(MatchHistoryEntry* entry);

// Find a match by ID (returns pointer to internal list)
MatchHistoryEntry* match_history_find_by_id(const char* id);

// Delete a match by ID
void match_history_delete(const char* id);

typedef struct ReplayState {
    bool active;
    int current_ply;
    MatchHistoryEntry current_match;
} ReplayState;

typedef struct AppState {
    // Core Logic & Data
    GameLogic* logic;
    struct _AiController* ai_controller;
    ThemeData* theme;
    
    // Sub-states
    GuiState gui;
    TutorialState tutorial;
    PuzzleState puzzle;
    ReplayState replay;

    // Session State
    CvCMatchState cvc_match_state;
    char last_settings_page[32];
    
    // Analysis/Rating State
    int last_move_count;

    // Match Persistence
    bool match_saved;
    bool is_replaying;
    char* replay_match_id;
    ReplayController* replay_controller;

    // State restoration
    GameMode pre_replay_mode;
    Player pre_replay_side;

    // Timers
    guint settings_timer_id;
    guint onboarding_timer_id;
    guint ai_trigger_id;
} AppState;

#endif

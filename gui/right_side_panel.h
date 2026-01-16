#ifndef RIGHT_SIDE_PANEL_H
#define RIGHT_SIDE_PANEL_H

#include <gtk/gtk.h>
#include "theme_data.h"
#include "ai_analysis.h" // Full include for GameAnalysisResult visibility

// Navigation callbacks
typedef void (*RightSidePanelNavCallback)(const char* action, int ply_index, gpointer user_data);

typedef struct _RightSidePanel RightSidePanel;
struct _RightSidePanel {
    GtkWidget* container;   // Main horizontal container [Rail | Content]
    GameLogic* logic;
    ThemeData* theme;
    
    // Layout hierarchy
    GtkWidget* main_col;
    GtkWidget* pos_info;
    GtkWidget* eval_lbl;
    GtkWidget* mate_lbl;    // MATE IN X notice
    
    // Clock Labels (Removed)
    // GtkWidget* white_clock_lbl;
    // GtkWidget* black_clock_lbl;
    
    // Caching (Removed)
    
    GtkWidget* hanging_lbl;
    GtkWidget* analysis_side_lbl; // New: Analysis for [Side]
    
    // Feedback Zone (Middle of column)
    GtkWidget* feedback_zone;
    GtkWidget* feedback_rating_lbl;
    GtkWidget* feedback_desc_lbl;
    
    // Move History (Bottom of column)
    GtkWidget* history_zone;
    GtkWidget* history_list;
    GtkWidget* history_scrolled;

    // Analysis UI (New)
    GtkWidget* analysis_control_box;
    GtkWidget* analyze_btn;
    GtkWidget* analysis_stats_box;
    GtkWidget* accuracy_lbl;
    GtkWidget* acpl_lbl;
    GtkWidget* win_prob_lbl; // Win Probability Label
    
    // Footer Navigation (Removed)
    

    bool replay_lock;      // if true, ignore highlight clears
    int  locked_ply;       // last ply we want highlighted (optional)
    
    // State
    double current_eval;
    bool is_mate;
    int viewed_ply;
    int total_plies;
    int last_highlighted_ply; // NEW: For O(1) unhighlighting
    bool interactive;
    int last_feedback_ply; 
    bool flipped; // New member
    
    RightSidePanelNavCallback nav_cb;
    gpointer nav_cb_data;
};

RightSidePanel* right_side_panel_new(GameLogic* logic, ThemeData* theme);
void right_side_panel_free(RightSidePanel* panel);

GtkWidget* right_side_panel_get_widget(RightSidePanel* panel);

// Update functions
void right_side_panel_update_stats(RightSidePanel* panel, double evaluation, bool is_mate);
void right_side_panel_update_clock(RightSidePanel* panel);
void right_side_panel_set_mate_warning(RightSidePanel* panel, int moves);
void right_side_panel_set_hanging_pieces(RightSidePanel* panel, int white_count, int black_count);
void right_side_panel_show_rating_toast(RightSidePanel* panel, const char* rating, const char* reason, int ply_index);
void right_side_panel_show_toast(RightSidePanel* panel, const char* message);
void right_side_panel_set_interactive(RightSidePanel* panel, bool interactive);
void right_side_panel_set_visible(RightSidePanel* panel, bool visible);
void right_side_panel_set_analysis_visible(RightSidePanel* panel, bool visible);
void right_side_panel_sync_config(RightSidePanel* panel, const void* config); // Using void* to avoid circular dependency, cast in .c
void right_side_panel_set_flipped(RightSidePanel* panel, bool flipped); // New
void right_side_panel_set_analysis_result(RightSidePanel* panel, const GameAnalysisResult* res);

void right_side_panel_add_move(RightSidePanel* panel, Move move, int m_num, Player p);
void right_side_panel_add_move_notation(RightSidePanel* panel, const char* notation, PieceType p_type, int move_number, Player turn);
void right_side_panel_clear_history(RightSidePanel* panel);
void right_side_panel_set_current_move(RightSidePanel* panel, int move_index);
void right_side_panel_highlight_ply(RightSidePanel* panel, int ply_index);
void right_side_panel_refresh(RightSidePanel* panel);
void right_side_panel_scroll_to_top(RightSidePanel* panel);
void right_side_panel_scroll_to_bottom(RightSidePanel* panel);

void right_side_panel_set_nav_callback(RightSidePanel* panel, RightSidePanelNavCallback callback, gpointer user_data);
void right_side_panel_set_analyze_callback(RightSidePanel* panel, GCallback callback, gpointer user_data);
void right_side_panel_set_analyzing_state(RightSidePanel* panel, bool analyzing);

void right_side_panel_set_replay_lock(RightSidePanel* panel, bool locked);

#endif // RIGHT_SIDE_PANEL_H

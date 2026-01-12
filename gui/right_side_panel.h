#ifndef RIGHT_SIDE_PANEL_H
#define RIGHT_SIDE_PANEL_H

#include <gtk/gtk.h>
#include "gamelogic.h"  
#include "theme_data.h"

// Navigation callbacks
typedef void (*RightSidePanelNavCallback)(const char* action, int ply_index, gpointer user_data);

typedef struct _RightSidePanel RightSidePanel;
struct _RightSidePanel {
    GtkWidget* container;   // Main horizontal container [Rail | Content]
    GameLogic* logic;
    ThemeData* theme;
    
    // 1. Advantage Rail (Left side, full height)
    GtkWidget* adv_rail;    // Drawing area
    
    // 2. Main Content Column (Right side, vertical stack)
    GtkWidget* main_col;
    
    // Analysis Summary (Top of column)
    GtkWidget* pos_info;
    GtkWidget* eval_lbl;
    GtkWidget* mate_lbl;    // MATE IN X notice
    GtkWidget* hanging_lbl;
    
    // Feedback Zone (Middle of column)
    GtkWidget* feedback_zone;
    GtkWidget* feedback_rating_lbl;
    GtkWidget* feedback_desc_lbl;
    
    // Move History (Bottom of column)
    GtkWidget* history_zone;
    GtkWidget* history_list;
    GtkWidget* history_scrolled;
    
    // Footer Navigation
    GtkWidget* nav_box;
    GtkWidget* btn_start;
    GtkWidget* btn_prev;
    GtkWidget* btn_next;
    GtkWidget* btn_end;
    
    // State
    double current_eval;
    bool is_mate;
    int viewed_ply;
    int total_plies;
    bool interactive;
    int last_feedback_ply; 
    
    RightSidePanelNavCallback nav_cb;
    gpointer nav_cb_data;
};

RightSidePanel* right_side_panel_new(GameLogic* logic, ThemeData* theme);
void right_side_panel_free(RightSidePanel* panel);

GtkWidget* right_side_panel_get_widget(RightSidePanel* panel);

// Update functions
void right_side_panel_update_stats(RightSidePanel* panel, double evaluation, bool is_mate);
void right_side_panel_set_mate_warning(RightSidePanel* panel, int moves);
void right_side_panel_set_hanging_pieces(RightSidePanel* panel, int white_count, int black_count);
void right_side_panel_show_rating_toast(RightSidePanel* panel, const char* rating, const char* reason, int ply_index);
void right_side_panel_show_toast(RightSidePanel* panel, const char* message);
void right_side_panel_set_interactive(RightSidePanel* panel, bool interactive);
void right_side_panel_set_nav_visible(RightSidePanel* panel, bool visible);
void right_side_panel_set_analysis_visible(RightSidePanel* panel, bool visible);
void right_side_panel_sync_config(RightSidePanel* panel, const void* config); // Using void* to avoid circular dependency, cast in .c

void right_side_panel_add_move(RightSidePanel* panel, Move* move, int move_number, Player turn);
void right_side_panel_clear_history(RightSidePanel* panel);
void right_side_panel_set_current_move(RightSidePanel* panel, int move_index);
void right_side_panel_highlight_ply(RightSidePanel* panel, int ply_index);

void right_side_panel_set_nav_callback(RightSidePanel* panel, RightSidePanelNavCallback callback, gpointer user_data);

#endif // RIGHT_SIDE_PANEL_H

#include "info_panel.h"
#include "config_manager.h"
#include "replay_controller.h"
#include "app_state.h"
#include "board_widget.h"
#include "sound_engine.h"
#include "gamelogic.h"
#include "gui_utils.h"
#include <pango/pango.h>
#include <glib.h>
#include <stdlib.h>

static bool debug_mode = false;

// Simple list for captured pieces
typedef struct PieceTypeNode {
    PieceType type;
    struct PieceTypeNode* next;
} PieceTypeNode;

typedef struct {
    PieceTypeNode* head;
    int size;
} PieceTypeList;

static PieceTypeList* piece_type_list_create(void) {
    PieceTypeList* list = (PieceTypeList*)calloc(1, sizeof(PieceTypeList));
    return list;
}

static void piece_type_list_clear(PieceTypeList* list) {
    if (!list) return;
    PieceTypeNode* current = list->head;
    while (current) {
        PieceTypeNode* next = current->next;
        free(current);
        current = next;
    }
    list->head = NULL;
    list->size = 0;
}

static void piece_type_list_free(PieceTypeList* list) {
    if (list) {
        piece_type_list_clear(list);
        free(list);
    }
}

// InfoPanel structure
typedef struct {
    GameLogic* logic;
    GtkWidget* board_widget;
    
    // UI elements
    GtkWidget* scroll_content;
    GtkWidget* status_label;
    GtkWidget* white_captures_box;
    GtkWidget* black_captures_box;
    GtkWidget* black_label;  // "Captured by Black:" label
    GtkWidget* white_label;  // "Captured by White:" label
    GtkWidget* undo_button;
    GtkWidget* reset_button;
    
    // Game settings
    GtkWidget* game_mode_dropdown;
    GtkWidget* play_as_dropdown;
    
    // CvC Match Controls
    GtkWidget* cvc_start_btn;
    GtkWidget* cvc_pause_btn;
    GtkWidget* cvc_stop_btn;
    CvCMatchState cvc_state;
    CvCControlCallback cvc_callback;
    gpointer cvc_callback_data;
    
    // Visual settings
    GtkWidget* enable_animations_check;
    GtkWidget* hints_dropdown; // Replaced toggle buttons
    GtkWidget* enable_sfx_check;

    // Tutorial Mode UI
    struct {
        GtkWidget* box;
        GtkWidget* instruction_label;
        GtkWidget* learning_label;
        GtkWidget* reset_btn;
        GtkWidget* exit_btn;
    } tutorial_ui;
    
    // Captured pieces lists
    PieceTypeList* white_captures;
    PieceTypeList* black_captures;
    
    ThemeData* theme;
    
    // AI Settings
    GtkWidget* ai_settings_section;
    GtkWidget* ai_settings_btn; // Button to open dialog
    GCallback ai_settings_callback;
    gpointer ai_settings_callback_data;
    
    struct {
        GtkWidget* box;
        GtkWidget* title_label;
        GtkWidget* engine_dropdown;
        GtkWidget* elo_box;
        GtkWidget* elo_slider;
        GtkWidget* elo_spin;
        GtkWidget* adv_box;
        GtkWidget* depth_label;
    } white_ai, black_ai;

    bool custom_available;

    // Clock Settings
    GtkWidget* clock_settings_box;
    GtkWidget* clock_preset_dropdown;
    GtkWidget* clock_custom_box;
    GtkWidget* clock_min_spin;
    GtkWidget* clock_inc_spin;

    // Puzzle Mode UI
    struct {
        GtkWidget* box;
        GtkWidget* title_label;
        GtkWidget* desc_label;
        GtkWidget* status_label;
        GtkWidget* next_btn;
        GtkWidget* reset_btn;
        GtkWidget* puzzle_list_box;
        GtkWidget* puzzle_scroll;
        GtkWidget* exit_btn;
    } puzzle_ui;
    
    GtkWidget* standard_controls_box; // Container for normal game controls
    
    // Game reset callback to trigger AI
    GameResetCallback game_reset_callback;
    gpointer game_reset_callback_data;

    // Undo callback to refresh analysis
    UndoCallback undo_callback;
    gpointer undo_callback_data;
    // Replay Mode UI
    struct {
        GtkWidget* box;
        GtkWidget* play_pause_btn;
        // Stop button removed
        GtkWidget* prev_btn;
        GtkWidget* next_btn;
        GtkWidget* start_btn;
        GtkWidget* end_btn;
        GtkWidget* exit_btn;
        GtkWidget* speed_scale;
        GtkWidget* start_here_btn;
        GtkWidget* status_label;
        GtkWidget* speed_label;
        GtkWidget* anim_check;
        GtkWidget* sfx_check;
        
        // Playback slider
        GtkWidget* playback_slider;
        
        // Replay specific capture boxes
        GtkWidget* black_label;
        GtkWidget* white_label;
        GtkWidget* white_captures_box;
        GtkWidget* black_captures_box;
    } replay_ui;
    
    GCallback replay_exit_callback;
    gpointer replay_exit_data;

} InfoPanel;

// Replay UI Callbacks
static void on_replay_play_pause_clicked(GtkButton* btn, gpointer user_data);
// Stop callback removed
static void on_replay_speed_changed(GtkRange* range, gpointer user_data);
static void on_replay_start_here_clicked(GtkButton* btn, gpointer user_data);
static void on_replay_prev_clicked(GtkButton* btn, gpointer user_data);
static void on_replay_next_clicked(GtkButton* btn, gpointer user_data);
static void on_replay_start_clicked(GtkButton* btn, gpointer user_data);
static void on_replay_end_clicked(GtkButton* btn, gpointer user_data);
static void on_replay_exit_clicked(GtkButton* btn, gpointer user_data);
static void on_replay_slider_value_changed(GtkRange* range, gpointer user_data);

// Forward declarations
static GtkWidget* create_piece_widget(InfoPanel* panel, PieceType type, Player owner);
static void draw_graveyard_piece(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);

// Forward declarations (after InfoPanel is defined)
static void update_captured_labels(InfoPanel* panel);
static void on_sfx_toggled(GtkCheckButton* button, gpointer user_data);

static void on_focus_lost_gesture(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    GtkWidget* widget = GTK_WIDGET(user_data);
    gtk_widget_grab_focus(widget);
}

// AI Settings Callbacks
static void on_engine_selection_changed(GObject* obj, GParamSpec* pspec, gpointer user_data);
static void on_hints_mode_changed(GObject* obj, GParamSpec* pspec, gpointer user_data);

// Clock Callbacks
static void on_clock_preset_changed(GObject* obj, GParamSpec* pspec, gpointer user_data);
static void on_clock_custom_changed(GtkSpinButton* spin, gpointer user_data);
static void on_open_ai_settings_clicked(GtkButton* btn, gpointer user_data);

// CvC Callbacks
static void on_cvc_start_clicked(GtkButton* btn, gpointer user_data);
static void on_cvc_pause_clicked(GtkButton* btn, gpointer user_data);
static void on_cvc_stop_clicked(GtkButton* btn, gpointer user_data);

static void info_panel_create_replay_ui(InfoPanel* panel);

static void reset_game(InfoPanel* panel);
static void update_ai_settings_visibility(InfoPanel* panel);
static void on_elo_adjustment_changed(GtkAdjustment* adj, gpointer user_data);
static void on_clock_preset_changed(GObject* obj, GParamSpec* pspec, gpointer user_data);
static void on_clock_custom_changed(GtkSpinButton* spin, gpointer user_data);

typedef struct {
    InfoPanel* panel;
    bool is_black;
} SideCallbackData;

static void
set_ai_adv_ui(GtkWidget *elo_box,
              GtkWidget *adv_box,
              GtkLabel  *depth_label,
              gboolean   adv,
              int        depth)
{
    if (debug_mode) {
        printf("[InfoPanel] set_ai_adv_ui: adv=%d, depth=%d, elo_box=%p, adv_box=%p\n",
               adv, depth, (void*)elo_box, (void*)adv_box);
    }
    
    gtk_widget_set_visible(elo_box, !adv);
    gtk_widget_set_visible(adv_box,  adv);

    if (!adv) return;

    gtk_label_set_xalign(depth_label, 0.0f);

    g_autofree char *depth_markup = g_strdup_printf(
        "Depth\n<span size='xx-large' weight='bold'>%d</span>",
        depth
    );

    gtk_label_set_use_markup(depth_label, TRUE);
    gtk_label_set_markup(depth_label, depth_markup);
}

// Helper to build a side's AI settings block
static GtkWidget* create_ai_side_block(InfoPanel* panel, bool is_black, const char* title) {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_bottom(vbox, 15);

    // Title
    GtkWidget* title_label = gtk_label_new(title);
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_append(GTK_BOX(vbox), title_label);

    // Engine Choice
    GtkWidget* engine_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(engine_hbox), gtk_label_new("Engine:"));
    
    GtkWidget* dropdown = gtk_drop_down_new_from_strings((const char*[]){"Inbuilt Stockfish 17.1", "Add Custom Bot...", NULL});
    g_object_set_data(G_OBJECT(dropdown), "is-black", GINT_TO_POINTER(is_black));
    g_signal_connect(dropdown, "notify::selected", G_CALLBACK(on_engine_selection_changed), panel);
    gtk_box_append(GTK_BOX(engine_hbox), dropdown);
    gtk_box_append(GTK_BOX(vbox), engine_hbox);

    // ELO Box
    GtkWidget* elo_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(elo_box), gtk_label_new("ELO Difficulty:"));
    
    GtkAdjustment* adj = gtk_adjustment_new(1500, 100, 3600, 50, 500, 0);
    g_signal_connect(adj, "value-changed", G_CALLBACK(on_elo_adjustment_changed), panel);
    GtkWidget* elo_slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(elo_slider), FALSE);
    gtk_box_append(GTK_BOX(elo_box), elo_slider);
    
    GtkWidget* elo_spin = gtk_spin_button_new(adj, 50, 0);
    gtk_box_append(GTK_BOX(elo_box), elo_spin);
    gtk_box_append(GTK_BOX(vbox), elo_box);

    // Advanced Info Box
    GtkWidget* adv_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_visible(adv_box, FALSE);
    
    GtkWidget* depth_label = gtk_label_new("Depth: 10");
    gtk_widget_set_halign(depth_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(adv_box), depth_label);
    gtk_box_append(GTK_BOX(vbox), adv_box);

    // Save references to panel
    if (is_black) {
        panel->black_ai.box = vbox;
        panel->black_ai.title_label = title_label;
        panel->black_ai.engine_dropdown = dropdown;
        panel->black_ai.elo_box = elo_box;
        panel->black_ai.elo_slider = elo_slider;
        panel->black_ai.elo_spin = elo_spin;
        panel->black_ai.adv_box = adv_box;
        panel->black_ai.depth_label = depth_label;
    } else {
        panel->white_ai.box = vbox;
        panel->white_ai.title_label = title_label;
        panel->white_ai.engine_dropdown = dropdown;
        panel->white_ai.elo_box = elo_box;
        panel->white_ai.elo_slider = elo_slider;
        panel->white_ai.elo_spin = elo_spin;
        panel->white_ai.adv_box = adv_box;
        panel->white_ai.depth_label = depth_label;
    }

    return vbox;
}

// Get piece material value (for point calculation)
static int get_piece_value(PieceType type) {
    switch (type) {
        case PIECE_PAWN: return 1;
        case PIECE_KNIGHT: return 3;
        case PIECE_BISHOP: return 3;
        case PIECE_ROOK: return 5;
        case PIECE_QUEEN: return 9;
        default: return 0;
    }
}

// Get captured pieces from game logic
static void update_captured_pieces(InfoPanel* panel) {
    if (!panel || !panel->logic) return;
    
    // Clear existing lists
    piece_type_list_clear(panel->white_captures);
    piece_type_list_clear(panel->black_captures);
    
    // Get captured pieces from game logic
    gamelogic_get_captured_pieces(panel->logic, PLAYER_WHITE, panel->white_captures);
    gamelogic_get_captured_pieces(panel->logic, PLAYER_BLACK, panel->black_captures);

    // Swap destination boxes if board is flipped
    bool flipped = board_widget_is_flipped(panel->board_widget);
    GtkWidget* w_box = flipped ? panel->black_captures_box : panel->white_captures_box;
    GtkWidget* b_box = flipped ? panel->white_captures_box : panel->black_captures_box;
    GtkWidget* rw_box = flipped ? panel->replay_ui.black_captures_box : panel->replay_ui.white_captures_box;
    GtkWidget* rb_box = flipped ? panel->replay_ui.white_captures_box : panel->replay_ui.black_captures_box;

    // Clear the capture boxes (GtkBox) - remove all children
    GtkWidget* boxes[] = {panel->white_captures_box, panel->black_captures_box, 
                          panel->replay_ui.white_captures_box, panel->replay_ui.black_captures_box};
    for (int i = 0; i < 4; i++) {
        if (!boxes[i]) continue;
        GtkWidget* child = gtk_widget_get_first_child(boxes[i]);
        while (child) {
            GtkWidget* next = gtk_widget_get_next_sibling(child);
            gtk_box_remove(GTK_BOX(boxes[i]), child);
            child = next;
        }
    }
    
    // Add white captures (pieces captured by white = black pieces) - max 7 pieces
    if (panel->white_captures && panel->white_captures->head) {
        PieceTypeNode* current = panel->white_captures->head;
        int count = 0;
        int total = 0;
        while (current) {
            total++;
            if (count < 6) {
                GtkWidget* widget = create_piece_widget(panel, current->type, PLAYER_BLACK);
                gtk_box_append(GTK_BOX(w_box), widget);
                
                if (rw_box) {
                    GtkWidget* r_widget = create_piece_widget(panel, current->type, PLAYER_BLACK);
                    gtk_box_append(GTK_BOX(rw_box), r_widget);
                }
                count++;
            }
            current = current->next;
        }
        // Show "+N" if there are more than 6 pieces
        if (total > 6) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "+%d", total - 6);
            GtkWidget* plus_label = gtk_label_new(buffer);
            gtk_widget_add_css_class(plus_label, "capture-count");
            gtk_box_append(GTK_BOX(w_box), plus_label);
            
            if (rw_box) {
                GtkWidget* r_plus = gtk_label_new(buffer);
                gtk_widget_add_css_class(r_plus, "capture-count");
                gtk_box_append(GTK_BOX(rw_box), r_plus);
            }
        }
    }
    
    // Add black captures (pieces captured by black = white pieces) - max 7 pieces
    if (panel->black_captures && panel->black_captures->head) {
        PieceTypeNode* current = panel->black_captures->head;
        int count = 0;
        int total = 0;
        while (current) {
            total++;
            if (count < 6) {
                GtkWidget* widget = create_piece_widget(panel, current->type, PLAYER_WHITE);
                gtk_box_append(GTK_BOX(b_box), widget);
                
                if (rb_box) {
                    GtkWidget* r_widget = create_piece_widget(panel, current->type, PLAYER_WHITE);
                    gtk_box_append(GTK_BOX(rb_box), r_widget);
                }
                count++;
            }
            current = current->next;
        }
        // Show "+N" if there are more than 6 pieces
        if (total > 6) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "+%d", total - 6);
            GtkWidget* plus_label = gtk_label_new(buffer);
            gtk_widget_add_css_class(plus_label, "capture-count");
            gtk_box_append(GTK_BOX(b_box), plus_label);
            
            if (rb_box) {
                GtkWidget* r_plus = gtk_label_new(buffer);
                gtk_widget_add_css_class(r_plus, "capture-count");
                gtk_box_append(GTK_BOX(rb_box), r_plus);
            }
        }
    }
    
    // Update labels with points
    update_captured_labels(panel);
}

// Draw function for graveyard piece
static void draw_graveyard_piece(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    if (!gtk_widget_get_realized(GTK_WIDGET(area)) || !gtk_widget_get_visible(GTK_WIDGET(area)) || width <= 1 || height <= 1) return;
    (void)area;
    // Data passed is [type, owner] encoded as int or struct
    // We'll pack it into a pointer: owner << 8 | type
    int data = GPOINTER_TO_INT(user_data);
    Player owner = (Player)((data >> 8) & 0xFF);
    PieceType type = (PieceType)(data & 0xFF);
    
    // Get panel from widget data (stored on creation)
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(area), "panel");
    if (!panel || !panel->theme) return;
    
    // Use shared cache
    cairo_surface_t* surface = theme_data_get_piece_surface(panel->theme, type, owner);
    
    if (surface) {
        cairo_save(cr);
        
        int surf_w = cairo_image_surface_get_width(surface);
        int surf_h = cairo_image_surface_get_height(surface);
        
        // Scale to fit (roughly 85% of width/height)
        double scale = (double)height * 0.85 / surf_h;
        
        // Center
        double draw_w = surf_w * scale;
        double draw_h = surf_h * scale;
        double offset_x = (width - draw_w) / 2.0;
        double offset_y = (height - draw_h) / 2.0;
        
        cairo_translate(cr, offset_x, offset_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint_with_alpha(cr, 0.95); // High opacity for clarity
        cairo_restore(cr);
    } else {       
        // Match board_widget.c 'draw_piece_graphic' logic exactly for consistency
        const char* symbol = theme_data_get_piece_symbol(panel->theme, type, owner);
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* desc = pango_font_description_new();
        pango_font_description_set_family(desc, "Segoe UI Symbol");
        pango_font_description_set_size(desc, (int)(height * 0.7 * PANGO_SCALE)); 
        pango_font_description_set_weight(desc, PANGO_WEIGHT_SEMIBOLD);
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_text(layout, symbol ? symbol : "?", -1);
        
        int w, h;
        pango_layout_get_pixel_size(layout, &w, &h);
        double px = (width - w) / 2.0;
        double py = (height - h) / 2.0;
        
        cairo_move_to(cr, px, py);
        
        if (owner == PLAYER_WHITE) {
            double r, g, b, sr, sg, sb, cw;
            theme_data_get_white_piece_color(panel->theme, &r, &g, &b);
            theme_data_get_white_piece_stroke(panel->theme, &sr, &sg, &sb);
            cw = theme_data_get_white_stroke_width(panel->theme);
            
            cairo_set_source_rgb(cr, r, g, b);
            pango_cairo_layout_path(cr, layout);
            cairo_fill_preserve(cr);
            
            cairo_set_source_rgb(cr, sr, sg, sb);
            cairo_set_line_width(cr, cw);
            cairo_stroke(cr);
        } else {
            double r, g, b, sr, sg, sb, cw;
            theme_data_get_black_piece_color(panel->theme, &r, &g, &b);
            theme_data_get_black_piece_stroke(panel->theme, &sr, &sg, &sb);
            cw = theme_data_get_black_stroke_width(panel->theme);
            
            cairo_set_source_rgb(cr, r, g, b);
            pango_cairo_layout_path(cr, layout);
            cairo_fill_preserve(cr);
            
            if (cw > 0.0) {
                cairo_set_source_rgb(cr, sr, sg, sb);
                cairo_set_line_width(cr, cw);
                cairo_stroke(cr);
            } else {
                 cairo_new_path(cr);
            }
        }
        
        pango_font_description_free(desc);
        g_object_unref(layout);
    }
}

static GtkWidget* create_piece_widget(InfoPanel* panel, PieceType type, Player owner) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 32, 32); // Refined size for graveyard
    
    // Store panel for callback
    g_object_set_data(G_OBJECT(area), "panel", panel);
    
    // Pack data: owner << 8 | type
    int data = (owner << 8) | type;
    
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), 
                                  draw_graveyard_piece, 
                                  GINT_TO_POINTER(data), NULL);
                                  
    return area;
}

// Calculate material points for a captured pieces list
static int calculate_captured_points(PieceTypeList* captures) {
    int points = 0;
    if (captures && captures->head) {
        PieceTypeNode* current = captures->head;
        while (current) {
            points += get_piece_value(current->type);
            current = current->next;
        }
    }
    return points;
}

// Update status display (without points - points are shown next to labels)
static void update_status_display(InfoPanel* panel) {
    if (!panel || !panel->logic || !panel->status_label) return;
    
    const char* status = gamelogic_get_status_message(panel->logic);
    if (panel->status_label && GTK_IS_LABEL(panel->status_label)) {
        gtk_label_set_text(GTK_LABEL(panel->status_label), status);
    }
}

// Update captured pieces labels with relative points
static void update_captured_labels(InfoPanel* panel) {
    if (!panel) return;
    
    bool flipped = board_widget_is_flipped(panel->board_widget);

    // Calculate points
    int black_points = calculate_captured_points(panel->black_captures);
    int white_points = calculate_captured_points(panel->white_captures);
    
    // Calculate relative points (difference)
    int point_diff = white_points - black_points;
    
    // Determine which labels correspond to Black/White based on flipping
    // panel->black_label is Top, panel->white_label is Bottom
    GtkWidget* target_black = flipped ? panel->white_label : panel->black_label;
    GtkWidget* target_white = flipped ? panel->black_label : panel->white_label;

    GtkWidget* r_target_black = flipped ? panel->replay_ui.white_label : panel->replay_ui.black_label;
    GtkWidget* r_target_white = flipped ? panel->replay_ui.black_label : panel->replay_ui.white_label;

    // 1. Update Black Capturers
    char black_text[128];
    gtk_widget_remove_css_class(target_black, "captured-score-black");
    if (r_target_black) gtk_widget_remove_css_class(r_target_black, "captured-score-black");

    if (point_diff < 0) {
        // Black is ahead
        int diff = -point_diff;
        snprintf(black_text, sizeof(black_text), "Captured by Black: +%d", diff);
        gtk_label_set_text(GTK_LABEL(target_black), black_text);
        gtk_widget_add_css_class(target_black, "captured-score-black");
        
        if (r_target_black) {
            gtk_label_set_text(GTK_LABEL(r_target_black), black_text);
            gtk_widget_add_css_class(r_target_black, "captured-score-black");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(target_black), "Captured by Black:");
        if (r_target_black) {
             gtk_label_set_text(GTK_LABEL(r_target_black), "Captured by Black:");
        }
    }
    
    // 2. Update White Capturers
    char white_text[128];
    gtk_widget_remove_css_class(target_white, "captured-score-white");
    if (r_target_white) gtk_widget_remove_css_class(r_target_white, "captured-score-white");

    if (point_diff > 0) {
        // White is ahead
        snprintf(white_text, sizeof(white_text), "Captured by White: +%d", point_diff);
        gtk_label_set_text(GTK_LABEL(target_white), white_text);
        gtk_widget_add_css_class(target_white, "captured-score-white");
        
        if (r_target_white) {
            gtk_label_set_text(GTK_LABEL(r_target_white), white_text);
            gtk_widget_add_css_class(r_target_white, "captured-score-white");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(target_white), "Captured by White:");
        if (r_target_white) {
             gtk_label_set_text(GTK_LABEL(r_target_white), "Captured by White:");
        }
    }
}

// Update visibility of AI settings based on game mode
static void update_ai_settings_visibility(InfoPanel* panel) {
    if (!panel || !panel->ai_settings_section || !panel->logic) return;
    
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->game_mode_dropdown));
    bool show_ai = (selected == GAME_MODE_PVC || selected == GAME_MODE_CVC);
    bool show_cvc = (selected == GAME_MODE_CVC);
    
    gtk_widget_set_visible(panel->ai_settings_section, show_ai);
    
    // Toggle controls visibility
    if (show_cvc) {
        // Show CvC controls based on state
        bool show_start = (panel->cvc_state == CVC_STATE_STOPPED);
        bool show_pause = (panel->cvc_state != CVC_STATE_STOPPED);
        bool show_stop = (panel->cvc_state != CVC_STATE_STOPPED);
        
        gtk_widget_set_visible(panel->cvc_start_btn, show_start);
        gtk_widget_set_visible(panel->cvc_pause_btn, show_pause);
        gtk_widget_set_visible(panel->cvc_stop_btn, show_stop);
        
    } else {
        gtk_widget_set_visible(panel->cvc_start_btn, FALSE);
        gtk_widget_set_visible(panel->cvc_pause_btn, FALSE);
        gtk_widget_set_visible(panel->cvc_stop_btn, FALSE);
    }
    
    // Ensure Undo/Reset are always visible (user request)
    gtk_widget_set_visible(panel->undo_button, TRUE);
    gtk_widget_set_visible(panel->reset_button, TRUE);
    
    // Disable "Play as" in CvC mode
    gtk_widget_set_sensitive(panel->play_as_dropdown, !show_cvc);

    if (show_ai) {
        if (selected == GAME_MODE_PVC) {
            Player human_side = panel->logic->playerSide;
            guint play_as = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->play_as_dropdown));
            
            if (play_as == 2) { // Random
                gtk_widget_set_visible(panel->white_ai.box, TRUE);
                gtk_widget_set_visible(panel->black_ai.box, FALSE);
                gtk_label_set_text(GTK_LABEL(panel->white_ai.title_label), "AI Player");
            } else {
                bool is_white = (human_side == PLAYER_WHITE);
                gtk_widget_set_visible(panel->white_ai.box, !is_white);
                gtk_widget_set_visible(panel->black_ai.box, is_white);
                
                if (is_white) gtk_label_set_text(GTK_LABEL(panel->black_ai.title_label), "AI Player");
                else gtk_label_set_text(GTK_LABEL(panel->white_ai.title_label), "AI Player");
            }
        } else {
            // CvC: Show both
            gtk_widget_set_visible(panel->white_ai.box, TRUE);
            gtk_widget_set_visible(panel->black_ai.box, TRUE);
            gtk_label_set_text(GTK_LABEL(panel->white_ai.title_label), "White AI Player");
            gtk_label_set_text(GTK_LABEL(panel->black_ai.title_label), "Black AI Player");
        }
    }
}

static void on_engine_selection_changed(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    InfoPanel* panel = (InfoPanel*)user_data;
    GtkDropDown* dropdown = GTK_DROP_DOWN(obj);
    int selected = gtk_drop_down_get_selected(dropdown);

    // 0: Inbuilt
    // 1: Add Custom (if not available) OR Custom (if available)
    // 2: Add Custom (if available)
    bool triggered = false;
    if (!panel->custom_available && selected == 1) triggered = true;
    else if (panel->custom_available && selected == 2) triggered = true;

    if (triggered) {
        // Reset to Inbuilt and open dialog at tab 1 (Custom Engine)
        g_signal_handlers_block_by_func(dropdown, on_engine_selection_changed, panel);
        gtk_drop_down_set_selected(dropdown, 0); 
        g_signal_handlers_unblock_by_func(dropdown, on_engine_selection_changed, panel);
        
        if (panel->ai_settings_callback) {
            void (*cb)(int, gpointer) = (void (*)(int, gpointer))panel->ai_settings_callback;
            cb(1, panel->ai_settings_callback_data);
        }
    }

    reset_game(panel);
}

// ELO sync callbacks
static void on_open_ai_settings_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    InfoPanel* panel = (InfoPanel*)user_data;
    if (panel->ai_settings_callback) {
        ((void (*)(gpointer))panel->ai_settings_callback)(panel->ai_settings_callback_data);
    }
}

static void on_cvc_start_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    InfoPanel* panel = (InfoPanel*)user_data;
    
    // If stopped, reset board before starting
    if (panel->cvc_state == CVC_STATE_STOPPED) {
        reset_game(panel);
    }
    
    if (panel->cvc_callback) {
        panel->cvc_callback(CVC_STATE_RUNNING, panel->cvc_callback_data);
    }
}

static void on_cvc_pause_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    InfoPanel* panel = (InfoPanel*)user_data;
    CvCMatchState new_state = (panel->cvc_state == CVC_STATE_PAUSED) ? CVC_STATE_RUNNING : CVC_STATE_PAUSED;
    if (panel->cvc_callback) {
        panel->cvc_callback(new_state, panel->cvc_callback_data);
    }
}

static void on_cvc_stop_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    InfoPanel* panel = (InfoPanel*)user_data;
    if (panel->cvc_callback) {
        panel->cvc_callback(CVC_STATE_STOPPED, panel->cvc_callback_data);
    }
    // reset_game(panel); // REMOVED: User wants board to freeze on stop, not reset
}

static void on_elo_adjustment_changed(GtkAdjustment* adj, gpointer user_data) {
    (void)adj;
    InfoPanel* panel = (InfoPanel*)user_data;
    reset_game(panel);
}

// Callback for save dialog response
static void on_save_dialog_response(GObject* source, GAsyncResult* result, gpointer user_data) {
    GtkAlertDialog* dialog = GTK_ALERT_DIALOG(source);
    InfoPanel* panel = (InfoPanel*)user_data;
    
    GError* error = NULL;
    int response = gtk_alert_dialog_choose_finish(dialog, result, &error);
    
    if (error) {
        g_error_free(error);
        return;  // User cancelled or error occurred
    }
    
    // Response: 0 = Yes (save), 1 = No (don't save)
    // Note: Actual saving would need to be implemented via callback to main.c
    // For now, both options proceed with reset
    if (response == 0 || response == 1) {
        // User made a choice, proceed with reset
        reset_game(panel);
    }
    // Cancelled: Do nothing
}

// Show save confirmation dialog
static void show_save_before_reset_dialog(InfoPanel* panel) {
    if (!panel || !panel->board_widget) return;
    
    GtkWindow* parent = gui_utils_get_root_window(panel->board_widget);
    if (!parent) return;
    
    GtkAlertDialog* dialog = gtk_alert_dialog_new("%s", "Save Game?");
    gtk_alert_dialog_set_detail(dialog, "Would you like to save this game to your match history before starting a new one?");
    
    const char* buttons[] = {"Yes", "No", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 1);  // No button index
    gtk_alert_dialog_set_default_button(dialog, 0);  // Yes is default
    
    gtk_alert_dialog_choose(dialog, parent, NULL, on_save_dialog_response, panel);
}

static void on_reset_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    InfoPanel* panel = (InfoPanel*)user_data;
    
    // Check if there are at least 5 complete moves (10 plies)
    if (panel && panel->logic) {
        int move_count = gamelogic_get_move_count(panel->logic);
        if (move_count >= 10) {
            // Show save dialog
            show_save_before_reset_dialog(panel);
            return;
        }
    }
    
    // Less than 5 moves, reset directly without asking
    sound_engine_play(SOUND_RESET);
    reset_game(panel);
}

// Undo button callback
static void on_undo_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->logic) return;
    
    // Determine how many moves to undo based on mode
    int moves_to_undo = 0;
    
    if (panel->logic->gameMode == GAME_MODE_PVP) {
        // PvP: Undo 1 move
        moves_to_undo = 1;
    } else if (panel->logic->gameMode == GAME_MODE_CVC) {
        // CvC: Undo 1 move
        moves_to_undo = 1;
    } else if (panel->logic->gameMode == GAME_MODE_PVC) {
        // PvC: Dynamic
        Player current_turn = gamelogic_get_turn(panel->logic);
        bool is_ai_turn = gamelogic_is_computer(panel->logic, current_turn);
        
        if (is_ai_turn) {
            // AI is thinking (Player just moved) -> Undo 1 (Player's move)
            moves_to_undo = 1;
            
            // Also stop AI if it's thinking!
            // We need to tell main.c to stop thinking or ignore result?
            // Since we don't have direct access to AppState here easily without casting user_data or similar...
            // But board_widget usually has the logic.
            // For now, gamelogic_undo_move updates state which might invalidat AI move?
        } else {
            // It's Player's turn (AI already moved) -> Undo 2 (AI + Player)
            moves_to_undo = 2;
        }
    } else {
        // Default (Puzzle, etc)
        moves_to_undo = 1; 
    }
    
    // Execute undos
    for (int i = 0; i < moves_to_undo; i++) {
        if (gamelogic_get_move_count(panel->logic) > 0) {
            gamelogic_undo_move(panel->logic);
        }
    }
    
    // Reset selection and update
    board_widget_reset_selection(panel->board_widget);
    board_widget_refresh(panel->board_widget);
    update_captured_pieces(panel);
    // The exported info_panel_* APIs expect the root info panel widget (the scrolled window)
    GtkWidget* info_panel_widget = gtk_widget_get_parent(panel->scroll_content);
    if (info_panel_widget) {
        info_panel_update_status(info_panel_widget);
    }

    // Trigger undo callback to refresh live analysis
    if (panel->undo_callback) {
        panel->undo_callback(panel->undo_callback_data);
    }
}

// Helper function to reset game (used by reset button and settings changes)
static void reset_game(InfoPanel* panel) {
    if (!panel || !panel->logic) return;
    
    // Stop CvC if running
    if (panel->logic->gameMode == GAME_MODE_CVC && panel->cvc_state != CVC_STATE_STOPPED) {
        if (panel->cvc_callback) {
            panel->cvc_callback(CVC_STATE_STOPPED, panel->cvc_callback_data);
        }
    }
    
    // Play As Selection - ONLY set the Logic state here
    // The actual board flipping will be handled by the game_reset_callback (main.c:on_game_reset)
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->play_as_dropdown));
    
    if (selected == 0) { // White
        panel->logic->playerSide = PLAYER_WHITE;
    } else if (selected == 1) { // Black
        panel->logic->playerSide = PLAYER_BLACK;
    } else if (selected == 2) { // Random
        // Randomly pick White (0) or Black (1)
        int rand_side = (g_random_int() % 2);
        panel->logic->playerSide = (rand_side == 1) ? PLAYER_BLACK : PLAYER_WHITE;
    }
    
    // Clear selection on reset (UI request)
    board_widget_reset_selection(panel->board_widget);

    // Delegate EVERYTHING else to the callback (main.c)
    // This prevents double-resets and fighting over board state
    if (panel->game_reset_callback) {
        panel->game_reset_callback(panel->game_reset_callback_data);
    } else {
        // Fallback if no callback (shouldn't happen in main app)
        gamelogic_reset(panel->logic);
        bool flip = (panel->logic->playerSide == PLAYER_BLACK);
        board_widget_set_flipped(panel->board_widget, flip);
        board_widget_refresh(panel->board_widget);
    }
    
    // Update panel display
    update_status_display(panel);
    update_captured_pieces(panel);
    
    GtkWidget* info_panel_widget = gtk_widget_get_parent(panel->scroll_content);
    if (info_panel_widget) {
        info_panel_update_status(info_panel_widget);
    }
    
    // Refresh board display
    board_widget_refresh(panel->board_widget);
    
    // Trigger AI if needed (PvC with AI to move, or CvC)
    if (panel->game_reset_callback) {
        panel->game_reset_callback(panel->game_reset_callback_data);
    }
    
}


// Game mode dropdown callback - reset game on change
static void on_game_mode_changed(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->logic) return;
    
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->game_mode_dropdown));
    
    // If Puzzles is selected (index 3), trigger the puzzles menu action
    if (selected == GAME_MODE_PUZZLE) {
        // Find the main window and trigger the puzzles action
        GtkWidget* toplevel = gtk_widget_get_ancestor(GTK_WIDGET(panel->game_mode_dropdown), GTK_TYPE_WINDOW);
        if (toplevel) {
            GtkApplication* app = gtk_window_get_application(GTK_WINDOW(toplevel));
            if (app && G_IS_ACTION_GROUP(app)) {
                g_action_group_activate_action(G_ACTION_GROUP(app), "open-puzzles", NULL);
            } else {
                printf("[InfoPanel] ERROR: Application not valid for launching puzzles!\n");
            }
            gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->game_mode_dropdown), GAME_MODE_PVC);
        }
        // Reset dropdown back to previous mode (will be set by puzzle logic)
        return;
    }
    
    panel->logic->gameMode = (GameMode)selected;
    
    // Save to config
    AppConfig* cfg = config_get();
    cfg->game_mode = selected;
    config_save();
    
    // Update AI settings visibility
    update_ai_settings_visibility(panel);
    
    // Reset game when mode changes
    reset_game(panel);
}

// Play As dropdown callback
static void on_play_as_changed(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->logic) return;
    
    int selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->play_as_dropdown));
    
    // Save to config
    AppConfig* cfg = config_get();
    cfg->play_as = selected;
    config_save();
    
    // Reset game to apply new side
    reset_game(panel);
}

// Animations checkbox callback
static void on_animations_toggled(GtkCheckButton* button, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->board_widget) return;
    
    bool enabled = gtk_check_button_get_active(button);
    board_widget_set_animations_enabled(panel->board_widget, enabled);
    
    // Save to config
    AppConfig* cfg = config_get();
    cfg->enable_animations = enabled;
    config_save();
}

// SFX checkbox callback
static void on_sfx_toggled(GtkCheckButton* button, gpointer user_data) {
    (void)user_data;  // Unused
    bool enabled = gtk_check_button_get_active(button);
    sound_engine_set_enabled(enabled ? 1 : 0);
    
    // Save to config
    AppConfig* cfg = config_get();
    cfg->enable_sfx = enabled;
    config_save();
}

// Hints dropdown callback
static void on_hints_mode_changed(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->board_widget) return;
    
    GtkDropDown* dropdown = GTK_DROP_DOWN(obj);
    int selected = gtk_drop_down_get_selected(dropdown);
    
    // 0: Dots, 1: Squares
    bool use_dots = (selected == 0);
    board_widget_set_hints_mode(panel->board_widget, use_dots);
    board_widget_refresh(panel->board_widget);
    
    // Save to config
    AppConfig* cfg = config_get();
    cfg->hints_dots = use_dots;
    config_save();
}

static GtkWidget* create_clock_settings_ui(InfoPanel* panel) {
    if (!panel) return NULL;
    
    panel->clock_settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(panel->clock_settings_box, 10);
    
    GtkWidget* label = gtk_label_new("CLOCK SETTINGS");
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(label), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(panel->clock_settings_box), label);
    
    // Dropdown
    const char* presets[] = { 
        "No Clock", 
        "Bullet 1 min", "Bullet 1 + 1", "Bullet 2 + 1",
        "Blitz 3 min", "Blitz 3 + 2", "Blitz 5 min", "Blitz 5 + 3",
        "Rapid 10 min", "Rapid 10 + 5", "Rapid 15 + 10",
        "Classical 30 min", "Classical 30 + 20",
        "Custom", 
        NULL 
    };
    
    panel->clock_preset_dropdown = gtk_drop_down_new_from_strings(presets);
    g_signal_connect(panel->clock_preset_dropdown, "notify::selected", G_CALLBACK(on_clock_preset_changed), panel);
    gtk_widget_set_margin_top(panel->clock_preset_dropdown, 5);
    gtk_box_append(GTK_BOX(panel->clock_settings_box), panel->clock_preset_dropdown);
    
    // Custom Box (Vertical container)
    panel->clock_custom_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(panel->clock_custom_box, 5);
    gtk_box_append(GTK_BOX(panel->clock_settings_box), panel->clock_custom_box);
    
    // Row 1: Minutes
    GtkWidget* row_min = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(panel->clock_custom_box), row_min);

    GtkWidget* min_lbl = gtk_label_new("Min:");
    gtk_widget_set_hexpand(min_lbl, TRUE);
    gtk_widget_set_halign(min_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row_min), min_lbl);
    
    panel->clock_min_spin = gtk_spin_button_new_with_range(1, 180, 1); // Increased range
    gtk_box_append(GTK_BOX(row_min), panel->clock_min_spin);
    
    // Row 2: Increment
    GtkWidget* row_inc = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(panel->clock_custom_box), row_inc);

    GtkWidget* inc_lbl = gtk_label_new("Inc:");
    gtk_widget_set_hexpand(inc_lbl, TRUE);
    gtk_widget_set_halign(inc_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row_inc), inc_lbl);
    
    panel->clock_inc_spin = gtk_spin_button_new_with_range(0, 60, 1);
    gtk_box_append(GTK_BOX(row_inc), panel->clock_inc_spin);
    
    // Load config state
    AppConfig* cfg = config_get();
    int mins = cfg->clock_minutes;
    int inc = cfg->clock_increment;
    
    // Detect preset
    int preset_idx = 13; // Default Custom (last index)
    
    if (mins == 0 && inc == 0) preset_idx = 0;
    else if (mins == 1 && inc == 0) preset_idx = 1;
    else if (mins == 1 && inc == 1) preset_idx = 2;
    else if (mins == 2 && inc == 1) preset_idx = 3;
    else if (mins == 3 && inc == 0) preset_idx = 4;
    else if (mins == 3 && inc == 2) preset_idx = 5;
    else if (mins == 5 && inc == 0) preset_idx = 6;
    else if (mins == 5 && inc == 3) preset_idx = 7;
    else if (mins == 10 && inc == 0) preset_idx = 8;
    else if (mins == 10 && inc == 5) preset_idx = 9;
    else if (mins == 15 && inc == 10) preset_idx = 10;
    else if (mins == 30 && inc == 0) preset_idx = 11;
    else if (mins == 30 && inc == 20) preset_idx = 12;
    
    gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->clock_preset_dropdown), preset_idx);
    
    // Set custom values anyway
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(panel->clock_min_spin), mins > 0 ? mins : 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(panel->clock_inc_spin), inc);
    
    // Connect custom signals
    g_signal_connect(panel->clock_min_spin, "value-changed", G_CALLBACK(on_clock_custom_changed), panel);
    g_signal_connect(panel->clock_inc_spin, "value-changed", G_CALLBACK(on_clock_custom_changed), panel);
    
    // Validation visibility
    bool is_custom = (preset_idx == 13);
    gtk_widget_set_visible(panel->clock_custom_box, is_custom);
    
    return panel->clock_settings_box;
}

static void on_clock_preset_changed(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    InfoPanel* panel = (InfoPanel*)user_data;
    GtkDropDown* dropdown = GTK_DROP_DOWN(obj);
    int selected = gtk_drop_down_get_selected(dropdown);
    
    int mins = 0;
    int inc = 0;
    bool custom = false;
    
    switch (selected) {
        case 0: mins = 0; inc = 0; break; // No Clock
        
        // Bullet
        case 1: mins = 1; inc = 0; break;
        case 2: mins = 1; inc = 1; break;
        case 3: mins = 2; inc = 1; break;
        
        // Blitz
        case 4: mins = 3; inc = 0; break;
        case 5: mins = 3; inc = 2; break;
        case 6: mins = 5; inc = 0; break;
        case 7: mins = 5; inc = 3; break;
        
        // Rapid
        case 8: mins = 10; inc = 0; break;
        case 9: mins = 10; inc = 5; break;
        case 10: mins = 15; inc = 10; break;
        
        // Classical
        case 11: mins = 30; inc = 0; break;
        case 12: mins = 30; inc = 20; break;
        
        // Custom
        case 13: custom = true; break;
    }
    
    gtk_widget_set_visible(panel->clock_custom_box, custom);
    
    if (!custom) {
        AppConfig* cfg = config_get();
        cfg->clock_minutes = mins;
        cfg->clock_increment = inc;
        config_save();
        
        // Auto-reset game with new settings
        if (panel->logic) {
            on_reset_clicked(NULL, panel);
        }
    } else {
        if (panel->status_label && GTK_IS_LABEL(panel->status_label)) {
            // If checkmate, we might want to append " - Checkmate" or similar if not in status_text
            // But logic->statusMessage usually says "Checkmate: White won"
        }
        // If switched to custom, we might want to load current values from spin buttons
        // But usually we just let the user edit them.
        on_clock_custom_changed(NULL, panel);
    }
}

static void on_clock_custom_changed(GtkSpinButton* spin, gpointer user_data) {
    (void)spin;
    InfoPanel* panel = (InfoPanel*)user_data;
    // Check if custom is selected
    int selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->clock_preset_dropdown));
    if (selected != 13) return;
    
    int mins = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(panel->clock_min_spin));
    int inc = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(panel->clock_inc_spin));
    
    AppConfig* cfg = config_get();
    cfg->clock_minutes = mins;
    cfg->clock_increment = inc;
    config_save();
    
    // Auto-reset game with new settings
    if (panel->logic) {
        on_reset_clicked(NULL, panel);
    }
}





// Cleanup function
static void info_panel_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    InfoPanel* panel = (InfoPanel*)user_data;
    if (panel) {
        piece_type_list_free(panel->white_captures);
        piece_type_list_free(panel->black_captures);
        g_free(panel);
    }
}

// Forward declaration removed (unused)

GtkWidget* info_panel_new(GameLogic* logic, GtkWidget* board_widget, ThemeData* theme) {
    InfoPanel* panel = (InfoPanel*)calloc(1, sizeof(InfoPanel));
    if (!panel) return NULL;
    
    panel->logic = logic;
    panel->board_widget = board_widget;
    panel->theme = theme;
    panel->white_captures = piece_type_list_create();
    panel->black_captures = piece_type_list_create();
    
    // Main container - VBox with scroll
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    // Don't propagate natural width - keep fixed width regardless of content
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), FALSE);
    // Set maximum width to prevent expansion
    gtk_widget_set_size_request(scrolled, 290, -1);
    
    // Scroll content (Main Wrapper)
    panel->scroll_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); // No spacing on wrapper
    
    // Puzzle UI Container
    panel->puzzle_ui.box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(panel->puzzle_ui.box, 15);
    gtk_widget_set_margin_bottom(panel->puzzle_ui.box, 15);
    gtk_widget_set_margin_start(panel->puzzle_ui.box, 15);
    gtk_widget_set_margin_end(panel->puzzle_ui.box, 15);
    gtk_widget_set_visible(panel->puzzle_ui.box, FALSE); // Hidden by default
    
    // Puzzle Widgets
    panel->puzzle_ui.title_label = gtk_label_new("Puzzle Title");
    PangoAttrList* puz_attrs = pango_attr_list_new();
    pango_attr_list_insert(puz_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(puz_attrs, pango_attr_size_new(16 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(panel->puzzle_ui.title_label), puz_attrs);
    pango_attr_list_unref(puz_attrs);
    gtk_label_set_wrap(GTK_LABEL(panel->puzzle_ui.title_label), TRUE);
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), panel->puzzle_ui.title_label);
    
    // Use a ScrolledWindow for the description to prevent layout jumping
    GtkWidget* desc_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(desc_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(desc_scroll, -1, 180); // Fixed height for description area
    gtk_widget_set_vexpand(desc_scroll, FALSE);
    gtk_widget_set_margin_bottom(desc_scroll, 15);
    
    panel->puzzle_ui.desc_label = gtk_label_new("Description");
    gtk_label_set_wrap(GTK_LABEL(panel->puzzle_ui.desc_label), TRUE);
    // Align to start (top-left) of the scroll area
    gtk_widget_set_halign(panel->puzzle_ui.desc_label, GTK_ALIGN_START);
    gtk_widget_set_valign(panel->puzzle_ui.desc_label, GTK_ALIGN_START);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(desc_scroll), panel->puzzle_ui.desc_label);
    
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), desc_scroll);

    // Separator
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    panel->puzzle_ui.status_label = gtk_label_new("");
    gtk_widget_set_margin_bottom(panel->puzzle_ui.status_label, 10);
    gtk_widget_set_margin_top(panel->puzzle_ui.status_label, 10);
    // Style status label
    PangoAttrList* status_attrs = pango_attr_list_new();
    pango_attr_list_insert(status_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(status_attrs, pango_attr_size_new(12 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(panel->puzzle_ui.status_label), status_attrs);
    pango_attr_list_unref(status_attrs);
    
    // Ensure text wraps instead of expanding width
    gtk_label_set_wrap(GTK_LABEL(panel->puzzle_ui.status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(panel->puzzle_ui.status_label), 25);
    
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), panel->puzzle_ui.status_label);
    
    // Separator
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Puzzle List (Embedded)
    panel->puzzle_ui.puzzle_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(panel->puzzle_ui.puzzle_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(panel->puzzle_ui.puzzle_scroll, -1, 250); // Fixed height for list
    gtk_widget_set_vexpand(panel->puzzle_ui.puzzle_scroll, FALSE);
    gtk_widget_set_margin_top(panel->puzzle_ui.puzzle_scroll, 10);
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), panel->puzzle_ui.puzzle_scroll);
    
    panel->puzzle_ui.puzzle_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(panel->puzzle_ui.puzzle_list_box), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(panel->puzzle_ui.puzzle_list_box, "sidebar");
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(panel->puzzle_ui.puzzle_list_box), TRUE);
    
    // Gesture removed to prevent double-activation and GTK critical errors.
    // relying on gtk_list_box_set_activate_on_single_click(..., TRUE)
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(panel->puzzle_ui.puzzle_scroll), panel->puzzle_ui.puzzle_list_box);
    
    // Visual Toggles for Puzzle Mode
    GtkWidget* puz_visuals = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(puz_visuals, 10);
    
    GtkWidget* puz_anim_check = gtk_check_button_new_with_label("Enable Animations");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(puz_anim_check), config_get()->enable_animations);
    g_signal_connect(puz_anim_check, "toggled", G_CALLBACK(on_animations_toggled), panel);
    gtk_box_append(GTK_BOX(puz_visuals), puz_anim_check);
    
    GtkWidget* puz_sfx_check = gtk_check_button_new_with_label("Enable SFX");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(puz_sfx_check), config_get()->enable_sfx);
    g_signal_connect(puz_sfx_check, "toggled", G_CALLBACK(on_sfx_toggled), panel);
    gtk_box_append(GTK_BOX(puz_visuals), puz_sfx_check);
    
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), puz_visuals);

    GtkWidget* puz_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(puz_btns, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(puz_btns, 10);
    
    panel->puzzle_ui.reset_btn = gtk_button_new_with_label("Try Again");
    gtk_box_append(GTK_BOX(puz_btns), panel->puzzle_ui.reset_btn);
    
    panel->puzzle_ui.next_btn = gtk_button_new_with_label("Next Puzzle");
    gtk_widget_add_css_class(panel->puzzle_ui.next_btn, "suggested-action");
    gtk_box_append(GTK_BOX(puz_btns), panel->puzzle_ui.next_btn);
    
    panel->puzzle_ui.exit_btn = gtk_button_new_with_label("Exit");
    gtk_widget_add_css_class(panel->puzzle_ui.exit_btn, "destructive-action");
    gtk_box_append(GTK_BOX(puz_btns), panel->puzzle_ui.exit_btn);
    
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), puz_btns);
    
    gtk_box_append(GTK_BOX(panel->scroll_content), panel->puzzle_ui.box);

    // Tutorial UI Container
    panel->tutorial_ui.box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(panel->tutorial_ui.box, 15);
    gtk_widget_set_margin_bottom(panel->tutorial_ui.box, 15);
    gtk_widget_set_margin_start(panel->tutorial_ui.box, 15);
    gtk_widget_set_margin_end(panel->tutorial_ui.box, 15);
    gtk_widget_set_visible(panel->tutorial_ui.box, FALSE); // Hidden by default

    // Tutorial Widgets
    GtkWidget* tut_title = gtk_label_new("Tutorial");
    PangoAttrList* tut_attrs = pango_attr_list_new();
    pango_attr_list_insert(tut_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(tut_attrs, pango_attr_size_new(18 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(tut_title), tut_attrs);
    pango_attr_list_unref(tut_attrs);
    gtk_widget_set_halign(tut_title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), tut_title);
    
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Learning Objective
    GtkWidget* learn_header = gtk_label_new("Currently Learning:");
    gtk_widget_set_halign(learn_header, GTK_ALIGN_START);
    gtk_widget_add_css_class(learn_header, "dim-label"); // Assuming class exists
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), learn_header);

    panel->tutorial_ui.learning_label = gtk_label_new("Basics");
    PangoAttrList* learn_attrs = pango_attr_list_new();
    pango_attr_list_insert(learn_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(learn_attrs, pango_attr_size_new(14 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(panel->tutorial_ui.learning_label), learn_attrs);
    pango_attr_list_unref(learn_attrs);
    gtk_widget_set_halign(panel->tutorial_ui.learning_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(panel->tutorial_ui.learning_label), TRUE);
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), panel->tutorial_ui.learning_label);

    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Instruction
    GtkWidget* instr_header = gtk_label_new("Instruction:");
    gtk_widget_set_halign(instr_header, GTK_ALIGN_START);
    gtk_widget_add_css_class(instr_header, "dim-label");
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), instr_header);

    panel->tutorial_ui.instruction_label = gtk_label_new("Welcome to the tutorial!");
    gtk_widget_set_halign(panel->tutorial_ui.instruction_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(panel->tutorial_ui.instruction_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(panel->tutorial_ui.instruction_label), 30);
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), panel->tutorial_ui.instruction_label);

    // Tutorial Buttons (Reset and Exit)
    // Visual Toggles for Tutorial Mode
    GtkWidget* tut_visuals = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(tut_visuals, 10);
    
    GtkWidget* tut_anim_check = gtk_check_button_new_with_label("Enable Animations");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(tut_anim_check), config_get()->enable_animations);
    g_signal_connect(tut_anim_check, "toggled", G_CALLBACK(on_animations_toggled), panel);
    gtk_box_append(GTK_BOX(tut_visuals), tut_anim_check);
    
    GtkWidget* tut_sfx_check = gtk_check_button_new_with_label("Enable SFX");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(tut_sfx_check), config_get()->enable_sfx);
    g_signal_connect(tut_sfx_check, "toggled", G_CALLBACK(on_sfx_toggled), panel);
    gtk_box_append(GTK_BOX(tut_visuals), tut_sfx_check);
    
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), tut_visuals);

    GtkWidget* tut_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(tut_btns, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(tut_btns, 15);
    
    panel->tutorial_ui.reset_btn = gtk_button_new_with_label("Reset Step");
    gtk_widget_add_css_class(panel->tutorial_ui.reset_btn, "suggested-action");
    gtk_box_append(GTK_BOX(tut_btns), panel->tutorial_ui.reset_btn);
    
    panel->tutorial_ui.exit_btn = gtk_button_new_with_label("Exit Tutorial");
    gtk_widget_add_css_class(panel->tutorial_ui.exit_btn, "destructive-action");
    gtk_box_append(GTK_BOX(tut_btns), panel->tutorial_ui.exit_btn);
    
    gtk_box_append(GTK_BOX(panel->tutorial_ui.box), tut_btns);

    gtk_box_append(GTK_BOX(panel->scroll_content), panel->tutorial_ui.box);

    // Standard Controls Container
    panel->standard_controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(panel->standard_controls_box, 15);
    gtk_widget_set_margin_bottom(panel->standard_controls_box, 15);
    gtk_widget_set_margin_start(panel->standard_controls_box, 15);
    gtk_widget_set_margin_end(panel->standard_controls_box, 15);
    gtk_box_append(GTK_BOX(panel->scroll_content), panel->standard_controls_box);

    // Initial setup
    gtk_widget_set_hexpand(panel->scroll_content, FALSE);
    gtk_widget_set_size_request(panel->scroll_content, 290, -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), panel->scroll_content);
    
    // Store panel for callback retrieval
    g_object_set_data(G_OBJECT(panel->scroll_content), "panel", panel);
    
    // Status label
    panel->status_label = gtk_label_new("White's Turn");
    gtk_label_set_wrap(GTK_LABEL(panel->status_label), TRUE);
    // Set max width so text wraps and doesn't expand panel
    gtk_label_set_max_width_chars(GTK_LABEL(panel->status_label), 20);
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* font_size = pango_attr_size_new(18 * PANGO_SCALE);
    PangoAttribute* weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(attrs, font_size);
    pango_attr_list_insert(attrs, weight);
    gtk_label_set_attributes(GTK_LABEL(panel->status_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(panel->status_label, GTK_ALIGN_CENTER);
    // Ensure label doesn't expand beyond panel width
    gtk_widget_set_hexpand(panel->status_label, FALSE);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), panel->status_label);
    
    // Separator line
    GtkWidget* separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(separator1, 10);
    gtk_widget_set_margin_bottom(separator1, 10);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), separator1);
    
    // Captured Pieces heading
    GtkWidget* captured_title = gtk_label_new("CAPTURED PIECES");
    PangoAttrList* captured_attrs = pango_attr_list_new();
    PangoAttribute* captured_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(captured_attrs, captured_weight);
    gtk_label_set_attributes(GTK_LABEL(captured_title), captured_attrs);
    pango_attr_list_unref(captured_attrs);
    gtk_widget_set_halign(captured_title, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(captured_title, FALSE);
    gtk_widget_set_margin_bottom(captured_title, 5);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), captured_title);
    
    // Graveyards section
    GtkWidget* graveyard_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(graveyard_section, FALSE);
    
    // Black captures (pieces captured by black) - store label in panel
    panel->black_label = gtk_label_new("Captured by Black:");
    gtk_widget_set_halign(panel->black_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(panel->black_label, FALSE);
    gtk_box_append(GTK_BOX(graveyard_section), panel->black_label);
    
    // Use horizontal box for black captures - single line, max 7 pieces
    panel->black_captures_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_top(panel->black_captures_box, 5);
    gtk_widget_set_margin_bottom(panel->black_captures_box, 5);
    gtk_widget_set_hexpand(panel->black_captures_box, FALSE);
    gtk_widget_add_css_class(panel->black_captures_box, "capture-box");
    gtk_widget_add_css_class(panel->black_captures_box, "capture-box-for-white-pieces"); // Holds White pieces
    gtk_box_append(GTK_BOX(graveyard_section), panel->black_captures_box);
    
    // White captures (pieces captured by white) - store label in panel
    panel->white_label = gtk_label_new("Captured by White:");
    gtk_widget_set_halign(panel->white_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(panel->white_label, FALSE);
    gtk_box_append(GTK_BOX(graveyard_section), panel->white_label);
    
    // Use horizontal box for white captures - single line, max 7 pieces
    panel->white_captures_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_top(panel->white_captures_box, 5);
    gtk_widget_set_margin_bottom(panel->white_captures_box, 5);
    gtk_widget_set_hexpand(panel->white_captures_box, FALSE);
    gtk_widget_add_css_class(panel->white_captures_box, "capture-box");
    gtk_widget_add_css_class(panel->white_captures_box, "capture-box-for-black-pieces"); // Holds Black pieces
    gtk_box_append(GTK_BOX(graveyard_section), panel->white_captures_box);
    
    gtk_box_append(GTK_BOX(panel->standard_controls_box), graveyard_section);
    
    // Separator line between graveyards and buttons
    GtkWidget* separator2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(separator2, 10);
    gtk_widget_set_margin_bottom(separator2, 10);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), separator2);
    
    // Actions header
    GtkWidget* actions_title = gtk_label_new("ACTIONS");
    PangoAttrList* actions_attrs = pango_attr_list_new();
    PangoAttribute* actions_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(actions_attrs, actions_weight);
    gtk_label_set_attributes(GTK_LABEL(actions_title), actions_attrs);
    pango_attr_list_unref(actions_attrs);
    gtk_widget_set_halign(actions_title, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(actions_title, FALSE);
    gtk_widget_set_margin_bottom(actions_title, 5);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), actions_title);
    
    // Control buttons
    // Actions container (Vertical to stack rows)
    GtkWidget* actions_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_halign(actions_vbox, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), actions_vbox);

    // Row 1: CvC Controls
    GtkWidget* cvc_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(cvc_row, GTK_ALIGN_CENTER);
    
    panel->cvc_start_btn = gtk_button_new_with_label("Start Match");
    gtk_widget_add_css_class(panel->cvc_start_btn, "success-action");
    g_signal_connect(panel->cvc_start_btn, "clicked", G_CALLBACK(on_cvc_start_clicked), panel);
    gtk_widget_set_visible(panel->cvc_start_btn, FALSE);
    gtk_box_append(GTK_BOX(cvc_row), panel->cvc_start_btn);
    
    panel->cvc_pause_btn = gtk_button_new_with_label("Pause");
    gtk_widget_add_css_class(panel->cvc_pause_btn, "success-action");
    g_signal_connect(panel->cvc_pause_btn, "clicked", G_CALLBACK(on_cvc_pause_clicked), panel);
    gtk_widget_set_visible(panel->cvc_pause_btn, FALSE);
    gtk_box_append(GTK_BOX(cvc_row), panel->cvc_pause_btn);
    
    panel->cvc_stop_btn = gtk_button_new_with_label("Stop");
    gtk_widget_add_css_class(panel->cvc_stop_btn, "destructive-action");
    g_signal_connect(panel->cvc_stop_btn, "clicked", G_CALLBACK(on_cvc_stop_clicked), panel);
    gtk_widget_set_visible(panel->cvc_stop_btn, FALSE);
    gtk_box_append(GTK_BOX(cvc_row), panel->cvc_stop_btn);
    
    gtk_box_append(GTK_BOX(actions_vbox), cvc_row);

    // Row 2: Standard Controls (Undo/Reset)
    GtkWidget* std_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(std_row, GTK_ALIGN_CENTER);
    
    panel->undo_button = gtk_button_new_with_label("Undo");
    gtk_widget_add_css_class(panel->undo_button, "suggested-action");
    g_signal_connect(panel->undo_button, "clicked", G_CALLBACK(on_undo_clicked), panel);
    gtk_box_append(GTK_BOX(std_row), panel->undo_button);
    
    panel->reset_button = gtk_button_new_with_label("Reset");
    gtk_widget_add_css_class(panel->reset_button, "destructive-action");
    g_signal_connect(panel->reset_button, "clicked", G_CALLBACK(on_reset_clicked), panel);
    gtk_box_append(GTK_BOX(std_row), panel->reset_button);
    
    gtk_box_append(GTK_BOX(actions_vbox), std_row);
    
    // Separator line between buttons and game settings
    GtkWidget* separator3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(separator3, 10);
    gtk_widget_set_margin_bottom(separator3, 10);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), separator3);
    
    // Game Settings section
    GtkWidget* settings_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(settings_section, FALSE);
    
    // Settings title
    GtkWidget* settings_title = gtk_label_new("GAME SETTINGS");
    PangoAttrList* title_attrs = pango_attr_list_new();
    PangoAttribute* title_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(title_attrs, title_weight);
    gtk_label_set_attributes(GTK_LABEL(settings_title), title_attrs);
    pango_attr_list_unref(title_attrs);
    gtk_widget_set_halign(settings_title, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(settings_title, FALSE);
    gtk_box_append(GTK_BOX(settings_section), settings_title);
    
    // Game mode dropdown
    GtkWidget* mode_label = gtk_label_new("Game Mode:");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(mode_label, FALSE);
    gtk_box_append(GTK_BOX(settings_section), mode_label);
    
    panel->game_mode_dropdown = gtk_drop_down_new_from_strings((const char*[]){"Player vs. Player", "Player vs. Computer", "Computer vs. Computer", "Puzzles", NULL});
    
    AppConfig* cfg = config_get();
    
    // Apply Game Mode from Config
    if (cfg->game_mode >= 0 && cfg->game_mode <= 3) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->game_mode_dropdown), cfg->game_mode);
        logic->gameMode = (GameMode)cfg->game_mode;
    } else {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->game_mode_dropdown), GAME_MODE_PVC); // Default
    }

    gtk_widget_set_hexpand(panel->game_mode_dropdown, FALSE);
    g_signal_connect(panel->game_mode_dropdown, "notify::selected", G_CALLBACK(on_game_mode_changed), panel);
    gtk_box_append(GTK_BOX(settings_section), panel->game_mode_dropdown);
    
    // Play as dropdown
    GtkWidget* play_as_label = gtk_label_new("Play as:");
    gtk_widget_set_halign(play_as_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(play_as_label, FALSE);
    gtk_widget_set_margin_top(play_as_label, 8);
    gtk_box_append(GTK_BOX(settings_section), play_as_label);
    
    panel->play_as_dropdown = gtk_drop_down_new_from_strings((const char*[]){"White", "Black", "Random", NULL});
    
    // Apply Play As from Config
    if (cfg->play_as >= 0 && cfg->play_as <= 2) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->play_as_dropdown), cfg->play_as);
        
        // Apply to logic immediately on startup
        if (cfg->play_as == 0) panel->logic->playerSide = PLAYER_WHITE;
        else if (cfg->play_as == 1) panel->logic->playerSide = PLAYER_BLACK;
        else if (cfg->play_as == 2) {
            // Randomly pick White (0) or Black (1)
            int rand_side = (g_random_int() % 2);
            panel->logic->playerSide = (rand_side == 1) ? PLAYER_BLACK : PLAYER_WHITE;
        }
    } else {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->play_as_dropdown), 0);
        panel->logic->playerSide = PLAYER_WHITE; // Default
    }

    gtk_widget_set_hexpand(panel->play_as_dropdown, FALSE);
    g_signal_connect(panel->play_as_dropdown, "notify::selected", G_CALLBACK(on_play_as_changed), panel);
    gtk_box_append(GTK_BOX(settings_section), panel->play_as_dropdown);
    
    // Old CvC Match Controls block removed

    
    gtk_box_append(GTK_BOX(panel->standard_controls_box), settings_section);

    // Separator line between Game Settings and Clock Settings
    GtkWidget* separator_game = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(separator_game, 10);
    gtk_widget_set_margin_bottom(separator_game, 10);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), separator_game);

    // Call helper to add clock settings
    GtkWidget* clock_box = create_clock_settings_ui(panel);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), clock_box);

    // Separator line between Clock Settings and AI Settings
    GtkWidget* separator_clock = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(separator_clock, 10);
    gtk_widget_set_margin_bottom(separator_clock, 10);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), separator_clock);
    
    // AI Settings section
    panel->ai_settings_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(panel->ai_settings_section, FALSE);
    
    // AI title
    GtkWidget* ai_title = gtk_label_new("AI SETTINGS");
    PangoAttrList* ai_title_attrs = pango_attr_list_new();
    pango_attr_list_insert(ai_title_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(ai_title), ai_title_attrs);
    pango_attr_list_unref(ai_title_attrs);
    gtk_widget_set_halign(ai_title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(panel->ai_settings_section), ai_title);

    // Side-specific engine selection
    gtk_box_append(GTK_BOX(panel->ai_settings_section), create_ai_side_block(panel, false, "White AI Player"));
    gtk_box_append(GTK_BOX(panel->ai_settings_section), create_ai_side_block(panel, true, "Black AI Player"));

    gtk_box_append(GTK_BOX(panel->standard_controls_box), panel->ai_settings_section);
    
    // Initialize visibility
    update_ai_settings_visibility(panel);
    
    // Separator line between game settings and visual settings
    GtkWidget* separator4 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(separator4, 10);
    gtk_widget_set_margin_bottom(separator4, 10);
    gtk_box_append(GTK_BOX(panel->standard_controls_box), separator4);
    
    // Visual Settings section
    GtkWidget* visual_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(visual_section, FALSE);
    
    // Visual settings title
    GtkWidget* visual_title = gtk_label_new("VISUAL SETTINGS");
    PangoAttrList* visual_attrs = pango_attr_list_new();
    pango_attr_list_insert(visual_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(visual_title), visual_attrs);
    pango_attr_list_unref(visual_attrs);
    gtk_widget_set_halign(visual_title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(visual_section), visual_title);
    
    // Enable Animations checkbox
    panel->enable_animations_check = gtk_check_button_new_with_label("Enable Animations");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->enable_animations_check), cfg->enable_animations);
    board_widget_set_animations_enabled(panel->board_widget, cfg->enable_animations);
    g_signal_connect(panel->enable_animations_check, "toggled", G_CALLBACK(on_animations_toggled), panel);
    gtk_box_append(GTK_BOX(visual_section), panel->enable_animations_check);
    
    // Hints toggle
    GtkWidget* hints_label = gtk_label_new("Hints Style:");
    gtk_widget_set_halign(hints_label, GTK_ALIGN_START); 
    gtk_box_append(GTK_BOX(visual_section), hints_label);
    
    panel->hints_dropdown = gtk_drop_down_new_from_strings((const char*[]){"Dots", "Squares", NULL});
    // 0=Dots, 1=Squares. Valid: hints_dots=true -> 0. hints_dots=false -> 1.
    gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->hints_dropdown), cfg->hints_dots ? 0 : 1); 
    board_widget_set_hints_mode(panel->board_widget, cfg->hints_dots);
    g_signal_connect(panel->hints_dropdown, "notify::selected", G_CALLBACK(on_hints_mode_changed), panel);
    gtk_box_append(GTK_BOX(visual_section), panel->hints_dropdown);
    
    // Enable SFX
    panel->enable_sfx_check = gtk_check_button_new_with_label("Enable SFX");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->enable_sfx_check), cfg->enable_sfx);
    sound_engine_set_enabled(cfg->enable_sfx ? 1 : 0);
    g_signal_connect(panel->enable_sfx_check, "toggled", G_CALLBACK(on_sfx_toggled), panel);
    gtk_box_append(GTK_BOX(visual_section), panel->enable_sfx_check);
    
    gtk_box_append(GTK_BOX(panel->standard_controls_box), visual_section);
    
    // CSS for custom elements is now handled globally

    gtk_widget_set_size_request(scrolled, 290, -1);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_focusable(scrolled, TRUE); // Replaced margin_all and enabled focus
    gtk_widget_add_css_class(scrolled, "info-panel");

    // Click-away to lose focus on entries
    GtkGesture* gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_focus_lost_gesture), scrolled);
    gtk_widget_add_controller(scrolled, GTK_EVENT_CONTROLLER(gesture));
    g_object_set_data(G_OBJECT(scrolled), "info-panel-data", panel);
    g_signal_connect(scrolled, "destroy", G_CALLBACK(info_panel_destroy), panel);
    
    return scrolled;
}

void info_panel_update_status(GtkWidget* info_panel) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    update_captured_pieces(panel);
    update_status_display(panel);

    // Stop CvC if game is over
    if (panel->logic->isGameOver && panel->cvc_state != CVC_STATE_STOPPED) {
        if (panel->cvc_callback) {
            panel->cvc_callback(CVC_STATE_STOPPED, panel->cvc_callback_data);
        }
    }
}

void info_panel_rebuild_layout(GtkWidget* info_panel) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (panel) update_ai_settings_visibility(panel);
}


void info_panel_update_ai_settings(GtkWidget* info_panel_widget, bool white_adv, int white_depth, bool black_adv, int black_depth) {
    if (debug_mode) {
        printf("[InfoPanel] info_panel_update_ai_settings called: white_adv=%d, white_depth=%d, black_adv=%d, black_depth=%d\n",
               white_adv, white_depth, black_adv, black_depth);
    }
    
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel_widget), "info-panel-data");
    if (!panel) {
        if (debug_mode) printf("[InfoPanel] ERROR: panel is NULL!\n");
        return;
    }

    if (debug_mode) {
        printf("[InfoPanel] Panel found, calling set_ai_adv_ui for WHITE\n");
    }

    // White AI
    set_ai_adv_ui(panel->white_ai.elo_box,
              panel->white_ai.adv_box,
              GTK_LABEL(panel->white_ai.depth_label),
              white_adv,
              white_depth);

    if (debug_mode) {
        printf("[InfoPanel] Calling set_ai_adv_ui for BLACK\n");
    }

    // Black AI
    set_ai_adv_ui(panel->black_ai.elo_box,
                panel->black_ai.adv_box,
                GTK_LABEL(panel->black_ai.depth_label),
                black_adv,
                black_depth);
                
    if (debug_mode) {
        printf("[InfoPanel] info_panel_update_ai_settings completed\n");
    }
}

void info_panel_set_elo(GtkWidget* info_panel, int elo, bool is_black) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    GtkWidget* slider = is_black ? panel->black_ai.elo_slider : panel->white_ai.elo_slider;
    // GtkWidget* spin = is_black ? panel->black_ai.elo_spin : panel->white_ai.elo_spin;
    
    if (slider) gtk_range_set_value(GTK_RANGE(slider), (double)elo);
    // Spin button usually synced with slider via adjustment, but set it just in case
    // if (spin) gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), (double)elo);
}

int info_panel_get_elo(GtkWidget* info_panel, bool for_black) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return 1500;
    GtkAdjustment* adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(for_black ? panel->black_ai.elo_spin : panel->white_ai.elo_spin));
    return (int)gtk_adjustment_get_value(adj);
}
void info_panel_set_cvc_callback(GtkWidget* info_panel, CvCControlCallback callback, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (panel) {
        panel->cvc_callback = callback;
        panel->cvc_callback_data = user_data;
    }
}

void info_panel_set_cvc_state(GtkWidget* info_panel, CvCMatchState state) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    panel->cvc_state = state;
    
    // Update button visibility based on state
    if (panel->logic && panel->logic->gameMode == GAME_MODE_CVC) {
         bool show_start = (state == CVC_STATE_STOPPED);
         bool show_pause = (state != CVC_STATE_STOPPED);
         bool show_stop = (state != CVC_STATE_STOPPED);
         
         gtk_widget_set_visible(panel->cvc_start_btn, show_start);
         gtk_widget_set_visible(panel->cvc_pause_btn, show_pause);
         gtk_widget_set_visible(panel->cvc_stop_btn, show_stop);
         
         if (show_pause) {
             gtk_button_set_label(GTK_BUTTON(panel->cvc_pause_btn), (state == CVC_STATE_PAUSED) ? "Continue" : "Pause");
         }
    }
}

void info_panel_set_custom_available(GtkWidget* info_panel, bool available) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || panel->custom_available == available) return;
    
    panel->custom_available = available;
    
    // Update dropdown strings
    const char* strings_avail[] = {"Inbuilt Stockfish 17.1", "Custom Engine", "Add Custom Engine...", NULL};
    const char* strings_none[] = {"Inbuilt Stockfish 17.1", "Add Custom Engine...", NULL};
    const char** active_strings = available ? strings_avail : strings_none;
    
    // Store current selections
    int white_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->white_ai.engine_dropdown));
    int black_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->black_ai.engine_dropdown));
    
    // Apply new strings
    for (int i = 0; i < 2; i++) {
        GtkDropDown* dd = GTK_DROP_DOWN(i == 0 ? panel->white_ai.engine_dropdown : panel->black_ai.engine_dropdown);
        g_signal_handlers_block_by_func(dd, on_engine_selection_changed, panel);
        
        GtkStringList* sl = gtk_string_list_new(active_strings);
        gtk_drop_down_set_model(dd, G_LIST_MODEL(sl));
        g_object_unref(sl);
        
        // Restore/Sanitize selection
        int old_sel = (i == 0) ? white_sel : black_sel;
        if (available) {
            if (old_sel == 1) gtk_drop_down_set_selected(dd, 2);
            else gtk_drop_down_set_selected(dd, 0);
        } else {
            if (old_sel == 2) gtk_drop_down_set_selected(dd, 1);
            else gtk_drop_down_set_selected(dd, 0);
        }
        
        g_signal_handlers_unblock_by_func(dd, on_engine_selection_changed, panel);
    }
}

void info_panel_set_ai_settings_callback(GtkWidget* info_panel, GCallback callback, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (panel) {
        panel->ai_settings_callback = callback;
        panel->ai_settings_callback_data = user_data;
    }
}

// Puzzle Functions
void info_panel_set_puzzle_mode(GtkWidget* info_panel, bool enabled) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    gtk_widget_set_visible(panel->standard_controls_box, !enabled);
    gtk_widget_set_visible(panel->puzzle_ui.box, enabled);
}

void info_panel_update_puzzle_info(GtkWidget* info_panel, const char* title, const char* description, const char* status, bool show_next_btn) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    if (title) gtk_label_set_text(GTK_LABEL(panel->puzzle_ui.title_label), title);
    if (title) gtk_label_set_text(GTK_LABEL(panel->puzzle_ui.title_label), title);
    if (description) {
        char* markup = g_markup_printf_escaped("<span size='14000'>%s</span>", description);
        gtk_label_set_markup(GTK_LABEL(panel->puzzle_ui.desc_label), markup);
        g_free(markup);
    }
    if (status) gtk_label_set_text(GTK_LABEL(panel->puzzle_ui.status_label), status);
    
    gtk_widget_set_sensitive(panel->puzzle_ui.next_btn, show_next_btn);
    // If completed (show_next_btn true), maybe hide reset? Or keep it.
    // Usually "Reset" is for retry. 
}

void info_panel_set_puzzle_callbacks(GtkWidget* info_panel, GCallback on_reset, GCallback on_next, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    if (on_reset) {
         g_signal_handlers_disconnect_by_func(panel->puzzle_ui.reset_btn, on_reset, user_data); // Clear old? simpler to just connect
         // Actually better to disconnect everything first or assume set once.
         g_signal_connect(panel->puzzle_ui.reset_btn, "clicked", on_reset, user_data);
    }
    if (on_next) {
         g_signal_handlers_disconnect_by_func(panel->puzzle_ui.next_btn, on_next, user_data);
         g_signal_connect(panel->puzzle_ui.next_btn, "clicked", on_next, user_data);
    }
}

void info_panel_set_puzzle_exit_callback(GtkWidget* info_panel, GCallback on_exit, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    if (on_exit) {
        g_signal_connect(panel->puzzle_ui.exit_btn, "clicked", on_exit, user_data);
    }
}

// List Management
// Puzzle List Management

void info_panel_clear_puzzle_list(GtkWidget* info_panel) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->puzzle_ui.puzzle_list_box) return;

    // Remove all rows
    GtkWidget* child = gtk_widget_get_first_child(panel->puzzle_ui.puzzle_list_box);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(panel->puzzle_ui.puzzle_list_box), child);
        child = next;
    }
}

void info_panel_add_puzzle_to_list(GtkWidget* info_panel, const char* title, int index) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->puzzle_ui.puzzle_list_box) return;

    GtkWidget* row_label = gtk_label_new(title);
    gtk_widget_set_halign(row_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(row_label, 12);
    gtk_widget_set_margin_end(row_label, 12);
    gtk_widget_set_margin_top(row_label, 8);
    gtk_widget_set_margin_bottom(row_label, 8);
    
    // Store index on label
    g_object_set_data(G_OBJECT(row_label), "puzzle-index", GINT_TO_POINTER(index));
    
    gtk_list_box_append(GTK_LIST_BOX(panel->puzzle_ui.puzzle_list_box), row_label);
}

// Forward declaration
static void on_puzzle_list_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer user_data);

void info_panel_set_puzzle_list_callback(GtkWidget* info_panel, GCallback on_selected, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->puzzle_ui.puzzle_list_box) return;

    // Store callback and data on the list box itself so the internal handler can find it
    g_object_set_data(G_OBJECT(panel->puzzle_ui.puzzle_list_box), "on-selected-cb", (gpointer)on_selected);
    g_object_set_data(G_OBJECT(panel->puzzle_ui.puzzle_list_box), "on-selected-data", user_data);
    
    // Connect internal handler (ensure single connection)
    g_signal_handlers_disconnect_by_func(panel->puzzle_ui.puzzle_list_box, on_puzzle_list_row_activated, NULL);
    g_signal_connect(panel->puzzle_ui.puzzle_list_box, "row-activated", G_CALLBACK(on_puzzle_list_row_activated), NULL);
}


void info_panel_highlight_puzzle(GtkWidget* info_panel, int index) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->puzzle_ui.puzzle_list_box) return;
    
    // Block signals to prevent recursion!
    g_signal_handlers_block_by_func(panel->puzzle_ui.puzzle_list_box, on_puzzle_list_row_activated, NULL);

    // Loop through rows to find matching index
    GtkWidget* child = gtk_widget_get_first_child(panel->puzzle_ui.puzzle_list_box);
    while (child) {
        // child is GtkListBoxRow wrapping our label
        GtkWidget* item = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(child));
        if (item) {
            int row_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "puzzle-index"));
            if (row_idx == index) {
                gtk_list_box_select_row(GTK_LIST_BOX(panel->puzzle_ui.puzzle_list_box), GTK_LIST_BOX_ROW(child));
                
                // Auto-scroll to ensure visible
                GtkAdjustment* adj_v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(panel->puzzle_ui.puzzle_scroll));
                if (adj_v) {
                   // Calculate position based on index and estimated row height (approx 36px per row + padding)
                   double row_height = 42.0; 
                   double target_y = row_idx * row_height;
                   
                   double page_size = gtk_adjustment_get_page_size(adj_v);
                   // double current_val = gtk_adjustment_get_value(adj_v); // Unused
                   
                   // Center the item
                   double centered_val = target_y - (page_size / 2.0) + (row_height / 2.0);
                   if (centered_val < 0) centered_val = 0;
                   double max_scroll = gtk_adjustment_get_upper(adj_v) - page_size;
                   if (centered_val > max_scroll) centered_val = max_scroll;
                   
                   gtk_adjustment_set_value(adj_v, centered_val);
                }
                
                 break;
            }
        }
        child = gtk_widget_get_next_sibling(child);
    }
    
    // Unblock
    g_signal_handlers_unblock_by_func(panel->puzzle_ui.puzzle_list_box, on_puzzle_list_row_activated, NULL);
}

static void on_puzzle_list_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
    (void)box; (void)user_data;
    if (!box || !G_IS_OBJECT(box)) return;
    
    // We can get the panel from the box (ancestor logic or data).
    // Let's use g_object_get_data on the box to retrieve the callback wrapper.
    void (*callback)(int, gpointer) = g_object_get_data(G_OBJECT(box), "on-selected-cb");
    gpointer cb_data = g_object_get_data(G_OBJECT(box), "on-selected-data");
    
    if (callback && row && G_IS_OBJECT(row)) {
        GtkWidget* child = gtk_list_box_row_get_child(row);
        if (child && G_IS_OBJECT(child)) {
            gpointer idx_ptr = g_object_get_data(G_OBJECT(child), "puzzle-index");
            // Direct cast handles NULL (0) correctly as index 0
            int idx = GPOINTER_TO_INT(idx_ptr);
            callback(idx, cb_data);
        }
    }
}

// Tutorial Public API

void info_panel_set_tutorial_mode(GtkWidget* info_panel, bool enabled) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;

    gtk_widget_set_visible(panel->standard_controls_box, !enabled);
    if (enabled) {
        // Ensure puzzle box is hidden if enabling tutorial
        gtk_widget_set_visible(panel->puzzle_ui.box, FALSE); 
    }
    gtk_widget_set_visible(panel->tutorial_ui.box, enabled);
}

void info_panel_update_tutorial_info(GtkWidget* info_panel, const char* instruction, const char* learning_objective) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;

    if (instruction) gtk_label_set_text(GTK_LABEL(panel->tutorial_ui.instruction_label), instruction);
    if (learning_objective) gtk_label_set_text(GTK_LABEL(panel->tutorial_ui.learning_label), learning_objective);
}

void info_panel_set_tutorial_callbacks(GtkWidget* info_panel, GCallback on_reset, GCallback on_exit, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;

    if (on_reset) {
        g_signal_handlers_disconnect_by_func(panel->tutorial_ui.reset_btn, on_reset, user_data);
        g_signal_connect(panel->tutorial_ui.reset_btn, "clicked", on_reset, user_data);
    }
    if (on_exit) {
        g_signal_handlers_disconnect_by_func(panel->tutorial_ui.exit_btn, on_exit, user_data);
        g_signal_connect(panel->tutorial_ui.exit_btn, "clicked", on_exit, user_data);
    }
}

void info_panel_set_game_mode(GtkWidget* info_panel, GameMode mode) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->game_mode_dropdown) return;
    
    g_signal_handlers_block_by_func(panel->game_mode_dropdown, on_game_mode_changed, panel);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->game_mode_dropdown), (guint)mode);
    g_signal_handlers_unblock_by_func(panel->game_mode_dropdown, on_game_mode_changed, panel);
}

void info_panel_set_player_side(GtkWidget* info_panel, Player side) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->play_as_dropdown) return;
    
    // 0: White, 1: Black, 2: Random
    g_signal_handlers_block_by_func(panel->play_as_dropdown, on_play_as_changed, panel);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->play_as_dropdown), (side == PLAYER_BLACK) ? 1 : 0);
    g_signal_handlers_unblock_by_func(panel->play_as_dropdown, on_play_as_changed, panel);
}

bool info_panel_is_custom_selected(GtkWidget* info_panel, bool for_black) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->custom_available) return false;
    GtkDropDown* dd = GTK_DROP_DOWN(for_black ? panel->black_ai.engine_dropdown : panel->white_ai.engine_dropdown);
    return gtk_drop_down_get_selected(dd) == 1;
}

void info_panel_show_ai_settings(GtkWidget* info_panel) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    on_open_ai_settings_clicked(NULL, panel);
}

void info_panel_set_sensitive(GtkWidget* info_panel, bool sensitive) {
    if (!info_panel) return;
    gtk_widget_set_sensitive(info_panel, sensitive);
}

void info_panel_set_game_reset_callback(GtkWidget* info_panel, GameResetCallback callback, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (panel) {
        panel->game_reset_callback = callback;
        panel->game_reset_callback_data = user_data;
    }
}

void info_panel_set_undo_callback(GtkWidget* info_panel, UndoCallback callback, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (panel) {
        panel->undo_callback = callback;
        panel->undo_callback_data = user_data;
    }
}

void info_panel_set_replay_exit_callback(GtkWidget* info_panel, GCallback callback, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (panel) {
        panel->replay_exit_callback = callback;
        panel->replay_exit_data = user_data;
    }
}
// Puzzle List Management

// Public wrapper to refresh graveyard (for theme updates)
void info_panel_refresh_graveyard(GtkWidget* info_panel) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (panel) {
        update_captured_pieces(panel);
    }
}

// Replay UI Implementation

static void on_replay_play_pause_clicked(GtkButton* btn, gpointer user_data) {
    (void)user_data;
    
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    // InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(root), "info-panel-data");
    
    if (state && state->replay_controller) {
        bool is_playing = replay_controller_is_playing(state->replay_controller);
        if (is_playing) {
             replay_controller_pause(state->replay_controller);
             // Icon updates are handled in update_replay_status or here
             gtk_button_set_icon_name(btn, "media-playback-start-symbolic");
             gtk_widget_remove_css_class(GTK_WIDGET(btn), "destructive-action");
             gtk_widget_add_css_class(GTK_WIDGET(btn), "suggested-action");
        } else {
             replay_controller_play(state->replay_controller);
             gtk_button_set_icon_name(btn, "media-playback-pause-symbolic");
             gtk_widget_remove_css_class(GTK_WIDGET(btn), "suggested-action"); // Remove green
             gtk_widget_add_css_class(GTK_WIDGET(btn), "destructive-action"); // Make it red/orange for "stop/pause" feel? Or just keep standard.
             // Usually Stop is red. Pause can be neutral or same as play.
             // Let's stick to standard or just remove suggested-action to make it neutral.
             gtk_widget_remove_css_class(GTK_WIDGET(btn), "destructive-action"); 
             // Neutral when playing (pause)
        }
    }
}

// Stop button callback removed

static void on_replay_prev_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    if (state && state->replay_controller) replay_controller_prev(state->replay_controller, false);  // Manual navigation
}

static void on_replay_next_clicked(GtkButton* btn, gpointer user_data) {
     (void)btn; (void)user_data;
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    if (state && state->replay_controller) replay_controller_next(state->replay_controller, false);  // Manual navigation
}

static void on_replay_start_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    if (state && state->replay_controller) replay_controller_seek(state->replay_controller, 0);
}

static void on_replay_end_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    // Seek to MAX_INT will clamp to end
    if (state && state->replay_controller) replay_controller_seek(state->replay_controller, 999999);
}

static void on_replay_exit_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(root), "info-panel-data");
    if (panel && panel->replay_exit_callback) {
        // Cast to appropriate type if needed, or just call as GCallback (void (*)(void))
        // Usually GCallback is generic, lets declare the signature of our callback as void(*)(gpointer)
        void (*cb)(gpointer) = (void(*)(gpointer))panel->replay_exit_callback;
        cb(panel->replay_exit_data);
    }
}

static void on_replay_speed_changed(GtkRange* range, gpointer user_data) {
    (void)user_data;
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(range), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    if (state && state->replay_controller) {
        double val = gtk_range_get_value(range);
        int speed = (int)val;
        if (speed < 1) speed = 1;
        int delay = 2000 / speed;
        replay_controller_set_speed(state->replay_controller, delay);
        
        // Update label
        InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(root), "info-panel-data");
        if (panel && panel->replay_ui.speed_label) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Playback Speed: %.1fx", speed / 2.0);
            gtk_label_set_text(GTK_LABEL(panel->replay_ui.speed_label), buf);
        }
    }
}

static void on_replay_start_here_clicked(GtkButton* btn, gpointer user_data) {
    (void)user_data;
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;
    
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    if (state && state->replay_controller) {
        Player turn = gamelogic_get_turn(state->logic);
        replay_controller_start_from_here(state->replay_controller, GAME_MODE_PVC, turn);
    }
}


static void on_replay_slider_value_changed(GtkRange* range, gpointer user_data) {
    (void)user_data;

    // We need to find the AppState to get the controller. 
    // Since panel doesn't store AppState directly but the widget hierarchy does:
    GtkWidget* root = gtk_widget_get_ancestor(GTK_WIDGET(range), GTK_TYPE_SCROLLED_WINDOW);
    if (!root) return;

    AppState* state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    if (state && state->replay_controller) {
        int target_ply = (int)gtk_range_get_value(range);
        // Only seek if we are not currently updating (handled by signal blocking in update_status, 
        // but good to be safe if we had a way to check, though signal blocking is enough).
        replay_controller_seek(state->replay_controller, target_ply);
    }
}

static void info_panel_create_replay_ui(InfoPanel* panel) {
    if (!panel) return;
    
    panel->replay_ui.box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(panel->replay_ui.box, 15);
    gtk_widget_set_margin_bottom(panel->replay_ui.box, 15);
    gtk_widget_set_margin_start(panel->replay_ui.box, 15);
    gtk_widget_set_margin_end(panel->replay_ui.box, 15);
    gtk_widget_set_visible(panel->replay_ui.box, FALSE); // Hidden by default
    
    // Header
    GtkWidget* title = gtk_label_new("REPLAY MODE");
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_size_new(18 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(title, 10);
    gtk_box_append(GTK_BOX(panel->replay_ui.box), title);

    // Separator
    GtkWidget* sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_bottom(sep1, 15);
    gtk_box_append(GTK_BOX(panel->replay_ui.box), sep1);
    
    // Game Status Label (White's Turn, Checkmate, etc.)
    GtkWidget* game_status_label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(game_status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(game_status_label), 20);
    
    PangoAttrList* status_attrs = pango_attr_list_new();
    pango_attr_list_insert(status_attrs, pango_attr_size_new(16 * PANGO_SCALE));
    pango_attr_list_insert(status_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(game_status_label), status_attrs);
    pango_attr_list_unref(status_attrs);
    
    gtk_widget_set_halign(game_status_label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(game_status_label, FALSE);
    gtk_widget_set_margin_bottom(game_status_label, 15);
    
    gtk_box_append(GTK_BOX(panel->replay_ui.box), game_status_label);
    g_object_set_data(G_OBJECT(panel->replay_ui.box), "game-status-label", game_status_label);

    // Replay Graveyard
    GtkWidget* replay_graveyard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(replay_graveyard, 10);
    
    // Black captures (pieces captured by black) 
    panel->replay_ui.black_label = gtk_label_new("Captured by Black:");
    gtk_widget_set_halign(panel->replay_ui.black_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(replay_graveyard), panel->replay_ui.black_label);
    
    panel->replay_ui.black_captures_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(panel->replay_ui.black_captures_box, "capture-box");
    gtk_widget_add_css_class(panel->replay_ui.black_captures_box, "capture-box-for-white-pieces");
    gtk_box_append(GTK_BOX(replay_graveyard), panel->replay_ui.black_captures_box);
    
    // White captures
    panel->replay_ui.white_label = gtk_label_new("Captured by White:");
    gtk_widget_set_halign(panel->replay_ui.white_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(replay_graveyard), panel->replay_ui.white_label);
    
    panel->replay_ui.white_captures_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(panel->replay_ui.white_captures_box, "capture-box");
    gtk_widget_add_css_class(panel->replay_ui.white_captures_box, "capture-box-for-black-pieces");
    gtk_box_append(GTK_BOX(replay_graveyard), panel->replay_ui.white_captures_box);
    
    gtk_box_append(GTK_BOX(panel->replay_ui.box), replay_graveyard);

    // separator
    GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep2, 15);
    gtk_widget_set_margin_bottom(sep2, 10);
    gtk_box_append(GTK_BOX(panel->replay_ui.box), sep2);

    gtk_widget_set_margin_bottom(panel->replay_ui.box, 10);

    // Status (Move Count)
    // Style: Monospace, large, centered
    panel->replay_ui.status_label = gtk_label_new("Move: 0 / 0");
    gtk_widget_add_css_class(panel->replay_ui.status_label, "info-label-value");
    gtk_widget_set_margin_bottom(panel->replay_ui.status_label, 5); // Reduced margin
    gtk_box_append(GTK_BOX(panel->replay_ui.box), panel->replay_ui.status_label);
    
    // Playback Slider
    panel->replay_ui.playback_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(panel->replay_ui.playback_slider), FALSE); // Value shown in label
    gtk_widget_set_margin_start(panel->replay_ui.playback_slider, 10);
    gtk_widget_set_margin_end(panel->replay_ui.playback_slider, 10);
    gtk_widget_set_margin_bottom(panel->replay_ui.playback_slider, 15);
    g_signal_connect(panel->replay_ui.playback_slider, "value-changed", G_CALLBACK(on_replay_slider_value_changed), panel);
    gtk_box_append(GTK_BOX(panel->replay_ui.box), panel->replay_ui.playback_slider);
    
    // Media Control Row
    GtkWidget* media_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(media_box, GTK_ALIGN_CENTER);
    
    // Start |<<
    panel->replay_ui.start_btn = gui_utils_new_button_from_system_icon("media-skip-backward-symbolic");
    gtk_widget_add_css_class(panel->replay_ui.start_btn, "media-button");
    gtk_widget_set_tooltip_text(panel->replay_ui.start_btn, "Go to Start");
    g_signal_connect(panel->replay_ui.start_btn, "clicked", G_CALLBACK(on_replay_start_clicked), panel);
    gtk_box_append(GTK_BOX(media_box), panel->replay_ui.start_btn);
    
    // Prev <<
    panel->replay_ui.prev_btn = gui_utils_new_button_from_system_icon("media-seek-backward-symbolic");
    gtk_widget_add_css_class(panel->replay_ui.prev_btn, "media-button");
    gtk_widget_set_tooltip_text(panel->replay_ui.prev_btn, "Previous Move");
    g_signal_connect(panel->replay_ui.prev_btn, "clicked", G_CALLBACK(on_replay_prev_clicked), panel);
    gtk_box_append(GTK_BOX(media_box), panel->replay_ui.prev_btn);

    // Stop button removed
    
    // Play/Pause > / ||
    panel->replay_ui.play_pause_btn = gui_utils_new_button_from_system_icon("media-playback-start-symbolic");
    gtk_widget_add_css_class(panel->replay_ui.play_pause_btn, "media-button");
    gtk_widget_add_css_class(panel->replay_ui.play_pause_btn, "suggested-action"); // Highlight play
    gtk_widget_set_tooltip_text(panel->replay_ui.play_pause_btn, "Play / Pause");
    g_signal_connect(panel->replay_ui.play_pause_btn, "clicked", G_CALLBACK(on_replay_play_pause_clicked), panel);
    gtk_box_append(GTK_BOX(media_box), panel->replay_ui.play_pause_btn);
    
    // Next >>
    panel->replay_ui.next_btn = gui_utils_new_button_from_system_icon("media-seek-forward-symbolic");
    gtk_widget_add_css_class(panel->replay_ui.next_btn, "media-button");
    gtk_widget_set_tooltip_text(panel->replay_ui.next_btn, "Next Move");
    g_signal_connect(panel->replay_ui.next_btn, "clicked", G_CALLBACK(on_replay_next_clicked), panel);
    gtk_box_append(GTK_BOX(media_box), panel->replay_ui.next_btn);
    
    // End >>|
    panel->replay_ui.end_btn = gui_utils_new_button_from_system_icon("media-skip-forward-symbolic");
    gtk_widget_add_css_class(panel->replay_ui.end_btn, "media-button");
    gtk_widget_set_tooltip_text(panel->replay_ui.end_btn, "Go to End");
    g_signal_connect(panel->replay_ui.end_btn, "clicked", G_CALLBACK(on_replay_end_clicked), panel);
    gtk_box_append(GTK_BOX(media_box), panel->replay_ui.end_btn);
    
    gtk_box_append(GTK_BOX(panel->replay_ui.box), media_box);
    
    // Speed Slider
    GtkWidget* speed_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_halign(speed_box, GTK_ALIGN_FILL);
    
    panel->replay_ui.speed_label = gtk_label_new("Playback Speed: 1.0x");
    gtk_widget_add_css_class(panel->replay_ui.speed_label, "info-label-title");
    gtk_widget_set_halign(panel->replay_ui.speed_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(speed_box), panel->replay_ui.speed_label);
    
    panel->replay_ui.speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 10, 1);
    gtk_range_set_value(GTK_RANGE(panel->replay_ui.speed_scale), 2); // Default 2 (~1s)
    g_signal_connect(panel->replay_ui.speed_scale, "value-changed", G_CALLBACK(on_replay_speed_changed), panel);
    gtk_box_append(GTK_BOX(speed_box), panel->replay_ui.speed_scale);
    
    gtk_widget_set_margin_bottom(speed_box, 10); // Spacing before separator
    gtk_box_append(GTK_BOX(panel->replay_ui.box), speed_box);
    
    // Bottom Separator
    gtk_box_append(GTK_BOX(panel->replay_ui.box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Visual Toggles
    GtkWidget* toggles_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    
    panel->replay_ui.anim_check = gtk_check_button_new_with_label("Enable Animations");
    // Sync initial state from standard toggle
    if (panel->enable_animations_check) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->replay_ui.anim_check), 
                                    gtk_check_button_get_active(GTK_CHECK_BUTTON(panel->enable_animations_check)));
    }
    // We can reuse the same callback as "on_animations_toggled" as it uses user_data=panel
    g_signal_connect(panel->replay_ui.anim_check, "toggled", G_CALLBACK(on_animations_toggled), panel);
    gtk_box_append(GTK_BOX(toggles_box), panel->replay_ui.anim_check);
    
    panel->replay_ui.sfx_check = gtk_check_button_new_with_label("Enable SFX");
    if (panel->enable_sfx_check) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->replay_ui.sfx_check), 
                                    gtk_check_button_get_active(GTK_CHECK_BUTTON(panel->enable_sfx_check)));
    }
    g_signal_connect(panel->replay_ui.sfx_check, "toggled", G_CALLBACK(on_sfx_toggled), panel);
    gtk_box_append(GTK_BOX(toggles_box), panel->replay_ui.sfx_check);
    
    gtk_box_append(GTK_BOX(panel->replay_ui.box), toggles_box);
    
    // Bottom Separator before actions
    GtkWidget* sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep3, 5);
    gtk_box_append(GTK_BOX(panel->replay_ui.box), sep3);
    
    // Action Buttons
    panel->replay_ui.start_here_btn = gtk_button_new_with_label("Play From Here");
    gtk_widget_add_css_class(panel->replay_ui.start_here_btn, "suggested-action");
    gtk_widget_set_tooltip_text(panel->replay_ui.start_here_btn, "Resume game from this position");
    g_signal_connect(panel->replay_ui.start_here_btn, "clicked", G_CALLBACK(on_replay_start_here_clicked), panel);
    gtk_box_append(GTK_BOX(panel->replay_ui.box), panel->replay_ui.start_here_btn);

    // Analysis UI moved to RightSidePanel

    panel->replay_ui.exit_btn = gtk_button_new_with_label("Exit Replay");
    gtk_widget_add_css_class(panel->replay_ui.exit_btn, "destructive-action");
    g_signal_connect(panel->replay_ui.exit_btn, "clicked", G_CALLBACK(on_replay_exit_clicked), panel);
    gtk_widget_set_margin_top(panel->replay_ui.exit_btn, 12); // More breathing room
    gtk_box_append(GTK_BOX(panel->replay_ui.box), panel->replay_ui.exit_btn);
    
    // Add to scroll content
    gtk_box_append(GTK_BOX(panel->scroll_content), panel->replay_ui.box);
}

void info_panel_update_replay_status(GtkWidget* info_panel, int current_ply, int total_plies) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->replay_ui.status_label) return;
    if(debug_mode) {
        printf("info_panel_update_replay_status: current_ply=%d, total_plies=%d\n", current_ply, total_plies);
    }
    
    // Show move count as plies (total individual moves), not pairs
    // current_ply is 0-indexed internally but represents "moves made"
    // When current_ply = 0, we're at start (0 moves made)
    // When current_ply = 7, we've made 7 moves
    char buf[64];
    snprintf(buf, sizeof(buf), "Move: %d / %d", current_ply, total_plies);
    gtk_label_set_text(GTK_LABEL(panel->replay_ui.status_label), buf);
    
    // Update game status label
    GtkWidget* game_status_label = g_object_get_data(G_OBJECT(panel->replay_ui.box), "game-status-label");
    if (game_status_label && panel->logic) {
        // Use the centralized status message which handles Checkmate, Stalemate, Turn, etc.
        const char* msg = gamelogic_get_status_message(panel->logic);
        gtk_label_set_text(GTK_LABEL(game_status_label), msg ? msg : "");
    }
    
    // Also update buttons sensitivity/icon states if needed provided we have references
    // E.g. disable Back if current_ply == 0
    if (panel->replay_ui.prev_btn) gtk_widget_set_sensitive(panel->replay_ui.prev_btn, current_ply > 0);
    if (panel->replay_ui.start_btn) gtk_widget_set_sensitive(panel->replay_ui.start_btn, current_ply > 0);
    
    if (panel->replay_ui.start_btn) gtk_widget_set_sensitive(panel->replay_ui.start_btn, current_ply > 0);
    
    if (panel->replay_ui.next_btn) gtk_widget_set_sensitive(panel->replay_ui.next_btn, current_ply < total_plies);
    if (panel->replay_ui.end_btn) gtk_widget_set_sensitive(panel->replay_ui.end_btn, current_ply < total_plies);

    // Update slider
    if (panel->replay_ui.playback_slider) {
        g_signal_handlers_block_by_func(panel->replay_ui.playback_slider, on_replay_slider_value_changed, panel);
        
        // Update range and value
        gtk_range_set_range(GTK_RANGE(panel->replay_ui.playback_slider), 0, total_plies);
        gtk_range_set_value(GTK_RANGE(panel->replay_ui.playback_slider), current_ply);
        
        g_signal_handlers_unblock_by_func(panel->replay_ui.playback_slider, on_replay_slider_value_changed, panel);
    }
}

void info_panel_show_replay_controls(GtkWidget* info_panel, gboolean visible) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;
    
    // Lazy init
    if (!panel->replay_ui.box) {
        info_panel_create_replay_ui(panel);
    }
    
    gtk_widget_set_visible(panel->standard_controls_box, !visible);
    gtk_widget_set_visible(panel->replay_ui.box, visible);
    
    // Hide other specific modes if any
    gtk_widget_set_visible(panel->puzzle_ui.box, FALSE);
    gtk_widget_set_visible(panel->tutorial_ui.box, FALSE);
    
    if (visible && panel->replay_ui.play_pause_btn) {
         // Reset state visuals if needed (assuming start is paused)
         AppState* state = (AppState*)g_object_get_data(G_OBJECT(info_panel), "app_state");
         bool is_playing = (state && state->replay_controller && replay_controller_is_playing(state->replay_controller));
         
         if (is_playing) {
             gui_utils_set_button_icon_name(GTK_BUTTON(panel->replay_ui.play_pause_btn), "media-playback-pause-symbolic");
             gtk_widget_remove_css_class(panel->replay_ui.play_pause_btn, "suggested-action"); 
             // gtk_widget_add_css_class(panel->replay_ui.play_pause_btn, "destructive-action");
         } else {
             gui_utils_set_button_icon_name(GTK_BUTTON(panel->replay_ui.play_pause_btn), "media-playback-start-symbolic");
             gtk_widget_add_css_class(panel->replay_ui.play_pause_btn, "suggested-action");
             // gtk_widget_remove_css_class(panel->replay_ui.play_pause_btn, "destructive-action");
         }
    }
}


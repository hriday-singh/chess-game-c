#include "info_panel.h"
#include "piece_symbols.h"
#include "board_widget.h"
#include "sound_engine.h"
#include "gamelogic.h"
#include "piece.h"
#include <pango/pango.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include "ai_engine.h"

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

// Note: piece_type_list_add is defined in gamelogic.c and used by gamelogic_get_captured_pieces
// We don't need it here since gamelogic_get_captured_pieces handles adding to the list

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
    GtkWidget* hints_toggle;
    GtkWidget* enable_sfx_check;
    
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
        GtkWidget* time_label;
    } white_ai, black_ai;

    bool custom_available;

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
} InfoPanel;

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
static void on_hints_squares_toggled(GtkToggleButton* button, gpointer user_data);
static void on_open_ai_settings_clicked(GtkButton* btn, gpointer user_data);

// CvC Callbacks
static void on_cvc_start_clicked(GtkButton* btn, gpointer user_data);
static void on_cvc_pause_clicked(GtkButton* btn, gpointer user_data);
static void on_cvc_stop_clicked(GtkButton* btn, gpointer user_data);

static void reset_game(InfoPanel* panel);
static void update_ai_settings_visibility(InfoPanel* panel);
static void on_elo_adjustment_changed(GtkAdjustment* adj, gpointer user_data);

typedef struct {
    InfoPanel* panel;
    bool is_black;
} SideCallbackData;

static void
set_ai_adv_ui(GtkWidget *elo_box,
              GtkWidget *adv_box,
              GtkLabel  *depth_label,
              GtkLabel  *time_label,
              gboolean   adv,
              int        depth,
              int        time_ms)
{
    gtk_widget_set_visible(elo_box, !adv);
    gtk_widget_set_visible(adv_box,  adv);

    if (!adv) return;

    gtk_label_set_xalign(depth_label, 0.0f);
    gtk_label_set_xalign(time_label,  0.0f);

    g_autofree char *depth_markup = g_strdup_printf(
        "Depth\n<span size='xx-large' weight='bold'>%d</span>",
        depth
    );

    g_autofree char *time_markup = g_strdup_printf(
        "Time\n<span size='xx-large' weight='bold'>%d</span><span size='large' weight='bold'>ms</span>",
        time_ms
    );

    gtk_label_set_use_markup(depth_label, TRUE);
    gtk_label_set_use_markup(time_label,  TRUE);

    gtk_label_set_markup(depth_label, depth_markup);
    gtk_label_set_markup(time_label,  time_markup);
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
    
    GtkWidget* time_label = gtk_label_new("Time: 500ms");
    gtk_widget_set_halign(time_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(adv_box), time_label);
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
        panel->black_ai.time_label = time_label;
    } else {
        panel->white_ai.box = vbox;
        panel->white_ai.title_label = title_label;
        panel->white_ai.engine_dropdown = dropdown;
        panel->white_ai.elo_box = elo_box;
        panel->white_ai.elo_slider = elo_slider;
        panel->white_ai.elo_spin = elo_spin;
        panel->white_ai.adv_box = adv_box;
        panel->white_ai.depth_label = depth_label;
        panel->white_ai.time_label = time_label;
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
    
    // Clear the capture boxes (GtkBox) - remove all children
    GtkWidget* child = gtk_widget_get_first_child(panel->white_captures_box);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(panel->white_captures_box), child);
        child = next;
    }

    child = gtk_widget_get_first_child(panel->black_captures_box);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(panel->black_captures_box), child);
        child = next;
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
                gtk_box_append(GTK_BOX(panel->white_captures_box), widget);
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
            gtk_box_append(GTK_BOX(panel->white_captures_box), plus_label);
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
                gtk_box_append(GTK_BOX(panel->black_captures_box), widget);
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
            gtk_box_append(GTK_BOX(panel->black_captures_box), plus_label);
        }
    }
    
    // Update labels with points
    update_captured_labels(panel);
}

// Draw function for graveyard piece
static void draw_graveyard_piece(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
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
        
        // Scale to fit (roughly 80% of width/height)
        double scale = (double)height * 0.9 / surf_h;
        
        // Center
        double draw_w = surf_w * scale;
        double draw_h = surf_h * scale;
        double offset_x = (width - draw_w) / 2.0;
        double offset_y = (height - draw_h) / 2.0;
        
        cairo_translate(cr, offset_x, offset_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        // Fallback or text rendering?
        // User wants SVG, but if not available (e.g. text theme), we should render text.
        // For simplicity and per user request ("I want the graveyard... to use these svgs only"),
        // we heavily prioritize SVG. If standard font, theme_data_get_piece_symbol returns symbol.
        
        const char* symbol = piece_symbols_get(type, owner);
        
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* desc = pango_font_description_new();
        // Use consistent font
        pango_font_description_set_family(desc, theme_data_get_font_name(panel->theme));
        pango_font_description_set_size(desc, (int)(height * 0.6 * PANGO_SCALE));
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_text(layout, symbol, -1);
        
        // Black pieces color
        if (owner == PLAYER_BLACK) {
             cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        } else {
             // White pieces color (often white with outline, but here simple text)
             // In graveyard, white pieces are "Captured by Black", usually shown as White pieces?
             // Or "Captured by White" means Black pieces.
             // create_piece_widget passes the PIECE COLOR.
             cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        }
        
        int w, h;
        pango_layout_get_pixel_size(layout, &w, &h);
        cairo_move_to(cr, (width - w)/2.0, (height - h)/2.0);
        pango_cairo_show_layout(cr, layout);
        
        // Add stroke for white pieces to be visible
        if (owner == PLAYER_WHITE) {
             cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
             cairo_move_to(cr, (width - w)/2.0, (height - h)/2.0);
             pango_cairo_layout_path(cr, layout);
             cairo_set_line_width(cr, 1.0);
             cairo_stroke(cr);
        }
        
        pango_font_description_free(desc);
        g_object_unref(layout);
    }
}

static GtkWidget* create_piece_widget(InfoPanel* panel, PieceType type, Player owner) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 32, 32); // Reasonable size for icons
    
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
    gtk_label_set_text(GTK_LABEL(panel->status_label), status);
}

// Update captured pieces labels with relative points
static void update_captured_labels(InfoPanel* panel) {
    if (!panel) return;
    
    // Calculate points
    int black_points = calculate_captured_points(panel->black_captures);
    int white_points = calculate_captured_points(panel->white_captures);
    
    // Calculate relative points (difference)
    int point_diff = white_points - black_points;
    
    // Update labels with relative points (only show if non-zero)
    char black_text[128];
    if (point_diff < 0) {
        // Black is ahead
        snprintf(black_text, sizeof(black_text), "Captured by Black: <span size='large' weight='bold' foreground='#d32f2f'>+%d</span>", -point_diff);
    } else {
        // Black is not ahead, show nothing
        snprintf(black_text, sizeof(black_text), "Captured by Black:");
    }
    gtk_label_set_markup(GTK_LABEL(panel->black_label), black_text);
    
    char white_text[128];
    if (point_diff > 0) {
        // White is ahead
        snprintf(white_text, sizeof(white_text), "Captured by White: <span size='large' weight='bold' foreground='#2e7d32'>+%d</span>", point_diff);
    } else {
        // White is not ahead, show nothing
        snprintf(white_text, sizeof(white_text), "Captured by White:");
    }
    gtk_label_set_markup(GTK_LABEL(panel->white_label), white_text);
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

static void on_reset_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    InfoPanel* panel = (InfoPanel*)user_data;
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
    
    // Play As Selection
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(panel->play_as_dropdown));
    
    if (selected == 0) { // White
        panel->logic->playerSide = PLAYER_WHITE;
        board_widget_set_flipped(panel->board_widget, false);
    } else if (selected == 1) { // Black
        panel->logic->playerSide = PLAYER_BLACK;
        board_widget_set_flipped(panel->board_widget, true);
    } else if (selected == 2) { // Random
        // Randomly pick White (0) or Black (1)
        int rand_side = (g_random_int() % 2);
        bool flipped = (rand_side == 1);
        
        // Set player side in game logic
        panel->logic->playerSide = flipped ? PLAYER_BLACK : PLAYER_WHITE;
        
        // Flip the board display
        board_widget_set_flipped(panel->board_widget, flipped);
    }
    
    // Reset game
    gamelogic_reset(panel->logic);
    
    // Reset selection
    board_widget_reset_selection(panel->board_widget);
    
    // Directly update the panel display (status and graveyards)
    update_status_display(panel);
    update_captured_pieces(panel);
    
    // Also update via the public API for consistency
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
                g_action_group_activate_action(G_ACTION_GROUP(app), "puzzles", NULL);
            } else {
                printf("ERROR: Application not valid for launching puzzles!\n");
            }
            gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->game_mode_dropdown), GAME_MODE_PVC);
        }
        // Reset dropdown back to previous mode (will be set by puzzle logic)
        return;
    }
    
    panel->logic->gameMode = (GameMode)selected;
    
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
    
    // Reset game to apply new side
    reset_game(panel);
}

// Animations checkbox callback
static void on_animations_toggled(GtkCheckButton* button, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->board_widget) return;
    
    bool enabled = gtk_check_button_get_active(button);
    board_widget_set_animations_enabled(panel->board_widget, enabled);
}

// SFX checkbox callback
static void on_sfx_toggled(GtkCheckButton* button, gpointer user_data) {
    (void)user_data;  // Unused
    bool enabled = gtk_check_button_get_active(button);
    sound_engine_set_enabled(enabled ? 1 : 0);
}

// Hints toggle callback (dots button)
static void on_hints_toggled(GtkToggleButton* button, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->board_widget) return;
    
    bool use_dots = gtk_toggle_button_get_active(button);
    board_widget_set_hints_mode(panel->board_widget, use_dots);
    board_widget_refresh(panel->board_widget);
    
    // Update squares button to be opposite
    GtkWidget* container = gtk_widget_get_parent(GTK_WIDGET(button));
    GtkWidget* squares_button = gtk_widget_get_last_child(container);
    if (squares_button && GTK_IS_TOGGLE_BUTTON(squares_button)) {
        g_signal_handlers_block_by_func(squares_button, on_hints_squares_toggled, panel);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(squares_button), !use_dots);
        g_signal_handlers_unblock_by_func(squares_button, on_hints_squares_toggled, panel);
    }
}

// Public API: Refresh graveyard
void info_panel_refresh_graveyard(GtkWidget* info_panel_widget) {
    if (!info_panel_widget) return;
    
    // Retrieve panel using the consistent key used in this file
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel_widget), "info-panel-data");
    if (!panel) return;
    
    update_captured_pieces(panel);
}

// Hints squares button callback
static void on_hints_squares_toggled(GtkToggleButton* button, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)user_data;
    if (!panel || !panel->board_widget) return;
    
    bool use_squares = gtk_toggle_button_get_active(button);
    bool use_dots = !use_squares;
    board_widget_set_hints_mode(panel->board_widget, use_dots);
    board_widget_refresh(panel->board_widget);
    
    // Update dots button to be opposite
    if (panel->hints_toggle && GTK_IS_TOGGLE_BUTTON(panel->hints_toggle)) {
        g_signal_handlers_block_by_func(panel->hints_toggle, on_hints_toggled, panel);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(panel->hints_toggle), use_dots);
        g_signal_handlers_unblock_by_func(panel->hints_toggle, on_hints_toggled, panel);
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

// Forward declaration for click handler
static void on_puzzle_list_click(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);

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
    gtk_widget_set_size_request(scrolled, 250, -1);
    
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
    
    panel->puzzle_ui.status_label = gtk_label_new("");
    gtk_widget_set_margin_bottom(panel->puzzle_ui.status_label, 10);
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), panel->puzzle_ui.status_label);
    
    // Puzzle List (Embedded)
    panel->puzzle_ui.puzzle_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(panel->puzzle_ui.puzzle_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(panel->puzzle_ui.puzzle_scroll, -1, 250); // Fixed height for list
    gtk_widget_set_vexpand(panel->puzzle_ui.puzzle_scroll, FALSE);
    gtk_box_append(GTK_BOX(panel->puzzle_ui.box), panel->puzzle_ui.puzzle_scroll);
    
    panel->puzzle_ui.puzzle_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(panel->puzzle_ui.puzzle_list_box), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(panel->puzzle_ui.puzzle_list_box), TRUE);
    
    // Add Click Gesture to absolutely guarantee clicks are caught
    // Add Click Gesture to absolutely guarantee clicks are caught
    GtkGesture* puzzle_click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(puzzle_click_gesture), 0); // All buttons
    g_signal_connect(puzzle_click_gesture, "released", G_CALLBACK(on_puzzle_list_click), panel->puzzle_ui.puzzle_list_box);
    gtk_widget_add_controller(panel->puzzle_ui.puzzle_list_box, GTK_EVENT_CONTROLLER(puzzle_click_gesture));
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(panel->puzzle_ui.puzzle_scroll), panel->puzzle_ui.puzzle_list_box);
    
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

    // Standard Controls Container
    panel->standard_controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(panel->standard_controls_box, 15);
    gtk_widget_set_margin_bottom(panel->standard_controls_box, 15);
    gtk_widget_set_margin_start(panel->standard_controls_box, 15);
    gtk_widget_set_margin_end(panel->standard_controls_box, 15);
    gtk_box_append(GTK_BOX(panel->scroll_content), panel->standard_controls_box);

    // Initial setup
    gtk_widget_set_hexpand(panel->scroll_content, FALSE);
    gtk_widget_set_size_request(panel->scroll_content, 220, -1);
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
    panel->black_captures_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_top(panel->black_captures_box, 5);
    gtk_widget_set_margin_bottom(panel->black_captures_box, 5);
    gtk_widget_set_hexpand(panel->black_captures_box, FALSE);
    gtk_widget_add_css_class(panel->black_captures_box, "capture-box");
    gtk_box_append(GTK_BOX(graveyard_section), panel->black_captures_box);
    
    // White captures (pieces captured by white) - store label in panel
    panel->white_label = gtk_label_new("Captured by White:");
    gtk_widget_set_halign(panel->white_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(panel->white_label, FALSE);
    gtk_box_append(GTK_BOX(graveyard_section), panel->white_label);
    
    // Use horizontal box for white captures - single line, max 7 pieces
    panel->white_captures_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_top(panel->white_captures_box, 5);
    gtk_widget_set_margin_bottom(panel->white_captures_box, 5);
    gtk_widget_set_hexpand(panel->white_captures_box, FALSE);
    gtk_widget_add_css_class(panel->white_captures_box, "capture-box");
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
    gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->game_mode_dropdown), GAME_MODE_PVC); // Default to PvC
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
    gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->play_as_dropdown), 0); // Default to White
    gtk_widget_set_hexpand(panel->play_as_dropdown, FALSE);
    g_signal_connect(panel->play_as_dropdown, "notify::selected", G_CALLBACK(on_play_as_changed), panel);
    gtk_box_append(GTK_BOX(settings_section), panel->play_as_dropdown);
    
    // Old CvC Match Controls block removed

    
    gtk_box_append(GTK_BOX(panel->standard_controls_box), settings_section);
    
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
    gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->enable_animations_check), TRUE);
    g_signal_connect(panel->enable_animations_check, "toggled", G_CALLBACK(on_animations_toggled), panel);
    gtk_box_append(GTK_BOX(visual_section), panel->enable_animations_check);
    
    // Hints toggle
    GtkWidget* hints_label = gtk_label_new("Hints:");
    gtk_widget_set_halign(hints_label, GTK_ALIGN_START); // Left align
    gtk_box_append(GTK_BOX(visual_section), hints_label);
    GtkWidget* hints_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(hints_container, "linked");
    GtkWidget* dots_btn = gtk_toggle_button_new_with_label("Dots");
    GtkWidget* sq_btn = gtk_toggle_button_new_with_label("Squares");
    panel->hints_toggle = dots_btn;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dots_btn), TRUE);
    g_signal_connect(dots_btn, "toggled", G_CALLBACK(on_hints_toggled), panel);
    g_signal_connect(sq_btn, "toggled", G_CALLBACK(on_hints_squares_toggled), panel);
    g_object_set_data(G_OBJECT(dots_btn), "squares-button", sq_btn);
    gtk_box_append(GTK_BOX(hints_container), dots_btn);
    gtk_box_append(GTK_BOX(hints_container), sq_btn);
    gtk_box_append(GTK_BOX(visual_section), hints_container);
    
    // Enable SFX
    panel->enable_sfx_check = gtk_check_button_new_with_label("Enable SFX");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->enable_sfx_check), TRUE);
    g_signal_connect(panel->enable_sfx_check, "toggled", G_CALLBACK(on_sfx_toggled), panel);
    gtk_box_append(GTK_BOX(visual_section), panel->enable_sfx_check);
    
    gtk_box_append(GTK_BOX(panel->standard_controls_box), visual_section);
    
    // CSS for custom elements
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, 
        ".capture-box { background: #e8e8e8; border-radius: 5px; padding: 8px; border: 1px solid #ccc; min-height: 50px; } "
        ".success-text { color: #2e7d32; font-size: 0.9em; } "
        ".error-text { color: #d32f2f; font-size: 0.9em; } "
        ".ai-note { color: #666; font-size: 0.8em; font-style: italic; } "
        ".success-action { background: #2e7d32; color: white; }" 
        ".destructive-action { background: #D32F2F; color: white; }"
        ".capture-count { font-size: 16px; font-weight: bold; margin-left: 4px; color: #666; }"
    );
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    gtk_widget_set_size_request(scrolled, 280, -1);
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


void info_panel_update_ai_settings(GtkWidget* info_panel, bool white_adv, int white_depth, int white_time, bool black_adv, int black_depth, int black_time) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel) return;

    // White AI
    set_ai_adv_ui(panel->white_ai.elo_box,
              panel->white_ai.adv_box,
              GTK_LABEL(panel->white_ai.depth_label),
              GTK_LABEL(panel->white_ai.time_label),
              white_adv,
              white_depth,
              white_time);

    set_ai_adv_ui(panel->black_ai.elo_box,
                panel->black_ai.adv_box,
                GTK_LABEL(panel->black_ai.depth_label),
                GTK_LABEL(panel->black_ai.time_label),
                black_adv,
                black_depth,
                black_time);
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

// Fallback click handler
static void on_puzzle_list_click(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x;
    GtkListBox* box = GTK_LIST_BOX(user_data);
    GtkListBoxRow* row = gtk_list_box_get_row_at_y(box, (int)y);
    
    if (row) {
        on_puzzle_list_row_activated(box, row, NULL);
    }
}

void info_panel_highlight_puzzle(GtkWidget* info_panel, int index) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->puzzle_ui.puzzle_list_box) return;

    
    // Block signals to prevent recursion!
    if (g_signal_handlers_disconnect_by_func(panel->puzzle_ui.puzzle_list_box, on_puzzle_list_row_activated, NULL) > 0) {
        // We disconnected it, we will reconnect later.
        // Actually, disconnect_by_func disconnects ALL matches. 
        // Best to use block/unblock if we have the handler ID, but we don't store it separate.
        // But block_by_func works by function pointer matching.
        g_signal_handlers_block_by_func(panel->puzzle_ui.puzzle_list_box, on_puzzle_list_row_activated, NULL);
    }

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
            if (idx_ptr != NULL) {
                 int idx = GPOINTER_TO_INT(idx_ptr);
                 callback(idx, cb_data);
            }
        }
    }
}

void info_panel_set_puzzle_list_callback(GtkWidget* info_panel, GCallback on_selected, gpointer user_data) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->puzzle_ui.puzzle_list_box) return;

    // Store callback and data on the listbox itself
    g_object_set_data(G_OBJECT(panel->puzzle_ui.puzzle_list_box), "on-selected-cb", (gpointer)on_selected);
    g_object_set_data(G_OBJECT(panel->puzzle_ui.puzzle_list_box), "on-selected-data", user_data);
    
    g_signal_handlers_disconnect_by_func(panel->puzzle_ui.puzzle_list_box, on_puzzle_list_row_activated, NULL);
    g_signal_connect(panel->puzzle_ui.puzzle_list_box, "row-activated", G_CALLBACK(on_puzzle_list_row_activated), NULL);
}

void info_panel_set_game_mode(GtkWidget* info_panel, GameMode mode) {
    InfoPanel* panel = (InfoPanel*)g_object_get_data(G_OBJECT(info_panel), "info-panel-data");
    if (!panel || !panel->game_mode_dropdown) return;
    
    gtk_drop_down_set_selected(GTK_DROP_DOWN(panel->game_mode_dropdown), (guint)mode);
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
    if (!panel) return;
    panel->game_reset_callback = callback;
    panel->game_reset_callback_data = user_data;
}

// Puzzle List Management

// End of file cleanup


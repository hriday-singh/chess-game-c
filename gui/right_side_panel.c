#include "right_side_panel.h"
#include <string.h>

typedef struct {
    RightSidePanel* panel;
    PieceType type;
    Player owner;
} PieceIconData;

static void on_piece_icon_data_free(gpointer data) {
    g_free(data);
}

static void on_nav_btn_clicked(GtkButton* btn, gpointer user_data); // Forward decl

static GtkWidget* create_move_label(RightSidePanel* panel, const char* text, int ply_index) {
    GtkWidget* btn = gtk_button_new_with_label(text);
    gtk_widget_add_css_class(btn, "move-text");
    g_object_set_data(G_OBJECT(btn), "ply-index", GINT_TO_POINTER(ply_index));
    g_object_set_data(G_OBJECT(btn), "action", (gpointer)"goto_ply");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_nav_btn_clicked), panel);
    return btn;
}

#define PIECE_NONE ((PieceType)6)

static void draw_piece_history_icon(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    if (!gtk_widget_get_realized(GTK_WIDGET(area)) || !gtk_widget_get_visible(GTK_WIDGET(area)) || width <= 1 || height <= 1) return;
    PieceIconData* data = (PieceIconData*)user_data;
    
    cairo_surface_t* surface = theme_data_get_piece_surface(data->panel->theme, data->type, data->owner);
    if (!surface) return;

    double sw = cairo_image_surface_get_width(surface);
    double sh = cairo_image_surface_get_height(surface);
    if (sw <= 0 || sh <= 0) return;

    double scale = (double)height / sh * 0.8;
    cairo_save(cr);
    cairo_translate(cr, (width - sw * scale) / 2.0, (height - sh * scale) / 2.0);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static GtkWidget* create_move_cell_contents(RightSidePanel* panel, PieceType type, Player owner, const char* san, int ply_index) {
    // Single button acting as the pill
    GtkWidget* btn = create_move_label(panel, san, ply_index);
    gtk_widget_set_sensitive(btn, panel->interactive);
    
    // Create a box inside the button to hold Icon + Text
    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);
    
    // Piece Icon (if any)
    // if (type != PIECE_NONE && type != PIECE_PAWN) {
    if (type !=PIECE_NONE) {
        GtkWidget* icon = gtk_drawing_area_new();
        gtk_widget_set_size_request(icon, 20, 20); // Slightly smaller to fit nicely
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        
        PieceIconData* data = g_new0(PieceIconData, 1);
        data->panel = panel;
        data->type = type;
        data->owner = owner;
        
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), draw_piece_history_icon, data, on_piece_icon_data_free);
        gtk_box_append(GTK_BOX(btn_box), icon);
    }
    
    // The label from create_move_label is technically the button's child label
    // But create_move_label returns a button with a label already.
    // We need to retrieve that label, unparent it (or create new one) and put it in our box.
    // Simpler: Just make a new label here and set the button child to our box.
    
    GtkWidget* lbl = gtk_label_new(san);
    gtk_widget_add_css_class(lbl, "move-text-label"); // reuse or new class
    gtk_box_append(GTK_BOX(btn_box), lbl);
    
    gtk_button_set_child(GTK_BUTTON(btn), btn_box);
    
    return btn; // Return the button directly, no outer box needed
}

static void on_nav_btn_clicked(GtkButton* btn, gpointer user_data) {
    RightSidePanel* panel = (RightSidePanel*)user_data;
    const char* action = (const char*)g_object_get_data(G_OBJECT(btn), "action");
    if (!action) return;
    
    int ply_index = -1;
    if (strcmp(action, "goto_ply") == 0) {
        ply_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "ply-index"));
    } else if (strcmp(action, "prev") == 0) {
        ply_index = panel->viewed_ply - 1;
    } else if (strcmp(action, "next") == 0) {
        ply_index = panel->viewed_ply + 1;
    } else if (strcmp(action, "start") == 0) {
        ply_index = -1; // Board start
    } else if (strcmp(action, "end") == 0) {
        ply_index = panel->total_plies - 1;
    }
    
    if (panel->nav_cb) {
        panel->nav_cb(action, ply_index, panel->nav_cb_data);
    }
}

// --- Advantage Bar Drawing ---
static void draw_advantage_bar(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    if (!gtk_widget_get_realized(GTK_WIDGET(area)) || !gtk_widget_get_visible(GTK_WIDGET(area)) || width <= 1 || height <= 1) return;
    RightSidePanel* panel = (RightSidePanel*)user_data;
    
    // Get theme colors from widget
    GdkRGBA fg;
    gtk_widget_get_color(GTK_WIDGET(area), &fg);
    
    // Background (Black side / Base)
    // Use a darkened version of the foreground or a deep grey
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.8); 
    
    // Draw rounded background track (Capsule shape)
    double radius = width / 2.0;

    cairo_new_sub_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI/2, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, radius, height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_fill(cr);
    
    // White Side Fill (Starts from top)
    double white_ratio = 0.5;
    if (panel->is_mate) {
        white_ratio = (panel->current_eval > 0) ? 1.0 : 0.0;
    } else {
        double val = panel->current_eval;
        if (val > 5.0) val = 5.0;
        if (val < -5.0) val = -5.0;
        white_ratio = 0.5 + (val / 10.0);
    }
    
    double white_h = height * white_ratio;
    
    // Clip to the rounded track for the fill
    cairo_save(cr);
    cairo_new_sub_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI/2, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, radius, height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_clip(cr);
    
    // Fill with theme foreground (White/Light)
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.95);
    
    // Draw rounded rect top portion (clipped by Pill Shape)
    // We draw a "pill" for the fill part too, to ensure rounded separation
    // But since the top is clipped, we just need to ensure the bottom edge is rounded at 'white_h'
    
    cairo_new_sub_path(cr);
    // Start at top-left
    cairo_arc(cr, width - radius, radius, radius, -G_PI/2, 0); // Top-Right Corner
    
    // Check if white_h is large enough to warrant a full rounded bottom
    // If white_h is very small (near top), we just draw what we can
    
    double bottom_y = white_h;
    if (bottom_y < radius) bottom_y = radius; // Clamp for safety if very high
    
    // Bottom-Right Corner of the FILL
    // We want the fill to end at 'white_h' with a rounded edge
    cairo_line_to(cr, width, white_h - radius);
    cairo_arc(cr, width - radius, white_h - radius, radius, 0, G_PI/2);
    
    // Bottom-Left Corner of the FILL
    cairo_arc(cr, radius, white_h - radius, radius, G_PI/2, G_PI);
    
    // Top-Left Corner
    cairo_line_to(cr, 0, radius);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
    
    // Zero Reference Line (Red, exact middle)
    // Darker Red as requested
    cairo_set_source_rgba(cr, 0.8, 0.0, 0.0, 1.0);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, 0, height / 2.0);
    cairo_line_to(cr, width, height / 2.0);
    cairo_stroke(cr);
}

RightSidePanel* right_side_panel_new(GameLogic* logic, ThemeData* theme) {
    RightSidePanel* panel = g_new0(RightSidePanel, 1);
    panel->logic = logic;
    panel->theme = theme;
    panel->current_eval = 0.0;
    panel->viewed_ply = -1;
    panel->total_plies = 0;
    panel->interactive = true;
    panel->last_feedback_ply = -1; // Initialize new member

    // --- Root Horizontal Container [Rail | Content Column] ---
    panel->container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(panel->container, "right-side-panel-v4");
    gtk_widget_set_size_request(panel->container, 300, -1);
    gtk_widget_set_hexpand(panel->container, FALSE); // STRICT: Do not grow
    gtk_widget_set_vexpand(panel->container, TRUE);  // Force vertical expansion
    gtk_widget_set_valign(panel->container, GTK_ALIGN_FILL);
    
    // --- 1. Side Rail (Full Height) ---
    GtkWidget* rail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(rail_box, "adv-rail-box");
    gtk_widget_set_size_request(rail_box, 28, -1);
    
    GtkWidget* w_lbl = gtk_label_new("W");
    gtk_widget_add_css_class(w_lbl, "rail-side-label");
    gtk_box_append(GTK_BOX(rail_box), w_lbl);
    
    panel->adv_rail = gtk_drawing_area_new();
    gtk_widget_set_vexpand(panel->adv_rail, TRUE);
    // Use a special class to pull the theme's accent color into 'color' property
    gtk_widget_add_css_class(panel->adv_rail, "accent-color-proxy"); 
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(panel->adv_rail), draw_advantage_bar, panel, NULL);
    gtk_box_append(GTK_BOX(rail_box), panel->adv_rail);
    
    GtkWidget* b_lbl = gtk_label_new("B");
    gtk_widget_add_css_class(b_lbl, "rail-side-label");
    gtk_box_append(GTK_BOX(rail_box), b_lbl);
    
    gtk_box_append(GTK_BOX(panel->container), rail_box);
    
    // --- 2. Main Content Column ---
    panel->main_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(panel->main_col, FALSE); // STRICT: Do not grow
    gtk_box_append(GTK_BOX(panel->container), panel->main_col);
    
    // --- 2a. Position Info (Top) ---
    panel->pos_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(panel->pos_info, "pos-info-v4");
    
    GtkWidget* eval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    panel->eval_lbl = gtk_label_new("+0.0");
    gtk_widget_add_css_class(panel->eval_lbl, "eval-text-v4");
    gtk_box_append(GTK_BOX(eval_row), panel->eval_lbl);
    
    panel->mate_lbl = gtk_label_new("");
    gtk_widget_add_css_class(panel->mate_lbl, "mate-notice-v4");
    gtk_widget_set_visible(panel->mate_lbl, FALSE);
    gtk_box_append(GTK_BOX(eval_row), panel->mate_lbl);
    
    gtk_box_append(GTK_BOX(panel->pos_info), eval_row);
    
    panel->hanging_lbl = gtk_label_new("HANGING\nWhite: 0   Black: 0");
    gtk_widget_add_css_class(panel->hanging_lbl, "hanging-text-v4");
    gtk_label_set_justify(GTK_LABEL(panel->hanging_lbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_wrap(GTK_LABEL(panel->hanging_lbl), FALSE);
    gtk_widget_set_halign(panel->hanging_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel->pos_info), panel->hanging_lbl);
    
    gtk_box_append(GTK_BOX(panel->main_col), panel->pos_info);
    gtk_box_append(GTK_BOX(panel->main_col), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // --- 2b. Feedback Zone (Middle) ---
    panel->feedback_zone = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(panel->feedback_zone, "feedback-zone-v4");
    
    panel->feedback_rating_lbl = gtk_label_new("");
    gtk_widget_add_css_class(panel->feedback_rating_lbl, "feedback-rating-v4");
    gtk_widget_set_halign(panel->feedback_rating_lbl, GTK_ALIGN_START);
    
    panel->feedback_desc_lbl = gtk_label_new("Analyzing position...");
    gtk_widget_add_css_class(panel->feedback_desc_lbl, "feedback-desc-v4");
    gtk_widget_set_halign(panel->feedback_desc_lbl, GTK_ALIGN_START);
    
    gtk_box_append(GTK_BOX(panel->feedback_zone), panel->feedback_rating_lbl);
    gtk_box_append(GTK_BOX(panel->feedback_zone), panel->feedback_desc_lbl);
    gtk_box_append(GTK_BOX(panel->main_col), panel->feedback_zone);
    gtk_box_append(GTK_BOX(panel->main_col), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // --- 2c. Move History (Bottom) ---
    panel->history_zone = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(panel->history_zone, TRUE);
    
    GtkWidget* hist_header = gtk_label_new("Move History");
    gtk_widget_add_css_class(hist_header, "history-header-v4");
    gtk_widget_set_halign(hist_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel->history_zone), hist_header);
    
    panel->history_scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(panel->history_scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(panel->history_scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    panel->history_list = gtk_list_box_new();
    gtk_widget_add_css_class(panel->history_list, "move-history-list-v4");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(panel->history_list), GTK_SELECTION_NONE);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(panel->history_scrolled), panel->history_list);
    gtk_box_append(GTK_BOX(panel->history_zone), panel->history_scrolled);
    gtk_box_append(GTK_BOX(panel->main_col), panel->history_zone);
    
    // Footer Navigation
    panel->nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(panel->nav_box, "nav-footer-v4");
    gtk_widget_set_halign(panel->nav_box, GTK_ALIGN_CENTER);
    
    const char* icons[] = {"go-first-symbolic", "go-previous-symbolic", "go-next-symbolic", "go-last-symbolic"};
    const char* actions[] = {"start", "prev", "next", "end"};
    GtkWidget** btns[] = {&panel->btn_start, &panel->btn_prev, &panel->btn_next, &panel->btn_end};
    
    for (int i = 0; i < 4; i++) {
        *btns[i] = gtk_button_new_from_icon_name(icons[i]);
        gtk_widget_add_css_class(*btns[i], "nav-btn-v4");
        g_object_set_data(G_OBJECT(*btns[i]), "action", (gpointer)actions[i]);
        g_signal_connect(*btns[i], "clicked", G_CALLBACK(on_nav_btn_clicked), panel);
        gtk_box_append(GTK_BOX(panel->nav_box), *btns[i]);
    }
    gtk_box_append(GTK_BOX(panel->main_col), panel->nav_box);
    
    return panel;
}

void right_side_panel_free(RightSidePanel* panel) {
    if (!panel) return;
    g_free(panel);
}
    
GtkWidget* right_side_panel_get_widget(RightSidePanel* panel) {
    if (!panel) return NULL;
    return panel->container;
}

void right_side_panel_update_stats(RightSidePanel* panel, double evaluation, bool is_mate) {
    if (!panel) return;
    panel->current_eval = evaluation;
    panel->is_mate = is_mate;
    
    if (panel->eval_lbl && GTK_IS_LABEL(panel->eval_lbl)) {
        char buf[32];
        if (is_mate) {
            snprintf(buf, sizeof(buf), "M%s", evaluation >= 0 ? "+" : "-"); 
        } else {
            snprintf(buf, sizeof(buf), "%s%.1f", evaluation >= 0 ? "+" : "", evaluation);
        }
        gtk_label_set_text(GTK_LABEL(panel->eval_lbl), buf);
    }
    
    if (panel->adv_rail) {
        gtk_widget_queue_draw(panel->adv_rail);
    }
}

void right_side_panel_set_mate_warning(RightSidePanel* panel, int moves) {
    if (!panel || !panel->mate_lbl || !GTK_IS_LABEL(panel->mate_lbl)) return;
    if (moves > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "MATE IN %d", moves);
        gtk_label_set_text(GTK_LABEL(panel->mate_lbl), buf);
        gtk_widget_set_visible(panel->mate_lbl, TRUE);
    } else {
        gtk_widget_set_visible(panel->mate_lbl, FALSE);
    }
}

void right_side_panel_set_hanging_pieces(RightSidePanel* panel, int white_count, int black_count) {
    if (!panel || !panel->hanging_lbl || !GTK_IS_LABEL(panel->hanging_lbl)) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "HANGING\nWhite: %d   Black: %d", white_count, black_count);
    gtk_label_set_text(GTK_LABEL(panel->hanging_lbl), buf);
}

void right_side_panel_show_rating_toast(RightSidePanel* panel, const char* rating, const char* reason, int ply_index) {
    if (!panel) return;
    
    panel->last_feedback_ply = ply_index;
    
    if (panel->feedback_rating_lbl && GTK_IS_LABEL(panel->feedback_rating_lbl)) {
        gtk_label_set_text(GTK_LABEL(panel->feedback_rating_lbl), rating);
    }
    if (panel->feedback_desc_lbl && GTK_IS_LABEL(panel->feedback_desc_lbl)) {
        gtk_label_set_text(GTK_LABEL(panel->feedback_desc_lbl), reason ? reason : "Analyzing...");
    }
    
    // Clear old severity classes
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-best");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-good");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-inaccuracy");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-mistake");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-blunder");
    
    // Add appropriate severity class for background tinting
    if (strcmp(rating, "Best") == 0) gtk_widget_add_css_class(panel->feedback_zone, "feedback-best");
    else if (strcmp(rating, "Good") == 0) gtk_widget_add_css_class(panel->feedback_zone, "feedback-good");
    else if (strcmp(rating, "Inaccuracy") == 0) gtk_widget_add_css_class(panel->feedback_zone, "feedback-inaccuracy");
    else if (strcmp(rating, "Mistake") == 0) gtk_widget_add_css_class(panel->feedback_zone, "feedback-mistake");
    else if (strcmp(rating, "Blunder") == 0) gtk_widget_add_css_class(panel->feedback_zone, "feedback-blunder");

    if (!gtk_widget_get_visible(panel->feedback_zone))
        gtk_widget_set_visible(panel->feedback_zone, TRUE);
}

void right_side_panel_show_toast(RightSidePanel* panel, const char* message) {
    if (!panel) return;
    gtk_label_set_text(GTK_LABEL(panel->feedback_rating_lbl), "INFO");
    gtk_label_set_text(GTK_LABEL(panel->feedback_desc_lbl), message);
    
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-best");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-good");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-inaccuracy");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-mistake");
    gtk_widget_remove_css_class(panel->feedback_zone, "feedback-blunder");
    
    if (!gtk_widget_get_visible(panel->feedback_zone))
        gtk_widget_set_visible(panel->feedback_zone, TRUE);
    // Persistence: No auto-fade timers in HAL design.
}

void right_side_panel_set_interactive(RightSidePanel* panel, bool interactive) {
    if (!panel) return;
    panel->interactive = interactive;
    
    // Footer button sensitivity
    gtk_widget_set_sensitive(panel->btn_start, interactive);
    gtk_widget_set_sensitive(panel->btn_prev, interactive);
    gtk_widget_set_sensitive(panel->btn_next, interactive);
    gtk_widget_set_sensitive(panel->btn_end, interactive);
    
    // Optionally hide when not interactive (as requested)
    if (gtk_widget_get_visible(panel->nav_box) != interactive)
        gtk_widget_set_visible(panel->nav_box, interactive);
    
    // Deep sensitivity for history list boxes
    GtkWidget* row = gtk_widget_get_first_child(panel->history_list);
    while (row) {
        GtkWidget* row_box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        GtkWidget* num_lbl = gtk_widget_get_first_child(row_box);
        GtkWidget* w_cell = gtk_widget_get_next_sibling(num_lbl);
        GtkWidget* b_cell = gtk_widget_get_next_sibling(w_cell);
        
        if (w_cell) {
            GtkWidget* w_hbox = gtk_widget_get_first_child(w_cell);
            if (w_hbox) {
                GtkWidget* w_btn = gtk_widget_get_last_child(w_hbox);
                if (w_btn && GTK_IS_BUTTON(w_btn)) gtk_widget_set_sensitive(w_btn, interactive);
            }
        }
        if (b_cell) {
            GtkWidget* b_hbox = gtk_widget_get_first_child(b_cell);
            if (b_hbox) {
                GtkWidget* b_btn = gtk_widget_get_last_child(b_hbox);
                if (b_btn && GTK_IS_BUTTON(b_btn)) gtk_widget_set_sensitive(b_btn, interactive);
            }
        }
        row = gtk_widget_get_next_sibling(row);
    }
}

void right_side_panel_truncate_history(RightSidePanel* panel, int ply_index) {
    if (!panel) return;
    
    // Convert ply_index to row index
    // ply 0,1 -> row 0
    // ply 2,3 -> row 1
    // Actually it's simpler to just count plies
    // and remove anything >= ply_index
    
    panel->total_plies = ply_index;
    
    // Traverse rows and clear or remove
    int current_ply = 0;
    GtkWidget* row = gtk_widget_get_first_child(panel->history_list);
    while (row) {
        GtkWidget* next = gtk_widget_get_next_sibling(row);
        
        if (current_ply >= ply_index) {
            // Remove entire row
            gtk_list_box_remove(GTK_LIST_BOX(panel->history_list), row);
        } else if (current_ply + 1 >= ply_index) {
            // This is the row where the truncation happens (if it's black's move being cut)
            GtkWidget* row_box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
            GtkWidget* b_cell = gtk_widget_get_last_child(row_box);
            if (b_cell) {
                GtkWidget* child;
                while ((child = gtk_widget_get_first_child(b_cell)) != NULL) {
                    gtk_box_remove(GTK_BOX(b_cell), child);
                }
            }
        }
        
        current_ply += 2;
        row = next;
    }
}

void right_side_panel_set_analysis_visible(RightSidePanel* panel, bool visible) {
    if (!panel) return;
    
    // pos_info is "greyed out" (insensitive) if disabled
    gtk_widget_set_sensitive(panel->pos_info, visible);
    // Optionally also lower opacity for a stronger greyed-out effect
    gtk_widget_set_opacity(panel->pos_info, visible ? 1.0 : 0.4);
    
    // Rail and Feedback are hidden if disabled
    gtk_widget_set_visible(panel->adv_rail, visible);
    gtk_widget_set_visible(panel->feedback_zone, visible);
}

#include "config_manager.h" // Include here for local casting

void right_side_panel_sync_config(RightSidePanel* panel, const void* config) {
    if (!panel || !config) return;
    AppConfig* cfg = (AppConfig*)config;
    
    bool master_on = cfg->enable_live_analysis;
    
    // 1. Advantage Rail
    bool show_rail = master_on && cfg->show_advantage_bar;
    gtk_widget_set_visible(panel->adv_rail, show_rail);
    // Hide the entire W/B rail box if rail is off
    GtkWidget* rail_box = gtk_widget_get_parent(panel->adv_rail);
    if (rail_box) gtk_widget_set_visible(rail_box, show_rail);
    
    // 2. Feedback Zone
    bool show_feedback = master_on && cfg->show_move_rating;
    gtk_widget_set_visible(panel->feedback_zone, show_feedback);
    
    // 3. Mate/Hanging labels inside pos_info
    bool show_mate = master_on && cfg->show_mate_warning;
    // We only set visible if it actually has content, but here we can hide if master/child is off
    if (!show_mate) gtk_widget_set_visible(panel->mate_lbl, FALSE);
    
    bool show_hanging = master_on && cfg->show_hanging_pieces;
    gtk_widget_set_visible(panel->hanging_lbl, show_hanging);
    
    // 4. Sensitivity of pos_info
    gtk_widget_set_sensitive(panel->pos_info, master_on);
    gtk_widget_set_opacity(panel->pos_info, master_on ? 1.0 : 0.4);
}

void right_side_panel_set_nav_visible(RightSidePanel* panel, bool visible) {
    if (!panel) return;
    if (gtk_widget_get_visible(panel->nav_box) != visible)
        gtk_widget_set_visible(panel->nav_box, visible);
}

void right_side_panel_add_move(RightSidePanel* panel, Move* move, int move_number, Player turn) {
    if (!panel) return;
    
    int ply_index = (move_number - 1) * 2 + (turn == PLAYER_WHITE ? 0 : 1);
    
    if (ply_index < panel->total_plies) {
        right_side_panel_truncate_history(panel, ply_index);
    }
    
    char san[16];
    gamelogic_get_move_san(panel->logic, move, san, sizeof(san));
    
    panel->total_plies = ply_index + 1;
    panel->viewed_ply = ply_index;
    
    if (turn == PLAYER_WHITE) {
        GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(row_box, "move-history-row-v2");
        
        // Col 1: Move Number (Fixed width via CSS)
        char num_buf[16];
        snprintf(num_buf, sizeof(num_buf), "%d.", move_number);
        GtkWidget* num_lbl = gtk_label_new(num_buf);
        gtk_widget_add_css_class(num_lbl, "move-number-v2");
        gtk_label_set_xalign(GTK_LABEL(num_lbl), 1.0);
        gtk_box_append(GTK_BOX(row_box), num_lbl);
        
        // Col 2: White Move
        GtkWidget* w_cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(w_cell, "move-cell-v2");
        gtk_widget_set_hexpand(w_cell, TRUE);
        
        PieceType p_type = PIECE_PAWN;
        if (panel->logic->board[move->endRow][move->endCol]) {
            p_type = panel->logic->board[move->endRow][move->endCol]->type;
        }
        
        GtkWidget* w_contents = create_move_cell_contents(panel, p_type, PLAYER_WHITE, san, ply_index);
        gtk_box_append(GTK_BOX(w_cell), w_contents);
        gtk_box_append(GTK_BOX(row_box), w_cell);
        
        // Col 3: Black Move (Empty for now)
        GtkWidget* b_cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(b_cell, "move-cell-v2");
        gtk_widget_set_hexpand(b_cell, TRUE);
        gtk_box_append(GTK_BOX(row_box), b_cell);
        
        gtk_list_box_append(GTK_LIST_BOX(panel->history_list), row_box);
    } else {
        GtkWidget* last_row = gtk_widget_get_last_child(panel->history_list);
        if (last_row) {
            GtkWidget* row_box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(last_row));
            GtkWidget* b_cell = gtk_widget_get_last_child(row_box);
            if (b_cell) {
                PieceType p_type = PIECE_PAWN;
                if (panel->logic->board[move->endRow][move->endCol]) {
                    p_type = panel->logic->board[move->endRow][move->endCol]->type;
                }
                GtkWidget* b_contents = create_move_cell_contents(panel, p_type, PLAYER_BLACK, san, ply_index);
                gtk_box_append(GTK_BOX(b_cell), b_contents);
            }
        }
    }
    
    right_side_panel_highlight_ply(panel, ply_index);
    
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(panel->history_scrolled));
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
}

void right_side_panel_highlight_ply(RightSidePanel* panel, int ply_index) {
    if (!panel) return;
    panel->viewed_ply = ply_index;
    
    // Traverse all rows and their labels to set the 'active' class
    GtkWidget* row = gtk_widget_get_first_child(panel->history_list);
    while (row) {
        GtkWidget* row_box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        GtkWidget* child = gtk_widget_get_first_child(row_box); // num_lbl
        child = gtk_widget_get_next_sibling(child); // w_cell
        
        for (int i = 0; i < 2; i++) {
            if (!child) break;
            if (child) {
                GtkWidget* hbox = gtk_widget_get_first_child(child);
                GtkWidget* lbl = hbox ? gtk_widget_get_last_child(hbox) : NULL;
                if (lbl && GTK_IS_BUTTON(lbl)) {
                    int p_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(lbl), "ply-index"));
                    if (p_idx == ply_index) {
                        gtk_widget_add_css_class(lbl, "active");
                    } else {
                        gtk_widget_remove_css_class(lbl, "active");
                    }
                }
            }
            child = gtk_widget_get_next_sibling(child); // next cell
        }
        row = gtk_widget_get_next_sibling(row);
    }
}

void right_side_panel_clear_history(RightSidePanel* panel) {
    if (!panel) return;
    panel->total_plies = 0;
    panel->viewed_ply = -1;
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(panel->history_list)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(panel->history_list), child);
    }
}

void right_side_panel_set_nav_callback(RightSidePanel* panel, RightSidePanelNavCallback callback, gpointer user_data) {
    if (!panel) return;
    panel->nav_cb = callback;
    panel->nav_cb_data = user_data;
}

void right_side_panel_set_current_move(RightSidePanel* panel, int move_index) {
    // move_index here usually means ply_index in our redesigned context
    right_side_panel_highlight_ply(panel, move_index);
}

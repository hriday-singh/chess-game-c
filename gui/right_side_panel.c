#include "right_side_panel.h"
#include <string.h>

static bool debug_mode = true;

typedef struct {
    RightSidePanel* panel;
    PieceType type;
    Player owner;
} PieceIconData;

static void on_piece_icon_data_free(gpointer data) {
    g_free(data);
}

static void on_nav_btn_clicked(GtkButton* btn, gpointer user_data) {
    RightSidePanel* panel = (RightSidePanel*)user_data;
    const char* action = (const char*)g_object_get_data(G_OBJECT(btn), "action");
    if (!action) return;
    
    int ply_index = -1;
    if (strcmp(action, "goto_ply") == 0) {
        ply_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "ply-index"));
    }
    
    if (panel->nav_cb) {
        panel->nav_cb(action, ply_index, panel->nav_cb_data);
    }
}

void right_side_panel_set_replay_lock(RightSidePanel* panel, bool locked) {
    if (!panel) return;
    panel->replay_lock = locked;
    if (!locked) panel->locked_ply = -1;
}

static GtkWidget* create_move_label(RightSidePanel* panel, const char* text, int ply_index) {
    GtkWidget* btn = gtk_button_new_with_label(text);
    gtk_widget_add_css_class(btn, "move-text-btn");
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE); // Remove standard GTK frame
    gtk_widget_set_can_focus(btn, FALSE);
    g_object_set_data(G_OBJECT(btn), "ply-index", GINT_TO_POINTER(ply_index));
    g_object_set_data(G_OBJECT(btn), "action", (gpointer)"goto_ply");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_nav_btn_clicked), panel);
    return btn;
}

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

static GtkWidget* create_move_cell_contents(RightSidePanel* panel, PieceType type, Player owner, const char* uci, int ply_index) {
    // Single button acting as the pill
    GtkWidget* btn = create_move_label(panel, uci, ply_index);
    gtk_widget_set_sensitive(btn, panel->interactive);
    
    // Create a box inside the button to hold Icon + Text
    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);
    
    // Piece Icon (if any)
    if (type != NO_PIECE) {
        GtkWidget* icon = gtk_drawing_area_new();
        gtk_widget_set_size_request(icon, 32, 32); // Even larger icons for better visibility
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        
        PieceIconData* data = g_new0(PieceIconData, 1);
        data->panel = panel;
        data->type = type;
        data->owner = owner;
        
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), draw_piece_history_icon, data, on_piece_icon_data_free);
        gtk_box_append(GTK_BOX(btn_box), icon);
    }
    
    GtkWidget* lbl = gtk_label_new(uci);
    gtk_widget_add_css_class(lbl, "move-text-label"); 
    gtk_box_append(GTK_BOX(btn_box), lbl);
    
    gtk_button_set_child(GTK_BUTTON(btn), btn_box);
    
    return btn; 
}

// on_nav_btn_clicked removed - buttons moved to InfoPanel

// --- Advantage Bar Drawing ---
static void draw_advantage_bar(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    if (!gtk_widget_get_realized(GTK_WIDGET(area)) || !gtk_widget_get_visible(GTK_WIDGET(area)) || width <= 1 || height <= 1) return;
    RightSidePanel* panel = (RightSidePanel*)user_data;
    if (!panel) return;
    
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
    cairo_arc(cr, radius, height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_fill(cr);
    
    double white_ratio = 0.5;
    if (panel->is_mate) {
        white_ratio = (panel->current_eval > 0) ? 1.0 : 0.0;
    } else {
        double val = panel->current_eval;
        if (val > 10.0) val = 10.0;
        if (val < -10.0) val = -10.0;
        white_ratio = 0.5 + (val / 20.0);
    }

    double white_h;
    bool w_at_top = panel->flipped; // True if Black at bottom (B at bottom implies W at top)
    
    if (w_at_top) {
        white_h = height * white_ratio;
    } else {
        white_h = height * (1.0 - white_ratio);
    }
    
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
    
    if (w_at_top) {
        // Draw from TOP
        cairo_rectangle(cr, 0, 0, width, white_h);
    } else {
        // Draw from BOTTOM
        cairo_rectangle(cr, 0, height - white_h, width, white_h);
    }
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

void right_side_panel_set_analyze_callback(RightSidePanel* panel, GCallback callback, gpointer user_data) {
    if (!panel || !panel->analyze_btn) {
        printf("[RightSidePanel] Error: Cannot set analyze callback (panel or btn is NULL)\n");
        return;
    }
    printf("[RightSidePanel] Connecting analyze callback to button %p\n", (void*)panel->analyze_btn);
    // Disconnect any previous handlers to avoid duplicates if called multiple times?
    // For now simple connection.
    g_signal_connect(panel->analyze_btn, "clicked", callback, user_data);
    g_object_set_data(G_OBJECT(panel->analyze_btn), "user_data", user_data);
}

// Global reference for loading dialog (simple singleton approach for this UI helper)
static GtkWidget* g_loading_dialog = NULL;

void right_side_panel_set_analyzing_state(RightSidePanel* panel, bool analyzing) {
    if (!panel) return;
    
    if (analyzing) {
        gtk_button_set_label(GTK_BUTTON(panel->analyze_btn), "Cancel Analysis");
        gtk_widget_remove_css_class(panel->analyze_btn, "suggested-action");
        gtk_widget_add_css_class(panel->analyze_btn, "destructive-action");
        
        // Show Loading Overlay
        if (!g_loading_dialog) {
            GtkWidget* win = GTK_WIDGET(gtk_widget_get_root(panel->container)); // Cast to GtkWidget*
            if (GTK_IS_WINDOW(win)) {
                g_loading_dialog = gtk_window_new();
                gtk_window_set_transient_for(GTK_WINDOW(g_loading_dialog), GTK_WINDOW(win));
                gtk_window_set_modal(GTK_WINDOW(g_loading_dialog), TRUE);
                gtk_window_set_decorated(GTK_WINDOW(g_loading_dialog), FALSE);
                // gtk_window_set_default_size(GTK_WINDOW(g_loading_dialog), 300, 150);
                
                // CSS for styling
                GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
                gtk_widget_set_margin_top(box, 40);
                gtk_widget_set_margin_bottom(box, 40);
                gtk_widget_set_margin_start(box, 60);
                gtk_widget_set_margin_end(box, 60);
                gtk_window_set_child(GTK_WINDOW(g_loading_dialog), box);
                
                GtkWidget* spinner = gtk_spinner_new();
                gtk_widget_set_size_request(spinner, 64, 64);
                gtk_spinner_start(GTK_SPINNER(spinner));
                gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
                gtk_box_append(GTK_BOX(box), spinner);
                
                GtkWidget* lbl = gtk_label_new("Analyzing Game...");
                gtk_widget_add_css_class(lbl, "loading-label"); // Define in CSS
                gtk_box_append(GTK_BOX(box), lbl);
                
                gtk_widget_add_css_class(g_loading_dialog, "loading-overlay"); // Define in CSS
                
                gtk_window_present(GTK_WINDOW(g_loading_dialog));
            }
        }
    } else {
        gtk_button_set_label(GTK_BUTTON(panel->analyze_btn), "Analyze Game");
        gtk_widget_remove_css_class(panel->analyze_btn, "destructive-action");
        gtk_widget_add_css_class(panel->analyze_btn, "suggested-action");
        
        if (g_loading_dialog) {
            gtk_window_destroy(GTK_WINDOW(g_loading_dialog));
            g_loading_dialog = NULL;
        }
    }
}

static void on_toggle_clicked(GtkButton* btn, gpointer user_data) {
    RightSidePanel* panel = (RightSidePanel*)user_data;
    if (!panel || !panel->content_side) return;
    
    bool is_visible = gtk_widget_get_visible(panel->content_side);
    bool target = !is_visible;
    
    gtk_widget_set_visible(panel->content_side, target);
    
    // Update arrow icon: Points left (start) when expanded, Points right (end) when collapsed
    // Note: GTK standard "pan-start" usually points left in LTR.
    const char* icon_name = target ? "pan-start-symbolic" : "pan-end-symbolic";
    gtk_button_set_icon_name(btn, icon_name);
    
    // Dynamic Tooltip
    gtk_widget_set_tooltip_text(GTK_WIDGET(btn), target ? "Hide Move History" : "Show Move History");
    
    // Adjust panel container width to match collapsed vs expanded
    if (target) {
        gtk_widget_set_size_request(panel->container, 380, -1);
    } else {
        gtk_widget_set_size_request(panel->container, 40, -1); // Narrow when collapsed
    }
}

RightSidePanel* right_side_panel_new(GameLogic* logic, ThemeData* theme) {
    RightSidePanel* panel = g_new0(RightSidePanel, 1);
    panel->logic = logic;
    panel->theme = theme;
    panel->current_eval = 0.0;
    panel->viewed_ply = -1;
    panel->total_plies = 0;
    panel->last_highlighted_ply = -1; // For O(1) unhighlighting
    panel->interactive = true;
    panel->last_feedback_ply = -1; // Initialize new member

    // --- Root Horizontal Container [Toggle | Content Side] ---
    panel->container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(panel->container, "right-side-panel-v4");
    gtk_widget_set_size_request(panel->container, 380, -1); 
    gtk_widget_set_hexpand(panel->container, FALSE); 
    gtk_widget_set_vexpand(panel->container, TRUE);
    gtk_widget_set_valign(panel->container, GTK_ALIGN_FILL);
    gtk_widget_set_halign(panel->container, GTK_ALIGN_CENTER);

    // --- 0. Toggle Button (Always Visible) ---
    panel->toggle_btn = gtk_button_new_from_icon_name("pan-start-symbolic");
    gtk_widget_add_css_class(panel->toggle_btn, "panel-toggle-btn");
    gtk_widget_set_valign(panel->toggle_btn, GTK_ALIGN_CENTER); // Centered vertically
    gtk_widget_set_tooltip_text(panel->toggle_btn, "Hide Move History");
    g_signal_connect(panel->toggle_btn, "clicked", G_CALLBACK(on_toggle_clicked), panel);
    gtk_box_append(GTK_BOX(panel->container), panel->toggle_btn);

    // --- Wrap Content in a Side Box ---
    panel->content_side = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(panel->content_side, TRUE);
    gtk_box_append(GTK_BOX(panel->container), panel->content_side);
    
    // --- 1. Side Rail (Full Height) ---
    panel->rail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(panel->rail_box, "adv-rail-box");
    gtk_widget_set_size_request(panel->rail_box, 28, -1);
    
    panel->w_lbl = gtk_label_new("W");
    gtk_widget_add_css_class(panel->w_lbl, "rail-side-label");
    
    panel->b_lbl = gtk_label_new("B");
    gtk_widget_add_css_class(panel->b_lbl, "rail-side-label");
    
    panel->adv_rail = gtk_drawing_area_new();
    gtk_widget_set_vexpand(panel->adv_rail, TRUE);
    gtk_widget_add_css_class(panel->adv_rail, "accent-color-proxy"); 
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(panel->adv_rail), draw_advantage_bar, panel, NULL);
    
    // Initial orientation: White at bottom (B at top, W at bottom)
    gtk_box_append(GTK_BOX(panel->rail_box), panel->b_lbl);
    gtk_box_append(GTK_BOX(panel->rail_box), panel->adv_rail);
    gtk_box_append(GTK_BOX(panel->rail_box), panel->w_lbl);
    
    gtk_box_append(GTK_BOX(panel->content_side), panel->rail_box);
    
    // --- 2. Main Content Column ---
    panel->main_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(panel->main_col, TRUE); 
    gtk_widget_set_halign(panel->main_col, GTK_ALIGN_FILL);
    // Add internal padding so separators and text don't touch edges
    gtk_widget_set_margin_start(panel->main_col, 12);
    gtk_widget_set_margin_end(panel->main_col, 12);
    gtk_box_append(GTK_BOX(panel->content_side), panel->main_col);
    
    // --- 2a. Position Info (Top) ---
    panel->pos_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(panel->pos_info, "pos-info-v4");
    
    GtkWidget* eval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(eval_row, GTK_ALIGN_START); // Revert to START
    panel->eval_lbl = gtk_label_new("+0.0");
    gtk_widget_add_css_class(panel->eval_lbl, "eval-text-v4");
    gtk_box_append(GTK_BOX(eval_row), panel->eval_lbl);
    
    panel->mate_lbl = gtk_label_new("");
    gtk_widget_add_css_class(panel->mate_lbl, "mate-notice-v4");
    gtk_label_set_wrap(GTK_LABEL(panel->mate_lbl), TRUE);
    gtk_widget_set_visible(panel->mate_lbl, FALSE);
    gtk_box_append(GTK_BOX(eval_row), panel->mate_lbl);
    
    gtk_box_append(GTK_BOX(panel->pos_info), eval_row);
    
    panel->hanging_lbl = gtk_label_new("HANGING | WHITE: 0  BLACK: 0"); 
    gtk_widget_add_css_class(panel->hanging_lbl, "hanging-text-v4");
    gtk_label_set_justify(GTK_LABEL(panel->hanging_lbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_wrap(GTK_LABEL(panel->hanging_lbl), FALSE);
    gtk_widget_set_halign(panel->hanging_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel->pos_info), panel->hanging_lbl);
    
    panel->analysis_side_lbl = gtk_label_new("Analysis for White");
    gtk_widget_add_css_class(panel->analysis_side_lbl, "analysis-side-lbl-v4");
    gtk_widget_set_halign(panel->analysis_side_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel->pos_info), panel->analysis_side_lbl);
    
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
    gtk_label_set_justify(GTK_LABEL(panel->feedback_desc_lbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_wrap(GTK_LABEL(panel->feedback_desc_lbl), TRUE);
    
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
    
    // --- 4. Analysis Control Zone (New) ---
    panel->analysis_control_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(panel->analysis_control_box, 10);
    gtk_widget_set_margin_bottom(panel->analysis_control_box, 10);
    gtk_widget_set_margin_start(panel->analysis_control_box, 10);
    gtk_widget_set_margin_end(panel->analysis_control_box, 10);
    
    // Analyze Button
    panel->analyze_btn = gtk_button_new_with_label("Analyze Game");
    gtk_widget_add_css_class(panel->analyze_btn, "suggested-action"); 
    gtk_box_append(GTK_BOX(panel->analysis_control_box), panel->analyze_btn);
    
    // Stats Box (Initially Hidden)
    panel->analysis_stats_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_visible(panel->analysis_stats_box, FALSE);
    gtk_widget_set_margin_top(panel->analysis_stats_box, 8);
    
    panel->accuracy_lbl = gtk_label_new("Accuracy: -");
    gtk_widget_set_halign(panel->accuracy_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(panel->accuracy_lbl, "analysis-stat-label");
    gtk_box_append(GTK_BOX(panel->analysis_stats_box), panel->accuracy_lbl);
    
    panel->acpl_lbl = gtk_label_new("ACPL: -");
    gtk_widget_set_halign(panel->acpl_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(panel->acpl_lbl, "analysis-stat-label");
    gtk_box_append(GTK_BOX(panel->analysis_stats_box), panel->acpl_lbl);
    
    panel->win_prob_lbl = gtk_label_new("Win Chance: -");
    gtk_widget_set_halign(panel->win_prob_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(panel->win_prob_lbl, "analysis-stat-label");
    gtk_box_append(GTK_BOX(panel->analysis_stats_box), panel->win_prob_lbl);
    
    gtk_box_append(GTK_BOX(panel->analysis_control_box), panel->analysis_stats_box);
    gtk_box_append(GTK_BOX(panel->main_col), panel->analysis_control_box);

    // --- 5. Move History List (Scrollable) ---(Moved to InfoPanel)
    
    // Footer Navigation Removed (Moved to InfoPanel)
    
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

void right_side_panel_set_visible(RightSidePanel* panel, bool visible) {
    if (!panel || !panel->container) return;
    gtk_widget_set_visible(panel->container, visible);
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
    snprintf(buf, sizeof(buf), "HANGING | White: %d  Black: %d", white_count, black_count);
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
    
    // Footer button sensitivity handled in info_panel now
    
    // Deep sensitivity for history list boxes
    GtkWidget* row = gtk_widget_get_first_child(panel->history_list);
    while (row) {
        GtkWidget* row_box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        GtkWidget* num_lbl = gtk_widget_get_first_child(row_box);
        GtkWidget* w_cell = gtk_widget_get_next_sibling(num_lbl);
        GtkWidget* b_cell = gtk_widget_get_next_sibling(w_cell);
        
        if (w_cell) {
            GtkWidget* w_btn = gtk_widget_get_first_child(w_cell);
            if (w_btn && GTK_IS_BUTTON(w_btn)) gtk_widget_set_sensitive(w_btn, interactive);
        }
        if (b_cell) {
            GtkWidget* b_btn = gtk_widget_get_first_child(b_cell);
            if (b_btn && GTK_IS_BUTTON(b_btn)) gtk_widget_set_sensitive(b_btn, interactive);
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
    panel->last_highlighted_ply = -1; // Reset for safety after truncate
    
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
    
    // 0. Update Orientation (Dynamic swap of W/B)
    bool flipped = (panel->logic->playerSide == PLAYER_BLACK);
    right_side_panel_set_flipped(panel, flipped);
    
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

// right_side_panel_set_nav_visible removed

// Helper to annotate a specific move cell
static void annotate_move_cell(GtkWidget* cell, const PlyAnalysisRecord* rec) {
    if (!cell || !rec) return;
    GtkWidget* btn = gtk_widget_get_first_child(cell);
    if (!btn || !GTK_IS_BUTTON(btn)) return;

    // Reset styles first
    gtk_widget_remove_css_class(btn, "move-best");
    gtk_widget_remove_css_class(btn, "move-excellent");
    gtk_widget_remove_css_class(btn, "move-good");
    gtk_widget_remove_css_class(btn, "move-inaccuracy");
    gtk_widget_remove_css_class(btn, "move-mistake");
    gtk_widget_remove_css_class(btn, "move-blunder");
    
    const char* style = NULL;
    if (rec->label == LABEL_BEST) style = "move-best";
    else if (rec->label == LABEL_EXCELLENT) style = "move-excellent";
    else if (rec->label == LABEL_GOOD) style = "move-good";
    else if (rec->label == LABEL_INACCURACY) style = "move-inaccuracy";
    else if (rec->label == LABEL_MISTAKE) style = "move-mistake";
    else if (rec->label == LABEL_BLUNDER) style = "move-blunder";
    
    if (style) gtk_widget_add_css_class(btn, style);

    // Add/Update Annotation Label
    // Structure: Button -> Box -> [Icon?, MoveText, Annotation?]
    GtkWidget* box = gtk_button_get_child(GTK_BUTTON(btn));
    if (box && GTK_IS_BOX(box)) {
        // Check if we already have an annotation label
        GtkWidget* last = gtk_widget_get_last_child(box);
        const char* name = gtk_widget_get_name(last);
        
        bool has_annot = (name && strcmp(name, "annot-lbl") == 0);
        
        GtkWidget* annot_lbl = has_annot ? last : NULL;
        
        // If not, create it
        if (!annot_lbl) {
            annot_lbl = gtk_label_new("");
            gtk_widget_set_name(annot_lbl, "annot-lbl");
            gtk_widget_add_css_class(annot_lbl, "move-annotation");
            gtk_widget_set_margin_start(annot_lbl, 6);
            gtk_box_append(GTK_BOX(box), annot_lbl);
        }
        
        // Text: Symbol + Eval
        char buf[32];
        const char* sym = "";
        if (rec->label == LABEL_BEST) sym = "!!";
        else if (rec->label == LABEL_EXCELLENT) sym = "!";
        else if (rec->label == LABEL_INACCURACY) sym = "?!";
        else if (rec->label == LABEL_MISTAKE) sym = "?";
        else if (rec->label == LABEL_BLUNDER) sym = "??";
        
        if (abs(rec->played_move_eval) > 10000) {
             snprintf(buf, sizeof(buf), "%s", sym); 
        } else {
             snprintf(buf, sizeof(buf), "%s %.2f", sym, rec->played_move_eval / 100.0);
        }
        
        gtk_label_set_text(GTK_LABEL(annot_lbl), buf);
    }
}

void right_side_panel_set_analysis_result(RightSidePanel* panel, const GameAnalysisResult* res) {
    if (!panel) return;
    
    if (!res) {
        gtk_widget_set_visible(panel->analysis_stats_box, FALSE);
        return; 
    }
    
    // Update Stats
    gtk_widget_set_visible(panel->analysis_stats_box, TRUE);
    
    char acc_buf[64];
    // Simple mock accuracy for now until we implement real formula
    double w_acc = 100.0 - (res->white_acpl / 5.0);
    double b_acc = 100.0 - (res->black_acpl / 5.0);
    if(w_acc < 0) w_acc = 0; 
    if(b_acc < 0) b_acc = 0;
    
    snprintf(acc_buf, sizeof(acc_buf), "Accuracy: W %.1f%% | B %.1f%%", w_acc, b_acc);
    gtk_label_set_text(GTK_LABEL(panel->accuracy_lbl), acc_buf);
    
    char acpl_buf[64];
    snprintf(acpl_buf, sizeof(acpl_buf), "ACPL: W %.1f | B %.1f", res->white_acpl, res->black_acpl);
    gtk_label_set_text(GTK_LABEL(panel->acpl_lbl), acpl_buf);

    // Calculate approximate Win Probability based on final eval or dominant side in analysis
    // Using simple logistic formula: P = 1 / (1 + 10^(-eval/4)) where eval is in pawns (approx)
    // Here we have ACPL, but we can look at the last ply's eval if available, or just omit for now.
    // Let's deduce "Win Gauge" from ACPL difference for summary.
    double acpl_diff = res->black_acpl - res->white_acpl; // +ve means White played better (lower ACPL)
    // This is heuristic. Real win prob requires eval from the engine at the current board position.
    // We will placeholder or use a derived metric.
    
    char wp_buf[64];
    const char* leader = (acpl_diff > 20) ? "White" : (acpl_diff < -20) ? "Black" : "Drawish";
    snprintf(wp_buf, sizeof(wp_buf), "perf: %s (diff %.1f)", leader, acpl_diff);
    gtk_label_set_text(GTK_LABEL(panel->win_prob_lbl), wp_buf);

    // Iterate through history rows
    int ply_idx = 0;
    GtkWidget* row = gtk_widget_get_first_child(panel->history_list);
    while (row && ply_idx < res->total_plies) {
        GtkWidget* row_box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        
        // White Move (ply_idx)
        if (ply_idx < res->total_plies) {
             GtkWidget* w_cell = NULL;
             // Find W cell (2nd child of row_box usually: num, w, b)
             GtkWidget* child = gtk_widget_get_first_child(row_box); // num
             if (child) child = gtk_widget_get_next_sibling(child); // w
             w_cell = child;
             
             if (w_cell) {
                 annotate_move_cell(w_cell, &res->plies[ply_idx]);
             }
             ply_idx++;
        }
        
        // Black Move (ply_idx)
        if (ply_idx < res->total_plies) {
             GtkWidget* b_cell = NULL;
             GtkWidget* child = gtk_widget_get_first_child(row_box); // num
             if (child) child = gtk_widget_get_next_sibling(child); // w
             if (child) child = gtk_widget_get_next_sibling(child); // b
             b_cell = child;
             
             if (b_cell) {
                 annotate_move_cell(b_cell, &res->plies[ply_idx]);
             }
             ply_idx++;
        }
        
        row = gtk_widget_get_next_sibling(row);
    }  
    // Also show toast summary
    if (res) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Analysis Complete. Accuracy: W %.1f | B %.1f", 
                 (100.0 - res->white_acpl/5.0), (100.0 - res->black_acpl/5.0)); // Mock accuracy
        right_side_panel_show_toast(panel, buf);
    }
}

void right_side_panel_add_uci_move(RightSidePanel* panel, const char* uci, PieceType p_type, int move_number, Player turn) {
    if (!panel) return;
    
    int ply_index = (move_number - 1) * 2 + (turn == PLAYER_WHITE ? 0 : 1);
    
    // PieceType is now passed in explicitly because UCI (e.g. "e2e4") 
    // does not contain piece information like SAN (e.g. "Nf3") did.
    
    panel->total_plies = ply_index + 1;
    panel->viewed_ply = ply_index;
    
    if (turn == PLAYER_WHITE) {
        GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(row_box, "move-history-row-v2");
        
        char num_buf[16];
        snprintf(num_buf, sizeof(num_buf), "%d.", move_number);
        GtkWidget* num_lbl = gtk_label_new(num_buf);
        gtk_widget_add_css_class(num_lbl, "move-number-v2");
        gtk_label_set_xalign(GTK_LABEL(num_lbl), 1.0);
        gtk_box_append(GTK_BOX(row_box), num_lbl);
        
        GtkWidget* w_cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(w_cell, "move-cell-v2");
        gtk_widget_set_hexpand(w_cell, TRUE);
        
        GtkWidget* w_contents = create_move_cell_contents(panel, p_type, PLAYER_WHITE, uci, ply_index);
        gtk_widget_set_hexpand(w_contents, TRUE);
        gtk_widget_set_halign(w_contents, GTK_ALIGN_FILL);
        gtk_box_append(GTK_BOX(w_cell), w_contents);
        gtk_box_append(GTK_BOX(row_box), w_cell);
        
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
                GtkWidget* b_contents = create_move_cell_contents(panel, p_type, PLAYER_BLACK, uci, ply_index);
                gtk_widget_set_hexpand(b_contents, TRUE);
                gtk_widget_set_halign(b_contents, GTK_ALIGN_FILL);
                gtk_box_append(GTK_BOX(b_cell), b_contents);
            }
        }
    }
    
    right_side_panel_highlight_ply(panel, ply_index);
    
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(panel->history_scrolled));
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
}

void right_side_panel_add_move(RightSidePanel* panel, Move move, int m_num, Player p) {
    if (!panel) return;
    
    int ply_index = (m_num - 1) * 2 + (p == PLAYER_WHITE ? 0 : 1);
    PieceType p_type = move.movedPieceType;
    
    if (debug_mode) {
        printf("[RightSidePanel] add_move: ply=%d, m_num=%d, turn=%d, p_type=%d (moved)\n", 
               ply_index, m_num, p, p_type);
    }

    if (ply_index < panel->total_plies) {
        right_side_panel_truncate_history(panel, ply_index);
    }
    
    char uci[16];
    gamelogic_get_move_uci(panel->logic, &move, uci, sizeof(uci));
    
    if (debug_mode) {
        printf("[RightSidePanel] add_move: UCI generated: '%s'\n", uci);
    }

    panel->total_plies = ply_index + 1;
    panel->viewed_ply = ply_index;
    
    if (p == PLAYER_WHITE) {
        GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(row_box, "move-history-row-v2");
        
        // Col 1: Move Number (Fixed width via CSS)
        char num_buf[16];
        snprintf(num_buf, sizeof(num_buf), "%d.", m_num);
        GtkWidget* num_lbl = gtk_label_new(num_buf);
        gtk_widget_add_css_class(num_lbl, "move-number-v2");
        gtk_label_set_xalign(GTK_LABEL(num_lbl), 1.0);
        gtk_box_append(GTK_BOX(row_box), num_lbl);
        
        // Col 2: White Move
        GtkWidget* w_cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(w_cell, "move-cell-v2");
        gtk_widget_set_hexpand(w_cell, TRUE);
        
        GtkWidget* w_contents = create_move_cell_contents(panel, p_type, PLAYER_WHITE, uci, ply_index);
        gtk_widget_set_hexpand(w_contents, TRUE);
        gtk_widget_set_halign(w_contents, GTK_ALIGN_FILL);
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
                GtkWidget* b_contents = create_move_cell_contents(panel, p_type, PLAYER_BLACK, uci, ply_index);
                gtk_widget_set_hexpand(b_contents, TRUE);
                gtk_widget_set_halign(b_contents, GTK_ALIGN_FILL);
                gtk_box_append(GTK_BOX(b_cell), b_contents);
            }
        }
    }
    
    right_side_panel_highlight_ply(panel, ply_index);
    
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(panel->history_scrolled));
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
}

static void set_pill_active(RightSidePanel* panel, int ply_index, bool active) {
    if (ply_index < 0) return;
    int row_idx = ply_index / 2;
    int col_idx = ply_index % 2; // 0 for White, 1 for Black

    GtkWidget* row_widget = GTK_WIDGET(gtk_list_box_get_row_at_index(GTK_LIST_BOX(panel->history_list), row_idx));
    if (!row_widget) return;

    GtkWidget* row_box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row_widget));
    if (!row_box) return;

    // row_box children: [num_lbl, w_cell, b_cell]
    GtkWidget* num_lbl = gtk_widget_get_first_child(row_box);
    GtkWidget* cell = num_lbl ? gtk_widget_get_next_sibling(num_lbl) : NULL; // w_cell
    if (col_idx == 1 && cell) {
        cell = gtk_widget_get_next_sibling(cell); // b_cell
    }

    if (cell) {
        GtkWidget* btn = gtk_widget_get_first_child(cell);
        if (btn && GTK_IS_BUTTON(btn)) {
            if (active) {
                gtk_widget_add_css_class(btn, "active");
                gtk_widget_add_css_class(row_box, "active-row");
                gtk_widget_queue_draw(btn);
            } else {
                gtk_widget_remove_css_class(btn, "active");
                // Only remove row highlight if the OTHER pill in this row isn't active
                // (Actually, usually only one is active at a time, but to be safe:)
                gtk_widget_remove_css_class(row_box, "active-row");
            }
        }
    }
}

void right_side_panel_highlight_ply(RightSidePanel* panel, int ply_index) {
    if (!panel) return;

    // During replay: do NOT allow anyone to clear highlight
    if (panel->replay_lock && ply_index < 0) {
        return;
    }

    // Performance OPTIMIZATION: O(1) update instead of O(N) traversal
    // 1. Unhighlight previous
    if (panel->last_highlighted_ply >= 0) {
        set_pill_active(panel, panel->last_highlighted_ply, false);
    }

    // 2. Highlight new
    if (ply_index >= 0) {
        set_pill_active(panel, ply_index, true);
    }

    panel->last_highlighted_ply = ply_index;
    panel->viewed_ply = ply_index;
    panel->locked_ply = ply_index;

    // 3. Auto-scroll to make the highlighted row visible
    if (ply_index >= 0) {
        int row_idx = ply_index / 2;
        GtkWidget* row_widget = GTK_WIDGET(gtk_list_box_get_row_at_index(GTK_LIST_BOX(panel->history_list), row_idx));
        if (row_widget) {
            // Get vertical adjustment
            GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(panel->history_scrolled));
            if (adj) {
                 double page_size = gtk_adjustment_get_page_size(adj);
                 double value = gtk_adjustment_get_value(adj);
                 
                 // Compute row position relative to list
                 graphene_point_t p_list = {0}, p_row = {0};
                 if (gtk_widget_compute_point(row_widget, panel->history_list, &p_row, &p_list)) {
                     double row_y = p_list.y;
                     double row_h = gtk_widget_get_height(row_widget);
                     
                     // Scroll if out of view
                     if (row_y < value) {
                         gtk_adjustment_set_value(adj, row_y);
                     } else if (row_y + row_h > value + page_size) {
                         gtk_adjustment_set_value(adj, row_y + row_h - page_size);
                     }
                 }
            }
        }
    }
}

void right_side_panel_clear_history(RightSidePanel* panel) {
    if (!panel) return;
    panel->total_plies = 0;
    panel->viewed_ply = -1;
    panel->last_highlighted_ply = -1;
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

void right_side_panel_set_flipped(RightSidePanel* panel, bool flipped) {
    if (!panel || panel->flipped == flipped) return;
    panel->flipped = flipped;
    
    // Update Rail Labels
    // Remove all and re-append in correct order
    g_object_ref(panel->w_lbl);
    g_object_ref(panel->b_lbl);
    g_object_ref(panel->adv_rail);
    
    gtk_box_remove(GTK_BOX(panel->rail_box), panel->w_lbl);
    gtk_box_remove(GTK_BOX(panel->rail_box), panel->b_lbl);
    gtk_box_remove(GTK_BOX(panel->rail_box), panel->adv_rail);
    
    if (flipped) {
        // Black at bottom, W at top
        gtk_box_append(GTK_BOX(panel->rail_box), panel->w_lbl);
        gtk_box_append(GTK_BOX(panel->rail_box), panel->adv_rail);
        gtk_box_append(GTK_BOX(panel->rail_box), panel->b_lbl);
        gtk_label_set_text(GTK_LABEL(panel->analysis_side_lbl), "Analysis for Black");
    } else {
        // White at bottom, B at top
        gtk_box_append(GTK_BOX(panel->rail_box), panel->b_lbl);
        gtk_box_append(GTK_BOX(panel->rail_box), panel->adv_rail);
        gtk_box_append(GTK_BOX(panel->rail_box), panel->w_lbl);
        gtk_label_set_text(GTK_LABEL(panel->analysis_side_lbl), "Analysis for White");
    }
    
    g_object_unref(panel->w_lbl);
    g_object_unref(panel->b_lbl);
    g_object_unref(panel->adv_rail);
    
    gtk_widget_queue_draw(panel->adv_rail);
}

static void refresh_history_icons_recursive(GtkWidget* widget) {
    if (!widget) return;
    if (GTK_IS_DRAWING_AREA(widget)) {
        gtk_widget_queue_draw(widget);
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        refresh_history_icons_recursive(child);
    }
}

void right_side_panel_refresh(RightSidePanel* panel) {
    if (!panel) return;
    if (panel->adv_rail) gtk_widget_queue_draw(panel->adv_rail);
    if (panel->history_list) {
        refresh_history_icons_recursive(panel->history_list);
    }

    // If locked and we have a valid ply, re-apply active class
    if (panel->replay_lock && panel->locked_ply >= 0) {
        right_side_panel_highlight_ply(panel, panel->locked_ply);
    }
}
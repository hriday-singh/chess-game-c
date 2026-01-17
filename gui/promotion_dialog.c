#include "promotion_dialog.h"
#include "theme_data.h"
#include "../game/types.h"
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <cairo.h>
#include <math.h>
#include <stdlib.h>
#include <glib.h>
#include "gui_utils.h"

// Static variable to store selected piece and main loop (for modal dialog)
static PieceType g_selected_piece = NO_PROMOTION;
static GMainLoop* g_modal_loop = NULL;

// Callback for piece selection
static void on_piece_selected(GtkButton* button, gpointer user_data) {
    (void)user_data;
    g_selected_piece = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "piece-type"));
    GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    gtk_window_close(GTK_WINDOW(window));
    // Quit the modal loop
    if (g_modal_loop) {
        g_main_loop_quit(g_modal_loop);
    }
}

// Callback for window close
static gboolean on_window_close(GtkWindow* window, gpointer user_data) {
    (void)user_data;
    // Hide the window and quit the modal loop; we'll destroy it ourselves
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    if (g_modal_loop) {
        g_main_loop_quit(g_modal_loop);
    }
    return TRUE; // Stop further handling (prevent double-destroy)
}

// Helper to draw rounded rectangle path
static void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    // Limit radius to half of width/height
    if (r > w/2.0) r = w/2.0;
    if (r > h/2.0) r = h/2.0;
    
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

// Draw piece on button
static void draw_piece_button(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area; // Unused parameter
    PieceType* piece_data = (PieceType*)user_data;
    PieceType type = piece_data[0];
    Player owner = (Player)piece_data[1];
    ThemeData* theme = (ThemeData*)g_object_get_data(G_OBJECT(area), "theme");
    
    // Draw background with rounded corners
    // White pieces on Dark Squares, Black pieces on Light Squares for contrast
    if (owner == PLAYER_WHITE) {
        double r, g, b;
        if (theme) theme_data_get_dark_square_color(theme, &r, &g, &b);
        else { r = 0.70; g = 0.50; b = 0.35; } // Fallback
        cairo_set_source_rgb(cr, r, g, b);
    } else {
        double r, g, b;
        if (theme) theme_data_get_light_square_color(theme, &r, &g, &b);
        else { r = 0.96; g = 0.96; b = 0.96; } // Fallback
        cairo_set_source_rgb(cr, r, g, b);
    }
    
    // Use rounded rect (radius ~15% of width)
    draw_rounded_rect(cr, 0, 0, width, height, width * 0.15);
    cairo_fill(cr);
    
    // Optional: Subtle border (rounded)
    /*
    cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
    cairo_set_line_width(cr, 1.5);
    draw_rounded_rect(cr, 0.5, 0.5, width - 1, height - 1, width * 0.15);
    cairo_stroke(cr);
    */
    
    // Draw piece
    cairo_surface_t* surface = theme_data_get_piece_surface(theme, type, owner);
    
    if (surface) {
         // SVG rendering
         int surf_w = cairo_image_surface_get_width(surface);
         int surf_h = cairo_image_surface_get_height(surface);
         
         double scale = (double)height * 0.8 / surf_h;
         double draw_w = surf_w * scale;
         double draw_h = surf_h * scale;
         
         cairo_translate(cr, (width - draw_w)/2.0, (height - draw_h)/2.0);
         cairo_scale(cr, scale, scale);
         cairo_set_source_surface(cr, surface, 0, 0);
         cairo_paint(cr);
    } else {
        // Fallback to text
        const char* symbol = theme_data_get_piece_symbol(theme, type, owner);
        
        // Set up Cairo for crisp rendering
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);
        
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* desc = pango_font_description_new();
        pango_font_description_set_family(desc, "Segoe UI Symbol");
        pango_font_description_set_size(desc, (int)(width * 0.7 * PANGO_SCALE));
        pango_font_description_set_weight(desc, PANGO_WEIGHT_SEMIBOLD);
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_text(layout, symbol, -1);
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        
        int text_width, text_height;
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        double x = (width - text_width) / 2.0;
        double y = (height - text_height) / 2.0;
        
        cairo_move_to(cr, round(x), round(y));
        
        if (owner == PLAYER_WHITE) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            pango_cairo_layout_path(cr, layout);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 0.13, 0.13, 0.13);
            cairo_set_line_width(cr, 0.8);
            cairo_stroke(cr);
        } else {
            cairo_set_source_rgb(cr, 0.192, 0.180, 0.169);
            pango_cairo_layout_path(cr, layout);
            cairo_fill(cr);
        }
        
        pango_font_description_free(desc);
        g_object_unref(layout);
    }
}

PieceType promotion_dialog_show(GtkWindow* parent, ThemeData* theme, Player player) {
    // Reset selection
    g_selected_piece = NO_PROMOTION;
    
    // Create dialog window
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Choose Promotion");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(dialog), TRUE);
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(GTK_WINDOW(dialog));
    
    // Main container
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_halign(main_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    
    // Title label
    GtkWidget* title_label = gtk_label_new("Choose a piece to promote to:");
    PangoAttrList* title_attrs = pango_attr_list_new();
    PangoAttribute* title_size = pango_attr_size_new(14 * PANGO_SCALE);
    PangoAttribute* title_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(title_attrs, title_size);
    pango_attr_list_insert(title_attrs, title_weight);
    gtk_label_set_attributes(GTK_LABEL(title_label), title_attrs);
    pango_attr_list_unref(title_attrs);
    gtk_widget_set_halign(title_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(main_box), title_label);
    
    // Pieces grid (2x2)
    GtkWidget* pieces_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pieces_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(pieces_grid), 10);
    gtk_widget_set_halign(pieces_grid, GTK_ALIGN_CENTER);
    
    // Piece types to show: Queen, Rook, Bishop, Knight
    PieceType pieces[] = {PIECE_QUEEN, PIECE_ROOK, PIECE_BISHOP, PIECE_KNIGHT};
    const char* names[] = {"Queen", "Rook", "Bishop", "Knight"};
    
    for (int i = 0; i < 4; i++) {
        // Container for piece + label
        GtkWidget* piece_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_halign(piece_container, GTK_ALIGN_CENTER);
        
        // Drawing area for piece
        GtkWidget* piece_area = gtk_drawing_area_new();
        gtk_widget_set_size_request(piece_area, 80, 80);
        
        // Store piece data for drawing
        PieceType* piece_data = (PieceType*)malloc(2 * sizeof(PieceType));
        piece_data[0] = pieces[i];
        piece_data[1] = (PieceType)player;
        g_object_set_data_full(G_OBJECT(piece_area), "piece-data", piece_data, free);
        g_object_set_data(G_OBJECT(piece_area), "theme", theme);
        
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(piece_area),
                                       draw_piece_button,
                                       piece_data, NULL);
        
        // Make it clickable
        GtkWidget* piece_button = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(piece_button), piece_area);
        gtk_widget_set_can_focus(piece_button, TRUE);
        
        // Remove frame/background from button to avoid "box in a box"
        gtk_button_set_has_frame(GTK_BUTTON(piece_button), FALSE);
        gtk_widget_add_css_class(piece_button, "promotion-button");
        
        // Store piece type in button
        g_object_set_data(G_OBJECT(piece_button), "piece-type", 
                         GINT_TO_POINTER(pieces[i]));
        g_signal_connect(piece_button, "clicked", G_CALLBACK(on_piece_selected), NULL);
        
        gtk_box_append(GTK_BOX(piece_container), piece_button);
        
        // Label with piece name
        GtkWidget* name_label = gtk_label_new(names[i]);
        gtk_widget_set_halign(name_label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(piece_container), name_label);
        
        // Add to grid (2x2 layout)
        int row = i / 2;
        int col = i % 2;
        gtk_grid_attach(GTK_GRID(pieces_grid), piece_container, col, row, 1, 1);
    }
    
    gtk_box_append(GTK_BOX(main_box), pieces_grid);
    
    gtk_window_set_child(GTK_WINDOW(dialog), main_box);
    
    // Connect close handler to quit the loop
    g_signal_connect(dialog, "close-request", G_CALLBACK(on_window_close), NULL);
    
    // Reset selection
    g_selected_piece = NO_PROMOTION;
    
    // Create a main loop for modal behavior (use default context)
    GMainContext* context = g_main_context_default();
    g_modal_loop = g_main_loop_new(context, FALSE);
    
    // Show dialog and ensure it's visible and focused
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(GTK_WIDGET(dialog));
    
    // Process events until dialog is closed or piece is selected
    // Use blocking iteration to allow dialog to be interactive
    while (gtk_widget_get_visible(dialog) && g_selected_piece == NO_PROMOTION) {
        // Process events with blocking to allow dialog interaction
        g_main_context_iteration(context, TRUE);
    }
    
    // Clean up
    g_main_loop_unref(g_modal_loop);
    g_modal_loop = NULL;
    
    PieceType result = g_selected_piece;
    gtk_window_destroy(GTK_WINDOW(dialog));
    
    return result;
}

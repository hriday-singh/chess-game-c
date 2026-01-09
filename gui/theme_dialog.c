#include "theme_dialog.h"
#include "theme_data.h"
#include "../game/types.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

struct ThemeDialog {
    ThemeData* theme;
    ThemeUpdateCallback on_update;
    void* user_data;
    
    GtkWindow* window;
    GtkWidget* preview_grid;
    GtkWidget* board_pane;
    GtkWidget* piece_pane;
    
    // Board controls
    GtkWidget* light_color_button;
    GtkWidget* dark_color_button;
    GtkWidget* template_combo;
    
    // Piece controls
    GtkWidget* font_combo;
    GtkWidget* white_fill_button;
    GtkWidget* white_stroke_button;
    GtkWidget* white_width_scale;
    GtkWidget* black_fill_button;
    GtkWidget* black_stroke_button;
    GtkWidget* black_width_scale;
};

// Forward declarations
static void update_preview(ThemeDialog* dialog);
static void refresh_dialog(ThemeDialog* dialog);
static GtkWidget* create_board_controls(ThemeDialog* dialog);
static GtkWidget* create_piece_controls(ThemeDialog* dialog);
static GtkWidget* create_io_buttons(ThemeDialog* dialog);

// Preview drawing function
static void on_preview_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area; (void)width; (void)height;
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    if (!dialog || !dialog->theme) return;
    
    double squareSize = 80.0;
    PieceType types[] = {PIECE_ROOK, PIECE_KNIGHT, PIECE_BISHOP, PIECE_QUEEN, 
                        PIECE_KING, PIECE_BISHOP, PIECE_KNIGHT, PIECE_ROOK};
    
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 8; c++) {
            double x = c * squareSize;
            double y = r * squareSize;
            
            bool isLight = (r + c) % 2 == 0;
            double sqR, sqG, sqB;
            if (isLight) {
                theme_data_get_light_square_color(dialog->theme, &sqR, &sqG, &sqB);
            } else {
                theme_data_get_dark_square_color(dialog->theme, &sqR, &sqG, &sqB);
            }
            
            cairo_set_source_rgb(cr, sqR, sqG, sqB);
            cairo_rectangle(cr, x, y, squareSize, squareSize);
            cairo_fill(cr);
            
            // Draw piece
            Player owner = (r == 0) ? PLAYER_BLACK : PLAYER_WHITE;
            const char* symbol = theme_data_get_piece_symbol(dialog->theme, types[c], owner);
            
            PangoLayout* layout = pango_cairo_create_layout(cr);
            PangoFontDescription* desc = pango_font_description_new();
            pango_font_description_set_family(desc, theme_data_get_font_name(dialog->theme));
            pango_font_description_set_size(desc, squareSize * 0.75 * PANGO_SCALE);
            pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, symbol, -1);
            
            double pieceR, pieceG, pieceB;
            double strokeR, strokeG, strokeB;
            double strokeWidth;
            
            if (owner == PLAYER_WHITE) {
                theme_data_get_white_piece_color(dialog->theme, &pieceR, &pieceG, &pieceB);
                theme_data_get_white_piece_stroke(dialog->theme, &strokeR, &strokeG, &strokeB);
                strokeWidth = theme_data_get_white_stroke_width(dialog->theme);
            } else {
                theme_data_get_black_piece_color(dialog->theme, &pieceR, &pieceG, &pieceB);
                theme_data_get_black_piece_stroke(dialog->theme, &strokeR, &strokeG, &strokeB);
                strokeWidth = theme_data_get_black_stroke_width(dialog->theme);
            }
            
            PangoRectangle extents;
            pango_layout_get_pixel_extents(layout, NULL, &extents);
            double textX = x + (squareSize - extents.width) / 2.0;
            double textY = y + (squareSize - extents.height) / 2.0;
            
            // Draw stroke
            cairo_set_source_rgb(cr, strokeR, strokeG, strokeB);
            cairo_set_line_width(cr, strokeWidth);
            cairo_move_to(cr, textX, textY);
            pango_cairo_layout_path(cr, layout);
            cairo_stroke(cr);
            
            // Draw fill
            cairo_set_source_rgb(cr, pieceR, pieceG, pieceB);
            cairo_move_to(cr, textX, textY);
            pango_cairo_show_layout(cr, layout);
            
            pango_font_description_free(desc);
            g_object_unref(layout);
        }
    }
}

// Callbacks
static void on_light_color_changed(GtkColorDialogButton* button, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    const GdkRGBA* color = gtk_color_dialog_button_get_rgba(button);
    theme_data_set_light_square_color(dialog->theme, color->red, color->green, color->blue);
    refresh_dialog(dialog);
}

static void on_dark_color_changed(GtkColorDialogButton* button, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    const GdkRGBA* color = gtk_color_dialog_button_get_rgba(button);
    theme_data_set_dark_square_color(dialog->theme, color->red, color->green, color->blue);
    refresh_dialog(dialog);
}

static void on_template_changed(GtkDropDown* combo, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    guint selected = gtk_drop_down_get_selected(combo);
    const char* templates[] = {"Classic Wood", "Green & White", "Blue Ocean", "Dark Mode"};
    if (selected < 4) {
        theme_data_apply_board_template(dialog->theme, templates[selected]);
        // Update color buttons
        double r, g, b;
        theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
        GdkRGBA light = {r, g, b, 1.0};
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light);
        theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
        GdkRGBA dark = {r, g, b, 1.0};
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark);
        refresh_dialog(dialog);
    }
}

static void on_font_changed(GtkDropDown* combo, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    guint selected = gtk_drop_down_get_selected(combo);
    const char* font = theme_data_get_available_font(selected);
    if (font) {
        theme_data_set_font_name(dialog->theme, font);
        refresh_dialog(dialog);
    }
}

static void on_white_fill_changed(GtkColorDialogButton* button, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    const GdkRGBA* color = gtk_color_dialog_button_get_rgba(button);
    theme_data_set_white_piece_color(dialog->theme, color->red, color->green, color->blue);
    refresh_dialog(dialog);
}

static void on_white_stroke_changed(GtkColorDialogButton* button, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    const GdkRGBA* color = gtk_color_dialog_button_get_rgba(button);
    theme_data_set_white_piece_stroke(dialog->theme, color->red, color->green, color->blue);
    refresh_dialog(dialog);
}

static void on_white_width_changed(GtkRange* range, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    double value = gtk_range_get_value(range);
    theme_data_set_white_stroke_width(dialog->theme, value);
    refresh_dialog(dialog);
}

static void on_black_fill_changed(GtkColorDialogButton* button, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    const GdkRGBA* color = gtk_color_dialog_button_get_rgba(button);
    theme_data_set_black_piece_color(dialog->theme, color->red, color->green, color->blue);
    refresh_dialog(dialog);
}

static void on_black_stroke_changed(GtkColorDialogButton* button, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    const GdkRGBA* color = gtk_color_dialog_button_get_rgba(button);
    theme_data_set_black_piece_stroke(dialog->theme, color->red, color->green, color->blue);
    refresh_dialog(dialog);
}

static void on_black_width_changed(GtkRange* range, gpointer user_data) {
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    double value = gtk_range_get_value(range);
    theme_data_set_black_stroke_width(dialog->theme, value);
    refresh_dialog(dialog);
}

static void on_reset_board(GtkButton* button, gpointer user_data) {
    (void)button;
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    theme_data_reset_board_defaults(dialog->theme);
    // Update controls
    double r, g, b;
    theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
    GdkRGBA light = {r, g, b, 1.0};
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light);
    theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
    GdkRGBA dark = {r, g, b, 1.0};
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark);
    refresh_dialog(dialog);
}

static void on_reset_piece(GtkButton* button, gpointer user_data) {
    (void)button;
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    theme_data_reset_piece_defaults(dialog->theme);
    // Recreate piece controls to update font combo
    GtkWidget* old_pane = dialog->piece_pane;
    dialog->piece_pane = create_piece_controls(dialog);
    gtk_expander_set_child(GTK_EXPANDER(old_pane), dialog->piece_pane);
    gtk_widget_unparent(old_pane);
    refresh_dialog(dialog);
}

// File dialog callbacks
static void on_export_board_finish(GObject* source, GAsyncResult* result, gpointer data) {
    ThemeDialog* dialog = (ThemeDialog*)data;
    GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char* json = theme_data_to_board_json(dialog->theme);
        if (json) {
            char* path = g_file_get_path(file);
            FILE* f = fopen(path, "w");
            if (f) {
                fputs(json, f);
                fclose(f);
            }
            g_free(path);
            free(json);
        }
        g_object_unref(file);
    }
    g_object_unref(source);
}

static void on_export_board(GtkButton* button, gpointer user_data) {
    (void)button;
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_initial_name(file_dialog, "style.chessboard");
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Chess Board (*.chessboard)");
    gtk_file_filter_add_pattern(filter, "*.chessboard");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
    gtk_file_dialog_save(file_dialog, GTK_WINDOW(dialog->window), NULL, 
                         on_export_board_finish, dialog);
}

static void on_import_board_finish(GObject* source, GAsyncResult* result, gpointer data) {
    ThemeDialog* dialog = (ThemeDialog*)data;
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char* path = g_file_get_path(file);
        FILE* f = fopen(path, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* json = (char*)malloc(size + 1);
            if (json) {
                fread(json, 1, size, f);
                json[size] = '\0';
                theme_data_load_board_json(dialog->theme, json);
                // Update controls
                double r, g, b;
                theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
                GdkRGBA light = {r, g, b, 1.0};
                gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light);
                theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
                GdkRGBA dark = {r, g, b, 1.0};
                gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark);
                free(json);
                refresh_dialog(dialog);
            }
            fclose(f);
        }
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(source);
}

static void on_import_board(GtkButton* button, gpointer user_data) {
    (void)button;
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Chess Board (*.chessboard)");
    gtk_file_filter_add_pattern(filter, "*.chessboard");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
    gtk_file_dialog_open(file_dialog, GTK_WINDOW(dialog->window), NULL,
                         on_import_board_finish, dialog);
}

static void on_export_piece_finish(GObject* source, GAsyncResult* result, gpointer data) {
    ThemeDialog* dialog = (ThemeDialog*)data;
    GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char* json = theme_data_to_piece_json(dialog->theme);
        if (json) {
            char* path = g_file_get_path(file);
            FILE* f = fopen(path, "w");
            if (f) {
                fputs(json, f);
                fclose(f);
            }
            g_free(path);
            free(json);
        }
        g_object_unref(file);
    }
    g_object_unref(source);
}

static void on_export_piece(GtkButton* button, gpointer user_data) {
    (void)button;
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_initial_name(file_dialog, "style.chesspiece");
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Chess Piece (*.chesspiece)");
    gtk_file_filter_add_pattern(filter, "*.chesspiece");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
    gtk_file_dialog_save(file_dialog, GTK_WINDOW(dialog->window), NULL,
                         on_export_piece_finish, dialog);
}

static void on_import_piece_finish(GObject* source, GAsyncResult* result, gpointer data) {
    ThemeDialog* dialog = (ThemeDialog*)data;
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char* path = g_file_get_path(file);
        FILE* f = fopen(path, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* json = (char*)malloc(size + 1);
            if (json) {
                fread(json, 1, size, f);
                json[size] = '\0';
                theme_data_load_piece_json(dialog->theme, json);
                // Recreate piece controls
                GtkWidget* old_pane = dialog->piece_pane;
                dialog->piece_pane = create_piece_controls(dialog);
                gtk_expander_set_child(GTK_EXPANDER(old_pane), dialog->piece_pane);
                gtk_widget_unparent(old_pane);
                free(json);
                refresh_dialog(dialog);
            }
            fclose(f);
        }
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(source);
}

static void on_import_piece(GtkButton* button, gpointer user_data) {
    (void)button;
    ThemeDialog* dialog = (ThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Chess Piece (*.chesspiece)");
    gtk_file_filter_add_pattern(filter, "*.chesspiece");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
    gtk_file_dialog_open(file_dialog, GTK_WINDOW(dialog->window), NULL,
                         on_import_piece_finish, dialog);
}

static void refresh_dialog(ThemeDialog* dialog) {
    if (dialog) {
        update_preview(dialog);
        if (dialog->on_update) {
            dialog->on_update(dialog->user_data);
        }
    }
}

static void update_preview(ThemeDialog* dialog) {
    if (dialog && dialog->preview_grid) {
        gtk_widget_queue_draw(dialog->preview_grid);
    }
}

// Create board controls
static GtkWidget* create_board_controls(ThemeDialog* dialog) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    
    // Templates
    GtkWidget* template_label = gtk_label_new("Templates:");
    gtk_box_append(GTK_BOX(box), template_label);
    
    GtkStringList* template_list = gtk_string_list_new((const char*[]) {
        "Classic Wood", "Green & White", "Blue Ocean", "Dark Mode", NULL
    });
    dialog->template_combo = gtk_drop_down_new(G_LIST_MODEL(template_list), NULL);
    g_signal_connect(dialog->template_combo, "notify::selected", G_CALLBACK(on_template_changed), dialog);
    gtk_box_append(GTK_BOX(box), dialog->template_combo);
    
    // Separator
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Color pickers
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    
    GtkWidget* light_label = gtk_label_new("Light Square:");
    gtk_grid_attach(GTK_GRID(grid), light_label, 0, 0, 1, 1);
    
    double r, g, b;
    theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
    GdkRGBA light_color = {r, g, b, 1.0};
    dialog->light_color_button = gtk_color_dialog_button_new(GTK_COLOR_DIALOG(gtk_color_dialog_new()));
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light_color);
    g_signal_connect(dialog->light_color_button, "notify::rgba", G_CALLBACK(on_light_color_changed), dialog);
    gtk_grid_attach(GTK_GRID(grid), dialog->light_color_button, 1, 0, 1, 1);
    
    GtkWidget* dark_label = gtk_label_new("Dark Square:");
    gtk_grid_attach(GTK_GRID(grid), dark_label, 0, 1, 1, 1);
    
    theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
    GdkRGBA dark_color = {r, g, b, 1.0};
    dialog->dark_color_button = gtk_color_dialog_button_new(GTK_COLOR_DIALOG(gtk_color_dialog_new()));
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark_color);
    g_signal_connect(dialog->dark_color_button, "notify::rgba", G_CALLBACK(on_dark_color_changed), dialog);
    gtk_grid_attach(GTK_GRID(grid), dialog->dark_color_button, 1, 1, 1, 1);
    
    gtk_box_append(GTK_BOX(box), grid);
    
    // Separator
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Reset button
    GtkWidget* reset_btn = gtk_button_new_with_label("Reset Board Theme");
    gtk_widget_add_css_class(reset_btn, "destructive-action");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_board), dialog);
    gtk_box_append(GTK_BOX(box), reset_btn);
    
    return box;
}

// Create piece controls
static GtkWidget* create_piece_controls(ThemeDialog* dialog) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    
    // Font selector
    GtkWidget* font_label = gtk_label_new("Font Family:");
    gtk_box_append(GTK_BOX(box), font_label);
    
    // Build font list
    int fontCount = theme_data_get_available_font_count();
    const char** fontNames = (const char**)malloc(sizeof(char*) * (fontCount + 1));
    for (int i = 0; i < fontCount; i++) {
        fontNames[i] = theme_data_get_available_font(i);
    }
    fontNames[fontCount] = NULL;
    
    GtkStringList* font_list = gtk_string_list_new(fontNames);
    dialog->font_combo = gtk_drop_down_new(G_LIST_MODEL(font_list), NULL);
    
    // Select current font
    const char* currentFont = theme_data_get_font_name(dialog->theme);
    for (int i = 0; i < fontCount; i++) {
        if (fontNames[i] && strcmp(fontNames[i], currentFont) == 0) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(dialog->font_combo), i);
            break;
        }
    }
    
    g_signal_connect(dialog->font_combo, "notify::selected", G_CALLBACK(on_font_changed), dialog);
    gtk_box_append(GTK_BOX(box), dialog->font_combo);
    free(fontNames);
    
    // Separator
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Headers
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* fill_header = gtk_label_new("Fill");
    GtkWidget* stroke_header = gtk_label_new("Line");
    GtkWidget* width_header = gtk_label_new("Thickness");
    gtk_widget_set_size_request(fill_header, 50, -1);
    gtk_widget_set_size_request(stroke_header, 50, -1);
    gtk_widget_set_size_request(width_header, 120, -1);
    gtk_widget_set_halign(fill_header, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(stroke_header, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(width_header, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header_box), fill_header);
    gtk_box_append(GTK_BOX(header_box), stroke_header);
    gtk_box_append(GTK_BOX(header_box), width_header);
    
    // Grid for controls
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_attach(GTK_GRID(grid), header_box, 1, 0, 1, 1);
    
    // White pieces row
    GtkWidget* white_label = gtk_label_new("White Pieces:");
    gtk_grid_attach(GTK_GRID(grid), white_label, 0, 1, 1, 1);
    
    GtkWidget* white_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    double r, g, b;
    theme_data_get_white_piece_color(dialog->theme, &r, &g, &b);
    GdkRGBA white_fill = {r, g, b, 1.0};
    dialog->white_fill_button = gtk_color_dialog_button_new(GTK_COLOR_DIALOG(gtk_color_dialog_new()));
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->white_fill_button), &white_fill);
    gtk_widget_set_size_request(dialog->white_fill_button, 50, -1);
    g_signal_connect(dialog->white_fill_button, "notify::rgba", G_CALLBACK(on_white_fill_changed), dialog);
    
    theme_data_get_white_piece_stroke(dialog->theme, &r, &g, &b);
    GdkRGBA white_stroke = {r, g, b, 1.0};
    dialog->white_stroke_button = gtk_color_dialog_button_new(GTK_COLOR_DIALOG(gtk_color_dialog_new()));
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->white_stroke_button), &white_stroke);
    gtk_widget_set_size_request(dialog->white_stroke_button, 50, -1);
    g_signal_connect(dialog->white_stroke_button, "notify::rgba", G_CALLBACK(on_white_stroke_changed), dialog);
    
    dialog->white_width_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 5.0, 0.1);
    gtk_range_set_value(GTK_RANGE(dialog->white_width_scale), theme_data_get_white_stroke_width(dialog->theme));
    gtk_widget_set_size_request(dialog->white_width_scale, 120, -1);
    g_signal_connect(dialog->white_width_scale, "value-changed", G_CALLBACK(on_white_width_changed), dialog);
    
    gtk_box_append(GTK_BOX(white_row), dialog->white_fill_button);
    gtk_box_append(GTK_BOX(white_row), dialog->white_stroke_button);
    gtk_box_append(GTK_BOX(white_row), dialog->white_width_scale);
    gtk_grid_attach(GTK_GRID(grid), white_row, 1, 1, 1, 1);
    
    // Black pieces row
    GtkWidget* black_label = gtk_label_new("Black Pieces:");
    gtk_grid_attach(GTK_GRID(grid), black_label, 0, 2, 1, 1);
    
    GtkWidget* black_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    theme_data_get_black_piece_color(dialog->theme, &r, &g, &b);
    GdkRGBA black_fill = {r, g, b, 1.0};
    dialog->black_fill_button = gtk_color_dialog_button_new(GTK_COLOR_DIALOG(gtk_color_dialog_new()));
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->black_fill_button), &black_fill);
    gtk_widget_set_size_request(dialog->black_fill_button, 50, -1);
    g_signal_connect(dialog->black_fill_button, "notify::rgba", G_CALLBACK(on_black_fill_changed), dialog);
    
    theme_data_get_black_piece_stroke(dialog->theme, &r, &g, &b);
    GdkRGBA black_stroke = {r, g, b, 1.0};
    dialog->black_stroke_button = gtk_color_dialog_button_new(GTK_COLOR_DIALOG(gtk_color_dialog_new()));
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->black_stroke_button), &black_stroke);
    gtk_widget_set_size_request(dialog->black_stroke_button, 50, -1);
    g_signal_connect(dialog->black_stroke_button, "notify::rgba", G_CALLBACK(on_black_stroke_changed), dialog);
    
    dialog->black_width_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 5.0, 0.1);
    gtk_range_set_value(GTK_RANGE(dialog->black_width_scale), theme_data_get_black_stroke_width(dialog->theme));
    gtk_widget_set_size_request(dialog->black_width_scale, 120, -1);
    g_signal_connect(dialog->black_width_scale, "value-changed", G_CALLBACK(on_black_width_changed), dialog);
    
    gtk_box_append(GTK_BOX(black_row), dialog->black_fill_button);
    gtk_box_append(GTK_BOX(black_row), dialog->black_stroke_button);
    gtk_box_append(GTK_BOX(black_row), dialog->black_width_scale);
    gtk_grid_attach(GTK_GRID(grid), black_row, 1, 2, 1, 1);
    
    gtk_box_append(GTK_BOX(box), grid);
    
    // Separator
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Reset button
    GtkWidget* reset_btn = gtk_button_new_with_label("Reset Piece Style");
    gtk_widget_add_css_class(reset_btn, "destructive-action");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_piece), dialog);
    gtk_box_append(GTK_BOX(box), reset_btn);
    
    return box;
}

// Create import/export buttons
static GtkWidget* create_io_buttons(ThemeDialog* dialog) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    
    GtkWidget* exp_board = gtk_button_new_with_label("Export Board Theme");
    GtkWidget* imp_board = gtk_button_new_with_label("Import Board Theme");
    GtkWidget* exp_piece = gtk_button_new_with_label("Export Piece Theme");
    GtkWidget* imp_piece = gtk_button_new_with_label("Import Piece Theme");
    
    g_signal_connect(exp_board, "clicked", G_CALLBACK(on_export_board), dialog);
    g_signal_connect(imp_board, "clicked", G_CALLBACK(on_import_board), dialog);
    g_signal_connect(exp_piece, "clicked", G_CALLBACK(on_export_piece), dialog);
    g_signal_connect(imp_piece, "clicked", G_CALLBACK(on_import_piece), dialog);
    
    gtk_grid_attach(GTK_GRID(grid), exp_board, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), imp_board, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), exp_piece, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), imp_piece, 1, 1, 1, 1);
    
    gtk_box_append(GTK_BOX(box), grid);
    return box;
}

ThemeDialog* theme_dialog_new(ThemeData* theme, ThemeUpdateCallback on_update, void* user_data) {
    if (!theme) return NULL;
    
    ThemeDialog* dialog = (ThemeDialog*)calloc(1, sizeof(ThemeDialog));
    if (!dialog) return NULL;
    
    dialog->theme = theme;
    dialog->on_update = on_update;
    dialog->user_data = user_data;
    
    // Create window
    dialog->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(dialog->window, "Theme Editor");
    gtk_window_set_modal(dialog->window, TRUE);
    gtk_window_set_default_size(dialog->window, 1100, 700);
    
    // Main container
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    
    // Left: Controls
    GtkWidget* controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_size_request(controls_box, 380, -1);
    
    // Board pane
    dialog->board_pane = gtk_expander_new("Board Style");
    gtk_expander_set_expanded(GTK_EXPANDER(dialog->board_pane), TRUE);
    gtk_expander_set_resize_toplevel(GTK_EXPANDER(dialog->board_pane), FALSE);
    gtk_expander_set_child(GTK_EXPANDER(dialog->board_pane), create_board_controls(dialog));
    gtk_box_append(GTK_BOX(controls_box), dialog->board_pane);
    
    // Piece pane
    dialog->piece_pane = gtk_expander_new("Piece Style");
    gtk_expander_set_expanded(GTK_EXPANDER(dialog->piece_pane), TRUE);
    gtk_expander_set_resize_toplevel(GTK_EXPANDER(dialog->piece_pane), FALSE);
    gtk_expander_set_child(GTK_EXPANDER(dialog->piece_pane), create_piece_controls(dialog));
    gtk_box_append(GTK_BOX(controls_box), dialog->piece_pane);
    
    // IO pane
    GtkWidget* io_pane = gtk_expander_new("Import / Export");
    gtk_expander_set_expanded(GTK_EXPANDER(io_pane), TRUE);
    gtk_expander_set_resize_toplevel(GTK_EXPANDER(io_pane), FALSE);
    gtk_expander_set_child(GTK_EXPANDER(io_pane), create_io_buttons(dialog));
    gtk_box_append(GTK_BOX(controls_box), io_pane);
    
    gtk_box_append(GTK_BOX(main_box), controls_box);
    
    // Right: Preview
    GtkWidget* preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_halign(preview_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(preview_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(preview_box, 20);
    gtk_widget_set_margin_bottom(preview_box, 20);
    gtk_widget_set_margin_start(preview_box, 20);
    gtk_widget_set_margin_end(preview_box, 20);
    
    GtkWidget* preview_label = gtk_label_new("Live Preview");
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(attrs, weight);
    gtk_label_set_attributes(GTK_LABEL(preview_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_append(GTK_BOX(preview_box), preview_label);
    
    // Preview frame with border
    GtkWidget* preview_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(preview_frame, "preview-frame");
    
    // Preview drawing area (2 rows x 8 columns = 16 squares)
    dialog->preview_grid = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(dialog->preview_grid), 640);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(dialog->preview_grid), 160);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dialog->preview_grid), on_preview_draw, dialog, NULL);
    
    gtk_frame_set_child(GTK_FRAME(preview_frame), dialog->preview_grid);
    gtk_box_append(GTK_BOX(preview_box), preview_frame);
    
    gtk_box_append(GTK_BOX(main_box), preview_box);
    
    // CSS for preview frame
    // CSS is now handled globally by theme_manager.c
    
    gtk_window_set_child(dialog->window, main_box);
    
    // Initial preview update
    update_preview(dialog);
    
    return dialog;
}

void theme_dialog_show(ThemeDialog* dialog) {
    if (dialog && dialog->window) {
        gtk_window_present(dialog->window);
    }
}

void theme_dialog_free(ThemeDialog* dialog) {
    if (dialog) {
        if (dialog->window) {
            gtk_window_destroy(dialog->window);
        }
        free(dialog);
    }
}


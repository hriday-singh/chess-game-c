#include "board_theme_dialog.h"
#include "theme_data.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdbool.h>

static bool debug_mode = true;

struct BoardThemeDialog {
    ThemeData* theme;
    BoardThemeUpdateCallback on_update;
    void* user_data;
    GtkWindow* parent_window; // Store parent for recreation if needed
    
    GtkWindow* window; // Can be NULL if embedded
    GtkWidget* content_box; // Main container
    GtkWidget* preview_area;
    
    // Color dialogs (for configuration)
    GtkColorDialog* light_color_dialog;
    GtkColorDialog* dark_color_dialog;
    
    // Controls
    GtkWidget* light_color_button;
    GtkWidget* dark_color_button;
    GtkWidget* template_combo;
    GtkWidget* reset_button;
    GtkWidget* export_button;
    GtkWidget* import_button;
};

// Forward declarations
static void update_preview(BoardThemeDialog* dialog);
static void refresh_dialog(BoardThemeDialog* dialog);
static void update_template_selection(BoardThemeDialog* dialog);
static void on_template_changed(GObject* object, GParamSpec* pspec, gpointer user_data);
static gboolean on_window_close_request(GtkWindow* window, gpointer user_data);

// Helper to nullify pointer when widget is destroyed
static void on_widget_destroyed(GtkWidget* widget, gpointer* pointer) {
    (void)widget;
    if (pointer) *pointer = NULL;
}

// Preview drawing function - shows a chessboard preview
static void on_preview_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    if (!dialog || !dialog->theme) return;
    
    // Draw an 8x8 board preview
    double squareSize = fmin(width, height) / 8.0;
    double boardSize = squareSize * 8.0;
    double offsetX = (width - boardSize) / 2.0;
    double offsetY = (height - boardSize) / 2.0;
    
    double lightR, lightG, lightB;
    double darkR, darkG, darkB;
    theme_data_get_light_square_color(dialog->theme, &lightR, &lightG, &lightB);
    theme_data_get_dark_square_color(dialog->theme, &darkR, &darkG, &darkB);
    
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            double x = offsetX + c * squareSize;
            double y = offsetY + r * squareSize;
            
            bool isLight = (r + c) % 2 == 0;
            if (isLight) {
                cairo_set_source_rgb(cr, lightR, lightG, lightB);
            } else {
                cairo_set_source_rgb(cr, darkR, darkG, darkB);
            }
            
            cairo_rectangle(cr, x, y, squareSize, squareSize);
            cairo_fill(cr);
        }
    }
}

// Callbacks
static void on_light_color_changed(GObject* source_object, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    GtkColorDialogButton* button = GTK_COLOR_DIALOG_BUTTON(source_object);
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    if (!dialog || !dialog->theme) return;
    
    const GdkRGBA* c = gtk_color_dialog_button_get_rgba(button);
    if (!c) return;
    
    theme_data_set_light_square_color(dialog->theme, c->red, c->green, c->blue);
    update_template_selection(dialog);
    refresh_dialog(dialog);
}

static void on_dark_color_changed(GObject* source_object, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    GtkColorDialogButton* button = GTK_COLOR_DIALOG_BUTTON(source_object);
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    if (!dialog || !dialog->theme) return;
    
    const GdkRGBA* c = gtk_color_dialog_button_get_rgba(button);
    if (!c) return;
    
    theme_data_set_dark_square_color(dialog->theme, c->red, c->green, c->blue);
    update_template_selection(dialog);
    refresh_dialog(dialog);
}

// Helper function to check if colors match a template (with small tolerance for floating point)
static bool colors_match_template(double lightR, double lightG, double lightB,
                                   double darkR, double darkG, double darkB,
                                   const char* template_name) {
    const double tolerance = 0.01; // Allow small floating point differences
    
    double template_lightR, template_lightG, template_lightB;
    double template_darkR, template_darkG, template_darkB;
    
    // Get template colors
    if (strcmp(template_name, "Classic Wood") == 0) {
        template_lightR = 240.0 / 255.0;
        template_lightG = 217.0 / 255.0;
        template_lightB = 181.0 / 255.0;
        template_darkR = 181.0 / 255.0;
        template_darkG = 136.0 / 255.0;
        template_darkB = 99.0 / 255.0;
    } else if (strcmp(template_name, "Green & White") == 0) {
        template_lightR = 238.0 / 255.0;
        template_lightG = 238.0 / 255.0;
        template_lightB = 210.0 / 255.0;
        template_darkR = 118.0 / 255.0;
        template_darkG = 150.0 / 255.0;
        template_darkB = 86.0 / 255.0;
    } else if (strcmp(template_name, "Blue Ocean") == 0) {
        template_lightR = 200.0 / 255.0;
        template_lightG = 220.0 / 255.0;
        template_lightB = 240.0 / 255.0;
        template_darkR = 80.0 / 255.0;
        template_darkG = 130.0 / 255.0;
        template_darkB = 180.0 / 255.0;
    } else if (strcmp(template_name, "Dark Mode") == 0) {
        template_lightR = 150.0 / 255.0;
        template_lightG = 150.0 / 255.0;
        template_lightB = 150.0 / 255.0;
        template_darkR = 50.0 / 255.0;
        template_darkG = 50.0 / 255.0;
        template_darkB = 50.0 / 255.0;
    } else {
        return false;
    }
    
    // Check if colors match within tolerance
    return (fabs(lightR - template_lightR) < tolerance &&
            fabs(lightG - template_lightG) < tolerance &&
            fabs(lightB - template_lightB) < tolerance &&
            fabs(darkR - template_darkR) < tolerance &&
            fabs(darkG - template_darkG) < tolerance &&
            fabs(darkB - template_darkB) < tolerance);
}

// Update template dropdown to reflect current colors
static void update_template_selection(BoardThemeDialog* dialog) {
    if (!dialog || !dialog->theme || !dialog->template_combo) {
        return;
    }
    
    double lightR, lightG, lightB;
    double darkR, darkG, darkB;
    theme_data_get_light_square_color(dialog->theme, &lightR, &lightG, &lightB);
    theme_data_get_dark_square_color(dialog->theme, &darkR, &darkG, &darkB);
    
    const char* templates[] = {"Classic Wood", "Green & White", "Blue Ocean", "Dark Mode"};
    guint selected = 4; // Default to "Custom"
    
    // Check which template matches
    for (guint i = 0; i < 4; i++) {
        if (colors_match_template(lightR, lightG, lightB, darkR, darkG, darkB, templates[i])) {
            selected = i;
            break;
        }
    }
    
    // Update dropdown selection (block signal to prevent recursive callback)
    g_signal_handlers_block_by_func(dialog->template_combo, G_CALLBACK(on_template_changed), dialog);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dialog->template_combo), selected);
    g_signal_handlers_unblock_by_func(dialog->template_combo, G_CALLBACK(on_template_changed), dialog);
}

// CORRECTED: notify:: signals require (GObject*, GParamSpec*, gpointer) signature
static void on_template_changed(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)pspec; // Unused parameter
    
    // Cast object to combo
    GtkDropDown* combo = GTK_DROP_DOWN(object);
    
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    if (!dialog || !dialog->theme) {
        return;
    }
    
    guint selected = gtk_drop_down_get_selected(combo);
    const char* templates[] = {"Classic Wood", "Green & White", "Blue Ocean", "Dark Mode"};
    
    // If "Custom" (index 4) is selected, do nothing
    if (selected >= 4) {
        return;
    }
    
    // Apply the selected template
    theme_data_apply_board_template(dialog->theme, templates[selected]);
    
    // Update color buttons - check if they exist and are valid
    if (dialog->light_color_button && GTK_IS_WIDGET(dialog->light_color_button) &&
        dialog->dark_color_button && GTK_IS_WIDGET(dialog->dark_color_button)) {
        
        double r, g, b;
        theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
        GdkRGBA light = {r, g, b, 1.0};
        
        // Block signal to prevent recursive callback 
        // Note: For GtkColorDialogButton, we need to block correct signal or just set rgba directly?
        // Blocking notify::rgba is correct.
        g_signal_handlers_block_by_func(dialog->light_color_button, G_CALLBACK(on_light_color_changed), dialog);
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light);
        g_signal_handlers_unblock_by_func(dialog->light_color_button, G_CALLBACK(on_light_color_changed), dialog);
        
        theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
        GdkRGBA dark = {r, g, b, 1.0};
        
        g_signal_handlers_block_by_func(dialog->dark_color_button, G_CALLBACK(on_dark_color_changed), dialog);
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark);
        g_signal_handlers_unblock_by_func(dialog->dark_color_button, G_CALLBACK(on_dark_color_changed), dialog);
    }
    
    refresh_dialog(dialog);
}

static void on_reset_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    if (!dialog || !dialog->theme) return;
    
    theme_data_reset_board_defaults(dialog->theme);
    // Update controls - check if buttons exist and are valid
    if (dialog->light_color_button && GTK_IS_WIDGET(dialog->light_color_button) &&
        dialog->dark_color_button && GTK_IS_WIDGET(dialog->dark_color_button)) {
        double r, g, b;
        theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
        GdkRGBA light = {r, g, b, 1.0};
        
        g_signal_handlers_block_by_func(dialog->light_color_button, G_CALLBACK(on_light_color_changed), dialog);
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light);
        g_signal_handlers_unblock_by_func(dialog->light_color_button, G_CALLBACK(on_light_color_changed), dialog);
        
        theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
        GdkRGBA dark = {r, g, b, 1.0};
        
        g_signal_handlers_block_by_func(dialog->dark_color_button, G_CALLBACK(on_dark_color_changed), dialog);
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark);
        g_signal_handlers_unblock_by_func(dialog->dark_color_button, G_CALLBACK(on_dark_color_changed), dialog);
    }
    update_template_selection(dialog);
    refresh_dialog(dialog);
}

static void on_export_finish(GObject* source, GAsyncResult* result, gpointer data) {
    BoardThemeDialog* dialog = (BoardThemeDialog*)data;
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

static void on_export_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_initial_name(file_dialog, "board_theme.chessboard");
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Chess Board Theme (*.chessboard)");
    gtk_file_filter_add_pattern(filter, "*.chessboard");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
    GtkWindow* w = dialog->window ? dialog->window : dialog->parent_window;
    gtk_file_dialog_save(file_dialog, w, NULL, 
                         on_export_finish, dialog);
}

static void on_import_finish(GObject* source, GAsyncResult* result, gpointer data) {
    BoardThemeDialog* dialog = (BoardThemeDialog*)data;
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
                // Update controls - check if buttons exist and are valid
                if (dialog->light_color_button && GTK_IS_WIDGET(dialog->light_color_button) &&
                    dialog->dark_color_button && GTK_IS_WIDGET(dialog->dark_color_button)) {
                    double r, g, b;
                    theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
                    GdkRGBA light = {r, g, b, 1.0};
                    g_signal_handlers_block_by_func(dialog->light_color_button, G_CALLBACK(on_light_color_changed), dialog);
                    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light);
                    g_signal_handlers_unblock_by_func(dialog->light_color_button, G_CALLBACK(on_light_color_changed), dialog);
                    
                    theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
                    GdkRGBA dark = {r, g, b, 1.0};
                    g_signal_handlers_block_by_func(dialog->dark_color_button, G_CALLBACK(on_dark_color_changed), dialog);
                    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark);
                    g_signal_handlers_unblock_by_func(dialog->dark_color_button, G_CALLBACK(on_dark_color_changed), dialog);
                }
                free(json);
                update_template_selection(dialog);
                refresh_dialog(dialog);
            }
            fclose(f);
        }
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(source);
}

static void on_import_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Chess Board Theme (*.chessboard)");
    gtk_file_filter_add_pattern(filter, "*.chessboard");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
    GtkWindow* w = dialog->window ? dialog->window : dialog->parent_window;
    gtk_file_dialog_open(file_dialog, w, NULL,
                         on_import_finish, dialog);
}

static void refresh_dialog(BoardThemeDialog* dialog) {
    if (!dialog) {
        return;
    }
    
    // Check if widget is valid
    if (dialog->preview_area) {
        update_preview(dialog);
    }
    
    // Call update callback if it exists
    if (dialog->on_update && dialog->user_data && (uintptr_t)dialog->user_data > 0x1000) {
        dialog->on_update(dialog->user_data);
    }
}

static void update_preview(BoardThemeDialog* dialog) {
    if (dialog && dialog->preview_area && GTK_IS_WIDGET(dialog->preview_area)) {
        gtk_widget_queue_draw(dialog->preview_area);
    }
}

static void board_theme_dialog_build_ui(BoardThemeDialog* dialog) {
    // Main container - vertical layout
    dialog->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    // Connect destroy signal to nullify the pointer in the struct
    g_signal_connect(dialog->content_box, "destroy", G_CALLBACK(on_widget_destroyed), &dialog->content_box);
    
    gtk_widget_set_margin_top(dialog->content_box, 24);
    gtk_widget_set_margin_bottom(dialog->content_box, 24);
    gtk_widget_set_margin_start(dialog->content_box, 24);
    gtk_widget_set_margin_end(dialog->content_box, 24);
    
    // Title
    GtkWidget* title_label = gtk_label_new("Customize Board Colors");
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttribute* size = pango_attr_size_new(24 * PANGO_SCALE);
    pango_attr_list_insert(attrs, weight);
    pango_attr_list_insert(attrs, size);
    gtk_label_set_attributes(GTK_LABEL(title_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dialog->content_box), title_label);
    
    // Content area - horizontal split
    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
    
    // Left: Controls
    GtkWidget* controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_size_request(controls_box, 300, -1);
    
    // Templates section
    GtkWidget* template_label = gtk_label_new("Quick Templates");
    gtk_widget_set_halign(template_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(template_label, "heading");
    gtk_box_append(GTK_BOX(controls_box), template_label);
    
    GtkStringList* template_list = gtk_string_list_new((const char*[]) {
        "Classic Wood", "Green & White", "Blue Ocean", "Dark Mode", "Custom", NULL
    });
    dialog->template_combo = gtk_drop_down_new(G_LIST_MODEL(template_list), NULL);
    g_signal_connect(dialog->template_combo, "notify::selected", G_CALLBACK(on_template_changed), dialog);
    gtk_box_append(GTK_BOX(controls_box), dialog->template_combo);
    
    // Separator
    gtk_box_append(GTK_BOX(controls_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Custom colors section
    GtkWidget* colors_label = gtk_label_new("Custom Colors");
    gtk_widget_set_halign(colors_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(colors_label, "heading");
    gtk_box_append(GTK_BOX(controls_box), colors_label);
    
    // Light square
    GtkWidget* light_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* light_label = gtk_label_new("Light Square");
    gtk_widget_set_halign(light_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(light_label, TRUE);
    
    double r, g, b;
    theme_data_get_light_square_color(dialog->theme, &r, &g, &b);
    GdkRGBA light_color = {r, g, b, 1.0};
    
    // Create and configure color dialog
    dialog->light_color_dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog->light_color_dialog, FALSE);
    
    dialog->light_color_button = gtk_color_dialog_button_new(dialog->light_color_dialog);
    if (dialog->light_color_button) {
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->light_color_button), &light_color);
        g_signal_connect(dialog->light_color_button, "notify::rgba", G_CALLBACK(on_light_color_changed), dialog);
        gtk_widget_set_margin_start(dialog->light_color_button, 8);
        gtk_widget_set_margin_end(dialog->light_color_button, 8);
        gtk_widget_set_margin_top(dialog->light_color_button, 4);
        gtk_widget_set_margin_bottom(dialog->light_color_button, 4);
    }
    
    gtk_box_append(GTK_BOX(light_box), light_label);
    gtk_box_append(GTK_BOX(light_box), dialog->light_color_button);
    gtk_box_append(GTK_BOX(controls_box), light_box);
    
    // Dark square
    GtkWidget* dark_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* dark_label = gtk_label_new("Dark Square");
    gtk_widget_set_halign(dark_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(dark_label, TRUE);
    
    theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
    GdkRGBA dark_color = {r, g, b, 1.0};
    
    dialog->dark_color_dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog->dark_color_dialog, FALSE);
    
    dialog->dark_color_button = gtk_color_dialog_button_new(dialog->dark_color_dialog);
    if (dialog->dark_color_button) {
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->dark_color_button), &dark_color);
        g_signal_connect(dialog->dark_color_button, "notify::rgba", G_CALLBACK(on_dark_color_changed), dialog);
        gtk_widget_set_margin_start(dialog->dark_color_button, 8);
        gtk_widget_set_margin_end(dialog->dark_color_button, 8);
        gtk_widget_set_margin_top(dialog->dark_color_button, 4);
        gtk_widget_set_margin_bottom(dialog->dark_color_button, 4);
    }
    
    gtk_box_append(GTK_BOX(dark_box), dark_label);
    gtk_box_append(GTK_BOX(dark_box), dialog->dark_color_button);
    gtk_box_append(GTK_BOX(controls_box), dark_box);
    
    // Separator
    gtk_box_append(GTK_BOX(controls_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Action buttons
    GtkWidget* actions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    
    dialog->reset_button = gtk_button_new_with_label("Reset to Default");
    gtk_widget_add_css_class(dialog->reset_button, "destructive-action");
    g_signal_connect(dialog->reset_button, "clicked", G_CALLBACK(on_reset_clicked), dialog);
    gtk_box_append(GTK_BOX(actions_box), dialog->reset_button);
    
    GtkWidget* io_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    dialog->export_button = gtk_button_new_with_label("Export");
    dialog->import_button = gtk_button_new_with_label("Import");
    g_signal_connect(dialog->export_button, "clicked", G_CALLBACK(on_export_clicked), dialog);
    g_signal_connect(dialog->import_button, "clicked", G_CALLBACK(on_import_clicked), dialog);
    gtk_box_append(GTK_BOX(io_box), dialog->export_button);
    gtk_box_append(GTK_BOX(io_box), dialog->import_button);
    gtk_box_append(GTK_BOX(actions_box), io_box);
    
    gtk_box_append(GTK_BOX(controls_box), actions_box);
    
    gtk_box_append(GTK_BOX(content_box), controls_box);
    
    // Right: Preview
    GtkWidget* preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(preview_box, TRUE);
    gtk_widget_set_halign(preview_box, GTK_ALIGN_CENTER);
    
    GtkWidget* preview_label = gtk_label_new("Preview");
    gtk_widget_set_halign(preview_label, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(preview_label, "heading");
    gtk_box_append(GTK_BOX(preview_box), preview_label);
    
    // Preview frame
    GtkWidget* preview_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(preview_frame, "preview-frame");
    
    // Preview drawing area
    dialog->preview_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(dialog->preview_area), 380);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(dialog->preview_area), 380);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dialog->preview_area), on_preview_draw, dialog, NULL);
    
    gtk_frame_set_child(GTK_FRAME(preview_frame), dialog->preview_area);
    gtk_box_append(GTK_BOX(preview_box), preview_frame);
    
    gtk_box_append(GTK_BOX(content_box), preview_box);
    
    gtk_box_append(GTK_BOX(dialog->content_box), content_box);
    
    // Initial preview update
    update_preview(dialog);
    
    // Set initial template selection based on current colors
    update_template_selection(dialog);
}

BoardThemeDialog* board_theme_dialog_new_embedded(ThemeData* theme, BoardThemeUpdateCallback on_update, void* user_data) {
    if(debug_mode) printf("[BoardTheme] Creating embedded dialog. Theme=%p\n", (void*)theme);
    if (!theme) return NULL;
    
    BoardThemeDialog* dialog = (BoardThemeDialog*)calloc(1, sizeof(BoardThemeDialog));
    if (!dialog) return NULL;
    
    dialog->theme = theme;
    dialog->on_update = on_update;
    dialog->user_data = user_data;
    
    board_theme_dialog_build_ui(dialog);
    
    return dialog;
}

BoardThemeDialog* board_theme_dialog_new(ThemeData* theme, BoardThemeUpdateCallback on_update, void* user_data, GtkWindow* parent_window) {
    BoardThemeDialog* dialog = board_theme_dialog_new_embedded(theme, on_update, user_data);
    if (!dialog) return NULL;
    
    dialog->parent_window = parent_window; 
    
    // Create window
    GtkWidget* window_widget = gtk_window_new();
    dialog->window = GTK_WINDOW(window_widget);
    if (dialog->window) {
        gtk_window_set_title(dialog->window, "Board Theme");
        gtk_window_set_modal(dialog->window, TRUE);
        gtk_window_set_default_size(dialog->window, 700, 550);
        gtk_window_set_resizable(dialog->window, TRUE);
        
        if (parent_window) {
            gtk_window_set_transient_for(dialog->window, parent_window);
        }
        
        g_signal_connect(dialog->window, "close-request", G_CALLBACK(on_window_close_request), dialog);
        gtk_window_set_child(dialog->window, dialog->content_box);
    }
    
    // CSS styling
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = 
        ".heading { font-weight: 600; font-size: 14px; color: #2c3e50; } "
        ".preview-frame { border: 2px solid #e0e0e0; border-radius: 8px; background: white; box-shadow: 0 2px 8px rgba(0,0,0,0.1); } "
        "button { border-radius: 6px; } "
        "button:hover { background: #f0f0f0; } "
        "window.dialog { padding: 12px; } "
        "window.dialog button { margin: 4px; padding: 8px 20px; min-width: 80px; } "
        "window.dialog box.horizontal button { margin: 6px; } "
        "window.dialog button:hover { background: inherit; color: inherit; } "
        "window.dialog button.suggested-action:hover { background: inherit; }";
    gtk_css_provider_load_from_string(provider, css);
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        gtk_style_context_add_provider_for_display(
            display,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
    
    return dialog;
}

// Handle window close request - hide instead of destroy
static gboolean on_window_close_request(GtkWindow* window, gpointer user_data) {
    (void)window; // Unused
    BoardThemeDialog* dialog = (BoardThemeDialog*)user_data;
    if (dialog && dialog->window) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), FALSE);
        if (dialog->parent_window) {
            gtk_window_present(dialog->parent_window);
        }
    }
    return TRUE; 
}

void board_theme_dialog_show(BoardThemeDialog* dialog) {
    if (!dialog) return;
    
    if (dialog->window && (uintptr_t)dialog->window > 0x1000 && GTK_IS_WINDOW(dialog->window)) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), TRUE);
        gtk_window_present(dialog->window);
    }
}

void board_theme_dialog_free(BoardThemeDialog* dialog) {
    if (debug_mode) printf("[BoardTheme] Freeing dialog %p\n", (void*)dialog);
    if (!dialog) return;

    if (dialog->content_box && GTK_IS_WIDGET(dialog->content_box)) {
        if (debug_mode) printf("[BoardTheme] Disconnecting destroy handler %p\n", (void*)dialog->content_box);
        g_signal_handlers_disconnect_by_func(dialog->content_box, G_CALLBACK(on_widget_destroyed), &dialog->content_box);
    }

    if (dialog->window) {
        if (debug_mode) printf("[BoardTheme] Destroying window\n");
        gtk_window_destroy(dialog->window);
    }
    
    if (dialog->light_color_dialog && G_IS_OBJECT(dialog->light_color_dialog)) {
        g_object_unref(dialog->light_color_dialog);
    }
    if (dialog->dark_color_dialog && G_IS_OBJECT(dialog->dark_color_dialog)) {
        g_object_unref(dialog->dark_color_dialog);
    }
    free(dialog);
    if (debug_mode) printf("[BoardTheme] Dialog freed\n");
}

GtkWidget* board_theme_dialog_get_widget(BoardThemeDialog* dialog) {
    return dialog ? dialog->content_box : NULL;
}

void board_theme_dialog_set_parent_window(BoardThemeDialog* dialog, GtkWindow* parent) {
    if (dialog) {
        dialog->parent_window = parent;
        if (dialog->window) {
            gtk_window_set_transient_for(dialog->window, parent);
        }
    }
}

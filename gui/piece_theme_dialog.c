#include "piece_theme_dialog.h"
#include "theme_data.h"
#include "../game/types.h"
#include "../game/piece.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cairo.h>

#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#define MAX_PIECE_SETS 100
#define MAX_STROKE_WIDTH 4.0

static bool debug_mode = false;

// Helper to print RAM usage
static void print_memory_usage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        // printf("[DEBUG] RAM Usage: %.2f MB\n", pmc.WorkingSetSize / (1024.0 * 1024.0));
    }
#endif
}

#define DEFAULT_WHITE_STROKE_WIDTH 0.5
#define DEFAULT_BLACK_STROKE_WIDTH 0.1

// Piece set information
typedef struct {
    char* name;  // Folder name (e.g., "alfonso", "alpha")
    char* display_name;  // Display name (capitalized, e.g., "Alfonso")
} PieceSetInfo;

struct PieceThemeDialog {
    ThemeData* theme;
    PieceThemeUpdateCallback on_update;
    void* user_data;
    GtkWindow* parent_window;
    
    GtkWindow* window;      // NULL if embedded
    GtkWidget* content_box; // Main widget (embedded root)
    GtkWidget* preview_area;
    
    // Piece set management
    PieceSetInfo* piece_sets;
    int piece_set_count;
    int selected_piece_set_index;
    
    // Controls
    GtkWidget* piece_set_combo;
    
    // Color Dialogs
    GtkColorDialog* white_piece_dialog;
    GtkColorDialog* white_stroke_dialog;
    GtkColorDialog* black_piece_dialog;
    GtkColorDialog* black_stroke_dialog;
    
    // Buttons
    GtkWidget* white_piece_color_button;
    GtkWidget* white_stroke_color_button;
    GtkWidget* white_stroke_width_spin;
    GtkWidget* black_piece_color_button;
    GtkWidget* black_stroke_color_button;
    GtkWidget* black_stroke_width_spin;
    GtkWidget* reset_piece_type_button;
    GtkWidget* reset_colors_button;
    GtkWidget* export_button;
    GtkWidget* import_button;
    
    // SVG Cache (Cairo Surfaces)
    cairo_surface_t* piece_cache[2][6];
    char* cached_font_name;
};

// Forward Declarations
static void refresh_dialog(PieceThemeDialog* dialog);
static void update_preview(PieceThemeDialog* dialog);
static void check_update_preview_cache(PieceThemeDialog* dialog);
static gboolean on_window_close_request(GtkWindow* window, gpointer user_data);

// Helper to nullify pointer when widget is destroyed
static void on_widget_destroyed(GtkWidget* widget, gpointer* pointer) {
    (void)widget;
    if (pointer) *pointer = NULL;
}

// Helpers
static char* capitalize_string(const char* str) {
    if (!str || !str[0]) return NULL;
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    strcpy(result, str);
    if (result[0] >= 'a' && result[0] <= 'z') result[0] -= 32;
    return result;
}

static cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf* pixbuf) {
    if (!pixbuf) return NULL;
    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    cairo_format_t format = gdk_pixbuf_get_has_alpha(pixbuf) ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    int stride = cairo_format_stride_for_width(format, width);
    guchar* cairo_data = g_malloc(stride * height);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    
    for (int y = 0; y < height; y++) {
         const guchar* src_row = pixels + y * rowstride;
         guint32* dst_row = (guint32*)(cairo_data + y * stride);
         for (int x = 0; x < width; x++) {
             guchar r = src_row[x * n_channels + 0];
             guchar g = src_row[x * n_channels + 1];
             guchar b = src_row[x * n_channels + 2];
             guchar a = has_alpha ? src_row[x * n_channels + 3] : 255;
             double alpha = a / 255.0;
             dst_row[x] = (a << 24) | ((int)(r * alpha) << 16) | ((int)(g * alpha) << 8) | (int)(b * alpha);
         }
    }
    cairo_surface_t* surface = cairo_image_surface_create_for_data(cairo_data, format, width, height, stride);
    static cairo_user_data_key_t key;
    cairo_surface_set_user_data(surface, &key, cairo_data, (cairo_destroy_func_t)g_free);
    return surface;
}

static void scan_piece_sets(PieceThemeDialog* dialog) {
    if (!dialog) return;
    const char* piece_dir = "assets/images/piece";
    DIR* dir = opendir(piece_dir);
    if (!dir) {
        piece_dir = "build/assets/images/piece";
        dir = opendir(piece_dir);
        if (!dir) return;
    }
    
    dialog->piece_sets = (PieceSetInfo*)calloc(MAX_PIECE_SETS, sizeof(PieceSetInfo));
    if (!dialog->piece_sets) return;
    dialog->piece_set_count = 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && dialog->piece_set_count < MAX_PIECE_SETS) {
        if (entry->d_name[0] == '.') continue;
        char* full_path = g_build_filename(piece_dir, entry->d_name, NULL);
        if (!full_path) continue;
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            char* knight_path = g_build_filename(full_path, "wN.svg", NULL);
            if (knight_path) {
                if (access(knight_path, F_OK) == 0) {
                    dialog->piece_sets[dialog->piece_set_count].name = strdup(entry->d_name);
                    dialog->piece_sets[dialog->piece_set_count].display_name = capitalize_string(entry->d_name);
                    dialog->piece_set_count++;
                }
                g_free(knight_path);
            }
        }
        g_free(full_path);
    }
    closedir(dir);
    
    // Sort
    for (int i = 0; i < dialog->piece_set_count - 1; i++) {
        for (int j = i + 1; j < dialog->piece_set_count; j++) {
            if (strcmp(dialog->piece_sets[i].display_name, dialog->piece_sets[j].display_name) > 0) {
                PieceSetInfo temp = dialog->piece_sets[i];
                dialog->piece_sets[i] = dialog->piece_sets[j];
                dialog->piece_sets[j] = temp;
            }
        }
    }
    
    // Add Default
    if (dialog->piece_set_count < MAX_PIECE_SETS) {
        for (int i = dialog->piece_set_count; i > 0; i--) {
            dialog->piece_sets[i] = dialog->piece_sets[i-1];
        }
        dialog->piece_sets[0].name = strdup("Default");
        dialog->piece_sets[0].display_name = strdup("Default (Segoe UI)");
        dialog->piece_set_count++;
    }
}

static void free_piece_sets(PieceThemeDialog* dialog) {
    if (!dialog || !dialog->piece_sets) return;
    for (int i = 0; i < dialog->piece_set_count; i++) {
        free(dialog->piece_sets[i].name);
        free(dialog->piece_sets[i].display_name);
    }
    free(dialog->piece_sets);
    dialog->piece_sets = NULL;
    dialog->piece_set_count = 0;
}

static int find_piece_set_index(PieceThemeDialog* dialog, const char* name) {
    if (!dialog || !name) return 0;
    if (theme_data_is_standard_font(name) || strcmp(name, "Default (Segoe UI)") == 0) return 0;
    for (int i = 0; i < dialog->piece_set_count; i++) {
        if (dialog->piece_sets[i].name && strcmp(dialog->piece_sets[i].name, name) == 0) return i;
    }
    return 0; 
}

// Drawing for List Items
static void on_dropdown_item_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)user_data;
    PieceThemeDialog* dialog = (PieceThemeDialog*)g_object_get_data(G_OBJECT(area), "dialog");
    const char* folder_name = (const char*)g_object_get_data(G_OBJECT(area), "folder_name");
    if (!dialog || !dialog->theme) return;

    double r, g, b;
    theme_data_get_dark_square_color(dialog->theme, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    double radius = 6.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI/2, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, radius, height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_fill(cr);

    if (folder_name) {
        if (strcmp(folder_name, "Default") == 0 || strcmp(folder_name, "Default (Segoe UI)") == 0) {
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_select_font_face(cr, "Segoe UI Symbol", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, height * 0.9);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, "\u2658", &extents);
            cairo_move_to(cr, (width - extents.width)/2.0 - extents.x_bearing, (height - extents.height)/2.0 - extents.y_bearing);
            cairo_show_text(cr, "\u2658");
        } else {
            char* path = g_strdup_printf("assets/images/piece/%s/wN.svg", folder_name);
            if (access(path, F_OK) != 0) {
                // Try build path
                g_free(path);
                path = g_strdup_printf("build/assets/images/piece/%s/wN.svg", folder_name);
            }
            GError* err = NULL;
            int pad = 6;
            GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path, width - pad, height - pad, TRUE, &err);
            if (pixbuf) {
                cairo_surface_t* s = pixbuf_to_cairo_surface(pixbuf);
                int sw = cairo_image_surface_get_width(s);
                int sh = cairo_image_surface_get_height(s);
                cairo_set_source_surface(cr, s, (width - sw)/2.0, (height - sh)/2.0);
                cairo_paint(cr);
                cairo_surface_destroy(s);
                g_object_unref(pixbuf);
            }
            if (err) g_error_free(err);
            g_free(path);
        }
    }
}

static GtkWidget* find_first_widget_of_type(GtkWidget* root, GType type) {
    if (!root) return NULL;
    if (G_TYPE_CHECK_INSTANCE_TYPE(root, type)) return root;
    for (GtkWidget* child = gtk_widget_get_first_child(root); child != NULL; child = gtk_widget_get_next_sibling(child)) {
        GtkWidget* found = find_first_widget_of_type(child, type);
        if (found) return found;
    }
    return NULL;
}

static void on_piece_dropdown_popover_visible(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    GtkPopover* popover = GTK_POPOVER(obj);
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    if (!dialog || !gtk_widget_get_visible(GTK_WIDGET(popover))) return;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dialog->piece_set_combo));
    GtkWidget* pop_child = gtk_popover_get_child(popover);
    GtkWidget* list_view = find_first_widget_of_type(pop_child, GTK_TYPE_LIST_VIEW);
    if (!list_view) return;
    gtk_list_view_scroll_to(GTK_LIST_VIEW(list_view), selected, GTK_LIST_SCROLL_FOCUS, NULL);
}

static void update_row_selected_state(PieceThemeDialog* dialog, GtkListItem* list_item) {
    GtkWidget* box = gtk_list_item_get_child(list_item);
    if (!box) return;
    GtkWidget* icon_area = gtk_widget_get_first_child(box);
    GtkWidget* label     = gtk_widget_get_next_sibling(icon_area);
    GtkWidget* check     = gtk_widget_get_last_child(box);
    guint position = gtk_list_item_get_position(list_item);
    guint selected_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dialog->piece_set_combo));
    gboolean is_selected = (position == selected_idx);
    gtk_widget_set_opacity(check, is_selected ? 1.0 : 0.0);
    if (is_selected) {
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(label), attrs);
        pango_attr_list_unref(attrs);
    } else {
        gtk_label_set_attributes(GTK_LABEL(label), NULL);
    }
}

static void on_dropdown_selected_changed_for_row(GObject* obj, GParamSpec* pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    GtkListItem* list_item = GTK_LIST_ITEM(user_data);
    PieceThemeDialog* dialog = (PieceThemeDialog*)g_object_get_data(G_OBJECT(list_item), "dialog");
    if (dialog) update_row_selected_state(dialog, list_item);
}

static void setup_list_item(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data) {
    (void)factory; (void)user_data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* icon_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(icon_area, 32, 32);
    gtk_widget_add_css_class(icon_area, "piece-icon-bg");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon_area), on_dropdown_item_draw, NULL, NULL);
    GtkWidget* label = gtk_label_new(NULL);
    GtkWidget* check = gtk_image_new_from_icon_name("object-select-symbolic");
    gtk_widget_set_opacity(check, 0.0); 
    gtk_box_append(GTK_BOX(box), icon_area);
    gtk_box_append(GTK_BOX(box), label);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), check);
    gtk_list_item_set_child(list_item, box);
}

static void bind_list_item(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data) {
    (void)factory; 
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    GtkWidget* box = gtk_list_item_get_child(list_item);
    GtkWidget* icon_area = gtk_widget_get_first_child(box);
    GtkWidget* label = gtk_widget_get_next_sibling(icon_area);
    GtkStringObject* strobj = GTK_STRING_OBJECT(gtk_list_item_get_item(list_item));
    const char* display_name = gtk_string_object_get_string(strobj);
    gtk_label_set_text(GTK_LABEL(label), display_name);
    
    const char* folder_name = NULL;
    for (int i = 0; i < dialog->piece_set_count; i++) {
        if (strcmp(dialog->piece_sets[i].display_name, display_name) == 0) {
            folder_name = dialog->piece_sets[i].name;
            break;
        }
    }
    g_object_set_data(G_OBJECT(icon_area), "dialog", dialog);
    g_object_set_data(G_OBJECT(icon_area), "folder_name", (gpointer)folder_name);
    
    g_object_set_data(G_OBJECT(list_item), "dialog", dialog);
    gulong old_id = (gulong)(uintptr_t)g_object_get_data(G_OBJECT(list_item), "sel_notify_id");
    if (old_id) g_signal_handler_disconnect(dialog->piece_set_combo, old_id);
    gulong id = g_signal_connect(dialog->piece_set_combo, "notify::selected", G_CALLBACK(on_dropdown_selected_changed_for_row), list_item);
    g_object_set_data(G_OBJECT(list_item), "sel_notify_id", (gpointer)(uintptr_t)id);
    update_row_selected_state(dialog, list_item);
    gtk_widget_queue_draw(icon_area);
}

static void setup_button_item(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data) {
    (void)factory; (void)user_data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* icon_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(icon_area, 32, 32);
    gtk_widget_add_css_class(icon_area, "piece-icon-bg");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon_area), on_dropdown_item_draw, NULL, NULL);
    GtkWidget* label = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(box), icon_area);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(list_item, box);
}

static void bind_button_item(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data) {
    (void)factory; 
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    GtkWidget* box = gtk_list_item_get_child(list_item);
    GtkWidget* icon_area = gtk_widget_get_first_child(box);
    GtkWidget* label = gtk_widget_get_next_sibling(icon_area);
    GtkStringObject* strobj = GTK_STRING_OBJECT(gtk_list_item_get_item(list_item));
    const char* display_name = gtk_string_object_get_string(strobj);
    gtk_label_set_text(GTK_LABEL(label), display_name);
    
    const char* folder_name = NULL;
    for (int i = 0; i < dialog->piece_set_count; i++) {
        if (strcmp(dialog->piece_sets[i].display_name, display_name) == 0) {
            folder_name = dialog->piece_sets[i].name;
            break;
        }
    }
    g_object_set_data(G_OBJECT(icon_area), "dialog", dialog);
    g_object_set_data(G_OBJECT(icon_area), "folder_name", (gpointer)folder_name);
    gtk_widget_queue_draw(icon_area);
}

// Drawing Preview
static void clear_preview_cache(PieceThemeDialog* dialog) {
    if (!dialog) return;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 6; j++) {
            if (dialog->piece_cache[i][j]) {
                cairo_surface_destroy(dialog->piece_cache[i][j]);
                dialog->piece_cache[i][j] = NULL;
            }
        }
    }
    if (dialog->cached_font_name) {
        free(dialog->cached_font_name);
        dialog->cached_font_name = NULL;
    }
}

static void check_update_preview_cache(PieceThemeDialog* dialog) {
    if (!dialog || !dialog->theme) return;
    const char* currentFont = theme_data_get_font_name(dialog->theme);
    if (!currentFont) return;
    
    if (dialog->cached_font_name && strcmp(dialog->cached_font_name, currentFont) == 0) {
        if (dialog->piece_cache[0][0] != NULL) return;
    }
    
    clear_preview_cache(dialog);
    dialog->cached_font_name = strdup(currentFont);
    
    for (int owner = 0; owner < 2; owner++) {
        for (int type = 0; type < 6; type++) {
            char* path = theme_data_get_piece_image_path(dialog->theme, (PieceType)type, (Player)owner);
            if (path) {
                GError* error = NULL;
                GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path, -1, 256, TRUE, &error);
                if (pixbuf) {
                    dialog->piece_cache[owner][type] = pixbuf_to_cairo_surface(pixbuf);
                    g_object_unref(pixbuf);
                }
                if (error) g_error_free(error);
                free(path);
            }
        }
    }
}

static void on_preview_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area; (void)height;
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    if (!dialog || !dialog->theme) return;
    
    int cols = 6; int rows = 2;
    double padding = 0.0;
    double headerHeight = 0.0; 
    double availableHeight = height - 2 * padding - headerHeight;
    double sqW = (width - 2 * padding) / cols;
    double sqH = availableHeight / rows;
    double squareSize = (sqW < sqH) ? sqW : sqH;
    double gridWidth = squareSize * cols;
    double gridHeight = squareSize * rows;
    double startX = (width - gridWidth) / 2.0;
    double startY = (height - gridHeight) / 2.0;
    
    // cairo_set_source_rgb(cr, 0.1, 0.1, 0.1); 
    // cairo_select_font_face(cr, "Inter, Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    // cairo_set_font_size(cr, 20.0);
    // cairo_text_extents_t ext;
    // cairo_text_extents(cr, "Preview", &ext);
    // cairo_move_to(cr, (width - ext.width)/2.0 - ext.x_bearing, startY - 25.0);
    // cairo_show_text(cr, "Preview");
    
    double lightR, lightG, lightB, darkR, darkG, darkB;
    theme_data_get_light_square_color(dialog->theme, &lightR, &lightG, &lightB);
    theme_data_get_dark_square_color(dialog->theme, &darkR, &darkG, &darkB);
    
    double radius = 12.0;
    cairo_new_path(cr);
    cairo_arc(cr, startX + gridWidth - radius, startY + radius, radius, -G_PI/2, 0);
    cairo_arc(cr, startX + gridWidth - radius, startY + gridHeight - radius, radius, 0, G_PI/2);
    cairo_arc(cr, startX + radius, startY + gridHeight - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, startX + radius, startY + radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_clip(cr);
    
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            bool isLight = ((c + r) % 2 == 0);
            if (isLight) cairo_set_source_rgb(cr, lightR, lightG, lightB);
            else cairo_set_source_rgb(cr, darkR, darkG, darkB);
            cairo_rectangle(cr, startX + c * squareSize, startY + r * squareSize, squareSize, squareSize);
            cairo_fill(cr);
        }
    }
    
    const char* font_name = theme_data_get_font_name(dialog->theme);
    if (!font_name) font_name = "Segoe UI Symbol";
    check_update_preview_cache(dialog);
    
    double wr, wg, wb, wsr, wsg, wsb, wsw;
    theme_data_get_white_piece_color(dialog->theme, &wr, &wg, &wb);
    theme_data_get_white_piece_stroke(dialog->theme, &wsr, &wsg, &wsb);
    wsw = theme_data_get_white_stroke_width(dialog->theme);

    double br, bg, bb, bsr, bsg, bsb, bsw;
    theme_data_get_black_piece_color(dialog->theme, &br, &bg, &bb);
    theme_data_get_black_piece_stroke(dialog->theme, &bsr, &bsg, &bsb);
    bsw = theme_data_get_black_stroke_width(dialog->theme);

    PieceType pieceTypes[] = {PIECE_PAWN, PIECE_KNIGHT, PIECE_BISHOP, PIECE_ROOK, PIECE_QUEEN, PIECE_KING};
    
    for (int row = 0; row < 2; row++) {
        for (int i = 0; i < 6; i++) {
            double x = startX + i * squareSize + squareSize / 2.0;
            double y = startY + row * squareSize + squareSize / 2.0;
            int owner = (row == 0) ? PLAYER_WHITE : PLAYER_BLACK;
            
            cairo_surface_t* s = dialog->piece_cache[owner][pieceTypes[i]];
            if (s) {
                 int img_w = cairo_image_surface_get_width(s);
                 int img_h = cairo_image_surface_get_height(s);
                 double targetSize = squareSize * 0.85;
                 double scale = targetSize / img_w;
                 if (targetSize / img_h < scale) scale = targetSize / img_h;
                 cairo_save(cr);
                 cairo_translate(cr, x - (img_w * scale)/2.0, y - (img_h * scale)/2.0);
                 cairo_scale(cr, scale, scale);
                 cairo_set_source_surface(cr, s, 0, 0);
                 cairo_paint(cr);
                 cairo_restore(cr);
            } else {
                // Text fallback
                const char* symbol = theme_data_get_piece_symbol(dialog->theme, pieceTypes[i], (Player)owner);
                PangoLayout* layout = pango_cairo_create_layout(cr);
                PangoFontDescription* desc = pango_font_description_new();
                pango_font_description_set_family(desc, font_name);
                pango_font_description_set_size(desc, (int)(squareSize * 0.7 * PANGO_SCALE));
                pango_font_description_set_weight(desc, PANGO_WEIGHT_SEMIBOLD);
                pango_layout_set_font_description(layout, desc);
                pango_layout_set_text(layout, symbol, -1);
                
                int tw, th;
                pango_layout_get_pixel_size(layout, &tw, &th);
                cairo_move_to(cr, x - tw/2.0, y - th/2.0);
                
                if (owner == PLAYER_WHITE) {
                    cairo_set_source_rgb(cr, wr, wg, wb);
                    pango_cairo_layout_path(cr, layout);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgb(cr, wsr, wsg, wsb);
                    cairo_set_line_width(cr, wsw);
                    cairo_stroke(cr);
                } else {
                    cairo_set_source_rgb(cr, br, bg, bb);
                    pango_cairo_layout_path(cr, layout);
                    cairo_fill_preserve(cr);
                    if (bsw > 0) {
                        cairo_set_source_rgb(cr, bsr, bsg, bsb);
                        cairo_set_line_width(cr, bsw);
                        cairo_stroke(cr);
                    } else {
                        cairo_new_path(cr); // Clear path
                    }
                }
                pango_font_description_free(desc);
                g_object_unref(layout);
            }
        }
    }
}

// Callbacks
static void refresh_dialog(PieceThemeDialog* dialog) {
    if (!dialog) return;
    update_preview(dialog);
    if (dialog->on_update) {
        // Only call if user_data is a valid pointer (simple heuristic check)
        if ((uintptr_t)dialog->user_data > 0x1000) {
            dialog->on_update(dialog->user_data);
        }
    }
    print_memory_usage();
}

static void update_preview(PieceThemeDialog* dialog) {
    if (dialog && dialog->preview_area && GTK_IS_WIDGET(dialog->preview_area)) {
        gtk_widget_queue_draw(dialog->preview_area);
    }
}

static void on_color_changed(GObject* source_object, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    GtkColorDialogButton* button = GTK_COLOR_DIALOG_BUTTON(source_object);
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    if (!dialog || !dialog->theme) return;
    
    const GdkRGBA* c = gtk_color_dialog_button_get_rgba(button);
    if (!c) return;
    
    GtkWidget* widget = GTK_WIDGET(button);
    if (widget == dialog->white_piece_color_button)
        theme_data_set_white_piece_color(dialog->theme, c->red, c->green, c->blue);
    else if (widget == dialog->white_stroke_color_button)
        theme_data_set_white_piece_stroke(dialog->theme, c->red, c->green, c->blue);
    else if (widget == dialog->black_piece_color_button)
        theme_data_set_black_piece_color(dialog->theme, c->red, c->green, c->blue);
    else if (widget == dialog->black_stroke_color_button)
        theme_data_set_black_piece_stroke(dialog->theme, c->red, c->green, c->blue);
        
    refresh_dialog(dialog);
}

static void on_stroke_width_changed(GtkSpinButton* spin, gpointer user_data) {
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    double val = gtk_spin_button_get_value(spin);
    if (GTK_WIDGET(spin) == dialog->white_stroke_width_spin)
        theme_data_set_white_stroke_width(dialog->theme, val);
    else
        theme_data_set_black_stroke_width(dialog->theme, val);
    refresh_dialog(dialog);
}

static void on_piece_set_changed(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    GtkDropDown* combo = GTK_DROP_DOWN(object);
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    guint selected = gtk_drop_down_get_selected(combo);
    if (selected < (guint)dialog->piece_set_count) {
        dialog->selected_piece_set_index = (int)selected;
        const char* name = dialog->piece_sets[selected].name;
        if (selected == 0 || strcmp(name, "Default") == 0)
            theme_data_set_font_name(dialog->theme, "Segoe UI Symbol");
        else
            theme_data_set_font_name(dialog->theme, name);
        refresh_dialog(dialog);
    }
}



static void on_reset_colors_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    theme_data_reset_piece_colors_only(dialog->theme);
    
    // Reset widths
    theme_data_set_white_stroke_width(dialog->theme, DEFAULT_WHITE_STROKE_WIDTH);
    theme_data_set_black_stroke_width(dialog->theme, DEFAULT_BLACK_STROKE_WIDTH);
    
    // Update UI controls
    if (dialog->white_piece_color_button) {
        double r, g, b;
        theme_data_get_white_piece_color(dialog->theme, &r, &g, &b);
        GdkRGBA c = {r, g, b, 1.0};
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->white_piece_color_button), &c);
        
        theme_data_get_white_piece_stroke(dialog->theme, &r, &g, &b); c = (GdkRGBA){r, g, b, 1.0};
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->white_stroke_color_button), &c);
        
        theme_data_get_black_piece_color(dialog->theme, &r, &g, &b); c = (GdkRGBA){r, g, b, 1.0};
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->black_piece_color_button), &c);
        
        theme_data_get_black_piece_stroke(dialog->theme, &r, &g, &b); c = (GdkRGBA){r, g, b, 1.0};
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->black_stroke_color_button), &c);
        
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->white_stroke_width_spin), DEFAULT_WHITE_STROKE_WIDTH);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->black_stroke_width_spin), DEFAULT_BLACK_STROKE_WIDTH);
    }

    refresh_dialog(dialog);
}

static void on_reset_piece_type_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    if (dialog->piece_set_count > 0) {
        // Just select 0. The signal handler will handle the rest (font setting etc).
        // This avoids the double-set / font mismatch error.
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dialog->piece_set_combo), 0);
    }
    refresh_dialog(dialog);
}

static gboolean on_window_close_request(GtkWindow* window, gpointer user_data) {
    (void)window;
    PieceThemeDialog* dialog = (PieceThemeDialog*)user_data;
    if (dialog && dialog->window) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), FALSE);
        if (dialog->parent_window) gtk_window_present(dialog->parent_window);
    }
    return TRUE;
}

// UI Construction
static void piece_theme_dialog_build_ui(PieceThemeDialog* dialog) {
    dialog->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    // Connect destroy signal to nullify the pointer in the struct
    g_signal_connect(dialog->content_box, "destroy", G_CALLBACK(on_widget_destroyed), &dialog->content_box);

    gtk_widget_set_margin_top(dialog->content_box, 24);
    gtk_widget_set_margin_bottom(dialog->content_box, 24);
    gtk_widget_set_margin_start(dialog->content_box, 24);
    gtk_widget_set_margin_end(dialog->content_box, 24);
    
    GtkWidget* title = gtk_label_new("Customize Piece Style");
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttribute* size = pango_attr_size_new(24 * PANGO_SCALE);
    pango_attr_list_insert(attrs, weight);
    pango_attr_list_insert(attrs, size);
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dialog->content_box), title);
    
    GtkWidget* main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
    gtk_box_append(GTK_BOX(dialog->content_box), main_hbox);
    
    // Left Container (Holds Scroll + Fixed Actions)
    GtkWidget* left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_size_request(left_vbox, 300, -1);
    gtk_box_append(GTK_BOX(main_hbox), left_vbox);

    // Left controls (Scrollable)
    GtkWidget* controls_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(controls_scroll, TRUE);
    GtkWidget* controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_end(controls_box, 12); // Add some padding for scrollbar
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(controls_scroll), controls_box);
    gtk_box_append(GTK_BOX(left_vbox), controls_scroll);
    
    // Piece Set
    GtkWidget* ps_label = gtk_label_new("Piece Set");
    gtk_widget_set_halign(ps_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(ps_label, "heading");
    gtk_box_append(GTK_BOX(controls_box), ps_label);
    GtkStringList* sl = gtk_string_list_new(NULL);
    for (int i=0; i<dialog->piece_set_count; i++) gtk_string_list_append(sl, dialog->piece_sets[i].display_name);
    
    dialog->piece_set_combo = gtk_drop_down_new(G_LIST_MODEL(sl), NULL);
    GtkListItemFactory* lf = gtk_signal_list_item_factory_new();
    g_signal_connect(lf, "setup", G_CALLBACK(setup_list_item), dialog);
    g_signal_connect(lf, "bind", G_CALLBACK(bind_list_item), dialog);
    GtkListItemFactory* bf = gtk_signal_list_item_factory_new();
    g_signal_connect(bf, "setup", G_CALLBACK(setup_button_item), dialog);
    g_signal_connect(bf, "bind", G_CALLBACK(bind_button_item), dialog);
    gtk_drop_down_set_list_factory(GTK_DROP_DOWN(dialog->piece_set_combo), lf);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(dialog->piece_set_combo), bf);
    g_object_unref(lf); g_object_unref(bf);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dialog->piece_set_combo), dialog->selected_piece_set_index);
    g_signal_connect(dialog->piece_set_combo, "notify::selected", G_CALLBACK(on_piece_set_changed), dialog);
    
    GtkWidget* pop = find_first_widget_of_type(GTK_WIDGET(dialog->piece_set_combo), GTK_TYPE_POPOVER);
    if (pop) g_signal_connect(pop, "notify::visible", G_CALLBACK(on_piece_dropdown_popover_visible), dialog);
    
    gtk_box_append(GTK_BOX(controls_box), dialog->piece_set_combo);

    // Separator
    gtk_box_append(GTK_BOX(controls_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Custom Style
    GtkWidget* colors_label = gtk_label_new("Custom Colors & Style");
    gtk_widget_set_halign(colors_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(colors_label, "heading");
    gtk_box_append(GTK_BOX(controls_box), colors_label);
    
    // Colors
    // White Piece Color
    GtkWidget* wp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* wp_label = gtk_label_new("White Piece Color");
    gtk_widget_set_hexpand(wp_label, TRUE);
    gtk_widget_set_halign(wp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(wp_box), wp_label);
    
    dialog->white_piece_dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog->white_piece_dialog, FALSE);
    dialog->white_piece_color_button = gtk_color_dialog_button_new(dialog->white_piece_dialog);
    g_signal_connect(dialog->white_piece_color_button, "notify::rgba", G_CALLBACK(on_color_changed), dialog);
    gtk_box_append(GTK_BOX(wp_box), dialog->white_piece_color_button);
    gtk_box_append(GTK_BOX(controls_box), wp_box);
    
    // White Stroke Color
    GtkWidget* ws_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* ws_label = gtk_label_new("White Stroke Color");
    gtk_widget_set_hexpand(ws_label, TRUE);
    gtk_widget_set_halign(ws_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(ws_box), ws_label);
    
    dialog->white_stroke_dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog->white_stroke_dialog, FALSE);
    dialog->white_stroke_color_button = gtk_color_dialog_button_new(dialog->white_stroke_dialog);
    g_signal_connect(dialog->white_stroke_color_button, "notify::rgba", G_CALLBACK(on_color_changed), dialog);
    gtk_box_append(GTK_BOX(ws_box), dialog->white_stroke_color_button);
    gtk_box_append(GTK_BOX(controls_box), ws_box);

    // White Stroke Width
    GtkWidget* wsw_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* wsw_label = gtk_label_new("White Stroke Width");
    gtk_widget_set_hexpand(wsw_label, TRUE);
    gtk_widget_set_halign(wsw_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(wsw_box), wsw_label);
    
    dialog->white_stroke_width_spin = gtk_spin_button_new_with_range(0, 5, 0.1);
    g_signal_connect(dialog->white_stroke_width_spin, "value-changed", G_CALLBACK(on_stroke_width_changed), dialog);
    gtk_box_append(GTK_BOX(wsw_box), dialog->white_stroke_width_spin);
    gtk_box_append(GTK_BOX(controls_box), wsw_box);
    
    // Black Piece Color
    GtkWidget* bp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* bp_label = gtk_label_new("Black Piece Color");
    gtk_widget_set_hexpand(bp_label, TRUE);
    gtk_widget_set_halign(bp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bp_box), bp_label);
    
    dialog->black_piece_dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog->black_piece_dialog, FALSE);
    dialog->black_piece_color_button = gtk_color_dialog_button_new(dialog->black_piece_dialog);
    g_signal_connect(dialog->black_piece_color_button, "notify::rgba", G_CALLBACK(on_color_changed), dialog);
    gtk_box_append(GTK_BOX(bp_box), dialog->black_piece_color_button);
    gtk_box_append(GTK_BOX(controls_box), bp_box);
    
    // Black Stroke Color
    GtkWidget* bs_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* bs_label = gtk_label_new("Black Stroke Color");
    gtk_widget_set_hexpand(bs_label, TRUE);
    gtk_widget_set_halign(bs_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bs_box), bs_label);
    
    dialog->black_stroke_dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog->black_stroke_dialog, FALSE);
    dialog->black_stroke_color_button = gtk_color_dialog_button_new(dialog->black_stroke_dialog);
    g_signal_connect(dialog->black_stroke_color_button, "notify::rgba", G_CALLBACK(on_color_changed), dialog);
    gtk_box_append(GTK_BOX(bs_box), dialog->black_stroke_color_button);
    gtk_box_append(GTK_BOX(controls_box), bs_box);
    
    // Black Stroke Width
    GtkWidget* bsw_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* bsw_label = gtk_label_new("Black Stroke Width");
    gtk_widget_set_hexpand(wsw_label, TRUE);
    gtk_widget_set_halign(bsw_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bsw_box), bsw_label);
    
    dialog->black_stroke_width_spin = gtk_spin_button_new_with_range(0, 5, 0.1);
    g_signal_connect(dialog->black_stroke_width_spin, "value-changed", G_CALLBACK(on_stroke_width_changed), dialog);
    gtk_box_append(GTK_BOX(bsw_box), dialog->black_stroke_width_spin);
    gtk_box_append(GTK_BOX(controls_box), bsw_box);
    
    // Actions (Fixed at bottom, outside scroll)
    GtkWidget* actions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(left_vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL)); // Optional separator
    
    dialog->reset_colors_button = gtk_button_new_with_label("Reset Colors & Thickness");
    g_signal_connect(dialog->reset_colors_button, "clicked", G_CALLBACK(on_reset_colors_clicked), dialog);
    gtk_box_append(GTK_BOX(actions_box), dialog->reset_colors_button);
    
    dialog->reset_piece_type_button = gtk_button_new_with_label("Reset Piece Set");
    g_signal_connect(dialog->reset_piece_type_button, "clicked", G_CALLBACK(on_reset_piece_type_clicked), dialog);
    gtk_box_append(GTK_BOX(actions_box), dialog->reset_piece_type_button);
    
    gtk_box_append(GTK_BOX(left_vbox), actions_box);
    
    // Right: Preview
    // Tighter spacing (2px)
    GtkWidget* preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(preview_box, TRUE);
    gtk_widget_set_vexpand(preview_box, TRUE);
    gtk_widget_set_halign(preview_box, GTK_ALIGN_CENTER);

    GtkWidget* preview_label = gtk_label_new("Preview");
    gtk_widget_set_halign(preview_label, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(preview_label, "heading");
    gtk_widget_set_margin_bottom(preview_label, 0); // Force 0 bottom margin
    gtk_box_append(GTK_BOX(preview_box), preview_label);

    dialog->preview_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(dialog->preview_area), 600);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(dialog->preview_area), 250);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dialog->preview_area), on_preview_draw, dialog, NULL);
    gtk_widget_set_halign(dialog->preview_area, GTK_ALIGN_CENTER);
    
    gtk_box_append(GTK_BOX(preview_box), dialog->preview_area);

    gtk_box_append(GTK_BOX(main_hbox), preview_box);
    
    // Set initial values
    double r, g, b;
    theme_data_get_white_piece_color(dialog->theme, &r, &g, &b);
    GdkRGBA c = {r, g, b, 1.0};
    if (dialog->white_piece_color_button) gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->white_piece_color_button), &c);
    
    theme_data_get_white_piece_stroke(dialog->theme, &r, &g, &b); c = (GdkRGBA){r, g, b, 1.0};
    if (dialog->white_stroke_color_button) gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->white_stroke_color_button), &c);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->white_stroke_width_spin), theme_data_get_white_stroke_width(dialog->theme));
    
    theme_data_get_black_piece_color(dialog->theme, &r, &g, &b); c = (GdkRGBA){r, g, b, 1.0};
    if (dialog->black_piece_color_button) gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->black_piece_color_button), &c);
    
    theme_data_get_black_piece_stroke(dialog->theme, &r, &g, &b); c = (GdkRGBA){r, g, b, 1.0};
    if (dialog->black_stroke_color_button) gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dialog->black_stroke_color_button), &c);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->black_stroke_width_spin), theme_data_get_black_stroke_width(dialog->theme));
}

// Public API
PieceThemeDialog* piece_theme_dialog_new_embedded(ThemeData* theme, PieceThemeUpdateCallback on_update, void* user_data) {
    if (debug_mode) printf("[PieceTheme] Creating embedded dialog. Theme=%p\n", (void*)theme);
    if (!theme) return NULL;
    PieceThemeDialog* dialog = (PieceThemeDialog*)calloc(1, sizeof(PieceThemeDialog));
    if (!dialog) return NULL;
    
    dialog->theme = theme;
    dialog->on_update = on_update;
    dialog->user_data = user_data;
    
    for(int i=0; i<2; i++) for(int j=0; j<6; j++) dialog->piece_cache[i][j] = NULL;
    scan_piece_sets(dialog);
    
    const char* current_font = theme_data_get_font_name(theme);
    if(current_font) {
        int idx = find_piece_set_index(dialog, current_font);
        if(idx>=0) dialog->selected_piece_set_index = idx;
    }
    
    piece_theme_dialog_build_ui(dialog);
    return dialog;
}

PieceThemeDialog* piece_theme_dialog_new(ThemeData* theme, PieceThemeUpdateCallback on_update, void* user_data, GtkWindow* parent_window) {
    PieceThemeDialog* dialog = piece_theme_dialog_new_embedded(theme, on_update, user_data);
    if (!dialog) return NULL;
    
    dialog->parent_window = parent_window;
    
    GtkWidget* win = gtk_window_new();
    dialog->window = GTK_WINDOW(win);
    gtk_window_set_title(dialog->window, "Piece Theme");
    gtk_window_set_modal(dialog->window, TRUE);
    gtk_window_set_default_size(dialog->window, 1000, 600);
    if (parent_window) gtk_window_set_transient_for(dialog->window, parent_window);
    
    g_signal_connect(dialog->window, "close-request", G_CALLBACK(on_window_close_request), dialog);
    gtk_window_set_child(dialog->window, dialog->content_box);
    
    return dialog;
}

GtkWidget* piece_theme_dialog_get_widget(PieceThemeDialog* dialog) {
    return dialog ? dialog->content_box : NULL;
}

void piece_theme_dialog_set_parent_window(PieceThemeDialog* dialog, GtkWindow* parent) {
    if (dialog) {
        dialog->parent_window = parent;
        if (dialog->window) gtk_window_set_transient_for(dialog->window, parent);
    }
}

void piece_theme_dialog_show(PieceThemeDialog* dialog) {
    if (dialog && dialog->window) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), TRUE);
        gtk_window_present(dialog->window);
    }
}

void piece_theme_dialog_free(PieceThemeDialog* dialog) {
    if (debug_mode) printf("[PieceTheme] Freeing dialog %p\n", (void*)dialog);
    if (!dialog) return;
    
    // [DEBUG] Fix: Disconnect destroy handler to prevent use-after-free
    if (dialog->content_box && GTK_IS_WIDGET(dialog->content_box)) {
        if (debug_mode) printf("[PieceTheme] Disconnecting destroy handler %p\n", (void*)dialog->content_box);
        g_signal_handlers_disconnect_by_func(dialog->content_box, G_CALLBACK(on_widget_destroyed), &dialog->content_box);
    }

    // Free fonts/surfaces cache
    clear_preview_cache(dialog);
    free_piece_sets(dialog);

    if (dialog->window) {
        if (debug_mode) printf("[PieceTheme] Destroying window\n");
        gtk_window_destroy(dialog->window);
    }
    
    // Unref color dialogs
    if (dialog->white_piece_dialog && G_IS_OBJECT(dialog->white_piece_dialog)) g_object_unref(dialog->white_piece_dialog);
    if (dialog->white_stroke_dialog && G_IS_OBJECT(dialog->white_stroke_dialog)) g_object_unref(dialog->white_stroke_dialog);
    if (dialog->black_piece_dialog && G_IS_OBJECT(dialog->black_piece_dialog)) g_object_unref(dialog->black_piece_dialog);
    if (dialog->black_stroke_dialog && G_IS_OBJECT(dialog->black_stroke_dialog)) g_object_unref(dialog->black_stroke_dialog);
    
    free(dialog);
    if (debug_mode) printf("[PieceTheme] Dialog freed\n");
}

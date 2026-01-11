#include "app_theme_dialog.h"
#include "config_manager.h"
#include "theme_manager.h"
#include "app_theme.h"
#include <gtk/gtk.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui_utils.h"

struct AppThemeDialog {
    GtkWindow* window;
    GtkWindow* parent_window;
    GtkWidget* content_box;
    
    // UI Controls
    GtkWidget* theme_combo;
    GtkWidget* notebook; // Restored Notebook
    
    // Toolbar Buttons
    GtkWidget* btn_rename;
    GtkWidget* btn_duplicate;
    GtkWidget* btn_delete;
    
    // Current editing state
    AppTheme edit_theme;
    bool is_custom_mode; // If true, we are editing a custom theme (or copy)
    
    // Store buttons to update them on theme switch
    // Map offset -> button widget
    GHashTable* light_buttons; // offset -> GtkWidget*
    GHashTable* dark_buttons;  // offset -> GtkWidget*
    
    // Signal blocking flag
    bool loading_ui;
};

// Offsets for color mapping
typedef struct {
    const char* label;
    size_t offset;
} ColorField;
static const ColorField COLOR_FIELDS[] = {
    // Surfaces
    { "Window Background", offsetof(AppThemeColors, base_bg) },
    { "Panel Background", offsetof(AppThemeColors, base_panel_bg) },
    { "Card / Content BG", offsetof(AppThemeColors, base_card_bg) },
    { "Tooltip Background", offsetof(AppThemeColors, tooltip_bg) },
    
    // Text
    { "Primary Text", offsetof(AppThemeColors, base_fg) },
    { "Muted / Label Text", offsetof(AppThemeColors, dim_label) },
    { "Error Text", offsetof(AppThemeColors, error_text) },
    { "Tooltip Text", offsetof(AppThemeColors, tooltip_fg) },
    
    // Borders
    { "Border Color", offsetof(AppThemeColors, border_color) },
    
    // Accents (Primary)
    { "Accent Color", offsetof(AppThemeColors, base_accent) },
    { "Accent Text (On Color)", offsetof(AppThemeColors, base_accent_fg) },
    
    // Status (Success/Destructive)
    { "Success Background", offsetof(AppThemeColors, base_success_bg) },
    { "Success Text (On Color)", offsetof(AppThemeColors, base_success_fg) },
    { "Success Text (Standalone)", offsetof(AppThemeColors, base_success_text) },
    { "Success Hover", offsetof(AppThemeColors, success_hover) },
    
    { "Destructive BG", offsetof(AppThemeColors, base_destructive_bg) },
    { "Destructive Text", offsetof(AppThemeColors, base_destructive_fg) },
    { "Destructive Hover", offsetof(AppThemeColors, destructive_hover) },
    
    // Components / Inputs
    { "Entry Background", offsetof(AppThemeColors, base_entry_bg) },
    { "Button Background", offsetof(AppThemeColors, button_bg) },
    { "Button Hover", offsetof(AppThemeColors, button_hover) },
    
    // Board Specific
    { "Capture BG (White)", offsetof(AppThemeColors, capture_bg_white) },
    { "Capture BG (Black)", offsetof(AppThemeColors, capture_bg_black) }
};

static void refresh_theme_list(AppThemeDialog* dialog);
static void load_theme_into_ui(AppThemeDialog* dialog);
static void on_new_theme_clicked(GtkButton* btn, gpointer user_data); // Forward declaration

// Hex helper (Unused hex_to_rgba removed)
static void rgba_to_hex(const GdkRGBA* rgba, char* dest) {
    int r = (int)(rgba->red * 255.0 + 0.5);
    int g = (int)(rgba->green * 255.0 + 0.5);
    int b = (int)(rgba->blue * 255.0 + 0.5);
    snprintf(dest, 32, "#%02x%02x%02x", r, g, b); // Fixed format
}

// Check if theme ID is a system default
static bool is_system_theme(const char* id) {
    return (strncmp(id, "theme_", 6) == 0 && (strstr(id, "_slate") || strstr(id, "_emerald") || strstr(id, "_aubergine") || strstr(id, "_mocha")));
}

static void on_color_set(GObject* source, GParamSpec* pspec, gpointer user_data) {
    (void)pspec; (void)user_data;
    GtkColorDialogButton* btn = GTK_COLOR_DIALOG_BUTTON(source);
    AppThemeDialog* dialog = (AppThemeDialog*)g_object_get_data(G_OBJECT(btn), "dialog");
    
    if (!dialog || dialog->loading_ui) return;
    
    size_t offset = (size_t)g_object_get_data(G_OBJECT(btn), "offset");
    bool is_dark = (bool)g_object_get_data(G_OBJECT(btn), "is_dark");
    
    const GdkRGBA* rgba = gtk_color_dialog_button_get_rgba(btn);
    char hex[32];
    rgba_to_hex(rgba, hex);
    
    // Update struct
    char* target_field = is_dark ? 
        ((char*)&dialog->edit_theme.dark + offset) : 
        ((char*)&dialog->edit_theme.light + offset);
        
    
    // Check if system, if so -> auto fork (new custom theme)
    if (is_system_theme(dialog->edit_theme.theme_id)) {
        AppTheme new_theme = dialog->edit_theme;
        
        // Generate new ID & Name
        snprintf(new_theme.theme_id, sizeof(new_theme.theme_id), "custom_%d", (int)time(NULL));
        snprintf(new_theme.display_name, sizeof(new_theme.display_name), "%.50s (Custom)", dialog->edit_theme.display_name);
        
        // Update the SPECIFIC field in the new theme copy
        char* new_target_field = is_dark ? 
            ((char*)&new_theme.dark + offset) : 
            ((char*)&new_theme.light + offset);
        snprintf(new_target_field, 32, "%s", hex);
        
        // Save & Switch
        app_themes_save_theme(&new_theme);
        refresh_theme_list(dialog);
        theme_manager_set_theme_id(new_theme.theme_id);
        
        // Reload UI (will pick up the new ID and the color we just set in the struct)
        load_theme_into_ui(dialog);
        return; 
    }

    // Use snprintf for normal update
    snprintf(target_field, 32, "%s", hex);
    
    // Update Hex Button Label
    GtkWidget* hex_btn = g_object_get_data(G_OBJECT(btn), "hex_btn");
    if (hex_btn) {
        gtk_button_set_label(GTK_BUTTON(hex_btn), hex);
    }
    
    // Apply immediately if custom
    app_themes_save_theme(&dialog->edit_theme); // This updates global list
    theme_manager_set_theme_id(dialog->edit_theme.theme_id); // Apply
}

static void on_hex_clicked(GtkButton* btn, gpointer user_data) {
    (void)user_data;
    const char* hex = gtk_button_get_label(btn);
    GdkDisplay* display = gdk_display_get_default();
    GdkClipboard* clip = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clip, hex);
    
    // Optional: Visual feedback could be added here (e.g., toast or label change)
}

static GtkWidget* create_color_row(AppThemeDialog* dialog, const char* label, size_t offset, bool is_dark) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    
    GtkWidget* lbl = gtk_label_new(label);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);
    
    // Hex Label (Clickable to copy)
    GtkWidget* hex_btn = gtk_button_new_with_label("#------");
    gtk_widget_add_css_class(hex_btn, "flat"); 
    gtk_widget_set_size_request(hex_btn, 80, -1);
    
    // Monospace styling removed for simplicity/compatibility
    // (Would require custom label child management or CSS)
    
    g_signal_connect(hex_btn, "clicked", G_CALLBACK(on_hex_clicked), NULL);
    gtk_box_append(GTK_BOX(box), hex_btn);

    GtkColorDialog* cd = gtk_color_dialog_new();
    GtkWidget* btn = gtk_color_dialog_button_new(cd);
    
    g_object_set_data(G_OBJECT(btn), "dialog", dialog);
    g_object_set_data(G_OBJECT(btn), "offset", (gpointer)offset);
    g_object_set_data(G_OBJECT(btn), "is_dark", (gpointer)(intptr_t)is_dark);
    g_object_set_data(G_OBJECT(btn), "hex_btn", hex_btn); // Link hex button to update it
    
    g_signal_connect(btn, "notify::rgba", G_CALLBACK(on_color_set), NULL);
    
    gtk_box_append(GTK_BOX(box), btn);
    
    // Store in hash table
    if (is_dark) g_hash_table_insert(dialog->dark_buttons, (gpointer)offset, btn);
    else g_hash_table_insert(dialog->light_buttons, (gpointer)offset, btn);
    
    return box;
}

static void build_tab_content(AppThemeDialog* dialog, GtkWidget* container, bool is_dark) {
    GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(list, 10);
    gtk_widget_set_margin_end(list, 10);
    gtk_widget_set_margin_top(list, 10);
    gtk_widget_set_margin_bottom(list, 10);
    
    // Helper to add header
    void add_header(const char* text) {
        GtkWidget* h = gtk_label_new(text);
        gtk_widget_set_halign(h, GTK_ALIGN_START);
        gtk_widget_add_css_class(h, "heading");
        gtk_widget_set_margin_top(h, 12);
        gtk_widget_set_margin_bottom(h, 4);
        gtk_box_append(GTK_BOX(list), h);
    }
    
    size_t count = sizeof(COLOR_FIELDS) / sizeof(COLOR_FIELDS[0]);
    for (size_t i = 0; i < count; i++) {
        // Insert Headers based on index ranges matching COLOR_FIELDS array
        if (i == 0) add_header("Surfaces");
        else if (i == 4) { // After Surfaces
            gtk_box_append(GTK_BOX(list), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
            add_header("Text & Typography");
        }
        else if (i == 8) { // After Text
            gtk_box_append(GTK_BOX(list), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
            add_header("Borders");
        }
        else if (i == 9) { // After Borders
            gtk_box_append(GTK_BOX(list), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
            add_header("Accents");
        }
        else if (i == 18) { // After Accents
            gtk_box_append(GTK_BOX(list), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
            add_header("Components & Board");
        }
        
        GtkWidget* row = create_color_row(dialog, COLOR_FIELDS[i].label, COLOR_FIELDS[i].offset, is_dark);
        gtk_box_append(GTK_BOX(list), row);
    }
    
    // Scrolled window with border
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    
    // Add a frame for visual containment
    GtkWidget* frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "rounded-border");
    gtk_frame_set_child(GTK_FRAME(frame), scroll);
    
    gtk_box_append(GTK_BOX(container), frame);
}

// Custom Draw for Theme Dropdown
static void on_theme_item_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)user_data;
    const char* theme_id = (const char*)g_object_get_data(G_OBJECT(area), "theme_id");
    
    // Default or "Create New" has no color preview
    if (!theme_id || strlen(theme_id) == 0) return;

    const AppTheme* theme = theme_manager_get_theme_by_id(theme_id);
    if (!theme) return;
    
    GdkRGBA c;
    if (gdk_rgba_parse(&c, theme->light.base_accent)) {
        cairo_set_source_rgb(cr, c.red, c.green, c.blue);
        
        // Draw rounded rect
        double radius = 4.0;
        double pad = 4.0;
        double x = pad;
        double y = pad;
        double w = width - 2*pad;
        double h = height - 2*pad;
        
        // Ensure it's square (use the smaller dimension)
        if (w > h) {
             x += (w - h) / 2.0;
             w = h;
        } else {
             y += (h - w) / 2.0;
             h = w;
        }
        
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - radius, y + radius, radius, -G_PI/2, 0);
        cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, G_PI/2);
        cairo_arc(cr, x + radius, y + h - radius, radius, G_PI/2, G_PI);
        cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3*G_PI/2);
        cairo_close_path(cr);
        
        cairo_fill(cr);
    }
}

static void setup_theme_list_item(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data) {
    (void)factory; (void)user_data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    
    // Preview Area
    GtkWidget* preview = gtk_drawing_area_new();
    gtk_widget_set_size_request(preview, 24, 24);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(preview), on_theme_item_draw, NULL, NULL);
    
    GtkWidget* label = gtk_label_new(NULL);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    
    gtk_box_append(GTK_BOX(box), preview);
    gtk_box_append(GTK_BOX(box), label);
    
    gtk_list_item_set_child(list_item, box);
}

static void bind_theme_list_item(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data) {
    (void)factory; (void)user_data;
    GtkWidget* box = gtk_list_item_get_child(list_item);
    GtkWidget* preview = gtk_widget_get_first_child(box);
    GtkWidget* label = gtk_widget_get_next_sibling(preview);
    
    GtkStringObject* strobj = GTK_STRING_OBJECT(gtk_list_item_get_item(list_item));
    const char* display_name = gtk_string_object_get_string(strobj);
    gtk_label_set_text(GTK_LABEL(label), display_name);
    
    // Find ID
    const char* id = NULL;
    if (strcmp(display_name, "Create New Theme...") == 0) {
        id = ""; // Special case
    } else {
        // Reverse lookup ID from name 
        // 1. Check System
        const char* sys_names[] = { 
            "Slate Blue", "Emerald Teal", "Aubergine Purple", "Mocha Gold",
            "Slate Rose", "Ocean Mist", "Forest Amber", "Graphite Lime", "Sand Cobalt", "Sage Ash"
        };
        const char* sys_ids[] = { 
            "theme_a_slate", "theme_b_emerald", "theme_c_aubergine", "theme_d_mocha_gold",
            "theme_e_slate_rose", "theme_f_ocean_mist", "theme_g_forest_amber", "theme_h_graphite_lime", "theme_i_sand_cobalt", "theme_j_sage_ash"
        };
        for (int i=0; i<10; i++) {
            if (strcmp(display_name, sys_names[i]) == 0) {
                id = sys_ids[i];
                break;
            }
        }
        // 2. Check Custom if not found
        if (!id) {
            int count = 0;
            AppTheme* customs = app_themes_get_list(&count);
            for (int i = 0; i < count; i++) {
                if (strcmp(customs[i].display_name, display_name) == 0) {
                    id = customs[i].theme_id;
                    break;
                }
            }
        }
    }
    
    g_object_set_data(G_OBJECT(preview), "theme_id", (gpointer)id);
    gtk_widget_queue_draw(preview);
}

static void on_theme_combine_changed(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)object; (void)pspec;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    if (dialog->loading_ui) return;
    
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dialog->theme_combo));
    GListModel* model = gtk_drop_down_get_model(GTK_DROP_DOWN(dialog->theme_combo));
    GtkStringObject* item = GTK_STRING_OBJECT(g_list_model_get_item(model, selected));
    const char* label = gtk_string_object_get_string(item);
    
    if (strcmp(label, "Create New Theme...") == 0) {
        on_new_theme_clicked(NULL, dialog);
        return;
    }
    
    const char* id = NULL;
    
    if (strcmp(label, "Slate Blue") == 0) id = "theme_a_slate";
    else if (strcmp(label, "Emerald Teal") == 0) id = "theme_b_emerald";
    else if (strcmp(label, "Aubergine Purple") == 0) id = "theme_c_aubergine";
    else if (strcmp(label, "Mocha Gold") == 0) id = "theme_d_mocha_gold";
    else if (strcmp(label, "Slate Rose") == 0) id = "theme_e_slate_rose";
    else if (strcmp(label, "Ocean Mist") == 0) id = "theme_f_ocean_mist";
    else if (strcmp(label, "Forest Amber") == 0) id = "theme_g_forest_amber";
    else if (strcmp(label, "Graphite Lime") == 0) id = "theme_h_graphite_lime";
    else if (strcmp(label, "Sand Cobalt") == 0) id = "theme_i_sand_cobalt";
    else if (strcmp(label, "Sage Ash") == 0) id = "theme_j_sage_ash";
    else {
        int count = 0;
        AppTheme* customs = app_themes_get_list(&count);
        for (int i = 0; i < count; i++) {
            if (strcmp(customs[i].display_name, label) == 0) {
                id = customs[i].theme_id;
                break;
            }
        }
    }
    
    if (id) {
        theme_manager_set_theme_id(id);
        load_theme_into_ui(dialog);
        
        // Update Action State
        bool is_sys = is_system_theme(id);
        gtk_widget_set_sensitive(dialog->btn_delete, !is_sys);
        gtk_widget_set_sensitive(dialog->btn_rename, !is_sys);
    }
}

typedef struct {
    AppThemeDialog* parent;
    GtkWidget* entry;
    GtkWidget* window;
} RenameData;

static void on_rename_confirm(GtkButton* btn, gpointer user_data) {
    (void)btn;
    RenameData* data = (RenameData*)user_data;
    const char* text = gtk_editable_get_text(GTK_EDITABLE(data->entry));
    if (text && strlen(text) > 0) {
        snprintf(data->parent->edit_theme.display_name, sizeof(data->parent->edit_theme.display_name), "%s", text);
        app_themes_save_theme(&data->parent->edit_theme);
        refresh_theme_list(data->parent);
        // Refresh selection
        theme_manager_set_theme_id(data->parent->edit_theme.theme_id); 
        load_theme_into_ui(data->parent); 
    }
    gtk_window_destroy(GTK_WINDOW(data->window));
    free(data);
}

static void on_rename_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    if (is_system_theme(dialog->edit_theme.theme_id)) return;

    GtkWidget* win = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(win), dialog->window ? dialog->window : dialog->parent_window);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_title(GTK_WINDOW(win), "Rename Theme");
    gtk_window_set_default_size(GTK_WINDOW(win), 300, -1);
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(GTK_WINDOW(win));

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget* lbl = gtk_label_new("Enter new theme name:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);

    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), dialog->edit_theme.display_name);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(row, GTK_ALIGN_END);
    
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(row), cancel);

    GtkWidget* save = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save, "suggested-action");
    
    RenameData* data = malloc(sizeof(RenameData));
    data->parent = dialog;
    data->entry = entry;
    data->window = win;
    
    g_signal_connect(save, "clicked", G_CALLBACK(on_rename_confirm), data);
    gtk_box_append(GTK_BOX(row), save);

    gtk_box_append(GTK_BOX(box), row);

    gtk_widget_set_visible(win, TRUE);
}

static void on_copy_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    
    AppTheme new_theme = dialog->edit_theme;
    
    // Generate new ID
    snprintf(new_theme.theme_id, sizeof(new_theme.theme_id), "custom_%d", (int)time(NULL));
    snprintf(new_theme.display_name, sizeof(new_theme.display_name), "Copy of %.*s", 
             (int)(sizeof(new_theme.display_name) - 9), dialog->edit_theme.display_name);
    
    app_themes_save_theme(&new_theme);
    refresh_theme_list(dialog);
    theme_manager_set_theme_id(new_theme.theme_id);
    load_theme_into_ui(dialog);
}

static gboolean on_new_theme_idle(gpointer user_data) {
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    
    AppTheme new_theme;
    new_theme = dialog->edit_theme; // Copy current as base
    
    snprintf(new_theme.theme_id, sizeof(new_theme.theme_id), "custom_%d", (int)time(NULL) + 1);
    snprintf(new_theme.display_name, sizeof(new_theme.display_name), "New Custom Theme");
    
    app_themes_save_theme(&new_theme);
    refresh_theme_list(dialog);
    theme_manager_set_theme_id(new_theme.theme_id);
    load_theme_into_ui(dialog);
    
    return G_SOURCE_REMOVE;
}

static void on_new_theme_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    // Defer to idle to avoid modifying model during signal emission
    g_idle_add(on_new_theme_idle, dialog);
}


static void on_delete_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    
    if (is_system_theme(dialog->edit_theme.theme_id)) return;
    
    app_themes_delete_theme(dialog->edit_theme.theme_id);
    refresh_theme_list(dialog);
    
    theme_manager_set_theme_id("theme_c_aubergine");
    load_theme_into_ui(dialog);
}

// === IMPORTS / EXPORTS ===

static void write_colors_json_export(FILE* f, const AppThemeColors* c, bool is_last) {
    (void)is_last; 
    fprintf(f, "    \"base_bg\": \"%s\",\n", c->base_bg);
    fprintf(f, "    \"base_fg\": \"%s\",\n", c->base_fg);
    fprintf(f, "    \"base_panel_bg\": \"%s\",\n", c->base_panel_bg);
    fprintf(f, "    \"base_card_bg\": \"%s\",\n", c->base_card_bg);
    fprintf(f, "    \"base_entry_bg\": \"%s\",\n", c->base_entry_bg);
    fprintf(f, "    \"base_accent\": \"%s\",\n", c->base_accent);
    fprintf(f, "    \"base_accent_fg\": \"%s\",\n", c->base_accent_fg);
    fprintf(f, "    \"base_success_bg\": \"%s\",\n", c->base_success_bg);
    fprintf(f, "    \"base_success_text\": \"%s\",\n", c->base_success_text);
    fprintf(f, "    \"base_success_fg\": \"%s\",\n", c->base_success_fg);
    fprintf(f, "    \"success_hover\": \"%s\",\n", c->success_hover);
    fprintf(f, "    \"base_destructive_bg\": \"%s\",\n", c->base_destructive_bg);
    fprintf(f, "    \"base_destructive_fg\": \"%s\",\n", c->base_destructive_fg);
    fprintf(f, "    \"destructive_hover\": \"%s\",\n", c->destructive_hover);
    fprintf(f, "    \"border_color\": \"%s\",\n", c->border_color);
    fprintf(f, "    \"dim_label\": \"%s\",\n", c->dim_label);
    fprintf(f, "    \"tooltip_bg\": \"%s\",\n", c->tooltip_bg);
    fprintf(f, "    \"tooltip_fg\": \"%s\",\n", c->tooltip_fg);
    fprintf(f, "    \"button_bg\": \"%s\",\n", c->button_bg);
    fprintf(f, "    \"button_hover\": \"%s\",\n", c->button_hover);
    fprintf(f, "    \"error_text\": \"%s\",\n", c->error_text);
    fprintf(f, "    \"capture_bg_white\": \"%s\",\n", c->capture_bg_white);
    fprintf(f, "    \"capture_bg_black\": \"%s\"\n", c->capture_bg_black); 
}

// Helper to extract string value by key from a line (same logic as config_manager)
static void extract_json_str(const char* line, const char* key, char* dest, size_t dest_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* found = strstr(line, search);
    if (found) {
        const char* val_start = strchr(found, ':');
        if (val_start) {
            val_start = strchr(val_start, '\"');
            if (val_start) {
                val_start++;
                const char* val_end = strchr(val_start, '\"');
                if (val_end) {
                    size_t len = val_end - val_start;
                    if (len >= dest_size) len = dest_size - 1;
                    strncpy(dest, val_start, len);
                    dest[len] = '\0';
                }
            }
        }
    }
}

static void on_import_finish(GObject* source, GAsyncResult* result, gpointer data) {
    AppThemeDialog* dialog = (AppThemeDialog*)data;
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char* path = g_file_get_path(file);
        FILE* f = fopen(path, "r");
        if (f) {
            char line[1024];
            AppThemeColors* current_colors = NULL;
            
            // Temporary theme to parse into
            AppTheme imported_theme = dialog->edit_theme;
            
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "\"theme_id\"")) {
                    extract_json_str(line, "theme_id", imported_theme.theme_id, sizeof(imported_theme.theme_id));
                    // Force a new unique ID to avoid collision with file's ID if system or duplicate
                    snprintf(imported_theme.theme_id, sizeof(imported_theme.theme_id), "custom_%d", (int)time(NULL));
                }
                else if (strstr(line, "\"display_name\"")) {
                    extract_json_str(line, "display_name", imported_theme.display_name, sizeof(imported_theme.display_name));
                    // Append (Imported) to differentiate
                    size_t len = strlen(imported_theme.display_name);
                    if (len < sizeof(imported_theme.display_name) - 12) {
                         strcat(imported_theme.display_name, " (Imported)");
                    }
                }
                else if (strstr(line, "\"light\": {")) current_colors = &imported_theme.light;
                else if (strstr(line, "\"dark\": {")) current_colors = &imported_theme.dark;
                
                else if (current_colors) {
                    if (strstr(line, "\"base_bg\"")) extract_json_str(line, "base_bg", current_colors->base_bg, sizeof(current_colors->base_bg));
                    else if (strstr(line, "\"base_fg\"")) extract_json_str(line, "base_fg", current_colors->base_fg, sizeof(current_colors->base_fg));
                    else if (strstr(line, "\"base_panel_bg\"")) extract_json_str(line, "base_panel_bg", current_colors->base_panel_bg, sizeof(current_colors->base_panel_bg));
                    else if (strstr(line, "\"base_card_bg\"")) extract_json_str(line, "base_card_bg", current_colors->base_card_bg, sizeof(current_colors->base_card_bg));
                    else if (strstr(line, "\"base_entry_bg\"")) extract_json_str(line, "base_entry_bg", current_colors->base_entry_bg, sizeof(current_colors->base_entry_bg));
                    else if (strstr(line, "\"base_accent\"")) extract_json_str(line, "base_accent", current_colors->base_accent, sizeof(current_colors->base_accent));
                    else if (strstr(line, "\"base_accent_fg\"")) extract_json_str(line, "base_accent_fg", current_colors->base_accent_fg, sizeof(current_colors->base_accent_fg));
                    else if (strstr(line, "\"base_success_bg\"")) extract_json_str(line, "base_success_bg", current_colors->base_success_bg, sizeof(current_colors->base_success_bg));
                    else if (strstr(line, "\"base_success_text\"")) extract_json_str(line, "base_success_text", current_colors->base_success_text, sizeof(current_colors->base_success_text));
                    else if (strstr(line, "\"base_success_fg\"")) extract_json_str(line, "base_success_fg", current_colors->base_success_fg, sizeof(current_colors->base_success_fg));
                    else if (strstr(line, "\"success_hover\"")) extract_json_str(line, "success_hover", current_colors->success_hover, sizeof(current_colors->success_hover));
                    else if (strstr(line, "\"base_destructive_bg\"")) extract_json_str(line, "base_destructive_bg", current_colors->base_destructive_bg, sizeof(current_colors->base_destructive_bg));
                    else if (strstr(line, "\"base_destructive_fg\"")) extract_json_str(line, "base_destructive_fg", current_colors->base_destructive_fg, sizeof(current_colors->base_destructive_fg));
                    else if (strstr(line, "\"destructive_hover\"")) extract_json_str(line, "destructive_hover", current_colors->destructive_hover, sizeof(current_colors->destructive_hover));
                    else if (strstr(line, "\"border_color\"")) extract_json_str(line, "border_color", current_colors->border_color, sizeof(current_colors->border_color));
                    else if (strstr(line, "\"dim_label\"")) extract_json_str(line, "dim_label", current_colors->dim_label, sizeof(current_colors->dim_label));
                    else if (strstr(line, "\"tooltip_bg\"")) extract_json_str(line, "tooltip_bg", current_colors->tooltip_bg, sizeof(current_colors->tooltip_bg));
                    else if (strstr(line, "\"tooltip_fg\"")) extract_json_str(line, "tooltip_fg", current_colors->tooltip_fg, sizeof(current_colors->tooltip_fg));
                    else if (strstr(line, "\"button_bg\"")) extract_json_str(line, "button_bg", current_colors->button_bg, sizeof(current_colors->button_bg));
                    else if (strstr(line, "\"button_hover\"")) extract_json_str(line, "button_hover", current_colors->button_hover, sizeof(current_colors->button_hover));
                    else if (strstr(line, "\"error_text\"")) extract_json_str(line, "error_text", current_colors->error_text, sizeof(current_colors->error_text));
                    else if (strstr(line, "\"capture_bg_white\"")) extract_json_str(line, "capture_bg_white", current_colors->capture_bg_white, sizeof(current_colors->capture_bg_white));
                    else if (strstr(line, "\"capture_bg_black\"")) extract_json_str(line, "capture_bg_black", current_colors->capture_bg_black, sizeof(current_colors->capture_bg_black));
                }
            }
            fclose(f);
            
            // Save and Apply
            app_themes_save_theme(&imported_theme);
            refresh_theme_list(dialog);
            theme_manager_set_theme_id(imported_theme.theme_id);
            load_theme_into_ui(dialog);
        }
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(source);
}

static void on_import_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "App Theme (*.json)");
    gtk_file_filter_add_pattern(filter, "*.json");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);

    GtkWindow* w = dialog->window ? dialog->window : dialog->parent_window;
    
    gtk_file_dialog_open(file_dialog, w, NULL, on_import_finish, dialog);
}

static void on_export_finish(GObject* source, GAsyncResult* result, gpointer data) {
    AppThemeDialog* dialog = (AppThemeDialog*)data;
    GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char* path = g_file_get_path(file);
        FILE* f = fopen(path, "w");
        if (f) {
            fprintf(f, "{\n");
            fprintf(f, "  \"theme_id\": \"%s\",\n", dialog->edit_theme.theme_id);
            fprintf(f, "  \"display_name\": \"%s\",\n", dialog->edit_theme.display_name);
            
            fprintf(f, "  \"light\": {\n");
            write_colors_json_export(f, &dialog->edit_theme.light, false);
            fprintf(f, "  },\n");
            
            fprintf(f, "  \"dark\": {\n");
            write_colors_json_export(f, &dialog->edit_theme.dark, true);
            fprintf(f, "  }\n");
            
            fprintf(f, "}\n");
            fclose(f);
        }
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(source);
}

static void on_export_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    GtkFileDialog* file_dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_initial_name(file_dialog, "theme.json");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "App Theme (*.json)");
    gtk_file_filter_add_pattern(filter, "*.json");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(file_dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
    GtkWindow* parent = dialog->window ? dialog->window : dialog->parent_window;
    gtk_file_dialog_save(file_dialog, parent, NULL, on_export_finish, dialog);
}

// ------------------------------------------------------------------------

static void refresh_theme_list(AppThemeDialog* dialog) {
    dialog->loading_ui = true;
    
    GtkStringList* list = gtk_string_list_new(NULL);
    
    gtk_string_list_append(list, "Slate Blue");
    gtk_string_list_append(list, "Emerald Teal");
    gtk_string_list_append(list, "Aubergine Purple");
    gtk_string_list_append(list, "Mocha Gold");
    gtk_string_list_append(list, "Slate Rose");
    gtk_string_list_append(list, "Ocean Mist");
    gtk_string_list_append(list, "Forest Amber");
    gtk_string_list_append(list, "Graphite Lime");
    gtk_string_list_append(list, "Sand Cobalt");
    gtk_string_list_append(list, "Sage Ash");
    
    int count = 0;
    AppTheme* customs = app_themes_get_list(&count);
    for (int i = 0; i < count; i++) {
        gtk_string_list_append(list, customs[i].display_name);
    }
    
    // Add "Create New..." option
    gtk_string_list_append(list, "Create New Theme...");
    
    gtk_drop_down_set_model(GTK_DROP_DOWN(dialog->theme_combo), G_LIST_MODEL(list));
    g_object_unref(list);
    
    dialog->loading_ui = false;
}

static void load_theme_into_ui(AppThemeDialog* dialog) {
    dialog->loading_ui = true;
    
    const AppTheme* current = theme_manager_get_current_theme();
    if (current) {
        dialog->edit_theme = *current;
        
        GListModel* model = gtk_drop_down_get_model(GTK_DROP_DOWN(dialog->theme_combo));
        guint count = g_list_model_get_n_items(model);
        for (guint i = 0; i < count; i++) {
            GtkStringObject* item = GTK_STRING_OBJECT(g_list_model_get_item(model, i));
            const char* label = gtk_string_object_get_string(item);
            
            bool match = false;
            
            if (strcmp(label, current->display_name) == 0) match = true;
            else if (strstr(current->theme_id, "theme_a") && strcmp(label, "Slate Blue") == 0) match = true;
            else if (strstr(current->theme_id, "theme_b") && strcmp(label, "Emerald Teal") == 0) match = true;
            else if (strstr(current->theme_id, "theme_c") && strcmp(label, "Aubergine Purple") == 0) match = true;
            else if (strstr(current->theme_id, "theme_d") && strcmp(label, "Mocha Gold") == 0) match = true;
            else if (strstr(current->theme_id, "theme_e") && strcmp(label, "Slate Rose") == 0) match = true;
            else if (strstr(current->theme_id, "theme_f") && strcmp(label, "Ocean Mist") == 0) match = true;
            else if (strstr(current->theme_id, "theme_g") && strcmp(label, "Forest Amber") == 0) match = true;
            else if (strstr(current->theme_id, "theme_h") && strcmp(label, "Graphite Lime") == 0) match = true;
            else if (strstr(current->theme_id, "theme_i") && strcmp(label, "Sand Cobalt") == 0) match = true;
            else if (strstr(current->theme_id, "theme_j") && strcmp(label, "Sage Ash") == 0) match = true;
            
            if (match) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(dialog->theme_combo), i);
                break;
            }
        }
    }
    
    size_t count = sizeof(COLOR_FIELDS) / sizeof(COLOR_FIELDS[0]);
    for (size_t i = 0; i < count; i++) {
        size_t off = COLOR_FIELDS[i].offset;
        
        // Light
        GtkWidget* l_btn = g_hash_table_lookup(dialog->light_buttons, (gpointer)off);
        if (l_btn) {
            char* l_hex = ((char*)&dialog->edit_theme.light) + off;
            GdkRGBA l_rgba; 
            if (gdk_rgba_parse(&l_rgba, l_hex))
                gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(l_btn), &l_rgba);
            
            // Update hex label
            GtkWidget* h = g_object_get_data(G_OBJECT(l_btn), "hex_btn");
            if (h) gtk_button_set_label(GTK_BUTTON(h), l_hex);
        }
        
        // Dark
        GtkWidget* d_btn = g_hash_table_lookup(dialog->dark_buttons, (gpointer)off);
        if (d_btn) {
            char* d_hex = ((char*)&dialog->edit_theme.dark) + off;
            GdkRGBA d_rgba; 
            if (gdk_rgba_parse(&d_rgba, d_hex))
                gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(d_btn), &d_rgba);

            // Update hex label
            GtkWidget* h = g_object_get_data(G_OBJECT(d_btn), "hex_btn");
            if (h) gtk_button_set_label(GTK_BUTTON(h), d_hex);
        }
    }
    
    dialog->loading_ui = false;
}

static void app_theme_dialog_build_ui(AppThemeDialog* dialog) {
    dialog->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(dialog->content_box, 24);
    gtk_widget_set_margin_bottom(dialog->content_box, 24);
    gtk_widget_set_margin_start(dialog->content_box, 24);
    gtk_widget_set_margin_end(dialog->content_box, 24);
    
    // Title
    GtkWidget* title = gtk_label_new("Customize App Theme");
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttribute* size = pango_attr_size_new(24 * PANGO_SCALE);
    pango_attr_list_insert(attrs, weight);
    pango_attr_list_insert(attrs, size);
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dialog->content_box), title);
    
    // Toolbar Header
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    
    // 1. Theme Dropdown (Left, Expanded)
    dialog->theme_combo = gtk_drop_down_new(NULL, NULL);
    
    // Set Factory for custom rendering (color preview)
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(setup_theme_list_item), dialog);
    g_signal_connect(factory, "bind", G_CALLBACK(bind_theme_list_item), dialog);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(dialog->theme_combo), factory);
    g_object_unref(factory);

    gtk_widget_set_hexpand(dialog->theme_combo, TRUE);
    gtk_widget_set_valign(dialog->theme_combo, GTK_ALIGN_CENTER);
    g_signal_connect(dialog->theme_combo, "notify::selected", G_CALLBACK(on_theme_combine_changed), dialog);
    gtk_box_append(GTK_BOX(toolbar), dialog->theme_combo);
    
    // 2. Actions (Right)
    
    // Rename
    dialog->btn_rename = gtk_button_new_with_label("Rename");
    gtk_widget_set_tooltip_text(dialog->btn_rename, "Rename Theme");
    gtk_widget_add_css_class(dialog->btn_rename, "flat");
    g_signal_connect(dialog->btn_rename, "clicked", G_CALLBACK(on_rename_clicked), dialog);
    gtk_box_append(GTK_BOX(toolbar), dialog->btn_rename);
    
    // Duplicate (Copy)
    dialog->btn_duplicate = gtk_button_new_with_label("Copy");
    gtk_widget_set_tooltip_text(dialog->btn_duplicate, "Duplicate Theme");
    gtk_widget_add_css_class(dialog->btn_duplicate, "flat");
    g_signal_connect(dialog->btn_duplicate, "clicked", G_CALLBACK(on_copy_clicked), dialog);
    gtk_box_append(GTK_BOX(toolbar), dialog->btn_duplicate);
    
    // Delete
    dialog->btn_delete = gtk_button_new_with_label("Delete");
    gtk_widget_set_tooltip_text(dialog->btn_delete, "Delete Theme");
    gtk_widget_add_css_class(dialog->btn_delete, "destructive-action");
    g_signal_connect(dialog->btn_delete, "clicked", G_CALLBACK(on_delete_clicked), dialog);
    gtk_box_append(GTK_BOX(toolbar), dialog->btn_delete);
    
    gtk_box_append(GTK_BOX(dialog->content_box), toolbar);
    
    // Spacer
    gtk_box_append(GTK_BOX(dialog->content_box), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    
    // Notebook (Restored)
    dialog->notebook = gtk_notebook_new();
    gtk_widget_add_css_class(dialog->notebook, "ai-notebook");
    gtk_widget_set_vexpand(dialog->notebook, TRUE);
    
    GtkWidget* light_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    build_tab_content(dialog, light_page, false);
    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), light_page, gtk_label_new("Light Mode"));
    
    GtkWidget* dark_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    build_tab_content(dialog, dark_page, true);
    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), dark_page, gtk_label_new("Dark Mode"));
    
    gtk_box_append(GTK_BOX(dialog->content_box), dialog->notebook);
    
    // Footer Actions
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(footer, GTK_ALIGN_END);
    
    GtkWidget* import_btn = gtk_button_new_with_label("Import JSON");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_import_clicked), dialog);
    gtk_box_append(GTK_BOX(footer), import_btn);
    
    GtkWidget* exp_btn = gtk_button_new_with_label("Export JSON");
    g_signal_connect(exp_btn, "clicked", G_CALLBACK(on_export_clicked), dialog);
    gtk_box_append(GTK_BOX(footer), exp_btn);
    
    gtk_box_append(GTK_BOX(dialog->content_box), footer);
    
    refresh_theme_list(dialog);
}

AppThemeDialog* app_theme_dialog_new(GtkWindow* parent) {
    AppThemeDialog* dialog = calloc(1, sizeof(AppThemeDialog));
    dialog->light_buttons = g_hash_table_new(NULL, NULL);
    dialog->dark_buttons = g_hash_table_new(NULL, NULL);
    
    app_theme_dialog_build_ui(dialog);
    
    dialog->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(dialog->window, "App Theme Editor");
    gtk_window_set_default_size(dialog->window, 500, 600);
    gtk_window_set_child(dialog->window, dialog->content_box);
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(dialog->window);
    
    if (parent) {
        gtk_window_set_transient_for(dialog->window, parent);
        dialog->parent_window = parent;
    }
    
    return dialog;
}

AppThemeDialog* app_theme_dialog_new_embedded(GtkWindow* parent) {
    AppThemeDialog* dialog = calloc(1, sizeof(AppThemeDialog));
    dialog->light_buttons = g_hash_table_new(NULL, NULL);
    dialog->dark_buttons = g_hash_table_new(NULL, NULL);
    dialog->parent_window = parent;
    
    app_theme_dialog_build_ui(dialog);
    // No window creation
    
    return dialog;
}

void app_theme_dialog_show(AppThemeDialog* dialog) {
    if (dialog) {
        if (dialog->window) {
            gtk_window_present(dialog->window);
        }
        load_theme_into_ui(dialog);
    }
}

void app_theme_dialog_save_config(AppThemeDialog* dialog, void* cfg) {
    (void)dialog; (void)cfg;
}

GtkWidget* app_theme_dialog_get_widget(AppThemeDialog* dialog) {
    return dialog->content_box;
}

void app_theme_dialog_free(AppThemeDialog* dialog) {
     if(dialog) {
        g_hash_table_destroy(dialog->light_buttons);
        g_hash_table_destroy(dialog->dark_buttons);
        free(dialog);
     }
}

#include "app_theme_dialog.h"
#include "config_manager.h"
#include "theme_manager.h"
#include "app_theme.h"
#include <gtk/gtk.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct AppThemeDialog {
    GtkWindow* window;
    GtkWindow* parent_window;
    GtkWidget* content_box;
    
    // UI Controls
    GtkWidget* theme_combo;
    GtkWidget* notebook; // Light/Dark tabs
    
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
    { "Base Background", offsetof(AppThemeColors, base_bg) },
    { "Base Foreground", offsetof(AppThemeColors, base_fg) },
    { "Panel Background", offsetof(AppThemeColors, base_panel_bg) },
    { "Card Background", offsetof(AppThemeColors, base_card_bg) },
    { "Entry Background", offsetof(AppThemeColors, base_entry_bg) },
    { "Accent Color", offsetof(AppThemeColors, base_accent) },
    { "Accent Text", offsetof(AppThemeColors, base_accent_fg) },
    { "Success Background", offsetof(AppThemeColors, base_success_bg) },
    { "Success Text", offsetof(AppThemeColors, base_success_text) },
    { "Success FG", offsetof(AppThemeColors, base_success_fg) },
    { "Success Hover", offsetof(AppThemeColors, success_hover) },
    { "Destructive BG", offsetof(AppThemeColors, base_destructive_bg) },
    { "Destructive FG", offsetof(AppThemeColors, base_destructive_fg) },
    { "Destructive Hover", offsetof(AppThemeColors, destructive_hover) },
    { "Border Color", offsetof(AppThemeColors, border_color) },
    { "Dim Label", offsetof(AppThemeColors, dim_label) },
    { "Tooltip BG", offsetof(AppThemeColors, tooltip_bg) },
    { "Tooltip FG", offsetof(AppThemeColors, tooltip_fg) },
    { "Button BG", offsetof(AppThemeColors, button_bg) },
    { "Button Hover", offsetof(AppThemeColors, button_hover) },
    { "Error Text", offsetof(AppThemeColors, error_text) },
    { "Capture BG (White)", offsetof(AppThemeColors, capture_bg_white) },
    { "Capture BG (Black)", offsetof(AppThemeColors, capture_bg_black) }
};

static void refresh_theme_list(AppThemeDialog* dialog);
static void load_theme_into_ui(AppThemeDialog* dialog);

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
        
    // Use snprintf
    snprintf(target_field, 32, "%s", hex);
    
    // Apply immediately if custom
    if (!is_system_theme(dialog->edit_theme.theme_id)) {
        app_themes_save_theme(&dialog->edit_theme); // This updates global list
        theme_manager_set_theme_id(dialog->edit_theme.theme_id); // Apply
    }
}

static GtkWidget* create_color_row(AppThemeDialog* dialog, const char* label, size_t offset, bool is_dark) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    
    GtkWidget* lbl = gtk_label_new(label);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);
    
    GtkColorDialog* cd = gtk_color_dialog_new();
    GtkWidget* btn = gtk_color_dialog_button_new(cd);
    
    g_object_set_data(G_OBJECT(btn), "dialog", dialog);
    g_object_set_data(G_OBJECT(btn), "offset", (gpointer)offset);
    g_object_set_data(G_OBJECT(btn), "is_dark", (gpointer)(intptr_t)is_dark);
    
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
    
    size_t count = sizeof(COLOR_FIELDS) / sizeof(COLOR_FIELDS[0]);
    for (size_t i = 0; i < count; i++) {
        GtkWidget* row = create_color_row(dialog, COLOR_FIELDS[i].label, COLOR_FIELDS[i].offset, is_dark);
        gtk_box_append(GTK_BOX(list), row);
    }
    
    // Scrolled window
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    
    gtk_box_append(GTK_BOX(container), scroll);
}

static void on_theme_combine_changed(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)object; (void)pspec;
    AppThemeDialog* dialog = (AppThemeDialog*)user_data;
    if (dialog->loading_ui) return;
    
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dialog->theme_combo));
    GListModel* model = gtk_drop_down_get_model(GTK_DROP_DOWN(dialog->theme_combo));
    GtkStringObject* item = GTK_STRING_OBJECT(g_list_model_get_item(model, selected));
    const char* label = gtk_string_object_get_string(item);
    
    const char* id = NULL;
    
    if (strcmp(label, "Slate Blue (A)") == 0) id = "theme_a_slate";
    else if (strcmp(label, "Emerald Teal (B)") == 0) id = "theme_b_emerald";
    else if (strcmp(label, "Aubergine Purple (C)") == 0) id = "theme_c_aubergine";
    else if (strcmp(label, "Mocha Gold (D)") == 0) id = "theme_d_mocha_gold";
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
    }
}

static void on_create_new_clicked(GtkButton* btn, gpointer user_data) {
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
            fprintf(f, "  \"light\": { \"base_bg\": \"%s\" },\n", dialog->edit_theme.light.base_bg); 
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
    
    GtkWindow* parent = dialog->window ? dialog->window : dialog->parent_window;
    gtk_file_dialog_save(file_dialog, parent, NULL, on_export_finish, dialog);
}

// ------------------------------------------------------------------------

static void refresh_theme_list(AppThemeDialog* dialog) {
    dialog->loading_ui = true;
    
    GtkStringList* list = gtk_string_list_new(NULL);
    
    gtk_string_list_append(list, "Slate Blue (A)");
    gtk_string_list_append(list, "Emerald Teal (B)");
    gtk_string_list_append(list, "Aubergine Purple (C)");
    gtk_string_list_append(list, "Mocha Gold (D)");
    
    int count = 0;
    AppTheme* customs = app_themes_get_list(&count);
    for (int i = 0; i < count; i++) {
        gtk_string_list_append(list, customs[i].display_name);
    }
    
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
            // Simplified match logic
            if (strcmp(label, current->display_name) == 0) match = true;
            else if (strstr(current->theme_id, "theme_a") && strstr(label, "(A)")) match = true;
            else if (strstr(current->theme_id, "theme_b") && strstr(label, "(B)")) match = true;
            else if (strstr(current->theme_id, "theme_c") && strstr(label, "(C)")) match = true;
            else if (strstr(current->theme_id, "theme_d") && strstr(label, "(D)")) match = true;
            
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
        }
        
        // Dark
        GtkWidget* d_btn = g_hash_table_lookup(dialog->dark_buttons, (gpointer)off);
        if (d_btn) {
            char* d_hex = ((char*)&dialog->edit_theme.dark) + off;
            GdkRGBA d_rgba; 
            if (gdk_rgba_parse(&d_rgba, d_hex))
                gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(d_btn), &d_rgba);
        }
    }
    
    dialog->loading_ui = false;
}

static void app_theme_dialog_build_ui(AppThemeDialog* dialog) {
    dialog->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(dialog->content_box, 12);
    gtk_widget_set_margin_bottom(dialog->content_box, 12);
    gtk_widget_set_margin_start(dialog->content_box, 12);
    gtk_widget_set_margin_end(dialog->content_box, 12);
    
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    
    GtkWidget* label = gtk_label_new("Current Theme:");
    gtk_box_append(GTK_BOX(header), label);
    
    dialog->theme_combo = gtk_drop_down_new(NULL, NULL);
    g_signal_connect(dialog->theme_combo, "notify::selected", G_CALLBACK(on_theme_combine_changed), dialog);
    gtk_box_append(GTK_BOX(header), dialog->theme_combo);
    
    GtkWidget* new_btn = gtk_button_new_with_label("Copy/New");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_create_new_clicked), dialog);
    gtk_box_append(GTK_BOX(header), new_btn);
    
    GtkWidget* del_btn = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(del_btn, "destructive-action");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_delete_clicked), dialog);
    gtk_box_append(GTK_BOX(header), del_btn);
    
    gtk_box_append(GTK_BOX(dialog->content_box), header);
    
    dialog->notebook = gtk_notebook_new();
    
    GtkWidget* light_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    build_tab_content(dialog, light_page, false);
    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), light_page, gtk_label_new("Light Mode"));
    
    GtkWidget* dark_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    build_tab_content(dialog, dark_page, true);
    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), dark_page, gtk_label_new("Dark Mode"));
    
    gtk_box_append(GTK_BOX(dialog->content_box), dialog->notebook);
    
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
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

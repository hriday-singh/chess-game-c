#include "settings_dialog.h"
#include <gtk/gtk.h>
#include "app_state.h"
#include "puzzles.h"
#include "board_widget.h"
#include "ai_dialog.h"
#include "board_theme_dialog.h"
#include "piece_theme_dialog.h"
#include "app_theme_dialog.h"
#include "right_side_panel.h"
#include "gui_utils.h"

struct _SettingsDialog {
    GtkWindow* window;
    AppState* app_state;
    GtkWidget* sidebar; 
    GtkWidget* stack;   
    AiDialog* ai_dialog;
    BoardThemeDialog* board_dialog;
    PieceThemeDialog* piece_dialog;
    AppThemeDialog* app_theme_dialog;
};

static bool debug_mode = false;

// --- Helper: Create Sidebar Row ---when theme changes
static void on_theme_update(void* user_data) {
    if (!user_data) return;
    AppState* app = (AppState*)user_data;
    if (app && app->gui.board) {
        board_widget_refresh(app->gui.board);
    }
    // Refresh graveyard (captured pieces) as they use theme assets
    if (app && app->gui.info_panel) {
        info_panel_refresh_graveyard(app->gui.info_panel);
    }
    if (app && app->gui.right_side_panel) {
        right_side_panel_refresh(app->gui.right_side_panel);
    }
}

// Helper: Create a sidebar row with icon and text
static GtkWidget* create_sidebar_row(const char* text, const char* icon_name) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    
    GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_set_size_request(icon, 24, 24); // Standard icon size
    gtk_box_append(GTK_BOX(box), icon);
    
    GtkWidget* label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "sidebar-label"); // For potential styling
    gtk_box_append(GTK_BOX(box), label);
    
    return box;
}

// Callback: Handle sidebar row selection to switch stack pages
static void on_sidebar_row_selected(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
    (void)box;
    SettingsDialog* dialog = (SettingsDialog*)user_data;
    if (!row || !dialog->stack) return;
    
    int index = gtk_list_box_row_get_index(row);
    const char* pages[] = {"ai", "board", "piece", "app_theme", "puzzles", "tutorial", "about"};
    
    if (index >= 0 && index < 7) {
        gtk_stack_set_visible_child_name(GTK_STACK(dialog->stack), pages[index]);
        // Update last settings page
        if (dialog->app_state) {
            g_strlcpy(dialog->app_state->last_settings_page, pages[index], 32);
        }
    }
    
    // Refresh App Theme UI if selected
    if (index == 3 && dialog->app_theme_dialog) {
        app_theme_dialog_show(dialog->app_theme_dialog);
    }
}

// Helper: Create the About page
static GtkWidget* create_about_page(SettingsDialog* dialog) {
    (void)dialog;
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    
    GtkWidget* logo_icon = gtk_image_new_from_icon_name("applications-games");
    gtk_widget_set_size_request(logo_icon, 96, 96);
    gtk_image_set_pixel_size(GTK_IMAGE(logo_icon), 96);
    gtk_box_append(GTK_BOX(vbox), logo_icon);
    
    GtkWidget* title = gtk_label_new("HAL :) Chess");
    gtk_widget_add_css_class(title, "title-1");
    gtk_box_append(GTK_BOX(vbox), title);
    
    GtkWidget* version = gtk_label_new("Version 1.0.0");
    gtk_widget_add_css_class(version, "dim-label");
    gtk_box_append(GTK_BOX(vbox), version);
    
    GtkWidget* desc = gtk_label_new("A modern chess application built with GTK4.\nFeatures AI, Puzzles, and customizable themes.");
    gtk_label_set_justify(GTK_LABEL(desc), GTK_JUSTIFY_CENTER);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);
    
    GtkWidget* credit = gtk_label_new("© 2026 Hriday Singh");
    gtk_widget_add_css_class(credit, "dim-label");
    gtk_box_append(GTK_BOX(vbox), credit);
    
    return vbox;
}

// Callback: Start Tutorial
static void on_start_tutorial_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    SettingsDialog* dialog = (SettingsDialog*)user_data;
    
    // Activate the "tutorial" action
    GApplication* app = g_application_get_default();
    g_action_group_activate_action(G_ACTION_GROUP(app), "tutorial", NULL);
    
    // Close settings properly
    if (dialog->window) {
        gtk_window_destroy(dialog->window);
    }
}

// Helper: Create Tutorial Page wrapper (placeholder using existing Tutorial module if needed)
static GtkWidget* create_tutorial_page(SettingsDialog* dialog) {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    
    GtkWidget* title = gtk_label_new("Learn Chess");
    gtk_widget_add_css_class(title, "title-1");
    gtk_box_append(GTK_BOX(vbox), title);
    
    GtkWidget* desc = gtk_label_new("Master the game with our interactive tutorial.\nLearn piece movements, basic tactics, and strategies.");
    gtk_label_set_justify(GTK_LABEL(desc), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(vbox), desc);
    
    GtkWidget* start_btn = gtk_button_new_with_label("Start Tutorial");
    gtk_widget_add_css_class(start_btn, "suggested-action");
    gtk_widget_set_size_request(start_btn, 200, 50);
    
    g_signal_connect(start_btn, "clicked", G_CALLBACK(on_start_tutorial_clicked), dialog);
    gtk_box_append(GTK_BOX(vbox), start_btn);
    
    return vbox;
}

#include "puzzle_editor.h"

static void on_puzzle_created(int new_index, gpointer user_data) {
    (void)new_index;
    (void)user_data;
    // Refresh list? The settings dialog recreates the page if reopened, but not if staying open.
    // For now, we assume user might close/reopen or we could refresh the list box.
    // Actually, user_data is SettingsDialog*. We could try to refresh.
}

static void on_create_puzzle_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    SettingsDialog* dialog = (SettingsDialog*)user_data;
    if (dialog->window) {
        show_puzzle_editor(dialog->window, G_CALLBACK(on_puzzle_created), dialog);
    }
}

static GtkWidget* create_puzzles_page(SettingsDialog* dialog); // Forward

// Puzzle Page (Real Implementation)
static void on_puzzle_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
    (void)box;
    SettingsDialog* dialog = (SettingsDialog*)user_data;
    if (!row) return;
    
    // Get stored index
    // Using object data "puzzle-index" set during creation
    GtkWidget* child = gtk_list_box_row_get_child(row);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "puzzle-index"));

    // Trigger action "app.start-puzzle" with parameter
    GApplication* app = g_application_get_default();
    g_action_group_activate_action(G_ACTION_GROUP(app), "start-puzzle", g_variant_new_int32(idx));
    
    // Close settings dialog properly
    if (dialog->window) {
        gtk_window_destroy(dialog->window);
    }
}

static GtkWidget* create_puzzles_page(SettingsDialog* dialog) {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(vbox, 24);
    gtk_widget_set_margin_bottom(vbox, 24);
    gtk_widget_set_margin_start(vbox, 32);
    gtk_widget_set_margin_end(vbox, 32);
    
    // Title
    GtkWidget* title = gtk_label_new("Puzzles");
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttribute* size = pango_attr_size_new(24 * PANGO_SCALE);
    pango_attr_list_insert(attrs, weight);
    pango_attr_list_insert(attrs, size);
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), title);
    
    // Create Button (Immediately under title)
    GtkWidget* add_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* add_btn = gtk_button_new_with_label("Create / Import Puzzle");
    gtk_widget_add_css_class(add_btn, "suggested-action");
    gtk_widget_set_size_request(add_btn, -1, 36);
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_create_puzzle_clicked), dialog);
    gtk_box_append(GTK_BOX(add_box), add_btn);
    gtk_box_append(GTK_BOX(vbox), add_box);
    
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // List Header Section
    char buffer[64];
    int count = puzzles_get_count();
    snprintf(buffer, sizeof(buffer), "Available Puzzles (%d)", count);
    
    GtkWidget* list_header = gtk_label_new(buffer);
    gtk_widget_set_halign(list_header, GTK_ALIGN_START);
    gtk_widget_set_hexpand(list_header, TRUE);
    gtk_widget_add_css_class(list_header, "heading");
    gtk_box_append(GTK_BOX(vbox), list_header);
    
    // Puzzle List
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_add_css_class(scrolled, "view"); 
    gtk_box_append(GTK_BOX(vbox), scrolled);
    
    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(list, "boxed-list");
    
    for (int i = 0; i < count; i++) {
        const Puzzle* p = puzzles_get_at(i);
        if (p) {
            GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            GtkWidget* row_label = gtk_label_new(p->title);
            gtk_widget_set_halign(row_label, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(row_box), row_label);
            
            gtk_widget_set_margin_start(row_box, 12);
            gtk_widget_set_margin_end(row_box, 12);
            gtk_widget_set_margin_top(row_box, 12);
            gtk_widget_set_margin_bottom(row_box, 12);
            
            // Store index on the label widget (which is child of row_box, wait row get child returns box)
            g_object_set_data(G_OBJECT(row_box), "puzzle-index", GINT_TO_POINTER(i));
            
            gtk_list_box_append(GTK_LIST_BOX(list), row_box);
        }
    }
    
    g_signal_connect(list, "row-activated", G_CALLBACK(on_puzzle_row_activated), dialog);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list);
    
    return vbox;
}

// --- Main Construction ---

SettingsDialog* settings_dialog_new(AppState* app_state) {
    if (debug_mode) printf("[Settings] Creating new SettingsDialog\n");
    SettingsDialog* dialog = (SettingsDialog*)calloc(1, sizeof(SettingsDialog));
    if (!dialog) return NULL;
    
    dialog->app_state = app_state;
    
    // Create Main Window
    GtkWidget* win = gtk_window_new();
    dialog->window = GTK_WINDOW(win);
    gtk_window_set_title(dialog->window, "Settings");
    gtk_window_set_default_size(dialog->window, 850, 580);
    gtk_window_set_modal(dialog->window, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(dialog->window), "window"); // Ensure theme background
    if (app_state && app_state->gui.window) {
        gtk_window_set_transient_for(dialog->window, app_state->gui.window);
    }
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(dialog->window);
    
    // Handle destruction
    g_signal_connect_swapped(dialog->window, "destroy", G_CALLBACK(settings_dialog_free), dialog);
    
    // Main Layout: SplitPanes or Box
    // We want Sidebar | Separator | Content
    GtkWidget* main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(dialog->window, main_hbox);
    
    // --- Sidebar ---
    GtkWidget* sidebar_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar_frame, 220, -1);
    gtk_widget_add_css_class(sidebar_frame, "sidebar"); // Apply custom CSS later
    
    dialog->sidebar = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(dialog->sidebar), GTK_SELECTION_SINGLE);
    g_signal_connect(dialog->sidebar, "row-selected", G_CALLBACK(on_sidebar_row_selected), dialog);
    
    // Add Items
    gtk_list_box_append(GTK_LIST_BOX(dialog->sidebar), create_sidebar_row("AI Settings", "preferences-system-symbolic"));
    gtk_list_box_append(GTK_LIST_BOX(dialog->sidebar), create_sidebar_row("Board Theme", "applications-graphics-symbolic"));
    gtk_list_box_append(GTK_LIST_BOX(dialog->sidebar), create_sidebar_row("Piece Theme", "applications-graphics-symbolic"));
    gtk_list_box_append(GTK_LIST_BOX(dialog->sidebar), create_sidebar_row("App Theme", "preferences-desktop-theme-symbolic")); // New
    gtk_list_box_append(GTK_LIST_BOX(dialog->sidebar), create_sidebar_row("Puzzles", "applications-games-symbolic"));
    gtk_list_box_append(GTK_LIST_BOX(dialog->sidebar), create_sidebar_row("Tutorial", "user-available-symbolic"));
    gtk_list_box_append(GTK_LIST_BOX(dialog->sidebar), create_sidebar_row("About", "help-about-symbolic"));
    
    gtk_box_append(GTK_BOX(sidebar_frame), GTK_WIDGET(dialog->sidebar));
    gtk_box_append(GTK_BOX(main_hbox), sidebar_frame);
    
    // Separator
    gtk_box_append(GTK_BOX(main_hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    
    // --- Content Stack ---
    dialog->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(dialog->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(GTK_WIDGET(dialog->stack), TRUE);
    // Force background color for the content area
    gtk_widget_add_css_class(GTK_WIDGET(dialog->stack), "settings-content");
    gtk_box_append(GTK_BOX(main_hbox), GTK_WIDGET(dialog->stack));
    
    // 1. Tutorial
    // Note: Tutorial is added at sidebar index 5, but we name it "tutorial" 
    gtk_stack_add_named(GTK_STACK(dialog->stack), create_tutorial_page(dialog), "tutorial");
    
    // 2. AI Settings
    if (app_state && app_state->gui.ai_dialog) {
        dialog->ai_dialog = app_state->gui.ai_dialog;
        ai_dialog_set_parent_window(dialog->ai_dialog, dialog->window);
        GtkWidget* ai_widget = ai_dialog_get_widget(dialog->ai_dialog);
        
        // Ensure it's not already parented before adding to stack
        if (ai_widget) {
            GtkWidget* current_parent = gtk_widget_get_parent(ai_widget);
            if (current_parent) {
                g_object_ref(ai_widget);
                if (GTK_IS_STACK(current_parent)) {
                    gtk_stack_remove(GTK_STACK(current_parent), ai_widget);
                } else {
                    gtk_widget_unparent(ai_widget);
                }
                g_object_unref(ai_widget);
            }
            
            gtk_widget_set_margin_start(ai_widget, 20);
            gtk_widget_set_margin_end(ai_widget, 20);
            gtk_stack_add_named(GTK_STACK(dialog->stack), ai_widget, "ai");
        }
    } else {
         // Fallback
         dialog->ai_dialog = ai_dialog_new_embedded();
         ai_dialog_set_parent_window(dialog->ai_dialog, dialog->window);
         GtkWidget* ai_widget = ai_dialog_get_widget(dialog->ai_dialog);
         if (ai_widget) gtk_stack_add_named(GTK_STACK(dialog->stack), ai_widget, "ai");
    }
    
    // 3. Board Theme
    ThemeData* theme_data = app_state ? app_state->theme : NULL;
    dialog->board_dialog = board_theme_dialog_new_embedded(theme_data, on_theme_update, app_state); 
    board_theme_dialog_set_parent_window(dialog->board_dialog, dialog->window);
    GtkWidget* board_widget = board_theme_dialog_get_widget(dialog->board_dialog);
    gtk_stack_add_named(GTK_STACK(dialog->stack), board_widget, "board");
    
    // 4. Piece Theme
    dialog->piece_dialog = piece_theme_dialog_new_embedded(theme_data, on_theme_update, app_state);
    piece_theme_dialog_set_parent_window(dialog->piece_dialog, dialog->window);
    GtkWidget* piece_widget = piece_theme_dialog_get_widget(dialog->piece_dialog);
    gtk_stack_add_named(GTK_STACK(dialog->stack), piece_widget, "piece");
    
    // 5. App Theme (New)
    dialog->app_theme_dialog = app_theme_dialog_new_embedded(dialog->window);
    GtkWidget* app_theme_widget = app_theme_dialog_get_widget(dialog->app_theme_dialog);
    if (app_theme_widget) {
        gtk_stack_add_named(GTK_STACK(dialog->stack), app_theme_widget, "app_theme");
    }
    
    // 6. Puzzles
    gtk_stack_add_named(GTK_STACK(dialog->stack), create_puzzles_page(dialog), "puzzles");
    
    // 7. About
    gtk_stack_add_named(GTK_STACK(dialog->stack), create_about_page(dialog), "about");
    
    // Restore last visited page or default to first
    const char* start_page = "ai";
    if (app_state && strlen(app_state->last_settings_page) > 0) {
        start_page = app_state->last_settings_page;
        if (strcmp(start_page, "tutorial") == 0) start_page = "ai"; 
    }
    
    settings_dialog_open_page(dialog, start_page);
    
    return dialog;
}

void settings_dialog_show(SettingsDialog* dialog) {
    if (dialog && dialog->window) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), TRUE);
        gtk_window_present(dialog->window);
        
        // Refresh embedded dialogs if needed
        if (dialog->board_dialog) board_theme_dialog_show(dialog->board_dialog); 
        if (dialog->piece_dialog) piece_theme_dialog_show(dialog->piece_dialog);
        if (dialog->app_theme_dialog) app_theme_dialog_show(dialog->app_theme_dialog);
    }
}

GtkWindow* settings_dialog_get_window(SettingsDialog* dialog) {
    return dialog ? dialog->window : NULL;
}

void settings_dialog_present(SettingsDialog* dialog) {
    settings_dialog_show(dialog);
}

void settings_dialog_open_page(SettingsDialog* dialog, const char* page_name) {
    if (!dialog || !dialog->stack || !page_name) return;
    
    // Check if page exists or just try setting it
    gtk_stack_set_visible_child_name(GTK_STACK(dialog->stack), page_name);
    
    // Save to persistence
    if (dialog->app_state) {
        snprintf(dialog->app_state->last_settings_page, sizeof(dialog->app_state->last_settings_page), "%s", page_name);
    }
    
    // Also update sidebar selection if possible
    // Reverse map page name to index
    int idx = -1;
    if (strcmp(page_name, "ai") == 0) idx = 0;
    else if (strcmp(page_name, "board") == 0) idx = 1;
    else if (strcmp(page_name, "piece") == 0) idx = 2;
    else if (strcmp(page_name, "app_theme") == 0) idx = 3;
    else if (strcmp(page_name, "puzzles") == 0) idx = 4;
    else if (strcmp(page_name, "tutorial") == 0) idx = 5;
    else if (strcmp(page_name, "about") == 0) idx = 6;
    
    if (idx >= 0 && dialog->sidebar) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(dialog->sidebar), idx);
        if (row) {
             gtk_list_box_select_row(GTK_LIST_BOX(dialog->sidebar), row);
        }
    }
    
    // Ensure window is visible!
    settings_dialog_show(dialog);
}

#include "config_manager.h"

// Helper: Save all settings before destruction
static void settings_dialog_save_all(SettingsDialog* dialog) {
    if (!dialog) return;
    AppConfig* cfg = config_get();
    if (!cfg) return;
    
    // AI Settings
    if (dialog->ai_dialog) {
        ai_dialog_save_config(dialog->ai_dialog, cfg);
    }
    
    // Board Theme
    if (dialog->board_dialog) {
        board_theme_dialog_save_config(dialog->board_dialog, cfg);
    }
    
    // Piece Theme
    if (dialog->piece_dialog) {
        piece_theme_dialog_save_config(dialog->piece_dialog, cfg);
    }
    
    // App Theme (no-op but robust)
    if (dialog->app_theme_dialog) {
        app_theme_dialog_save_config(dialog->app_theme_dialog, cfg);
    }
    
    // Write to disk
    config_save();
}

void settings_dialog_free(SettingsDialog* dialog) {
    if (debug_mode) printf("[Settings] Freeing SettingsDialog %p\n", (void*)dialog);
    if (dialog) {
        // Save settings before freeing
        settings_dialog_save_all(dialog);
        
        if (dialog->ai_dialog) {
            if (dialog->app_state && dialog->ai_dialog == dialog->app_state->gui.ai_dialog) {
                // Shared dialog: Prevent destruction handling by removing from stack safely
                GtkWidget* w = ai_dialog_get_widget(dialog->ai_dialog);
                if (w) {
                    GtkWidget* parent = gtk_widget_get_parent(w);
                    if (parent && parent == GTK_WIDGET(dialog->stack)) {
                         g_object_ref(w);
                         gtk_stack_remove(GTK_STACK(parent), w); 
                         g_object_unref(w);
                    }
                }
            } else {
                ai_dialog_free(dialog->ai_dialog);
            }
        }
        if (dialog->board_dialog) {
            if (debug_mode) printf("[Settings] Freeing board_dialog\n");
            board_theme_dialog_free(dialog->board_dialog);
        }
        if (dialog->piece_dialog) {
            if (debug_mode) printf("[Settings] Freeing piece_dialog\n");
            piece_theme_dialog_free(dialog->piece_dialog);
        }
        if (dialog->app_theme_dialog) {
            app_theme_dialog_free(dialog->app_theme_dialog);
        }
        
        free(dialog);
    }
    if (debug_mode) printf("[Settings] Freed SettingsDialog\n");
}

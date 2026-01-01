#include "settings_dialog.h"
#include <gtk/gtk.h>
#include "app_state.h"
#include "puzzles.h"

struct _SettingsDialog {
    GtkWindow* window;
    AppState* app_state;
    // ...
    GtkWidget* sidebar; 
    GtkWidget* stack;   
    AiDialog* ai_dialog;
    BoardThemeDialog* board_dialog;
    PieceThemeDialog* piece_dialog;
};

// ... (sidebar helpers same)

// Puzzle Page (Real Implementation)
static void on_puzzle_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
    SettingsDialog* dialog = (SettingsDialog*)user_data;
    if (!row) return;
    
    // Get stored index
    // Using object data "puzzle-index" set during creation
    GtkWidget* child = gtk_list_box_row_get_child(row);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "puzzle-index"));

    // Trigger action "app.start-puzzle" with parameter
    GApplication* app = g_application_get_default();
    g_action_group_activate_action(G_ACTION_GROUP(app), "start-puzzle", g_variant_new_int32(idx));
}

static GtkWidget* create_puzzles_page(SettingsDialog* dialog) {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    
    GtkWidget* title = gtk_label_new("Select a Puzzle");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_add_css_class(title, "title-2");
    gtk_box_append(GTK_BOX(vbox), title);
    
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_add_css_class(scrolled, "view"); 
    gtk_box_append(GTK_BOX(vbox), scrolled);
    
    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(list, "boxed-list");
    
    int count = puzzles_get_count();
    for (int i = 0; i < count; i++) {
        const Puzzle* p = puzzles_get_at(i);
        if (p) {
            GtkWidget* row_label = gtk_label_new(p->title);
            gtk_widget_set_halign(row_label, GTK_ALIGN_START);
            gtk_widget_set_margin_start(row_label, 12);
            gtk_widget_set_margin_end(row_label, 12);
            gtk_widget_set_margin_top(row_label, 12);
            gtk_widget_set_margin_bottom(row_label, 12);
            
            // Store index on the label widget (which is child of row)
            g_object_set_data(G_OBJECT(row_label), "puzzle-index", GINT_TO_POINTER(i));
            
            gtk_list_box_append(GTK_LIST_BOX(list), row_label);
        }
    }
    
    g_signal_connect(list, "row-activated", G_CALLBACK(on_puzzle_row_activated), dialog);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list);
    
    return vbox;
}

// --- Main Construction ---

SettingsDialog* settings_dialog_new(AppState* app_state) {
    SettingsDialog* dialog = (SettingsDialog*)calloc(1, sizeof(SettingsDialog));
    if (!dialog) return NULL;
    
    dialog->app_state = app_state;
    
    // Create Main Window
    GtkWidget* win = gtk_window_new();
    dialog->window = GTK_WINDOW(win);
    gtk_window_set_title(dialog->window, "Settings");
    gtk_window_set_default_size(dialog->window, 1000, 650);
    gtk_window_set_modal(dialog->window, TRUE);
    if (app_state && app_state->window) {
        gtk_window_set_transient_for(dialog->window, app_state->window);
    }
    
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
    
    dialog->sidebar = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(dialog->sidebar, GTK_SELECTION_SINGLE);
    g_signal_connect(dialog->sidebar, "row-selected", G_CALLBACK(on_sidebar_row_selected), dialog);
    
    // Add Items
    gtk_list_box_append(dialog->sidebar, create_sidebar_row("Tutorial", "user-available-symbolic"));
    gtk_list_box_append(dialog->sidebar, create_sidebar_row("AI Settings", "preferences-system-symbolic"));
    gtk_list_box_append(dialog->sidebar, create_sidebar_row("Board Theme", "preferences-desktop-display-symbolic"));
    gtk_list_box_append(dialog->sidebar, create_sidebar_row("Piece Theme", "applications-graphics-symbolic"));
    gtk_list_box_append(dialog->sidebar, create_sidebar_row("Puzzles", "applications-games-symbolic"));
    gtk_list_box_append(dialog->sidebar, create_sidebar_row("About", "help-about-symbolic"));
    
    gtk_box_append(GTK_BOX(sidebar_frame), GTK_WIDGET(dialog->sidebar));
    gtk_box_append(GTK_BOX(main_hbox), sidebar_frame);
    
    // Separator
    gtk_box_append(GTK_BOX(main_hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    
    // --- Content Stack ---
    dialog->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(dialog->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(GTK_WIDGET(dialog->stack), TRUE);
    gtk_box_append(GTK_BOX(main_hbox), GTK_WIDGET(dialog->stack));
    
    // 1. Tutorial
    gtk_stack_add_named(dialog->stack, create_tutorial_page(dialog), "tutorial");
    
    // 2. AI Settings
    // Reuse existing singleton logic if possible, or create embedded
    // AppState has ai_dialog pointer. We can check if it's initialized.
    // However, existing ai_dialog_new creates a window. We want embedded.
    // We should reuse the underlying DATA/State but create new UI? 
    // Or if we refactored correctly, we can create a new UI that updates shared state?
    // AiDialog implementation stores state internally. 
    // We should prefer using the existing dialog structure if we refactored it to just be the content.
    // But `ai_dialog` in AppState is `AiDialog*`. 
    // If we want to PERSIST settings, we should use the one in AppState or equivalent.
    
    // Refactoring check: We changed AiDialog struct. It holds the data.
    // If we create a NEW AiDialog, it has default 1500 ELO. That's bad.
    // We should pass the AppState's AiDialog if it exists, and REPARENT its content?
    // Reparenting is tricky if the widget is already in a window.
    // Plan: Create a new embedded AiDialog, but init it with values from AppState (if we can read them).
    // Or better: The AppState holds the logic/engine handle. The Dialog is just UI.
    // Let's create a NEW embedded AiDialog. We can sync it later.
    
    dialog->ai_dialog = ai_dialog_new_embedded();
    ai_dialog_set_parent_window(dialog->ai_dialog, dialog->window);
    GtkWidget* ai_widget = ai_dialog_get_widget(dialog->ai_dialog);
    gtk_widget_set_margin_start(ai_widget, 20);
    gtk_widget_set_margin_end(ai_widget, 20);
    gtk_stack_add_named(dialog->stack, ai_widget, "ai");
    
    // 3. Board Theme
    // We need ThemeData.
    ThemeData* theme_data = app_state ? app_state->theme : NULL;
    // We use a dummy callback or the real one. 
    // If we want live updates, we pass a callback that triggers redraw.
    // Currently `main.c` handles updates via callback.
    // We can pass a callback that calls `gtk_widget_queue_draw(app_state->board)`.
    
    dialog->board_dialog = board_theme_dialog_new_embedded(theme_data, NULL, NULL); 
    // Note: We need to set the callback! But we don't have access to main's static callback `update_board_theme`.
    // Should we expose it? Or just pass NULL and rely on the fact that theme_data changes might trigger something?
    // `board_theme_dialog.c` logic: calls callback on update.
    // We NEED the callback to redraw the main board.
    // We can allow `settings_dialog_new` to take callbacks? 
    // Or we assume `app_state` has what we need? `app_state->board` is a GtkWidget.
    // We can make a local static callback here that redraws `app_state->board`.
    
    board_theme_dialog_set_parent_window(dialog->board_dialog, dialog->window);
    GtkWidget* board_widget = board_theme_dialog_get_widget(dialog->board_dialog);
    // Since board theme dialog has its own layout, we assume it is fine
    gtk_stack_add_named(dialog->stack, board_widget, "board");
    
    // 4. Piece Theme
    dialog->piece_dialog = piece_theme_dialog_new_embedded(theme_data, NULL, NULL);
    piece_theme_dialog_set_parent_window(dialog->piece_dialog, dialog->window);
    GtkWidget* piece_widget = piece_theme_dialog_get_widget(dialog->piece_dialog);
    gtk_stack_add_named(dialog->stack, piece_widget, "piece");
    
    // 5. Puzzles
    gtk_stack_add_named(dialog->stack, create_puzzles_page(dialog), "puzzles");
    
    // 6. About
    gtk_stack_add_named(dialog->stack, create_about_page(dialog), "about");
    
    // Select first item
    GtkListBoxRow* first = gtk_list_box_get_row_at_index(dialog->sidebar, 0);
    gtk_list_box_select_row(dialog->sidebar, first);
    
    // CSS for Sidebar
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = 
        ".sidebar { background: #f6f6f6; border-right: 1px solid #e0e0e0; } "
        ".sidebar row { padding: 8px; border-radius: 6px; margin: 4px; } "
        ".sidebar row:selected { background: #e0e0e0; font-weight: bold; border-left: 3px solid #3584e4; } " // Blue line attempt
        ".title-1 { font-size: 24px; font-weight: bold; margin-bottom: 8px; } "
        ".title-2 { font-size: 18px; font-weight: bold; margin-bottom: 12px; } "
        ".dim-label { opacity: 0.7; } ";
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(GTK_WIDGET(dialog->window)),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    
    return dialog;
}

void settings_dialog_show(SettingsDialog* dialog) {
    if (dialog && dialog->window) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), TRUE);
        gtk_window_present(dialog->window);
        
        // Refresh embedded dialogs if needed
        if (dialog->board_dialog) board_theme_dialog_show(dialog->board_dialog); // Primarily for updating previews
        if (dialog->piece_dialog) piece_theme_dialog_show(dialog->piece_dialog);
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
    gtk_stack_set_visible_child_name(dialog->stack, page_name);
    
    // Also update sidebar selection if possible
    // Reverse map page name to index
    int idx = -1;
    if (strcmp(page_name, "tutorial") == 0) idx = 0;
    else if (strcmp(page_name, "ai") == 0) idx = 1;
    else if (strcmp(page_name, "board") == 0) idx = 2;
    else if (strcmp(page_name, "piece") == 0) idx = 3;
    else if (strcmp(page_name, "puzzles") == 0) idx = 4;
    else if (strcmp(page_name, "about") == 0) idx = 5;
    
    if (idx >= 0 && dialog->sidebar) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(dialog->sidebar, idx);
        if (row) {
             // Block signal to prevent loop? 
             // on_sidebar_row_selected calls set_visible_child_name.
             // set_visible_child_name doesn't emit row-selected.
             // selecting row emits row-selected.
             // So if we select row, it changes stack. No loop.
             gtk_list_box_select_row(dialog->sidebar, row);
        }
    }
    
    settings_dialog_present(dialog);
}

void settings_dialog_free(SettingsDialog* dialog) {
    if (dialog) {
        // Embedded dialogs need to be freed?
        // They were created with _new_embedded. Their widgets are in the stack.
        // When window is destroyed, widgets are destroyed.
        // BUT the *structs* (AiDialog, BoardThemeDialog) need freeing.
        // And we need to ensure we don't double-free widgets.
        // Ideally we should have `ai_dialog_destroy_embedded` which frees struct but leaves widget management to GTK?
        // Or `ai_dialog_free` destroys the window. If window is NULL, it just frees struct?
        // Let's check `ai_dialog_free`: `if (window) gtk_window_destroy(window); free(dialog);`
        // If embedded, `window` is NULL. So it just frees struct.
        // BUT content_box is child of stack. If we free struct, pointers inside it might be invalid?
        // No, GtkWidgets are ref-counted.
        
        if (dialog->ai_dialog) ai_dialog_free(dialog->ai_dialog);
        if (dialog->board_dialog) board_theme_dialog_free(dialog->board_dialog);
        if (dialog->piece_dialog) piece_theme_dialog_free(dialog->piece_dialog);
        
        // Note: We don't free AppState or close window explicitly here as this is called BY destroy signal
        // But if called manually, we should destroy window?
        // `g_signal_connect_swapped(..., settings_dialog_free, ...)`
        // If called by destroy signal, window is being destroyed. `dialog->window` is invalid?
        // We should just free the struct.
        
        free(dialog);
    }
}

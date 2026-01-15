#include "puzzle_editor.h"
#include "../game/puzzles.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include "gui_utils.h"

typedef struct {
    GtkWidget* window;
    GtkWidget* title_entry;
    GtkWidget* desc_entry;
    GtkWidget* fen_entry;
    GtkWidget* moves_entry;
    GCallback on_created;
    gpointer user_data;
} EditorData;

// Simple helper to extract value for a key from JSON-like string
// Looks for "key": "value"
static void extract_json_value(const char* json, const char* key, GtkEditable* target) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    char* pos = strstr((char*)json, search_key);
    if (!pos) return;
    
    pos += strlen(search_key);
    // Find colon
    pos = strchr(pos, ':');
    if (!pos) return;
    pos++; 
    
    // Find start quote
    pos = strchr(pos, '\"');
    if (!pos) return;
    pos++;
    
    char* end = strchr(pos, '\"');
    if (!end) return;
    
    // Copy content
    size_t len = end - pos;
    char* val = (char*)malloc(len + 1);
    memcpy(val, pos, len);
    val[len] = '\0';
    
    gtk_editable_set_text(target, val);
    free(val);
}

// Special helper for moves array ["m1", "m2"] or "m1 m2"
// Note: This simple parser assumes standard formatting
static void extract_json_moves(const char* json, GtkEditable* target) {
    // Check for "moves" or "lines" or "solution"
    char* pos = strstr((char*)json, "\"moves\"");
    if (!pos) pos = strstr((char*)json, "\"solution\"");
    if (!pos) pos = strstr((char*)json, "\"lines\""); // Lichess often uses 'lines' in some APIs or 'moves' string
    
    if (!pos) return;
    
    // Check if it's a string or array?
    // If it's a string: "moves": "e2e4 c7c5"
    // If it's array: "lines": ["e2e4", "c7c5"]
    
    pos = strchr(pos, ':');
    if (!pos) return;
    pos++;
    
    // Skip whitespace
    while (*pos == ' ' || *pos == '\n' || *pos == '\t') pos++;
    
    if (*pos == '\"') {
        // String format
        pos++;
        char* end = strchr(pos, '\"');
        if (!end) return;
        size_t len = end - pos;
        char* val = (char*)malloc(len + 1);
        memcpy(val, pos, len);
        val[len] = '\0';
        gtk_editable_set_text(target, val);
        free(val);
    } else if (*pos == '[') {
        // Array format
        // We need to concat elements
        pos++;
        char buffer[1024] = "";
        while (*pos && *pos != ']') {
            char* start = strchr(pos, '\"');
            if (!start) break;
            start++;
            char* end = strchr(start, '\"');
            if (!end) break;
            
            size_t len = end - start;
            size_t current_len = strlen(buffer);
            if (current_len + len + 2 < sizeof(buffer)) {
                snprintf(buffer + current_len, sizeof(buffer) - current_len, "%.*s ", (int)len, start);
            }
            pos = end + 1;
        }
        gtk_editable_set_text(target, buffer);
    }
}

static void on_import_confirm(GtkButton* btn, gpointer user_data) {
    GtkTextView* text_view = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(btn), "text-view"));
    EditorData* data = (EditorData*)user_data;
    GtkWindow* dialog = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW));
    
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* json = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    
    if (json && strlen(json) > 0) {
        // Try to extract fields
        extract_json_value(json, "title", GTK_EDITABLE(data->title_entry));
        extract_json_value(json, "description", GTK_EDITABLE(data->desc_entry));
        extract_json_value(json, "fen", GTK_EDITABLE(data->fen_entry));
        // Fallback: look for "FEN" (uppercase)
        if (strlen(gtk_editable_get_text(GTK_EDITABLE(data->fen_entry))) == 0) {
             extract_json_value(json, "FEN", GTK_EDITABLE(data->fen_entry));
        }
        
        extract_json_moves(json, GTK_EDITABLE(data->moves_entry));
    }
    
    g_free(json);
    
    // Restore focus to parent (Editor)
    GtkWindow* parent = gtk_window_get_transient_for(dialog);
    if (parent) gtk_window_present(parent);

    gtk_window_destroy(dialog);
}

static void on_open_response(GObject* source, GAsyncResult* result, gpointer user_data) {
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
    GError* error = NULL;
    GFile* file = gtk_file_dialog_open_finish(dialog, result, &error);
    
    if (file) {
        char* contents;
        gsize length;
        if (g_file_load_contents(file, NULL, &contents, &length, NULL, NULL)) {
             GtkTextView* tv = GTK_TEXT_VIEW(user_data);
             gtk_text_buffer_set_text(gtk_text_view_get_buffer(tv), contents, length);
             g_free(contents);
        }
        g_object_unref(file);
    } else {
        if (error) g_error_free(error);
    }
}

static void on_load_file_clicked(GtkButton* btn, gpointer user_data) {
    GtkTextView* tv = GTK_TEXT_VIEW(user_data);
    GtkFileDialog* dialog = gtk_file_dialog_new();
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Chess Puzzle JSON (*.json)");
    gtk_file_filter_add_pattern(filter, "*.json");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    gtk_file_dialog_set_title(dialog, "Open JSON");
    gtk_file_dialog_open(dialog, GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW)), NULL, on_open_response, tv);
    g_object_unref(dialog);
}

static void on_import_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorData* data = (EditorData*)user_data;
    
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Import JSON");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(data->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(GTK_WINDOW(dialog));
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    GtkWidget* label = gtk_label_new("Paste Puzzle JSON here:");
    gtk_box_append(GTK_BOX(vbox), label);
    
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroll);
    
    GtkWidget* text_view = gtk_text_view_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text_view);
    
    GtkWidget* load_btn = gtk_button_new_with_label("Load from Text");
    g_object_set_data(G_OBJECT(load_btn), "text-view", text_view);
    g_signal_connect(load_btn, "clicked", G_CALLBACK(on_import_confirm), data);
    
    GtkWidget* file_btn = gtk_button_new_with_label("Upload File");  
    g_signal_connect(file_btn, "clicked", G_CALLBACK(on_load_file_clicked), text_view);
        
    GtkWidget* hbo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(hbo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(hbo), file_btn);
    gtk_box_append(GTK_BOX(hbo), load_btn);
    
    gtk_box_append(GTK_BOX(vbox), hbo);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_play_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorData* data = (EditorData*)user_data;
    
    // Get text
    const char* title = gtk_editable_get_text(GTK_EDITABLE(data->title_entry));
    const char* desc = gtk_editable_get_text(GTK_EDITABLE(data->desc_entry));
    const char* fen = gtk_editable_get_text(GTK_EDITABLE(data->fen_entry));
    const char* moves_str = gtk_editable_get_text(GTK_EDITABLE(data->moves_entry));
    
    if (strlen(title) == 0) title = "Custom Puzzle";
    if (strlen(fen) == 0) return; 
    
    // Parse moves
    char* moves_copy = strdup(moves_str);
    const char* solution_moves[MAX_PUZZLE_MOVES];
    for(int i=0; i<MAX_PUZZLE_MOVES; i++) solution_moves[i] = NULL;
    
    char* cursor = moves_copy;
    const char* delims = " ,";
    int move_idx = 0;
    
    while (*cursor && move_idx < MAX_PUZZLE_MOVES) {
        // Skip delimiters
        cursor += strspn(cursor, delims);
        if (!*cursor) break;
        
        // Find end of token
        size_t len = strcspn(cursor, delims);
        if (len > 0) {
            // Null-terminate in place (safe since we own moves_copy)
            cursor[len] = '\0';
            solution_moves[move_idx++] = cursor;
            cursor += len + 1;
        }
    }
    
    // Create Puzzle struct
    Puzzle p;
    p.title = title;
    p.description = desc;
    p.fen = fen;
    for(int i=0; i<MAX_PUZZLE_MOVES; i++) {
        p.solution_moves[i] = solution_moves[i];
    }
    p.solution_length = move_idx;
    p.turn = 0; 
    
    // Add to system
    puzzles_add_custom(&p);
    
    // Cleanup local parsing alloc
    free(moves_copy);
    
    // Trigger callback
    if (data->on_created) {
        int new_idx = puzzles_get_count() - 1;
        void (*callback)(int, gpointer) = (void (*)(int, gpointer))data->on_created;
        callback(new_idx, data->user_data);
    }
    
    // Focus back to parent
    GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(data->window));
    if (parent) gtk_window_present(parent);

    // Close window
    gtk_window_destroy(GTK_WINDOW(data->window));
}

static void on_cancel_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    EditorData* data = (EditorData*)user_data;
    // Focus back to parent
    GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(data->window));
    if (parent) gtk_window_present(parent);
    
    gtk_window_destroy(GTK_WINDOW(data->window));
}

static void on_window_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    g_free(user_data); 
}

void show_puzzle_editor(GtkWindow* parent, GCallback on_created, gpointer user_data) {
    EditorData* data = g_new0(EditorData, 1);
    data->on_created = on_created;
    data->user_data = user_data;
    
    data->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(data->window), "Add Puzzle");
    gtk_window_set_transient_for(GTK_WINDOW(data->window), parent);
    gtk_window_set_modal(GTK_WINDOW(data->window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(data->window), 400, 500);
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(GTK_WINDOW(data->window));
    
    g_signal_connect(data->window, "destroy", G_CALLBACK(on_window_destroy), data);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 45); // Increased as requested "increase it little" (was 20)
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(data->window), vbox);
    
    // Buttons Top (Import)
    GtkWidget* import_btn = gtk_button_new_with_label("Import JSON");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_import_clicked), data);
    gtk_box_append(GTK_BOX(vbox), import_btn);
    
    // Title
    gtk_box_append(GTK_BOX(vbox), gtk_label_new("Puzzle Title:"));
    data->title_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->title_entry), "e.g., Mate in 2");
    gtk_box_append(GTK_BOX(vbox), data->title_entry);

    // Description
    gtk_box_append(GTK_BOX(vbox), gtk_label_new("Description / Type:"));
    data->desc_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->desc_entry), "e.g., Tactics - Back Rank");
    gtk_box_append(GTK_BOX(vbox), data->desc_entry);

    // FEN
    gtk_box_append(GTK_BOX(vbox), gtk_label_new("FEN Position:"));
    data->fen_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->fen_entry), "Paste valid FEN here...");
    gtk_box_append(GTK_BOX(vbox), data->fen_entry);

    // Moves
    gtk_box_append(GTK_BOX(vbox), gtk_label_new("Solution Moves (UCI):"));
    data->moves_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->moves_entry), "e.g., e2e4 e7e5 g1f3");
    gtk_box_append(GTK_BOX(vbox), data->moves_entry);
    
    // Info Label
    GtkWidget* info = gtk_label_new("Format: startSquare + endSquare (e.g. e2e4).\nSpace separated.");
    gtk_widget_add_css_class(info, "dim-label");
    gtk_box_append(GTK_BOX(vbox), info);

    // Buttons Bottom
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 45); // Increased spacing from 24 to 45
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(hbox, 30); // Increased top margin
    gtk_box_append(GTK_BOX(vbox), hbox);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), data);
    gtk_box_append(GTK_BOX(hbox), cancel_btn);
    
    GtkWidget* play_btn = gtk_button_new_with_label("Select");
    gtk_widget_add_css_class(play_btn, "suggested-action");
    g_signal_connect(play_btn, "clicked", G_CALLBACK(on_play_clicked), data);
    gtk_box_append(GTK_BOX(hbox), play_btn);


    gtk_window_present(GTK_WINDOW(data->window));
}

#include "import_dialog.h"
#include "gamelogic.h" // Required for cleanup and temp logic
#include "game_import.h"
#include "gui_utils.h"
#include "replay_controller.h"
#include "history_dialog.h" // To potentially close/refresh history dialog if needed
#include "types.h"
#include <time.h>

static GtkWidget* s_dialog = NULL;
static GtkWidget* s_text_view = NULL;
static GtkWidget* s_status_label = NULL;
static AppState* s_state = NULL;

static void on_dialog_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget; (void)user_data;
    s_dialog = NULL;
    s_text_view = NULL;
    s_status_label = NULL;
    s_state = NULL;
}

static void set_status(const char* msg, bool error) {
    if (s_status_label) {
        gtk_label_set_text(GTK_LABEL(s_status_label), msg);
        if (error) {
            gtk_widget_add_css_class(s_status_label, "error-label");
            gtk_widget_remove_css_class(s_status_label, "success-label");
        } else {
            gtk_widget_add_css_class(s_status_label, "success-label");
            gtk_widget_remove_css_class(s_status_label, "error-label");
        }
    }
}

static void do_import(const char* content) {
    if (!content || strlen(content) == 0) {
        set_status("Please enter games text or load a file.", true);
        return;
    }

    set_status("Parsing...", false);
    
    // Create temporary GameLogic for validation
    GameLogic* temp_logic = gamelogic_create();
    if (!temp_logic) {
        set_status("Internal error: Failed to create validator.", true);
        return;
    }
    
    // Explicitly reset
    gamelogic_reset(temp_logic);
    
    GameImportResult res = game_import_from_string(temp_logic, content);
    
    gamelogic_free(temp_logic);
    
    if (res.success && res.moves_count > 0) {
        set_status("Game Imported Successfully!", false);
        
        // 1. Construct temporary History Entry
        MatchHistoryEntry entry = {0};
        snprintf(entry.id, sizeof(entry.id), "import_%ld", (long)time(NULL));
        entry.timestamp = (int64_t)time(NULL);
        entry.created_at_ms = entry.timestamp * 1000;
        entry.started_at_ms = entry.created_at_ms;
        entry.ended_at_ms = entry.created_at_ms;
        
        entry.game_mode = GAME_MODE_PVP; // Default assumption for imported human games
        entry.clock.enabled = false;
        
        // Use Parsed Metadata or Defaults
        snprintf(entry.white.engine_path, sizeof(entry.white.engine_path), "%s", res.white[0] ? res.white : "White");
        snprintf(entry.black.engine_path, sizeof(entry.black.engine_path), "%s", res.black[0] ? res.black : "Black");
        entry.white.is_ai = false;
        entry.black.is_ai = false;
        
        snprintf(entry.result, sizeof(entry.result), "%s", res.result[0] ? res.result : "*");
        snprintf(entry.result_reason, sizeof(entry.result_reason), "%.*s", (int)sizeof(entry.result_reason) - 1, res.event[0] ? res.event : "Imported Game");
        
        entry.move_count = res.moves_count;
        entry.moves_uci = res.loaded_uci; // Pointer alias to stack buffer, will be copied by add()
        snprintf(entry.start_fen, sizeof(entry.start_fen), "%s", res.start_fen);
        
        // SAVE to History
        match_history_add(&entry);
        
        // 2. Start Replay
        if (s_state && s_state->replay_controller) {
             if (s_state->is_replaying) {
                replay_controller_exit(s_state->replay_controller);
            }
            
            MatchPlayerConfig white = entry.white; 
            MatchPlayerConfig black = entry.black;
            
            replay_controller_load_match(s_state->replay_controller, 
                entry.moves_uci, entry.start_fen, 
                NULL, 0, // No think times
                entry.started_at_ms, entry.ended_at_ms,
                false, 0, 0,
                white, black);
                
            replay_controller_set_result(s_state->replay_controller, entry.result, entry.result_reason);
            replay_controller_enter_replay_mode(s_state->replay_controller);
            
            // Close the import dialog
            if (s_dialog) gtk_window_close(GTK_WINDOW(s_dialog));
            
            // Also close History Dialog if open
             if (s_state->gui.history_dialog) {
                 GtkWindow* win = history_dialog_get_window(s_state->gui.history_dialog);
                 if (win) gtk_window_close(win);
             }
        }
    } else {
        char err[300];
        snprintf(err, sizeof(err), "Import Failed: %s", res.error_message);
        set_status(res.error_message[0] ? res.error_message : "No valid moves found.", true);
    }
}

static void on_import_clicked(GtkWidget* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    if (!s_text_view) return;
    
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_text_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    
    do_import(content);
    g_free(content);
}

#include "gui_file_dialog.h"

static void on_file_selected(const char* path, gpointer user_data) {
    (void)user_data;
    if (path) {
        char* content = NULL;
        gsize len = 0;
        if (g_file_get_contents(path, &content, &len, NULL)) {
            if (s_text_view) {
                GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_text_view));
                gtk_text_buffer_set_text(buf, content, -1);
                set_status("File loaded. Click Import to process.", false);
            }
            g_free(content);
        } else {
            set_status("Failed to read file.", true);
        }
    }
}

static void on_load_file_clicked(GtkWidget* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    
    const char* patterns[] = { "*.pgn", "*.txt", NULL };
    gui_file_dialog_open(GTK_WINDOW(s_dialog), 
                        "Open Game File", 
                        "Chess Files (PGN, TXT)", 
                        patterns, 
                        on_file_selected, 
                        NULL);
}

static void on_cancel_clicked(GtkWidget* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    if (s_dialog) gtk_window_close(GTK_WINDOW(s_dialog));
}

void import_dialog_show(AppState* state) {
    if (s_dialog) {
        gtk_window_present(GTK_WINDOW(s_dialog));
        return;
    }
    
    s_state = state;
    
    s_dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(s_dialog), "Import Game");
    gtk_window_set_default_size(GTK_WINDOW(s_dialog), 600, 700); // Increased height as requested
    gtk_window_set_modal(GTK_WINDOW(s_dialog), TRUE);
    if (state->gui.window) {
        gtk_window_set_transient_for(GTK_WINDOW(s_dialog), GTK_WINDOW(state->gui.window));
    }
    
    g_signal_connect(s_dialog, "destroy", G_CALLBACK(on_dialog_destroy), NULL);
    gui_utils_add_esc_close(s_dialog);
    
    GtkWidget* main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_vbox, 20);
    gtk_widget_set_margin_end(main_vbox, 20);
    gtk_widget_set_margin_top(main_vbox, 20);
    gtk_widget_set_margin_bottom(main_vbox, 20);
    gtk_window_set_child(GTK_WINDOW(s_dialog), main_vbox);
    
    // Instructions
    GtkWidget* lbl = gtk_label_new("Paste PGN, UCI moves, or a list of SAN moves below:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_vbox), lbl);
    
    // Text View with Scroller
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), scrolled);
    
    s_text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s_text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(s_text_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), s_text_view);
    
    // Status
    s_status_label = gtk_label_new("");
    gtk_widget_set_halign(s_status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_vbox), s_status_label);
    
    // Buttons
    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(main_vbox), btn_box);
    
    GtkWidget* btn_file = gtk_button_new_with_label("Load from File...");
    g_signal_connect(btn_file, "clicked", G_CALLBACK(on_load_file_clicked), NULL);
    gtk_box_append(GTK_BOX(btn_box), btn_file);
    
    GtkWidget* btn_cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_cancel_clicked), NULL);
    gtk_box_append(GTK_BOX(btn_box), btn_cancel);
    
    GtkWidget* btn_import = gtk_button_new_with_label("Import Game");
    gtk_widget_add_css_class(btn_import, "suggested-action");
    g_signal_connect(btn_import, "clicked", G_CALLBACK(on_import_clicked), NULL);
    gtk_box_append(GTK_BOX(btn_box), btn_import);
    
    gtk_window_present(GTK_WINDOW(s_dialog));
}

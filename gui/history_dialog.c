#include "history_dialog.h"
#include "config_manager.h"
#include "gui_utils.h"
#include "import_dialog.h" // NEW
#include "app_state.h"     // Need AppState to pass to import
#include <time.h>

extern AppState* g_app_state; // Access global state for import dialog

#include <time.h>

static bool debug_mode = false;

struct _HistoryDialog {
    GtkWindow* window;
    GtkWidget* list_box;
    GtkWidget* scrolled_window;  // NEW: Keep reference for scroll detection
    GSimpleAction* replay_action;
    int current_page;            // NEW: Current page number
    bool loading;                // NEW: Prevent concurrent loads
};

static GtkWidget* create_match_row(const MatchHistoryEntry* m, HistoryDialog* dialog);
static void on_replay_clicked(GtkButton* btn, gpointer user_data);
static void on_delete_clicked(GtkButton* btn, gpointer user_data);
static void import_btn_clicked(GtkButton* btn, gpointer user_data); // Forward decl
static void load_next_page(HistoryDialog* dialog);  // NEW: Load next page
static void on_scroll_edge_reached(GtkScrolledWindow* sw, GtkPositionType pos, gpointer user_data);  // NEW: Scroll handler

static GtkWidget* create_match_row(const MatchHistoryEntry* m, HistoryDialog* dialog) {
    // Styling: Use a Frame for better visual separation
    GtkWidget* frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "match-row"); // Apply theme style
    gtk_widget_set_margin_start(frame, 5);
    gtk_widget_set_margin_end(frame, 5);
    gtk_widget_set_margin_top(frame, 2);
    gtk_widget_set_margin_bottom(frame, 2);

    GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(row_box, 10);
    gtk_widget_set_margin_bottom(row_box, 10);
    gtk_widget_set_margin_start(row_box, 12);
    gtk_widget_set_margin_end(row_box, 12);
    
    gtk_frame_set_child(GTK_FRAME(frame), row_box);

    // Mode & Result (Left Side)
    char summary[256];
    const char* mode_str = (m->game_mode == 0) ? "PvP" : (m->game_mode == 1) ? "PvC" : "CvC";
    
    char readable_result[128];
    
    // Determine player roles for White and Black
    const char* white_role = m->white.is_ai ? "AI" : "Player";
    const char* black_role = m->black.is_ai ? "AI" : "Player";
    
    // Build detailed result text showing color and player type
    if (strcmp(m->result, "1-0") == 0) {
        // White won
        if (m->game_mode == 1 && !m->white.is_ai) {
            // PvC: Human player as White won
            snprintf(readable_result, sizeof(readable_result), "You Won! (White Player)");
        } else if (m->game_mode == 1 && m->white.is_ai) {
            // PvC: AI as White won
            snprintf(readable_result, sizeof(readable_result), "AI Won (White AI)");
        } else {
            // PvP or CvC
            snprintf(readable_result, sizeof(readable_result), "White %s Won", white_role);
        }
    } else if (strcmp(m->result, "0-1") == 0) {
        // Black won
        if (m->game_mode == 1 && !m->black.is_ai) {
            // PvC: Human player as Black won
            snprintf(readable_result, sizeof(readable_result), "You Won! (Black Player)");
        } else if (m->game_mode == 1 && m->black.is_ai) {
            // PvC: AI as Black won
            snprintf(readable_result, sizeof(readable_result), "AI Won (Black AI)");
        } else {
            // PvP or CvC
            snprintf(readable_result, sizeof(readable_result), "Black %s Won", black_role);
        }
    } else if (strcmp(m->result, "1/2-1/2") == 0) {
        snprintf(readable_result, sizeof(readable_result), "Draw");
    } else {
        snprintf(readable_result, sizeof(readable_result), "No Result");
    }

    // Validate result_reason to prevent FEN/garbage display
    const char* reason = m->result_reason;
    // Common valid codes. If it looks like FEN (contains /) or is excessively long/suspicious, hide it.
    if (strchr(reason, '/') || strstr(reason, "BNR") || strlen(reason) > 30) {
        reason = "Unknown";
    }

    snprintf(summary, sizeof(summary), "<b>%s</b>  <span alpha='45%%'>|</span>  %s (%s)", 
             mode_str, readable_result, reason);
    
    GtkWidget* summary_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(summary_lbl), summary);
    gtk_widget_set_hexpand(summary_lbl, TRUE);
    gtk_widget_set_halign(summary_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(summary_lbl), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(row_box), summary_lbl);

    // Date/Time (Right Side)
    struct tm tm_info;
    // MSVC localtime_s: errno_t localtime_s(struct tm* _tm, const time_t *time);
    localtime_s(&tm_info, (const time_t*)&m->timestamp);
    
    char time_str[64];
    // 12-hour format: "Feb 20, 3:17 PM"
    strftime(time_str, sizeof(time_str), "%b %d, %I:%M %p", &tm_info);
    
    GtkWidget* time_lbl = gtk_label_new(time_str);
    gtk_widget_add_css_class(time_lbl, "dim-label");
    gtk_widget_add_css_class(time_lbl, "numeric"); 
    gtk_widget_set_margin_end(time_lbl, 8); // Spacing before buttons
    gtk_box_append(GTK_BOX(row_box), time_lbl);

    // Buttons
    GtkWidget* btn_replay = gtk_button_new_with_label("Replay");
    gtk_widget_add_css_class(btn_replay, "suggested-action");
    gtk_widget_set_tooltip_text(btn_replay, "Replay this match");
    g_object_set_data_full(G_OBJECT(btn_replay), "match-id", g_strdup(m->id), g_free);
    g_signal_connect(btn_replay, "clicked", G_CALLBACK(on_replay_clicked), dialog);
    
    gtk_box_append(GTK_BOX(row_box), btn_replay);

    GtkWidget* btn_del = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(btn_del, "destructive-action");
    gtk_widget_set_tooltip_text(btn_del, "Delete match record");
    g_object_set_data_full(G_OBJECT(btn_del), "match-id", g_strdup(m->id), g_free);
    g_signal_connect(btn_del, "clicked", G_CALLBACK(on_delete_clicked), dialog);
    
    gtk_box_append(GTK_BOX(row_box), btn_del);

    return frame; // Return frame instead of box
}

// NEW: Load next page of matches
static void load_next_page(HistoryDialog* dialog) {
    if (!dialog || !dialog->list_box || dialog->loading) return;
    
    int total_count = match_history_get_count();
    int loaded_count = dialog->current_page * 20;  // PAGE_SIZE = 20
    
    if (debug_mode) {
        printf("[HistoryDialog] load_next_page: page=%d, loaded=%d, total=%d\n", 
               dialog->current_page, loaded_count, total_count);
    }

    // Check if we've loaded everything
    if (loaded_count >= total_count) {
        return;
    }
    
    dialog->loading = true;
    
    int count = 0;
    MatchHistoryEntry* entries = match_history_get_page(dialog->current_page, &count);
    if (debug_mode) printf("[HistoryDialog] retrieved %d entries for page %d\n", count, dialog->current_page);
    
    if (count == 0 && dialog->current_page == 0) {
        // No matches at all
        GtkWidget* empty_lbl = gtk_label_new("No matches played yet.");
        gtk_widget_add_css_class(empty_lbl, "dim-label");
        gtk_widget_set_margin_top(empty_lbl, 20);
        gtk_widget_set_margin_bottom(empty_lbl, 20);
        gtk_list_box_append(GTK_LIST_BOX(dialog->list_box), empty_lbl);
    } else {
        // Append rows for this page
        for (int i = 0; i < count; i++) {
            if (debug_mode) {
                printf("[History] Row %d: ID=%s | Mode=%d | Res=%s | W_AI=%d ELO=%d | B_AI=%d ELO=%d\n",
                       i, entries[i].id, entries[i].game_mode, entries[i].result,
                       entries[i].white.is_ai, entries[i].white.elo,
                       entries[i].black.is_ai, entries[i].black.elo);
            }
            GtkWidget* row_content = create_match_row(&entries[i], dialog);
            
            GtkWidget* row = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_content);
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
            gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
            
            gtk_list_box_append(GTK_LIST_BOX(dialog->list_box), row);
        }
    }
    
    dialog->current_page++;
    dialog->loading = false;
}

// NEW: Scroll edge detection for infinite scroll
static void on_scroll_edge_reached(GtkScrolledWindow* sw, GtkPositionType pos, gpointer user_data) {
    (void)sw;  // Unused
    if (pos == GTK_POS_BOTTOM) {
        HistoryDialog* dialog = (HistoryDialog*)user_data;
        load_next_page(dialog);
    }
}

// MODIFIED: Clear list and reset pagination state
static void refresh_match_list(HistoryDialog* dialog) {
    if (!dialog || !dialog->list_box) return;

    // Clear existing
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(dialog->list_box)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(dialog->list_box), child);
    }
    
    // Reset pagination state
    dialog->current_page = 0;
    dialog->loading = false;
    
    // Load first page
    load_next_page(dialog);
}


static void on_delete_clicked(GtkButton* btn, gpointer user_data) {
    HistoryDialog* dialog = (HistoryDialog*)user_data;
    const char* id = (const char*)g_object_get_data(G_OBJECT(btn), "match-id");
    if (id) {
        match_history_delete(id);
        // Refresh list content ONLY, do not re-present window which can toggle focus state
        refresh_match_list(dialog);
    }
}

static void on_replay_clicked(GtkButton* btn, gpointer user_data) {
    HistoryDialog* dialog = (HistoryDialog*)user_data;
    const char* id = (const char*)g_object_get_data(G_OBJECT(btn), "match-id");
    if (id) {
        // Activate the action in main window
        GApplication* app = g_application_get_default();
        g_action_group_activate_action(G_ACTION_GROUP(app), "start-replay", g_variant_new_string(id));
        
        // Close the dialog after starting replay
        if (dialog->window) {
            gtk_window_destroy(dialog->window);
        }
    }
}

HistoryDialog* history_dialog_new(GtkWindow* parent) {
    HistoryDialog* dialog = g_new0(HistoryDialog, 1);
    
    dialog->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(dialog->window, "Game History");
    gui_utils_set_window_size_relative(dialog->window, parent, 0.7, 0.7);
    gtk_window_set_resizable(dialog->window, TRUE);
    gtk_window_set_modal(dialog->window, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(dialog->window), "window"); // Ensure theme background
    gtk_window_set_transient_for(dialog->window, parent);

    GtkWidget* main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(main_vbox, 20);
    gtk_widget_set_margin_bottom(main_vbox, 20);
    gtk_widget_set_margin_start(main_vbox, 20); // More margin
    gtk_widget_set_margin_end(main_vbox, 20);
    gtk_window_set_child(dialog->window, main_vbox);

    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(main_vbox), header_box);

    GtkWidget* header = gtk_label_new("Match History");
    gtk_widget_add_css_class(header, "title-2");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_widget_set_hexpand(header, TRUE); // Push button to right
    gtk_box_append(GTK_BOX(header_box), header);

    GtkWidget* btn_import = gtk_button_new_with_label("Import Game");
    gtk_widget_add_css_class(btn_import, "suggested-action"); 
    // We need to pass g_app_state to import_dialog_show.
    // Since history_dialog_new doesn't take AppState, we use the global g_app_state.
    // Alternatively, we could attach it to the button data.
    // For now, let's allow a static callback that uses g_app_state.
    g_signal_connect(btn_import, "clicked", G_CALLBACK(import_btn_clicked), NULL);
    gtk_box_append(GTK_BOX(header_box), btn_import);

    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    
    // NEW: Store reference for scroll detection
    dialog->scrolled_window = scrolled;
    
    // Add frame around list for depth
    GtkWidget* list_frame = gtk_frame_new(NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_frame);
    
    dialog->list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(dialog->list_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(dialog->list_box, "history-list"); // Changed class
    gtk_frame_set_child(GTK_FRAME(list_frame), dialog->list_box);
    
    gtk_box_append(GTK_BOX(main_vbox), scrolled);
    
    // NEW: Connect scroll edge signal for infinite scroll
    g_signal_connect(scrolled, "edge-reached", 
                     G_CALLBACK(on_scroll_edge_reached), dialog);
    
    // NEW: Initialize pagination state
    dialog->current_page = 0;
    dialog->loading = false;

    g_signal_connect_swapped(dialog->window, "destroy", G_CALLBACK(history_dialog_free), dialog);

    return dialog;
}

void history_dialog_show(HistoryDialog* dialog) {
    if (!dialog) return;

    refresh_match_list(dialog);

    gtk_widget_set_visible(GTK_WIDGET(dialog->window), TRUE);
    gtk_window_present(dialog->window);
    
    // Auto-focus the list so keyboard works immediately
    if (dialog->list_box) {
        gtk_widget_grab_focus(dialog->list_box);
    }
}

GtkWindow* history_dialog_get_window(HistoryDialog* dialog) {
    return dialog ? dialog->window : NULL;
}

void history_dialog_set_replay_callback(HistoryDialog* dialog, GCallback callback, gpointer user_data) {
    // We already use a global action for this, but let's keep the hook if needed.
    (void)dialog;
    (void)callback;
    (void)user_data;
}

void history_dialog_free(HistoryDialog* dialog) {
    if (dialog) {
        g_free(dialog);
    }
}

static void import_btn_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn; (void)user_data;
    if (g_app_state) {
        import_dialog_show(g_app_state);
        // Note: history_dialog doesn't close here, we let import dialog handle success actions
    }
}

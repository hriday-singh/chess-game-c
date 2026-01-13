#include "history_dialog.h"
#include "config_manager.h"
#include <time.h>

struct _HistoryDialog {
    GtkWindow* window;
    GtkWidget* list_box;
    GSimpleAction* replay_action;
};

static GtkWidget* create_match_row(const MatchHistoryEntry* m, HistoryDialog* dialog) {
    GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(row_box, 8);
    gtk_widget_set_margin_bottom(row_box, 8);
    gtk_widget_set_margin_start(row_box, 12);
    gtk_widget_set_margin_end(row_box, 12);

    // Date/Time
    struct tm* tm_info = localtime((const time_t*)&m->timestamp);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
    
    GtkWidget* time_lbl = gtk_label_new(time_str);
    gtk_widget_add_css_class(time_lbl, "dim-label");
    gtk_box_append(GTK_BOX(row_box), time_lbl);

    // Mode & Result
    char summary[128];
    const char* mode_str = (m->game_mode == 0) ? "PvP" : (m->game_mode == 1) ? "PvC" : "CvC";
    snprintf(summary, sizeof(summary), "[%s] %s (%s)", mode_str, m->result, m->result_reason);
    
    GtkWidget* summary_lbl = gtk_label_new(summary);
    gtk_widget_set_hexpand(summary_lbl, TRUE);
    gtk_widget_set_halign(summary_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row_box), summary_lbl);

    // Buttons
    GtkWidget* btn_replay = gtk_button_new_with_label("Replay");
    gtk_widget_add_css_class(btn_replay, "suggested-action");
    g_object_set_data_full(G_OBJECT(btn_replay), "match-id", g_strdup(m->id), g_free);
    // Connect to replay logic in main.c (via AppState action)
    g_signal_connect_swapped(btn_replay, "clicked", G_CALLBACK(gtk_window_destroy), dialog->window);
    
    // Custom action signal or similar could be used. For now, let's just use a simple callback pattern
    // or trigger a global action.
    
    gtk_box_append(GTK_BOX(row_box), btn_replay);

    GtkWidget* btn_del = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(btn_del, "destructive-action");
    g_object_set_data_full(G_OBJECT(btn_del), "match-id", g_strdup(m->id), g_free);
    // g_signal_connect...
    
    gtk_box_append(GTK_BOX(row_box), btn_del);

    return row_box;
}

static void on_delete_clicked(GtkButton* btn, gpointer user_data) {
    HistoryDialog* dialog = (HistoryDialog*)user_data;
    const char* id = (const char*)g_object_get_data(G_OBJECT(btn), "match-id");
    if (id) {
        match_history_delete(id);
        // Refresh list
        history_dialog_show(dialog);
    }
}

static void on_replay_clicked(GtkButton* btn, gpointer user_data) {
    HistoryDialog* dialog = (HistoryDialog*)user_data;
    const char* id = (const char*)g_object_get_data(G_OBJECT(btn), "match-id");
    if (id) {
        GApplication* app = g_application_get_default();
        g_action_group_activate_action(G_ACTION_GROUP(app), "app.start-replay", g_variant_new_string(id));
        gtk_window_destroy(dialog->window);
    }
}

HistoryDialog* history_dialog_new(GtkWindow* parent) {
    HistoryDialog* dialog = g_new0(HistoryDialog, 1);
    
    dialog->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(dialog->window, "Game History");
    gtk_window_set_default_size(dialog->window, 600, 450);
    gtk_window_set_modal(dialog->window, TRUE);
    gtk_window_set_transient_for(dialog->window, parent);

    GtkWidget* main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(main_vbox, 16);
    gtk_widget_set_margin_bottom(main_vbox, 16);
    gtk_widget_set_margin_start(main_vbox, 16);
    gtk_widget_set_margin_end(main_vbox, 16);
    gtk_window_set_child(dialog->window, main_vbox);

    GtkWidget* header = gtk_label_new("Past Matches");
    gtk_widget_add_css_class(header, "title-2");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_vbox), header);

    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), scrolled);

    dialog->list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(dialog->list_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(dialog->list_box, "boxed-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), dialog->list_box);

    g_signal_connect_swapped(dialog->window, "destroy", G_CALLBACK(history_dialog_free), dialog);

    return dialog;
}

void history_dialog_show(HistoryDialog* dialog) {
    if (!dialog) return;

    // Clear existing
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(dialog->list_box)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(dialog->list_box), child);
    }

    int count = 0;
    MatchHistoryEntry* matches = match_history_get_list(&count);
    for (int i = count - 1; i >= 0; i--) {
        GtkWidget* row = create_match_row(&matches[i], dialog);
        // Find buttons and connect
        GtkWidget* box = row;
        GtkWidget* btn_replay = gtk_widget_get_last_child(box); // This is trash icon? No, trash is last.
        GtkWidget* btn_trash = btn_replay;
        btn_replay = gtk_widget_get_prev_sibling(btn_trash);
        
        g_signal_connect(btn_replay, "clicked", G_CALLBACK(on_replay_clicked), dialog);
        g_signal_connect(btn_trash, "clicked", G_CALLBACK(on_delete_clicked), dialog);

        gtk_list_box_append(GTK_LIST_BOX(dialog->list_box), row);
    }

    gtk_widget_set_visible(GTK_WIDGET(dialog->window), TRUE);
    gtk_window_present(dialog->window);
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

#include "ai_dialog.h"
#include "ai_engine.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

struct _AiDialog {
    GtkWindow* parent_window;
    GtkWindow* window; // Can be NULL if embedded
    GtkWidget* content_box; // The main container
    GtkWidget* notebook;
    
    // --- Internal Engine Tab ---
    GtkWidget* elo_slider;
    GtkWidget* elo_spin;
    GtkWidget* int_adv_check;
    GtkWidget* int_adv_vbox;
    GtkWidget* int_depth_spin;
    GtkWidget* int_time_spin;
    bool int_manual_movetime;
    
    // NNUE (Internal)
    GtkWidget* nnue_path_label;
    GtkWidget* nnue_toggle;
    char* nnue_path;
    
    // --- Custom Engine Tab ---
    GtkWidget* custom_path_entry;
    GtkWidget* custom_status_label;
    GtkWidget* custom_adv_check;
    GtkWidget* custom_adv_vbox;
    GtkWidget* custom_depth_spin;
    GtkWidget* custom_time_spin;
    bool custom_manual_movetime;
    bool is_custom_configured;
    
    // State
    int current_elo;
    
    AiSettingsChangedCallback change_cb;
    void* change_cb_data;
};

// --- Callbacks ---

#include <math.h>

static void on_elo_changed(GtkAdjustment* adj, gpointer data) {
    AiDialog* dialog = (AiDialog*)data;
    dialog->current_elo = (int)gtk_adjustment_get_value(adj);
    if (dialog->change_cb) dialog->change_cb(dialog->change_cb_data);
}

// Fixed Movetime sync math: 1.5x multiplier per depth unit baseline 10@500ms
static int calculate_movetime(int depth) {
    if (depth <= 1) return 10;
    // 500 * (1.5 ^ (depth - 10))
    double val = 500.0 * pow(1.5, (double)depth - 10.0);
    if (val < 10) return 10;
    if (val > 60000) return 60000;
    return (int)val;
}

static bool validate_nnue_file(const char* path) {
    if (!path || strlen(path) == 0) return false;
    
    // Check if file exists first
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);

    // Try loading with internal engine
    EngineHandle* test = ai_engine_init_internal();
    if (!test) return false;

    // SF 17.1 doesn't always report "error" on setoption, but let's try
    ai_engine_send_command(test, "uci");
    ai_engine_set_option(test, "EvalFile", path);
    ai_engine_send_command(test, "isready");
    
    // Wait a bit for it to load
    g_usleep(200000); // 200ms

    // Check if it's still alive or if it gave an error
    char* resp;
    bool found_ready = false;
    int tries = 5;
    while (tries-- > 0) {
        resp = ai_engine_try_get_response(test);
        if (resp) {
            if (strstr(resp, "readyok")) found_ready = true;
            // Many SF builds print "failed to open" or similar if eval file is bad
            if (strstr(resp, "error") || strstr(resp, "failed") || strstr(resp, "No such file")) {
                ai_engine_free_response(resp);
                ai_engine_cleanup(test);
                return false;
            }
            ai_engine_free_response(resp);
        }
        if (found_ready) break;
        g_usleep(100000);
    }

    ai_engine_cleanup(test);
    return found_ready;
}

static void on_int_depth_changed(GtkSpinButton* spin, gpointer user_data) {
    AiDialog* dialog = (AiDialog*)user_data;
    if (!dialog->int_manual_movetime) {
        int depth = gtk_spin_button_get_value_as_int(spin);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_time_spin), calculate_movetime(depth));
    }
}

static void on_int_time_changed(GtkSpinButton* spin, gpointer user_data) {
    (void)spin;
    AiDialog* dialog = (AiDialog*)user_data;
    dialog->int_manual_movetime = true;
}

static void on_int_advanced_toggled(GtkCheckButton* btn, gpointer user_data) {
    AiDialog* dialog = (AiDialog*)user_data;
    bool active = gtk_check_button_get_active(btn);
    gtk_widget_set_visible(dialog->int_adv_vbox, active);
    // Disable ELO controls when advanced is on
    gtk_widget_set_sensitive(dialog->elo_slider, !active);
    gtk_widget_set_sensitive(dialog->elo_spin, !active);
}

static void on_int_reset_adv_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)user_data;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_depth_spin), 10);
    dialog->int_manual_movetime = false;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_time_spin), 500);
}

static void on_custom_depth_changed(GtkSpinButton* spin, gpointer user_data) {
    AiDialog* dialog = (AiDialog*)user_data;
    if (!dialog->custom_manual_movetime) {
        int depth = gtk_spin_button_get_value_as_int(spin);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_time_spin), calculate_movetime(depth));
    }
}

static void on_custom_time_changed(GtkSpinButton* spin, gpointer user_data) {
    (void)spin;
    AiDialog* dialog = (AiDialog*)user_data;
    dialog->custom_manual_movetime = true;
}

static void on_custom_advanced_toggled(GtkCheckButton* btn, gpointer user_data) {
    AiDialog* dialog = (AiDialog*)user_data;
    bool active = gtk_check_button_get_active(btn);
    gtk_widget_set_visible(dialog->custom_adv_vbox, active);
}

static void on_custom_reset_adv_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)user_data;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_depth_spin), 10);
    dialog->custom_manual_movetime = false;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_time_spin), 500);
}

static void on_clear_path_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)user_data;
    gtk_editable_set_text(GTK_EDITABLE(dialog->custom_path_entry), "");
}

static void on_custom_path_changed(GtkEditable* editable, gpointer user_data) {
    AiDialog* dialog = (AiDialog*)user_data;
    const char* path = gtk_editable_get_text(editable);
    if (path && strlen(path) > 0) {
        if (ai_engine_test_binary(path)) {
            gtk_label_set_text(GTK_LABEL(dialog->custom_status_label), "Configured successfully");
            gtk_widget_add_css_class(dialog->custom_status_label, "success-text");
            gtk_widget_remove_css_class(dialog->custom_status_label, "error-text");
            dialog->is_custom_configured = true;
        } else {
            gtk_label_set_text(GTK_LABEL(dialog->custom_status_label), "Invalid UCI engine path");
            gtk_widget_add_css_class(dialog->custom_status_label, "error-text");
            gtk_widget_remove_css_class(dialog->custom_status_label, "success-text");
            dialog->is_custom_configured = false;
        }
    } else {
        gtk_label_set_text(GTK_LABEL(dialog->custom_status_label), "");
        dialog->is_custom_configured = false;
    }
}

static void on_browse_finished(GObject* src, GAsyncResult* r, gpointer d) {
    GtkFileDialog* fd = GTK_FILE_DIALOG(src);
    AiDialog* dialog = (AiDialog*)d;
    GFile* file = gtk_file_dialog_open_finish(fd, r, NULL);
    if (file) {
        char* path = g_file_get_path(file);
        gtk_editable_set_text(GTK_EDITABLE(dialog->custom_path_entry), path);
        g_free(path);
        g_object_unref(file);
    }
    g_object_unref(fd);
}

static void on_browse_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)user_data;
    GtkFileDialog* fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Select Engine Binary");
    // Use dialog->window if available, else parent_window
    GtkWindow* w = dialog->window ? dialog->window : dialog->parent_window;
    gtk_file_dialog_open(fd, w, NULL, on_browse_finished, dialog);
}

static void on_nnue_import_finished(GObject* src, GAsyncResult* r, gpointer d) {
    GtkFileDialog* fd = GTK_FILE_DIALOG(src);
    AiDialog* dialog = (AiDialog*)d;
    GFile* file = gtk_file_dialog_open_finish(fd, r, NULL);
    if (file) {
        char* path = g_file_get_path(file);
        if (validate_nnue_file(path)) {
            if (dialog->nnue_path) g_free(dialog->nnue_path);
            dialog->nnue_path = path;
            gtk_label_set_text(GTK_LABEL(dialog->nnue_path_label), g_path_get_basename(path));
            gtk_widget_set_visible(dialog->nnue_toggle, TRUE);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->nnue_toggle), TRUE);
        } else {
            // Show error using modern GTK4 window
            GtkWindow* parent = dialog->window ? dialog->window : dialog->parent_window;
            GtkWidget* error_window = gtk_window_new();
            gtk_window_set_title(GTK_WINDOW(error_window), "Error");
            if (parent) gtk_window_set_transient_for(GTK_WINDOW(error_window), parent);
            gtk_window_set_modal(GTK_WINDOW(error_window), TRUE);
            gtk_window_set_default_size(GTK_WINDOW(error_window), 300, 150);
            
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            gtk_widget_set_margin_top(box, 20);
            gtk_widget_set_margin_bottom(box, 20);
            gtk_widget_set_margin_start(box, 20);
            gtk_widget_set_margin_end(box, 20);
            
            GtkWidget* label = gtk_label_new("Invalid NNUE file. The inbuilt engine could not load this file.");
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_box_append(GTK_BOX(box), label);
            
            GtkWidget* btn = gtk_button_new_with_label("OK");
            gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
            g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), error_window);
            gtk_box_append(GTK_BOX(box), btn);
            
            gtk_window_set_child(GTK_WINDOW(error_window), box);
            gtk_window_present(GTK_WINDOW(error_window));
            
            // Clear NNUE
            if (dialog->nnue_path) {
                g_free(dialog->nnue_path);
                dialog->nnue_path = NULL;
            }
            gtk_label_set_text(GTK_LABEL(dialog->nnue_path_label), "None");
            gtk_widget_set_visible(dialog->nnue_toggle, FALSE);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->nnue_toggle), FALSE);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(fd);
}

static void on_nnue_import_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)user_data;
    GtkFileDialog* fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Select NNUE File");
    GtkWindow* w = dialog->window ? dialog->window : dialog->parent_window;
    gtk_file_dialog_open(fd, w, NULL, on_nnue_import_finished, dialog);
}

static void on_nnue_delete_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)user_data;
    if (dialog->nnue_path) {
        g_free(dialog->nnue_path);
        dialog->nnue_path = NULL;
    }
    gtk_label_set_text(GTK_LABEL(dialog->nnue_path_label), "None");
    gtk_widget_set_visible(dialog->nnue_toggle, FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->nnue_toggle), FALSE);
}

static void on_ok_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)data;
    if (dialog->window) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), FALSE);
    }
}

static gboolean on_close_request(GtkWindow* window, gpointer data) {
    (void)data;
    AiDialog* dialog = (AiDialog*)g_object_get_data(G_OBJECT(window), "dialog");
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    // Return focus to parent if possible
    if (dialog && dialog->parent_window) {
        gtk_window_present(dialog->parent_window);
    }
    return TRUE;
}

static void on_focus_lost_gesture(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    GtkWidget* widget = GTK_WIDGET(user_data);
    gtk_widget_grab_focus(widget);
}

// --- Construction Helper ---

static void ai_dialog_build_ui(AiDialog* dialog) {
    dialog->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(dialog->content_box, 15);
    gtk_widget_set_margin_bottom(dialog->content_box, 5); 
    gtk_widget_set_margin_start(dialog->content_box, 15);
    gtk_widget_set_margin_end(dialog->content_box, 15);
    gtk_widget_set_focusable(dialog->content_box, TRUE); // Allow it to take focus to clear entries
    
    dialog->notebook = gtk_notebook_new();
    gtk_box_append(GTK_BOX(dialog->content_box), dialog->notebook);
    
    // --- TAB 1: Internal Engine ---
    GtkWidget* int_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(int_tab, 15);
    gtk_widget_set_margin_bottom(int_tab, 15);
    gtk_widget_set_margin_start(int_tab, 15);
    gtk_widget_set_margin_end(int_tab, 15);
    
    GtkWidget* int_header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(int_header), "<span size='large' weight='bold'>Stockfish 17.1 (Inbuilt)</span>");
    gtk_widget_set_halign(int_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(int_tab), int_header);

    // ELO Settings
    GtkWidget* elo_label = gtk_label_new("Difficulty (ELO):");
    gtk_widget_set_halign(elo_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(int_tab), elo_label);
    
    GtkAdjustment* elo_adj = gtk_adjustment_new(1500, 100, 3600, 50, 500, 0);
    g_signal_connect(elo_adj, "value-changed", G_CALLBACK(on_elo_changed), dialog);
    
    dialog->elo_slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, elo_adj);
    gtk_scale_set_draw_value(GTK_SCALE(dialog->elo_slider), FALSE);
    gtk_box_append(GTK_BOX(int_tab), dialog->elo_slider);
    
    dialog->elo_spin = gtk_spin_button_new(elo_adj, 50, 0);
    gtk_box_append(GTK_BOX(int_tab), dialog->elo_spin);

    // UCI Usage Instructions for Inbuilt
    GtkWidget* inbuilt_instr_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(inbuilt_instr_vbox, 5);
    GtkWidget* inbuilt_instr_label = gtk_label_new(
        "<span size='small' font_style='italic'>This inbuilt Stockfish 17.1 can be used via UCI protocol.\n"
        "It supports standard options like Skill Level (ELO), Depth, and NNUE.</span>"
    );
    gtk_label_set_use_markup(GTK_LABEL(inbuilt_instr_label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(inbuilt_instr_label), TRUE);
    gtk_widget_set_opacity(inbuilt_instr_label, 0.7);
    gtk_box_append(GTK_BOX(inbuilt_instr_vbox), inbuilt_instr_label);
    gtk_box_append(GTK_BOX(int_tab), inbuilt_instr_vbox);

    gtk_box_append(GTK_BOX(int_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Internal Advanced
    dialog->int_adv_check = gtk_check_button_new_with_label("Use Advanced Search Mode");
    g_signal_connect(dialog->int_adv_check, "toggled", G_CALLBACK(on_int_advanced_toggled), dialog);
    gtk_box_append(GTK_BOX(int_tab), dialog->int_adv_check);

    dialog->int_adv_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_visible(dialog->int_adv_vbox, FALSE);
    
    GtkWidget* d_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(d_hbox), gtk_label_new("Target Depth:"));
    dialog->int_depth_spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_depth_spin), 10);
    g_signal_connect(dialog->int_depth_spin, "value-changed", G_CALLBACK(on_int_depth_changed), dialog);
    gtk_box_append(GTK_BOX(d_hbox), dialog->int_depth_spin);
    gtk_box_append(GTK_BOX(dialog->int_adv_vbox), d_hbox);
    
    GtkWidget* t_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(t_hbox), gtk_label_new("Move Time (ms):"));
    dialog->int_time_spin = gtk_spin_button_new_with_range(10, 600000, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_time_spin), 500);
    g_signal_connect(dialog->int_time_spin, "value-changed", G_CALLBACK(on_int_time_changed), dialog);
    gtk_box_append(GTK_BOX(t_hbox), dialog->int_time_spin);
    gtk_box_append(GTK_BOX(dialog->int_adv_vbox), t_hbox);
    
    GtkWidget* int_reset_btn = gtk_button_new_with_label("Reset to Defaults");
    g_signal_connect(int_reset_btn, "clicked", G_CALLBACK(on_int_reset_adv_clicked), dialog);
    gtk_box_append(GTK_BOX(dialog->int_adv_vbox), int_reset_btn);
    
    gtk_box_append(GTK_BOX(int_tab), dialog->int_adv_vbox);

    gtk_box_append(GTK_BOX(int_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // NNUE Section
    GtkWidget* nnue_header = gtk_label_new("NNUE Evaluation");
    gtk_widget_set_halign(nnue_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(int_tab), nnue_header);

    GtkWidget* nnue_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    dialog->nnue_path_label = gtk_label_new("None");
    gtk_widget_set_hexpand(dialog->nnue_path_label, TRUE);
    gtk_box_append(GTK_BOX(nnue_hbox), dialog->nnue_path_label);
    
    GtkWidget* imp_btn = gtk_button_new_from_icon_name("document-open-symbolic");
    g_signal_connect(imp_btn, "clicked", G_CALLBACK(on_nnue_import_clicked), dialog);
    gtk_box_append(GTK_BOX(nnue_hbox), imp_btn);
    
    GtkWidget* del_btn = gtk_button_new_from_icon_name("edit-delete-symbolic");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_nnue_delete_clicked), dialog);
    gtk_box_append(GTK_BOX(nnue_hbox), del_btn);
    gtk_box_append(GTK_BOX(int_tab), nnue_hbox);
    
    dialog->nnue_toggle = gtk_check_button_new_with_label("Enable NNUE");
    gtk_widget_set_visible(dialog->nnue_toggle, FALSE);
    gtk_box_append(GTK_BOX(int_tab), dialog->nnue_toggle);

    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), int_tab, gtk_label_new("Internal Engine"));

    // --- TAB 2: Custom Engine ---
    GtkWidget* custom_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(custom_tab, 15);
    gtk_widget_set_margin_bottom(custom_tab, 15);
    gtk_widget_set_margin_start(custom_tab, 15);
    gtk_widget_set_margin_end(custom_tab, 15);

    GtkWidget* cust_header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(cust_header), "<span size='large' weight='bold'>Custom UCI Engine</span>");
    gtk_widget_set_halign(cust_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(custom_tab), cust_header);
    
    GtkWidget* path_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    dialog->custom_path_entry = gtk_entry_new();
    gtk_widget_set_hexpand(dialog->custom_path_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(dialog->custom_path_entry), "Select UCI executable...");
    g_signal_connect(dialog->custom_path_entry, "changed", G_CALLBACK(on_custom_path_changed), dialog);
    gtk_box_append(GTK_BOX(path_hbox), dialog->custom_path_entry);
    
    GtkWidget* browse_btn = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_set_tooltip_text(browse_btn, "Browse...");
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_clicked), dialog);
    gtk_box_append(GTK_BOX(path_hbox), browse_btn);

    GtkWidget* clear_btn = gtk_button_new_from_icon_name("edit-clear-symbolic");
    gtk_widget_set_tooltip_text(clear_btn, "Clear Path / Remove Engine");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_path_clicked), dialog);
    gtk_box_append(GTK_BOX(path_hbox), clear_btn);

    gtk_box_append(GTK_BOX(custom_tab), path_hbox);
    
    dialog->custom_status_label = gtk_label_new("");
    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_status_label);

    gtk_box_append(GTK_BOX(custom_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Custom Advanced
    dialog->custom_adv_check = gtk_check_button_new_with_label("Use Advanced Search Mode");
    g_signal_connect(dialog->custom_adv_check, "toggled", G_CALLBACK(on_custom_advanced_toggled), dialog);
    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_adv_check);

    dialog->custom_adv_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_visible(dialog->custom_adv_vbox, FALSE);

    GtkWidget* cd_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(cd_hbox), gtk_label_new("Target Depth:"));
    dialog->custom_depth_spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_depth_spin), 10);
    g_signal_connect(dialog->custom_depth_spin, "value-changed", G_CALLBACK(on_custom_depth_changed), dialog);
    gtk_box_append(GTK_BOX(cd_hbox), dialog->custom_depth_spin);
    gtk_box_append(GTK_BOX(dialog->custom_adv_vbox), cd_hbox);

    GtkWidget* ct_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(ct_hbox), gtk_label_new("Move Time (ms):"));
    dialog->custom_time_spin = gtk_spin_button_new_with_range(10, 600000, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_time_spin), 500);
    g_signal_connect(dialog->custom_time_spin, "value-changed", G_CALLBACK(on_custom_time_changed), dialog);
    gtk_box_append(GTK_BOX(ct_hbox), dialog->custom_time_spin);
    gtk_box_append(GTK_BOX(dialog->custom_adv_vbox), ct_hbox);

    GtkWidget* cust_reset_btn = gtk_button_new_with_label("Reset to Defaults");
    g_signal_connect(cust_reset_btn, "clicked", G_CALLBACK(on_custom_reset_adv_clicked), dialog);
    gtk_box_append(GTK_BOX(dialog->custom_adv_vbox), cust_reset_btn);

    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_adv_vbox);

    gtk_box_append(GTK_BOX(custom_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Instructions
    GtkWidget* usage_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget* usage_title = gtk_label_new("<b>How to use:</b>");
    gtk_label_set_use_markup(GTK_LABEL(usage_title), TRUE);
    gtk_widget_set_halign(usage_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(usage_vbox), usage_title);
    GtkWidget* usage_label = gtk_label_new("Browse for a UCI compatible executable. Once selected, it will be available in the game panel dropdown.");
    gtk_label_set_wrap(GTK_LABEL(usage_label), TRUE);
    gtk_widget_set_opacity(usage_label, 0.7);
    gtk_box_append(GTK_BOX(usage_vbox), usage_label);
    gtk_box_append(GTK_BOX(custom_tab), usage_vbox);

    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), custom_tab, gtk_label_new("Custom Engine"));

    // Focus clearing gesture on main box
    GtkGesture* gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_focus_lost_gesture), dialog->content_box);
    gtk_widget_add_controller(dialog->content_box, GTK_EVENT_CONTROLLER(gesture));
}

// --- Public API ---

AiDialog* ai_dialog_new_embedded(void) {
    AiDialog* dialog = (AiDialog*)calloc(1, sizeof(AiDialog));
    if (!dialog) return NULL;
    
    dialog->current_elo = 1500;
    dialog->int_manual_movetime = false;
    dialog->custom_manual_movetime = false;
    
    ai_dialog_build_ui(dialog);
    // Only construct content, no window
    
    return dialog;
}

void ai_dialog_set_parent_window(AiDialog* dialog, GtkWindow* parent) {
    if (dialog) {
        dialog->parent_window = parent;
        if (dialog->window) {
            gtk_window_set_transient_for(dialog->window, parent);
        }
    }
}

AiDialog* ai_dialog_new(GtkWindow* parent) {
    AiDialog* dialog = ai_dialog_new_embedded();
    if (!dialog) return NULL;
    
    dialog->parent_window = parent;
    
    dialog->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(dialog->window, "AI Settings");
    gtk_window_set_transient_for(dialog->window, parent);
    gtk_window_set_modal(dialog->window, TRUE);
    gtk_window_set_default_size(dialog->window, 450, 500);
    
    g_signal_connect(dialog->window, "close-request", G_CALLBACK(on_close_request), NULL);
    g_object_set_data(G_OBJECT(dialog->window), "dialog", dialog);
    
    gtk_window_set_child(dialog->window, dialog->content_box);
    
    // Apply Button (Only for standalone)
    GtkWidget* ok_btn = gtk_button_new_with_label("Apply & Close");
    gtk_widget_set_halign(ok_btn, GTK_ALIGN_END);
    gtk_widget_set_margin_top(ok_btn, 10);
    gtk_widget_add_css_class(ok_btn, "suggested-action");
    g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_ok_clicked), dialog);
    gtk_box_append(GTK_BOX(dialog->content_box), ok_btn);
    
    return dialog;
}

GtkWidget* ai_dialog_get_widget(AiDialog* dialog) {
    return dialog ? dialog->content_box : NULL;
}

void ai_dialog_show(AiDialog* dialog) {
    if (dialog && dialog->window) {
        gtk_widget_set_visible(GTK_WIDGET(dialog->window), TRUE);
        gtk_window_present(dialog->window);
    }
}

void ai_dialog_free(AiDialog* dialog) {
    if (dialog) {
        if (dialog->nnue_path) g_free(dialog->nnue_path);
        if (dialog->window) gtk_window_destroy(dialog->window);
        free(dialog);
    }
}

int ai_dialog_get_elo(AiDialog* dialog) { return dialog ? dialog->current_elo : 1500; }

bool ai_dialog_is_advanced_enabled(AiDialog* dialog, bool is_custom) {
    if (!dialog) return false;
    if (is_custom) {
        if (!dialog->custom_adv_check) return false; 
        return gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->custom_adv_check));
    } else {
        if (!dialog->int_adv_check) return false;
        return gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->int_adv_check));
    }
}

int ai_dialog_get_depth(AiDialog* dialog, bool is_custom) {
    if (!dialog) return 10;
    if (is_custom) {
        if (!dialog->custom_depth_spin) return 10;
        return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->custom_depth_spin));
    } else {
        if (!dialog->int_depth_spin) return 10;
        return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->int_depth_spin));
    }
}

int ai_dialog_get_movetime(AiDialog* dialog, bool is_custom) {
    if (!dialog) return 500;
    if (is_custom) {
        if (!dialog->custom_time_spin) return 500;
        return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->custom_time_spin));
    } else {
        if (!dialog->int_time_spin) return 500;
        return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->int_time_spin));
    }
}

const char* ai_dialog_get_nnue_path(AiDialog* dialog, bool* enabled) {
    if (!dialog) { *enabled = false; return NULL; }
    if (!dialog->nnue_toggle) { *enabled = false; return NULL; } // Added NULL check for nnue_toggle
    *enabled = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->nnue_toggle));
    return dialog->nnue_path;
}

const char* ai_dialog_get_custom_path(AiDialog* dialog) {
    return dialog ? gtk_editable_get_text(GTK_EDITABLE(dialog->custom_path_entry)) : "";
}

bool ai_dialog_has_valid_custom_engine(AiDialog* dialog) { return dialog ? dialog->is_custom_configured : false; }

void ai_dialog_show_tab(AiDialog* dialog, int tab_index) {
    if (dialog && dialog->notebook) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(dialog->notebook), tab_index);
        ai_dialog_show(dialog);
    }
}

void ai_dialog_set_elo(AiDialog* dialog, int elo) {
    if (!dialog) return;
    dialog->current_elo = elo;
    // Update UI if exists (internal engine only primarily)
    if (dialog->elo_slider) {
        gtk_range_set_value(GTK_RANGE(dialog->elo_slider), (double)elo);
    }
}

void ai_dialog_set_settings_changed_callback(AiDialog* dialog, AiSettingsChangedCallback cb, void* data) {
    if (dialog) {
        dialog->change_cb = cb;
        dialog->change_cb_data = data;
    }
}

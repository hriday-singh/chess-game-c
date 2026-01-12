#include "ai_dialog.h"
#include "ai_engine.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "gui_utils.h"

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
    GtkAdjustment* int_elo_adj; 
    GtkAdjustment* custom_elo_adj;
    int int_elo;
    int custom_elo;
    
    // Custom Engine specific
    GtkWidget* custom_elo_slider;
    GtkWidget* custom_elo_spin;
    
    // --- Live Analysis Tab ---
    GtkWidget* analysis_toggle;
    GtkWidget* advantage_bar_toggle;
    GtkWidget* mate_warning_toggle;
    GtkWidget* hanging_pieces_toggle;
    GtkWidget* move_rating_toggle;
    GtkWidget* analysis_engine_internal;
    GtkWidget* analysis_engine_custom;
    GtkWidget* analysis_cust_hint;
    GtkWidget* analysis_cust_connect_btn;
    
    AiSettingsChangedCallback change_cb;
    void* change_cb_data;
};

// --- Forward Declarations ---
static void sync_analysis_tab_sensitivity(AiDialog* dialog);

// --- Callbacks ---

#include <math.h>

static void on_int_elo_changed(GtkAdjustment* adj, gpointer data) {
    AiDialog* dialog = (AiDialog*)data;
    dialog->int_elo = (int)gtk_adjustment_get_value(adj);
    if (dialog->change_cb) dialog->change_cb(dialog->change_cb_data);
}

static void on_custom_elo_changed(GtkAdjustment* adj, gpointer data) {
    AiDialog* dialog = (AiDialog*)data;
    dialog->custom_elo = (int)gtk_adjustment_get_value(adj);
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

static void update_custom_controls_state(AiDialog* dialog) {
    bool has_engine = dialog->is_custom_configured;
    
    // Advanced Checkbox: Only enabled if we have a valid engine
    gtk_widget_set_sensitive(dialog->custom_adv_check, has_engine);
    
    // Check current state (even if disabled)
    bool adv_active = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->custom_adv_check));
    
    // ELO Controls: Enabled only if Engine Valid AND Advanced Mode OFF
    bool elo_active = has_engine && !adv_active;
    
    if (dialog->custom_elo_slider) gtk_widget_set_sensitive(dialog->custom_elo_slider, elo_active);
    if (dialog->custom_elo_spin) gtk_widget_set_sensitive(dialog->custom_elo_spin, elo_active);
}

static void on_custom_advanced_toggled(GtkCheckButton* btn, gpointer user_data) {
    AiDialog* dialog = (AiDialog*)user_data;
    bool active = gtk_check_button_get_active(btn);
    gtk_widget_set_visible(dialog->custom_adv_vbox, active);
    update_custom_controls_state(dialog);
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
    if (dialog->custom_adv_check) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->custom_adv_check), FALSE);
    }
    gtk_editable_set_text(GTK_EDITABLE(dialog->custom_path_entry), "");
    sync_analysis_tab_sensitivity(dialog);
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
    update_custom_controls_state(dialog);
    sync_analysis_tab_sensitivity(dialog);
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
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "UCI Engine (*.exe)");
    gtk_file_filter_add_pattern(filter, "*.exe");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(fd, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
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
            
            // Auto-Focus Parent on Destroy
            gui_utils_setup_auto_focus_restore(GTK_WINDOW(error_window));
            
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
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "NNUE Evaluation (*.nnue)");
    gtk_file_filter_add_pattern(filter, "*.nnue");
    
    GListStore* filter_list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filter_list, filter);
    gtk_file_dialog_set_filters(fd, G_LIST_MODEL(filter_list));
    g_object_unref(filter_list);
    
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
        // Explicitly restore focus since we are hiding, not destroying
        if (dialog->parent_window) {
             gtk_window_present(dialog->parent_window);
        }
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

static void sync_analysis_tab_sensitivity(AiDialog* dialog) {
    if (!dialog) return;

    bool main_on = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->analysis_toggle));
    
    // 1. Child Toggles
    gtk_widget_set_sensitive(dialog->advantage_bar_toggle, main_on);
    gtk_widget_set_sensitive(dialog->mate_warning_toggle, main_on);
    gtk_widget_set_sensitive(dialog->hanging_pieces_toggle, main_on);
    gtk_widget_set_sensitive(dialog->move_rating_toggle, main_on);
    gtk_widget_set_sensitive(dialog->analysis_engine_internal, main_on);
    
    // 2. Custom Engine Gating
    const char* custom_path = gtk_editable_get_text(GTK_EDITABLE(dialog->custom_path_entry));
    bool custom_avail = (custom_path && strlen(custom_path) > 0);
    
    gtk_widget_set_sensitive(dialog->analysis_engine_custom, main_on && custom_avail);
    
    // Fallback if custom becomes unavailable while selected
    if (main_on && !custom_avail && gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->analysis_engine_custom))) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->analysis_engine_internal), TRUE);
    }
    
    // 3. Hint & Connect Button
    if (custom_avail) {
        char buf[256];
        char* basename = g_path_get_basename(custom_path);
        snprintf(buf, sizeof(buf), "<span size='small'>Connected: %s</span>", basename);
        gtk_label_set_markup(GTK_LABEL(dialog->analysis_cust_hint), buf);
        g_free(basename);
        if (gtk_widget_get_visible(dialog->analysis_cust_connect_btn))
            gtk_widget_set_visible(dialog->analysis_cust_connect_btn, FALSE);
    } else {
        gtk_label_set_markup(GTK_LABEL(dialog->analysis_cust_hint), 
            "<span size='small'>No custom engine connected. Configure it in the next tab.</span>");
        if (gtk_widget_get_visible(dialog->analysis_cust_connect_btn) != main_on)
            gtk_widget_set_visible(dialog->analysis_cust_connect_btn, main_on);
    }
    gtk_widget_set_opacity(dialog->analysis_cust_hint, 0.6);
}

static void on_analysis_toggle_toggled(GtkCheckButton* btn, gpointer user_data) {
    (void)btn;
    sync_analysis_tab_sensitivity((AiDialog*)user_data);
}


static void on_connect_btn_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AiDialog* dialog = (AiDialog*)user_data;
    // Jump to Custom Engine Tab (Index 1 usually, but let's check)
    // 0: Internal, 1: Custom, 2: Analysis
    gtk_notebook_set_current_page(GTK_NOTEBOOK(dialog->notebook), 1);
}

static void on_focus_lost_gesture(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    GtkWidget* widget = GTK_WIDGET(user_data);
    gtk_widget_grab_focus(widget);
}

// --- Construction Helper ---

static void ai_dialog_build_ui(AiDialog* dialog) {
    dialog->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_add_css_class(dialog->content_box, "settings-content");
    gtk_widget_set_margin_top(dialog->content_box, 24);
    gtk_widget_set_margin_bottom(dialog->content_box, 24); 
    gtk_widget_set_margin_start(dialog->content_box, 24);
    gtk_widget_set_margin_end(dialog->content_box, 24);
    gtk_widget_set_focusable(dialog->content_box, TRUE); // Allow it to take focus to clear entries
    
    // Main Title
    GtkWidget* title = gtk_label_new("AI Configuration");
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttribute* size = pango_attr_size_new(24 * PANGO_SCALE);
    pango_attr_list_insert(attrs, weight);
    pango_attr_list_insert(attrs, size);
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dialog->content_box), title);
    
    dialog->notebook = gtk_notebook_new();
    gtk_widget_add_css_class(dialog->notebook, "ai-notebook");
    gtk_widget_set_vexpand(dialog->notebook, TRUE);
    gtk_box_append(GTK_BOX(dialog->content_box), dialog->notebook);
    
    // --- TAB 1: Internal Engine ---
    GtkWidget* int_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(int_tab, "settings-content");
    gtk_widget_set_margin_top(int_tab, 24);
    gtk_widget_set_margin_bottom(int_tab, 24);
    gtk_widget_set_margin_start(int_tab, 24);
    gtk_widget_set_margin_end(int_tab, 24);
    
    GtkWidget* int_header = gtk_label_new("Stockfish 17.1 (Inbuilt)");
    gtk_widget_add_css_class(int_header, "heading");
    gtk_widget_set_halign(int_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(int_tab), int_header);

    // ELO Settings
    GtkWidget* elo_label = gtk_label_new("Difficulty (ELO):");
    gtk_widget_set_halign(elo_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(int_tab), elo_label);
    
    dialog->int_elo_adj = gtk_adjustment_new(1500, 100, 3600, 50, 500, 0);
    g_signal_connect(dialog->int_elo_adj, "value-changed", G_CALLBACK(on_int_elo_changed), dialog);
    
    dialog->elo_slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, dialog->int_elo_adj);
    gtk_scale_set_draw_value(GTK_SCALE(dialog->elo_slider), FALSE);
    gtk_box_append(GTK_BOX(int_tab), dialog->elo_slider);
    
    dialog->elo_spin = gtk_spin_button_new(dialog->int_elo_adj, 50, 0);
    gtk_box_append(GTK_BOX(int_tab), dialog->elo_spin);

    // UCI Usage Instructions for Inbuilt
    GtkWidget* inbuilt_instr_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(inbuilt_instr_vbox, 5);
    gtk_widget_set_halign(inbuilt_instr_vbox, GTK_ALIGN_START); // Align container to start
    GtkWidget* inbuilt_instr_label = gtk_label_new(
        "<span size='small' font_style='italic'>This inbuilt Stockfish 17.1 can be used via UCI protocol.\n"
        "It supports standard options like Skill Level (ELO), Depth, and NNUE.</span>"
    );
    gtk_label_set_use_markup(GTK_LABEL(inbuilt_instr_label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(inbuilt_instr_label), TRUE);
    gtk_widget_set_halign(inbuilt_instr_label, GTK_ALIGN_START); // Align label text to start
    gtk_label_set_xalign(GTK_LABEL(inbuilt_instr_label), 0.0); // Ensure text lines align left
    gtk_widget_set_opacity(inbuilt_instr_label, 0.7);
    gtk_box_append(GTK_BOX(inbuilt_instr_vbox), inbuilt_instr_label);
    gtk_box_append(GTK_BOX(int_tab), inbuilt_instr_vbox);

    gtk_box_append(GTK_BOX(int_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Internal Advanced
    dialog->int_adv_check = gtk_check_button_new_with_label("Use Advanced Search Mode");
    // gtk_widget_add_css_class(dialog->int_adv_check, "selection-mode"); // Optional trial
    g_signal_connect(dialog->int_adv_check, "toggled", G_CALLBACK(on_int_advanced_toggled), dialog);
    gtk_box_append(GTK_BOX(int_tab), dialog->int_adv_check);

    dialog->int_adv_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16); // Increased spacing
    gtk_widget_set_margin_top(dialog->int_adv_vbox, 12);
    gtk_widget_set_visible(dialog->int_adv_vbox, FALSE);
    
    GtkWidget* d_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* d_label = gtk_label_new("Target Depth:");
    gtk_widget_set_visible(d_label, TRUE); // Ensure visible
    gtk_widget_set_size_request(d_label, 140, -1); // Fixed width for alignment
    gtk_widget_set_halign(d_label, GTK_ALIGN_START); // Text start
    gtk_label_set_xalign(GTK_LABEL(d_label), 0.0);
    // gtk_widget_set_margin_end(d_label, 12); // Margin less critical with fixed width but kept if needed
    gtk_box_append(GTK_BOX(d_hbox), d_label);
    dialog->int_depth_spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_widget_set_size_request(dialog->int_depth_spin, 120, -1); // Fixed width
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_depth_spin), 10);
    g_signal_connect(dialog->int_depth_spin, "value-changed", G_CALLBACK(on_int_depth_changed), dialog);
    gtk_box_append(GTK_BOX(d_hbox), dialog->int_depth_spin);
    gtk_box_append(GTK_BOX(dialog->int_adv_vbox), d_hbox);
    
    GtkWidget* t_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* t_label = gtk_label_new("Move Time (ms):");
    gtk_widget_set_size_request(t_label, 140, -1); // Fixed width for alignment
    gtk_widget_set_halign(t_label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(t_label), 0.0);
    // gtk_widget_set_margin_end(t_label, 12);
    gtk_box_append(GTK_BOX(t_hbox), t_label);
    dialog->int_time_spin = gtk_spin_button_new_with_range(10, 600000, 100);
    gtk_widget_set_size_request(dialog->int_time_spin, 120, -1); // Fixed width
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_time_spin), 500);
    g_signal_connect(dialog->int_time_spin, "value-changed", G_CALLBACK(on_int_time_changed), dialog);
    gtk_box_append(GTK_BOX(t_hbox), dialog->int_time_spin);
    gtk_box_append(GTK_BOX(dialog->int_adv_vbox), t_hbox);
    
    GtkWidget* int_reset_btn = gtk_button_new_with_label("Reset to Defaults");
    gtk_widget_add_css_class(int_reset_btn, "destructive-action");
    g_signal_connect(int_reset_btn, "clicked", G_CALLBACK(on_int_reset_adv_clicked), dialog);
    gtk_box_append(GTK_BOX(dialog->int_adv_vbox), int_reset_btn);
    
    gtk_box_append(GTK_BOX(int_tab), dialog->int_adv_vbox);

    gtk_box_append(GTK_BOX(int_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // NNUE Section
    GtkWidget* nnue_header = gtk_label_new("NNUE Evaluation");
    gtk_widget_add_css_class(nnue_header, "heading");
    gtk_widget_set_halign(nnue_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(int_tab), nnue_header);

    GtkWidget* nnue_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    dialog->nnue_path_label = gtk_label_new("None");
    gtk_widget_set_hexpand(dialog->nnue_path_label, TRUE);
    gtk_box_append(GTK_BOX(nnue_hbox), dialog->nnue_path_label);
    
    GtkWidget* imp_btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(imp_btn), gtk_image_new_from_icon_name("document-open-symbolic"));
    gtk_widget_add_css_class(imp_btn, "ai-icon-button");
    g_signal_connect(imp_btn, "clicked", G_CALLBACK(on_nnue_import_clicked), dialog);
    gtk_box_append(GTK_BOX(nnue_hbox), imp_btn);
    
    GtkWidget* del_btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(del_btn), gtk_image_new_from_icon_name("edit-delete-symbolic"));
    gtk_widget_add_css_class(del_btn, "ai-icon-button");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_nnue_delete_clicked), dialog);
    gtk_box_append(GTK_BOX(nnue_hbox), del_btn);
    gtk_box_append(GTK_BOX(int_tab), nnue_hbox);
    
    dialog->nnue_toggle = gtk_check_button_new_with_label("Enable NNUE");
    gtk_widget_set_visible(dialog->nnue_toggle, FALSE);
    gtk_box_append(GTK_BOX(int_tab), dialog->nnue_toggle);

    // Wrap Internal Tab in Scroller
    GtkWidget* int_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(int_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(int_scroller), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(int_scroller), int_tab);

    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), int_scroller, gtk_label_new("Internal Engine"));

    // --- TAB 2: Custom Engine ---
    GtkWidget* custom_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(custom_tab, "settings-content");
    gtk_widget_set_margin_top(custom_tab, 24);
    gtk_widget_set_margin_bottom(custom_tab, 24);
    gtk_widget_set_margin_start(custom_tab, 24);
    gtk_widget_set_margin_end(custom_tab, 24);

    GtkWidget* cust_header = gtk_label_new("Custom UCI Engine");
    gtk_widget_add_css_class(cust_header, "heading");
    gtk_widget_set_halign(cust_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(custom_tab), cust_header);
    
    GtkWidget* path_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    dialog->custom_path_entry = gtk_entry_new();
    gtk_widget_set_hexpand(dialog->custom_path_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(dialog->custom_path_entry), "Select UCI executable...");
    g_signal_connect(dialog->custom_path_entry, "changed", G_CALLBACK(on_custom_path_changed), dialog);
    gtk_box_append(GTK_BOX(path_hbox), dialog->custom_path_entry);
    
    GtkWidget* browse_btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(browse_btn), gtk_image_new_from_icon_name("folder-open-symbolic"));
    gtk_widget_add_css_class(browse_btn, "ai-icon-button");
    gtk_widget_set_tooltip_text(browse_btn, "Browse...");
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_clicked), dialog);
    gtk_box_append(GTK_BOX(path_hbox), browse_btn);

    GtkWidget* clear_btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(clear_btn), gtk_image_new_from_icon_name("edit-delete-symbolic"));
    gtk_widget_add_css_class(clear_btn, "ai-icon-button");
    gtk_widget_set_tooltip_text(clear_btn, "Clear Path / Remove Engine");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_path_clicked), dialog);
    gtk_box_append(GTK_BOX(path_hbox), clear_btn);

    gtk_box_append(GTK_BOX(custom_tab), path_hbox);
    
    dialog->custom_status_label = gtk_label_new("");
    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_status_label);

    gtk_box_append(GTK_BOX(custom_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Custom ELO (Synced with Internal)
    GtkWidget* c_elo_label = gtk_label_new("Difficulty (ELO):");
    gtk_widget_set_halign(c_elo_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(custom_tab), c_elo_label);

    dialog->custom_elo_adj = gtk_adjustment_new(1500, 100, 3600, 50, 500, 0);
    g_signal_connect(dialog->custom_elo_adj, "value-changed", G_CALLBACK(on_custom_elo_changed), dialog);

    dialog->custom_elo_slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, dialog->custom_elo_adj);
    gtk_scale_set_draw_value(GTK_SCALE(dialog->custom_elo_slider), FALSE);
    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_elo_slider);

    dialog->custom_elo_spin = gtk_spin_button_new(dialog->custom_elo_adj, 50, 0);
    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_elo_spin);

    gtk_box_append(GTK_BOX(custom_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Custom Advanced
    dialog->custom_adv_check = gtk_check_button_new_with_label("Use Advanced Search Mode");
    g_signal_connect(dialog->custom_adv_check, "toggled", G_CALLBACK(on_custom_advanced_toggled), dialog);
    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_adv_check);

    dialog->custom_adv_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(dialog->custom_adv_vbox, 12);
    gtk_widget_set_visible(dialog->custom_adv_vbox, FALSE);

    GtkWidget* cd_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* cd_label = gtk_label_new("Target Depth:");
    gtk_widget_set_size_request(cd_label, 140, -1);
    gtk_widget_set_halign(cd_label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(cd_label), 0.0);
    // gtk_widget_set_margin_end(cd_label, 12);
    gtk_box_append(GTK_BOX(cd_hbox), cd_label);
    dialog->custom_depth_spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_widget_set_size_request(dialog->custom_depth_spin, 120, -1); // Fixed width
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_depth_spin), 10);
    g_signal_connect(dialog->custom_depth_spin, "value-changed", G_CALLBACK(on_custom_depth_changed), dialog);
    gtk_box_append(GTK_BOX(cd_hbox), dialog->custom_depth_spin);
    gtk_box_append(GTK_BOX(dialog->custom_adv_vbox), cd_hbox);

    GtkWidget* ct_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* ct_label = gtk_label_new("Move Time (ms):");
    gtk_widget_set_size_request(ct_label, 140, -1);
    gtk_widget_set_halign(ct_label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(ct_label), 0.0);
    // gtk_widget_set_margin_end(ct_label, 12);
    gtk_box_append(GTK_BOX(ct_hbox), ct_label);
    dialog->custom_time_spin = gtk_spin_button_new_with_range(10, 600000, 100);
    gtk_widget_set_size_request(dialog->custom_time_spin, 120, -1); // Fixed width
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_time_spin), 500);
    g_signal_connect(dialog->custom_time_spin, "value-changed", G_CALLBACK(on_custom_time_changed), dialog);
    gtk_box_append(GTK_BOX(ct_hbox), dialog->custom_time_spin);
    gtk_box_append(GTK_BOX(dialog->custom_adv_vbox), ct_hbox);

    GtkWidget* cust_reset_btn = gtk_button_new_with_label("Reset to Defaults");
    gtk_widget_add_css_class(cust_reset_btn, "destructive-action");
    g_signal_connect(cust_reset_btn, "clicked", G_CALLBACK(on_custom_reset_adv_clicked), dialog);
    gtk_box_append(GTK_BOX(dialog->custom_adv_vbox), cust_reset_btn);

    gtk_box_append(GTK_BOX(custom_tab), dialog->custom_adv_vbox);

    gtk_box_append(GTK_BOX(custom_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // Instructions
    GtkWidget* usage_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget* usage_title = gtk_label_new("How to use:");
    gtk_widget_add_css_class(usage_title, "heading");
    gtk_widget_set_halign(usage_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(usage_vbox), usage_title);
    GtkWidget* usage_label = gtk_label_new("Browse for a UCI compatible executable. Once selected, it will be available in the game panel dropdown.");
    gtk_label_set_wrap(GTK_LABEL(usage_label), TRUE);
    gtk_widget_set_opacity(usage_label, 0.7);
    gtk_box_append(GTK_BOX(usage_vbox), usage_label);
    gtk_box_append(GTK_BOX(custom_tab), usage_vbox);

    // Wrap Custom Tab in Scroller
    GtkWidget* cust_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cust_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(cust_scroller), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(cust_scroller), custom_tab);

    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), cust_scroller, gtk_label_new("Custom Engine"));

    // --- TAB 3: Live Analysis ---
    GtkWidget* analysis_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(analysis_tab, "settings-content");
    gtk_widget_set_margin_top(analysis_tab, 24);
    gtk_widget_set_margin_bottom(analysis_tab, 24);
    gtk_widget_set_margin_start(analysis_tab, 24);
    gtk_widget_set_margin_end(analysis_tab, 24);

    GtkWidget* anal_header = gtk_label_new("Real-time Evaluation");
    gtk_widget_add_css_class(anal_header, "heading");
    gtk_widget_set_halign(anal_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(analysis_tab), anal_header);

    dialog->analysis_toggle = gtk_check_button_new_with_label("Enable Live Analysis");
    g_signal_connect(dialog->analysis_toggle, "toggled", G_CALLBACK(on_analysis_toggle_toggled), dialog);
    gtk_box_append(GTK_BOX(analysis_tab), dialog->analysis_toggle);

    gtk_box_append(GTK_BOX(analysis_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    dialog->advantage_bar_toggle = gtk_check_button_new_with_label("Show Vertical Advantage Bar");
    gtk_box_append(GTK_BOX(analysis_tab), dialog->advantage_bar_toggle);

    dialog->mate_warning_toggle = gtk_check_button_new_with_label("Show Mate-in-X Warning Chip");
    gtk_box_append(GTK_BOX(analysis_tab), dialog->mate_warning_toggle);

    dialog->hanging_pieces_toggle = gtk_check_button_new_with_label("Show Hanging Pieces Counter");
    gtk_box_append(GTK_BOX(analysis_tab), dialog->hanging_pieces_toggle);

    dialog->move_rating_toggle = gtk_check_button_new_with_label("Show After-Move Rating Toast");
    gtk_box_append(GTK_BOX(analysis_tab), dialog->move_rating_toggle);

    GtkWidget* anal_instr = gtk_label_new("When enabled, the engine will analyze the current position in the background during your turn.");
    gtk_label_set_wrap(GTK_LABEL(anal_instr), TRUE);
    gtk_widget_set_opacity(anal_instr, 0.7);
    gtk_box_append(GTK_BOX(analysis_tab), anal_instr);

    gtk_box_append(GTK_BOX(analysis_tab), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* engine_header = gtk_label_new("Analysis Engine");
    gtk_widget_add_css_class(engine_header, "heading");
    gtk_widget_set_halign(engine_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(analysis_tab), engine_header);

    dialog->analysis_engine_internal = gtk_check_button_new_with_label("Internal Engine (Stockfish 17.1)");
    gtk_box_append(GTK_BOX(analysis_tab), dialog->analysis_engine_internal);

    dialog->analysis_engine_custom = gtk_check_button_new_with_label("Custom UCI Engine");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(dialog->analysis_engine_custom), GTK_CHECK_BUTTON(dialog->analysis_engine_internal));
    gtk_box_append(GTK_BOX(analysis_tab), dialog->analysis_engine_custom);
    
    // Help label for custom
    GtkWidget* hint_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    dialog->analysis_cust_hint = gtk_label_new("");
    gtk_label_set_use_markup(GTK_LABEL(dialog->analysis_cust_hint), TRUE);
    gtk_widget_set_halign(dialog->analysis_cust_hint, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(hint_hbox), dialog->analysis_cust_hint);

    dialog->analysis_cust_connect_btn = gtk_button_new_with_label("Connect...");
    gtk_widget_add_css_class(dialog->analysis_cust_connect_btn, "small-button");
    g_signal_connect(dialog->analysis_cust_connect_btn, "clicked", G_CALLBACK(on_connect_btn_clicked), dialog);
    gtk_box_append(GTK_BOX(hint_hbox), dialog->analysis_cust_connect_btn);
    
    gtk_box_append(GTK_BOX(analysis_tab), hint_hbox);

    gtk_notebook_append_page(GTK_NOTEBOOK(dialog->notebook), analysis_tab, gtk_label_new("Analysis"));

    // Focus clearing gesture on main box
    GtkGesture* gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_focus_lost_gesture), dialog->content_box);
    gtk_widget_add_controller(dialog->content_box, GTK_EVENT_CONTROLLER(gesture));
    
    // Init state
    update_custom_controls_state(dialog);
}

// --- Public API ---

AiDialog* ai_dialog_new_embedded(void) {
    AiDialog* dialog = (AiDialog*)calloc(1, sizeof(AiDialog));
    if (!dialog) return NULL;
    
    dialog->int_elo = 1500;
    dialog->custom_elo = 1500;
    dialog->int_manual_movetime = false;
    dialog->custom_manual_movetime = false;
    
    ai_dialog_build_ui(dialog);
    // Only construct content, no window
    
    // Take ownership of the content box to prevent destruction when removed from parents
    if (dialog->content_box) {
        g_object_ref_sink(dialog->content_box);
    }
    
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
        // If we have a window, destroying it handles the content (if it's child)
        // But if we hold a hard ref (embedded), we must release it.
        // Identify if we need to release content_box:
        if (dialog->content_box) {
            g_object_unref(dialog->content_box);
        }
        if (dialog->window) gtk_window_destroy(dialog->window);
        free(dialog);
    }
}

int ai_dialog_get_elo(AiDialog* dialog, bool is_custom) { 
    if (!dialog) return 1500;
    return is_custom ? dialog->custom_elo : dialog->int_elo;
}

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

void ai_dialog_set_elo(AiDialog* dialog, int elo, bool is_custom) {
    if (!dialog) return;
    
    if (is_custom) {
        dialog->custom_elo = elo;
        if (dialog->custom_elo_slider) {
            gtk_range_set_value(GTK_RANGE(dialog->custom_elo_slider), (double)elo);
        }
    } else {
        dialog->int_elo = elo;
        if (dialog->elo_slider) {
            gtk_range_set_value(GTK_RANGE(dialog->elo_slider), (double)elo);
        }
    }
}

void ai_dialog_set_settings_changed_callback(AiDialog* dialog, AiSettingsChangedCallback cb, void* data) {
    if (dialog) {
        dialog->change_cb = cb;
        dialog->change_cb_data = data;
    }
}

#include "config_manager.h"

void ai_dialog_load_config(AiDialog* dialog, void* config_struct) {
    if (!dialog || !config_struct) return;
    AppConfig* cfg = (AppConfig*)config_struct;
    
    // Internal Engine
    if (cfg->int_elo >= 100) ai_dialog_set_elo(dialog, cfg->int_elo, false);
    if (dialog->int_depth_spin) gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_depth_spin), (double)cfg->int_depth);
    if (dialog->int_time_spin) gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->int_time_spin), (double)cfg->int_movetime);
    if (dialog->int_adv_check) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->int_adv_check), cfg->int_is_advanced);
    if (dialog->int_adv_vbox) gtk_widget_set_visible(dialog->int_adv_vbox, cfg->int_is_advanced);
    
    // NNUE
    if (strlen(cfg->nnue_path) > 0) {
        if (dialog->nnue_path) g_free(dialog->nnue_path);
        dialog->nnue_path = g_strdup(cfg->nnue_path);
        if (dialog->nnue_path_label) {
            char* basename = g_path_get_basename(dialog->nnue_path);
            gtk_label_set_text(GTK_LABEL(dialog->nnue_path_label), basename);
            g_free(basename);
        }
    }
    if (dialog->nnue_toggle) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->nnue_toggle), cfg->nnue_enabled);

    // Custom Engine
    if (strlen(cfg->custom_engine_path) > 0) {
        if (dialog->custom_path_entry) gtk_editable_set_text(GTK_EDITABLE(dialog->custom_path_entry), cfg->custom_engine_path);
    }
    if (cfg->custom_elo >= 100) ai_dialog_set_elo(dialog, cfg->custom_elo, true);
    if (dialog->custom_depth_spin) gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_depth_spin), (double)cfg->custom_depth);
    if (dialog->custom_time_spin) gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->custom_time_spin), (double)cfg->custom_movetime);
    if (dialog->custom_adv_check) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->custom_adv_check), cfg->custom_is_advanced);
    if (dialog->custom_adv_vbox) gtk_widget_set_visible(dialog->custom_adv_vbox, cfg->custom_is_advanced);
    
    // Analysis
    if (dialog->analysis_toggle) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->analysis_toggle), cfg->enable_live_analysis);
    if (dialog->advantage_bar_toggle) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->advantage_bar_toggle), cfg->show_advantage_bar);
    if (dialog->mate_warning_toggle) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->mate_warning_toggle), cfg->show_mate_warning);
    if (dialog->hanging_pieces_toggle) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->hanging_pieces_toggle), cfg->show_hanging_pieces);
    if (dialog->move_rating_toggle) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->move_rating_toggle), cfg->show_move_rating);
    
    if (cfg->analysis_use_custom) {
        if (dialog->analysis_engine_custom) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->analysis_engine_custom), TRUE);
    } else {
        if (dialog->analysis_engine_internal) gtk_check_button_set_active(GTK_CHECK_BUTTON(dialog->analysis_engine_internal), TRUE);
    }

    sync_analysis_tab_sensitivity(dialog);
}

void ai_dialog_save_config(AiDialog* dialog, void* config_struct) {
    if (!dialog || !config_struct) return;
    AppConfig* cfg = (AppConfig*)config_struct;
    
    // Internal
    cfg->int_elo = ai_dialog_get_elo(dialog, false);
    if (dialog->int_depth_spin) cfg->int_depth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->int_depth_spin));
    if (dialog->int_time_spin) cfg->int_movetime = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->int_time_spin));
    if (dialog->int_adv_check) cfg->int_is_advanced = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->int_adv_check));
    
    // NNUE
    bool nnue_on = false;
    const char* path = ai_dialog_get_nnue_path(dialog, &nnue_on);
    cfg->nnue_enabled = nnue_on;
    if (path) {
        strncpy(cfg->nnue_path, path, sizeof(cfg->nnue_path) - 1);
        cfg->nnue_path[sizeof(cfg->nnue_path) - 1] = '\0';
    } else {
        cfg->nnue_path[0] = '\0';
    }
    
    // Custom
    const char* custom_path = ai_dialog_get_custom_path(dialog);
    if (custom_path) {
        strncpy(cfg->custom_engine_path, custom_path, sizeof(cfg->custom_engine_path) - 1);
        cfg->custom_engine_path[sizeof(cfg->custom_engine_path) - 1] = '\0';
    } else {
        cfg->custom_engine_path[0] = '\0';
    }
    
    cfg->custom_elo = ai_dialog_get_elo(dialog, true);
    if (dialog->custom_depth_spin) cfg->custom_depth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->custom_depth_spin));
    if (dialog->custom_time_spin) cfg->custom_movetime = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dialog->custom_time_spin));
    if (dialog->custom_adv_check) cfg->custom_is_advanced = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->custom_adv_check));

    // Analysis
    if (dialog->analysis_toggle) cfg->enable_live_analysis = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->analysis_toggle));
    if (dialog->advantage_bar_toggle) cfg->show_advantage_bar = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->advantage_bar_toggle));
    if (dialog->mate_warning_toggle) cfg->show_mate_warning = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->mate_warning_toggle));
    if (dialog->hanging_pieces_toggle) cfg->show_hanging_pieces = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->hanging_pieces_toggle));
    if (dialog->move_rating_toggle) cfg->show_move_rating = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->move_rating_toggle));
    
    if (dialog->analysis_engine_custom) {
        cfg->analysis_use_custom = gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->analysis_engine_custom));
    }
}

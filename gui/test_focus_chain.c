#include <gtk/gtk.h>
#include "gui_utils.h"

// Helper to close the window (Focus return is handled by destroy signal now)
static void on_close_clicked(GtkWidget *btn, gpointer user_data) {
    (void)user_data;
    GtkRoot *root = gtk_widget_get_root(btn);
    if (GTK_IS_WINDOW(root)) {
        gtk_window_destroy(GTK_WINDOW(root));
    }
}

// Window C (Depth 3)
static void on_open_c_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkWindow *parent = GTK_WINDOW(data);
    
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Window C (Depth 3)");
    gtk_window_set_default_size(GTK_WINDOW(win), 300, 200);
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(GTK_WINDOW(win));
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    
    GtkWidget *label = gtk_label_new("I am Window C.\nClosing me (via X or Button) focuses B.");
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);

    GtkWidget *btn_close = gtk_button_new_with_label("Close");
    g_signal_connect(btn_close, "clicked", G_CALLBACK(on_close_clicked), NULL);
    
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), btn_close);
    gtk_window_set_child(GTK_WINDOW(win), box);
    
    gtk_window_present(GTK_WINDOW(win));
}

// Window B (Depth 2)
static void on_open_b_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    GtkWindow *parent = GTK_WINDOW(data);
    
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Window B (Depth 2)");
    gtk_window_set_default_size(GTK_WINDOW(win), 300, 200);
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    
    // Auto-Focus Parent on Destroy
    gui_utils_setup_auto_focus_restore(GTK_WINDOW(win));
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    
    GtkWidget *label = gtk_label_new("I am Window B.\nOpen C, or close to focus A.");
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);

    GtkWidget *btn_open_c = gtk_button_new_with_label("Open Window C");
    g_signal_connect(btn_open_c, "clicked", G_CALLBACK(on_open_c_clicked), win);

    GtkWidget *btn_close = gtk_button_new_with_label("Close");
    g_signal_connect(btn_close, "clicked", G_CALLBACK(on_close_clicked), NULL);
    
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), btn_open_c);
    gtk_box_append(GTK_BOX(box), btn_close);
    gtk_window_set_child(GTK_WINDOW(win), box);
    
    gtk_window_present(GTK_WINDOW(win));
}

// Main Window A (Root)
static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Window A (Root)");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    
    GtkWidget *label = gtk_label_new("This is the Main Application Window (A).\nOpen the chain to test focus return.");
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);

    GtkWidget *btn = gtk_button_new_with_label("Open Window B");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_open_b_clicked), window);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), btn);
    gtk_window_set_child(GTK_WINDOW(window), box);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.gtk.example.focuschain", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

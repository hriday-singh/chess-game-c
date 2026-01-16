#include "gui_file_dialog.h"
#include <gtk/gtk.h>

typedef struct {
    FileSelectCallback callback;
    gpointer user_data;
} DialogContext;

static void finish_dialog(GObject* source, GAsyncResult* res, DialogContext* ctx, gboolean is_save) {
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
    GFile* file = NULL;
    GError* error = NULL;
    
    if (is_save) {
        file = gtk_file_dialog_save_finish(dialog, res, &error);
    } else {
        file = gtk_file_dialog_open_finish(dialog, res, &error);
    }
    
    if (file) {
        char* path = g_file_get_path(file);
        if (path) {
            if (ctx && ctx->callback) {
                ctx->callback(path, ctx->user_data);
            }
            g_free(path);
        }
        g_object_unref(file);
    } else {
        if (error) {
            // Cancelled or error
            g_error_free(error);
        }
    }
    
    if (ctx) free(ctx);
    g_object_unref(dialog); // Release reference added during creation
}

static void on_open_ready(GObject* source, GAsyncResult* res, gpointer user_data) {
    finish_dialog(source, res, (DialogContext*)user_data, FALSE);
}

static void on_save_ready(GObject* source, GAsyncResult* res, gpointer user_data) {
    finish_dialog(source, res, (DialogContext*)user_data, TRUE);
}

static GtkFileDialog* create_dialog_common(const char* title, 
                                          const char* filter_name, 
                                          const char** patterns) {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title);
    
    if (filter_name && patterns) {
        GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
        
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, filter_name);
        for (int i = 0; patterns[i] != NULL; i++) {
            gtk_file_filter_add_pattern(filter, patterns[i]);
        }
        g_list_store_append(filters, filter);
        g_object_unref(filter);
        
        gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
        g_object_unref(filters);
    }
    
    return dialog;
}

void gui_file_dialog_open(GtkWindow* parent, 
                          const char* title, 
                          const char* filter_name, 
                          const char** patterns, 
                          FileSelectCallback on_select, 
                          gpointer user_data) {
    GtkFileDialog* dialog = create_dialog_common(title, filter_name, patterns);
    
    DialogContext* ctx = malloc(sizeof(DialogContext));
    if (ctx) {
        ctx->callback = on_select;
        ctx->user_data = user_data;
    }
    
    // Pass dialog as source, but keep it alive
    // gtk_file_dialog_new returned a floating ref or regular? regular. 
    // It's a GObject.
    // We should ensure it lives until callback. GTask usually holds source ref.
    
    gtk_file_dialog_open(dialog, parent, NULL, on_open_ready, ctx);
}

void gui_file_dialog_save(GtkWindow* parent, 
                          const char* title, 
                          const char* suggested_name,
                          const char* filter_name, 
                          const char** patterns, 
                          FileSelectCallback on_select, 
                          gpointer user_data) {
    GtkFileDialog* dialog = create_dialog_common(title, filter_name, patterns);
    
    if (suggested_name) {
        gtk_file_dialog_set_initial_name(dialog, suggested_name);
    }
    
    DialogContext* ctx = malloc(sizeof(DialogContext));
    if (ctx) {
        ctx->callback = on_select;
        ctx->user_data = user_data;
    }
    
    gtk_file_dialog_save(dialog, parent, NULL, on_save_ready, ctx);
}

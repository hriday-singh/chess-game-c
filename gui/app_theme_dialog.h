#ifndef APP_THEME_DIALOG_H
#define APP_THEME_DIALOG_H

#include <gtk/gtk.h>

// Callback type (optional, generic)
typedef void (*AppThemeUpdateCallback)(void* user_data);

typedef struct AppThemeDialog AppThemeDialog;

// Create a new App Theme Dialog
// If parent_window is provided, it will be transient to it.
AppThemeDialog* app_theme_dialog_new(GtkWindow* parent_window);

// Create embedded version (no window created)
AppThemeDialog* app_theme_dialog_new_embedded(GtkWindow* parent_window);

// Show the dialog
void app_theme_dialog_show(AppThemeDialog* dialog);

// Free the dialog
void app_theme_dialog_free(AppThemeDialog* dialog);

// Get the widget (if embedded) - mostly for settings dialog
GtkWidget* app_theme_dialog_get_widget(AppThemeDialog* dialog);

// Save configuration (called when settings closes)
void app_theme_dialog_save_config(AppThemeDialog* dialog, void* config_struct);

#endif // APP_THEME_DIALOG_H

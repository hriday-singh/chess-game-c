#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <gtk/gtk.h>
#include "app_state.h"

// Forward declaration is handled by app_state.h include
// typedef struct AppState AppState;

typedef struct _SettingsDialog SettingsDialog;

// Create a new settings dialog
SettingsDialog* settings_dialog_new(AppState* app_state);

// Show the settings dialog
void settings_dialog_show(SettingsDialog* dialog);

// Get the GtkWindow
GtkWindow* settings_dialog_get_window(SettingsDialog* dialog);

// Ensure only one instance exists or bring to front
void settings_dialog_present(SettingsDialog* dialog);

// Switch to a specific page
void settings_dialog_open_page(SettingsDialog* dialog, const char* page_name);

// Free resources (though typically managed by window destroy)
void settings_dialog_free(SettingsDialog* dialog);

#endif

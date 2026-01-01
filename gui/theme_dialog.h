#ifndef THEME_DIALOG_H
#define THEME_DIALOG_H

#include <gtk/gtk.h>
#include "theme_data.h"

// Forward declaration
typedef struct ThemeDialog ThemeDialog;

// Callback type for theme updates
typedef void (*ThemeUpdateCallback)(void* user_data);

// Create and show theme dialog
// on_update callback will be called whenever theme changes
ThemeDialog* theme_dialog_new(ThemeData* theme, ThemeUpdateCallback on_update, void* user_data);

// Show the dialog (modal)
void theme_dialog_show(ThemeDialog* dialog);

// Free the dialog
void theme_dialog_free(ThemeDialog* dialog);

#endif // THEME_DIALOG_H


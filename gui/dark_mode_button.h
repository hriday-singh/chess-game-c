#ifndef DARK_MODE_BUTTON_H
#define DARK_MODE_BUTTON_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * Creates a new Dark Mode toggle button widget.
 * This widget handles its own drawing, animations, and input.
 * It does NOT change the global theme, only its own visual state.
 */
GtkWidget* dark_mode_button_new(void);

/**
 * Checks if the button is currently in "Dark Mode" visual state.
 */
gboolean dark_mode_button_is_dark(GtkWidget* button);

G_END_DECLS

#endif // DARK_MODE_BUTTON_H

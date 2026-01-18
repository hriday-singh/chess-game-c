#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include <gtk/gtk.h>

/**
 * @brief Initializes and shows the splash screen.
 * 
 * @param parent_window The main application window (overlay will be added here).
 * @return GtkWidget* The splash screen container widget.
 */
GtkWidget* splash_screen_show(GtkWindow* parent_window);

/**
 * @brief Updates the status message on the splash screen.
 * 
 * @param splash The splash screen widget returned by splash_screen_show.
 * @param status The new status text.
 */
void splash_screen_update_status(GtkWidget* splash, const char* status);

/**
 * @brief Starts the transition from splash screen to main UI.
 * This includes the 800ms delay and fade-out animation.
 * 
 * @param splash The splash screen widget.
 * @param on_finished Callback to run after the splash screen is fully removed.
 * @param user_data Data to pass to the callback.
 */
void splash_screen_finish(GtkWidget* splash, GCallback on_finished, gpointer user_data);

#endif // SPLASH_SCREEN_H

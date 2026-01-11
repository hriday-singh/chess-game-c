#ifndef GUI_UTILS_H
#define GUI_UTILS_H

#include <gtk/gtk.h>

/**
 * @brief Finds the root window for the given widget and presents it (grabs focus).
 * 
 * This function effectively traverses up the widget hierarchy to find the toplevel
 * GtkWindow and calls gtk_window_present() on it. This is useful for returning
 * focus to the main application window from any child widget, dialog, or popover.
 * 
 * @param context_widget Any widget in the hierarchy (e.g., a button in a dialog).
 */
void gui_utils_focus_root(GtkWidget *context_widget);

/**
 * @brief Finds the root window for the given widget.
 * 
 * @param context_widget Any widget in the hierarchy.
 * @return GtkWindow* The root window, or NULL if not found.
 */
GtkWindow* gui_utils_get_root_window(GtkWidget *context_widget);

/**
 * @brief Focuses the transient parent of the window containing the context widget.
 * 
 * Use this when closing a dialog to ensure focus returns to the specific window
 * that opened it (the parent), rather than the absolute root of the application.
 * This preserves the focus chain (C -> B -> A).
 * 
 * @param context_widget The widget initiating the action (or the window itself).
 */
void gui_utils_focus_parent(GtkWidget *context_widget);

/**
 * @brief Sets up a window to automatically focus its parent when destroyed.
 * 
 * Creates a signal handler for the "destroy" signal that calls gui_utils_focus_parent().
 * This ensures that closing the window via 'X', Esc, or programmatic destroy handles
 * restores focus correctly.
 * 
 * @param window The GtkWindow to set up.
 */
void gui_utils_setup_auto_focus_restore(GtkWindow *window);

#endif // GUI_UTILS_H

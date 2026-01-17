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

/**
 * @brief Adds a controller to close the window when Escape is pressed.
 * 
 * @param window The GtkWindow (or widget castable to GtkWindow/GtkWidget).
 */
void gui_utils_add_esc_close(GtkWidget* window);

/**
 * @brief Creates a standard loading overlay widget structure.
 * 
 * Creates a "dimmed" background box containing a centered card with a spinner,
 * title, and subtitle. Adds this widget to the provided GtkOverlay as an overlay child.
 * 
 * @param parent_overlay The GtkOverlay to attach the loading view to.
 * @param out_spinner Pointer to store the created GtkSpinner widget (for start/stop).
 * @param title The main title text (e.g. "Importing Game").
 * @param subtitle The subtitle text (e.g. "Please wait...").
 * @return GtkWidget* The container widget representing the loading overlay (hidden by default).
 */
GtkWidget* gui_utils_create_loading_overlay(GtkOverlay* parent_overlay, GtkWidget** out_spinner, const char* title, const char* subtitle);

/**
 * @brief Sets the window size relative to a parent window (e.g. 0.8 width).
 * Falls back to reasonable defaults if parent is too small or NULL.
 */
void gui_utils_set_window_size_relative(GtkWindow* window, GtkWindow* relative_to, double w_factor, double h_factor);

#endif // GUI_UTILS_H

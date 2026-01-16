#ifndef GUI_FILE_DIALOG_H
#define GUI_FILE_DIALOG_H

#include <gtk/gtk.h>

/**
 * Callback function for file selection.
 * @param path The selected file path (absolute), or NULL if cancelled/error.
 * @param user_data The user data passed to the dialog function.
 */
typedef void (*FileSelectCallback)(const char* path, gpointer user_data);

/**
 * Opens a file selection dialog for opening files.
 * 
 * @param parent Parent window (transient for).
 * @param title Dialog title.
 * @param filter_name Name of the file filter (e.g. "Chess Files").
 * @param patterns NULL-terminated array of glob patterns (e.g. "*.pgn", "*.txt").
 * @param on_select Callback to invoke when a file is selected.
 * @param user_data User data passed to the callback.
 */
void gui_file_dialog_open(GtkWindow* parent, 
                          const char* title, 
                          const char* filter_name, 
                          const char** patterns, 
                          FileSelectCallback on_select, 
                          gpointer user_data);

/**
 * Opens a file selection dialog for saving files.
 * 
 * @param parent Parent window (transient for).
 * @param title Dialog title.
 * @param suggested_name Suggested filename (can be NULL).
 * @param filter_name Name of the file filter (e.g. "PGN Files").
 * @param patterns NULL-terminated array of glob patterns (e.g. "*.pgn").
 * @param on_select Callback to invoke when a file is selected.
 * @param user_data User data passed to the callback.
 */
void gui_file_dialog_save(GtkWindow* parent, 
                          const char* title, 
                          const char* suggested_name,
                          const char* filter_name, 
                          const char** patterns, 
                          FileSelectCallback on_select, 
                          gpointer user_data);

#endif // GUI_FILE_DIALOG_H

#ifndef PUZZLE_EDITOR_H
#define PUZZLE_EDITOR_H

#include <gtk/gtk.h>

// Forward declaration of AppState is not possible if it is defined in main.c
// But we can pass it as void* or include main's type if exposed.
// Main.c defines AppState internally. We should probably accept void* user_data 
// which is AppState*, or move AppState to a header.
// For now, assume void* user_data which we cast to AppState* in implementation.

// Callback type: void (*)(int new_puzzle_index, gpointer user_data)
void show_puzzle_editor(GtkWindow* parent, GCallback on_created, gpointer user_data);

#endif // PUZZLE_EDITOR_H

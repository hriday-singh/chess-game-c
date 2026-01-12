#ifndef PUZZLE_EDITOR_H
#define PUZZLE_EDITOR_H

#include <gtk/gtk.h>

// Callback type: void (*)(int new_puzzle_index, gpointer user_data)
void show_puzzle_editor(GtkWindow* parent, GCallback on_created, gpointer user_data);

#endif // PUZZLE_EDITOR_H

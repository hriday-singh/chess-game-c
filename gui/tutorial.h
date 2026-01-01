#ifndef TUTORIAL_H
#define TUTORIAL_H

#include <gtk/gtk.h>
#include "app_state.h"

// Handle tutorial menu action (Start Tutorial)
void on_tutorial_action(GSimpleAction* action, GVariant* parameter, gpointer user_data);

// Exit tutorial explicitly
void on_tutorial_exit(GtkButton* btn, gpointer user_data);

// Check tutorial progress (called from main update loop)
void tutorial_check_progress(AppState* state);

// Callback for invalid moves
void on_invalid_tutorial_move(void* user_data);

// Dialog helper (shared for tutorial)
void show_message_dialog(GtkWindow* parent, const char* message, AppState* state);

#endif

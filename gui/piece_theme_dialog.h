#ifndef PIECE_THEME_DIALOG_H
#define PIECE_THEME_DIALOG_H

#include "theme_data.h"
#include <gtk/gtk.h>

typedef struct PieceThemeDialog PieceThemeDialog;
typedef void (*PieceThemeUpdateCallback)(void* user_data);

PieceThemeDialog* piece_theme_dialog_new(ThemeData* theme, PieceThemeUpdateCallback on_update, void* user_data, GtkWindow* parent_window);
PieceThemeDialog* piece_theme_dialog_new_embedded(ThemeData* theme, PieceThemeUpdateCallback on_update, void* user_data);

GtkWidget* piece_theme_dialog_get_widget(PieceThemeDialog* dialog);
void piece_theme_dialog_set_parent_window(PieceThemeDialog* dialog, GtkWindow* parent);

void piece_theme_dialog_show(PieceThemeDialog* dialog);
void piece_theme_dialog_free(PieceThemeDialog* dialog);

// Save state to config
void piece_theme_dialog_save_config(PieceThemeDialog* dialog, void* config_struct);

#endif

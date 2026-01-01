#ifndef BOARD_THEME_DIALOG_H
#define BOARD_THEME_DIALOG_H

#include "theme_data.h"
#include <gtk/gtk.h>

typedef struct BoardThemeDialog BoardThemeDialog;
typedef void (*BoardThemeUpdateCallback)(void* user_data);

BoardThemeDialog* board_theme_dialog_new(ThemeData* theme, BoardThemeUpdateCallback on_update, void* user_data, GtkWindow* parent_window);
BoardThemeDialog* board_theme_dialog_new_embedded(ThemeData* theme, BoardThemeUpdateCallback on_update, void* user_data);

GtkWidget* board_theme_dialog_get_widget(BoardThemeDialog* dialog);
void board_theme_dialog_set_parent_window(BoardThemeDialog* dialog, GtkWindow* parent);

void board_theme_dialog_show(BoardThemeDialog* dialog);
void board_theme_dialog_free(BoardThemeDialog* dialog);

#endif

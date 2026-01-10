#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include "app_theme.h"

void theme_manager_init(void);
void theme_manager_set_dark(bool is_dark);
void theme_manager_toggle(void);
bool theme_manager_is_dark(void);
void theme_manager_set_theme_id(const char* id);
const AppTheme* theme_manager_get_current_theme(void);
bool theme_manager_is_system_theme(const char* id);

#endif // THEME_MANAGER_H

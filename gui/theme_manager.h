#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <gtk/gtk.h>
#include <stdbool.h>

void theme_manager_init(void);
void theme_manager_set_dark(bool is_dark);
void theme_manager_toggle(void);
bool theme_manager_is_dark(void);
void theme_manager_set_theme_id(const char* id);

#endif // THEME_MANAGER_H

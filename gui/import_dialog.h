#ifndef IMPORT_DIALOG_H
#define IMPORT_DIALOG_H

#include "app_state.h"
#include <gtk/gtk.h>

/**
 * Shows the "Import Game" dialog.
 * If the dialog already exists, presents it.
 * 
 * @param state The internal application state.
 */
void import_dialog_show(AppState* state);

#endif // IMPORT_DIALOG_H

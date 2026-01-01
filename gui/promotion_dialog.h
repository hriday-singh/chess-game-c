#ifndef PROMOTION_DIALOG_H
#define PROMOTION_DIALOG_H

#include <gtk/gtk.h>
#include "../game/types.h"
#include "theme_data.h"

// Show promotion dialog and return selected piece type
// Returns NO_PROMOTION if cancelled
PieceType promotion_dialog_show(GtkWindow* parent, ThemeData* theme, Player player);

#endif // PROMOTION_DIALOG_H


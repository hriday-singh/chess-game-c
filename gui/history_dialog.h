#ifndef HISTORY_DIALOG_H
#define HISTORY_DIALOG_H

#include <gtk/gtk.h>
#include "app_state.h"

typedef struct _HistoryDialog HistoryDialog;

// Callback when a match is selected for replay
typedef void (*HistoryReplaySelectedCallback)(const char* match_id, void* user_data);

HistoryDialog* history_dialog_new(GtkWindow* parent);
GtkWindow* history_dialog_get_window(HistoryDialog* dialog);
void history_dialog_set_replay_callback(HistoryDialog* dialog, GCallback callback, gpointer user_data);
void history_dialog_show(HistoryDialog* dialog);
void history_dialog_free(HistoryDialog* dialog);

#endif // HISTORY_DIALOG_H

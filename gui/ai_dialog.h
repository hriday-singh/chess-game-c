#ifndef AI_DIALOG_H
#define AI_DIALOG_H

#include <gtk/gtk.h>

typedef struct _AiDialog AiDialog;

// Callback for AI settings update
typedef void (*AiUpdateCallback)(void* user_data);

// Difficulty parameters are now defined in ai_engine.h
typedef void (*AiSettingsChangedCallback)(void* user_data);

AiDialog* ai_dialog_new(GtkWindow* parent);
AiDialog* ai_dialog_new_embedded(void);
GtkWidget* ai_dialog_get_widget(AiDialog* dialog);
void ai_dialog_set_parent_window(AiDialog* dialog, GtkWindow* parent);
void ai_dialog_show(AiDialog* dialog);
void ai_dialog_free(AiDialog* dialog);

// Difficulty parameters
int ai_dialog_get_elo(AiDialog* dialog, bool is_custom);
void ai_dialog_set_elo(AiDialog* dialog, int elo, bool is_custom);
void ai_dialog_set_settings_changed_callback(AiDialog* dialog, AiSettingsChangedCallback cb, void* data);
bool ai_dialog_is_advanced_enabled(AiDialog* dialog, bool for_custom);
int ai_dialog_get_depth(AiDialog* dialog, bool for_custom);

// Engine management
const char* ai_dialog_get_custom_path(AiDialog* dialog);
bool ai_dialog_has_valid_custom_engine(AiDialog* dialog);

// NNUE
const char* ai_dialog_get_nnue_path(AiDialog* dialog, bool* enabled);

void ai_dialog_show_tab(AiDialog* dialog, int tab_index);

// Load state from config
void ai_dialog_load_config(AiDialog* dialog, void* config_struct);
// Save state to config
void ai_dialog_save_config(AiDialog* dialog, void* config_struct);

#endif // AI_DIALOG_H

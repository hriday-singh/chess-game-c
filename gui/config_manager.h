#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>

// Define defaults
#define DEFAULT_THEME "default"
#define DEFAULT_DARK_MODE false

typedef struct {
    // General
    bool show_tutorial_dialog;
    bool is_dark_mode;
    char theme[64]; // Theme ID (e.g. "theme_d_mocha_gold")
    
    // AI - Internal
    int int_elo;
    int int_depth;
    int int_movetime;
    bool int_is_advanced;
    
    // AI - NNUE
    bool nnue_enabled;
    char nnue_path[512];
    
    // AI - Custom
    char custom_engine_path[512];
    int custom_elo;
    int custom_depth;
    int custom_movetime;
    bool custom_is_advanced;
    
    // Board Theme
    char board_theme_name[64]; 
    char light_square_color[32]; // Hex or rgba string
    char dark_square_color[32];
    
    // Piece Theme
    char piece_set[64]; 
    char white_piece_color[32];
    char white_stroke_color[32];
    char black_piece_color[32];
    char black_stroke_color[32];
    double white_stroke_width;
    double black_stroke_width;
} AppConfig;

// Initialize the config manager.
// Determines the config file path and loads the config if it exists.
void config_init(void);

// Load config from the persistent storage.
// Returns true on success, false on failure.
bool config_load(void);

// Save current config to the persistent storage.
// Returns true on success, false on failure.
bool config_save(void);

// Get a pointer to the global configuration structure.
AppConfig* config_get(void);

// Get the full path to the config file.
const char* config_get_path(void);

// Set the directory name for the config file (default is "HAL Chess")
// Must be called before config_init() if you want to change it.
void config_set_app_param(const char* app_name);

#endif // CONFIG_MANAGER_H

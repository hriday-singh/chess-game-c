#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>

// Define defaults
#define DEFAULT_THEME "theme_b_emerald"
#define DEFAULT_DARK_MODE true

typedef struct {
    // General
    bool show_tutorial_dialog;
    bool is_dark_mode;
    char theme[64]; // Theme ID (e.g. "theme_d_mocha_gold")
    
    // Game Settings
    int game_mode;      // 0:PVP, 1:PVC, 2:CVC
    int play_as;        // 0:White, 1:Black, 2:Random
    bool hints_dots;    // true: dots, false: squares
    bool enable_animations;
    bool enable_sfx;
    
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

// --- App Themes (app_themes.json) ---
#include "app_theme.h"

// Load custom app themes
void app_themes_init(void);

// Get list of custom loaded themes
// Returns pointer to array and sets count
AppTheme* app_themes_get_list(int* count);

// Add or update a theme
void app_themes_save_theme(const AppTheme* theme);

// Delete a theme by ID
void app_themes_delete_theme(const char* id);

// Save all themes to disk
void app_themes_save_all(void);

#endif // CONFIG_MANAGER_H

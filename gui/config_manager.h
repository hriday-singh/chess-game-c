#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

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
    bool enable_live_analysis;
    bool show_advantage_bar;
    bool show_mate_warning;
    bool show_hanging_pieces;
    bool show_move_rating;
    bool analysis_use_custom;
    
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

// --- Match History (match_history.json) ---

typedef struct {
    bool is_ai;
    int elo;
    int depth;
    int movetime;
    int engine_type; // 0: Internal, 1: Custom
    char engine_path[512];
} MatchPlayerConfig;

typedef struct {
    char id[64];
    int64_t timestamp;
    int game_mode;
    MatchPlayerConfig white;
    MatchPlayerConfig black;
    char result[16];        // "1-0", "0-1", "1/2-1/2", "*"
    char result_reason[64]; // "Checkmate", "Stalemate", "Reset", "Incomplete"
    int move_count;
    char* moves_uci;        // Allocated string of moves "e2e4 e7e5 g1f3 ..."
    char start_fen[256];
    char final_fen[256];
} MatchHistoryEntry;

// Initialize the match history system
void match_history_init(void);

// Load all matches from disk
void match_history_load_all(void);

// Add a match to history and save to disk
void match_history_add(MatchHistoryEntry* entry);

// Find a match by ID (returns pointer to internal list)
MatchHistoryEntry* match_history_find_by_id(const char* id);

// Delete a match by ID
void match_history_delete(const char* id);

// Free memory for a match entry (especially moves_uci)
void match_history_free_entry(MatchHistoryEntry* entry);

// Get the list of historical matches
MatchHistoryEntry* match_history_get_list(int* count);

#endif // CONFIG_MANAGER_H

#include "config_manager.h"
#include "theme_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

static bool debug_mode = false;

// Global configuration instance
static AppConfig g_config;
static char g_config_path[2048] = {0};
static char g_base_dir[2048] = {0};
static char g_app_name[64] = "HAL Chess";

void config_set_app_param(const char* app_name) {
    if (app_name && strlen(app_name) > 0) {
        snprintf(g_app_name, sizeof(g_app_name), "%s", app_name);
        // Clear paths ensuring they are recalculated
        g_config_path[0] = '\0';
        g_base_dir[0] = '\0';
    }
}

static void determine_base_dir(void) {
    if (g_base_dir[0] != '\0') return;

    const char* home = NULL;
#ifdef _WIN32
    home = getenv("APPDATA");
    if (!home) home = getenv("USERPROFILE");
#else
    home = getenv("XDG_CONFIG_HOME");
    if (!home) home = getenv("HOME");
#endif

    if (!home) {
        snprintf(g_base_dir, sizeof(g_base_dir), ".");
    } else {
#ifdef _WIN32
        snprintf(g_base_dir, sizeof(g_base_dir), "%s/%s", home, g_app_name);
#else
        snprintf(g_base_dir, sizeof(g_base_dir), "%s/.config/%s", home, g_app_name);
#endif
    }
    
    // Try creating the system directory
    // If it fails and not because it exists, use fallback
    int res = MKDIR(g_base_dir);
    if (res != 0 && errno != EEXIST) {
        if (debug_mode) printf("[ConfigManager] Failed to create system config dir: %s (errno=%d). Using fallback.\n", g_base_dir, errno);
        
        // Fallback: ./.chessconfig
        snprintf(g_base_dir, sizeof(g_base_dir), "./.chessconfig");
        MKDIR(g_base_dir); // Try creating fallback
    }
}

static void determine_config_path(void) {
    if (g_config_path[0] != '\0') return;
    determine_base_dir();
    snprintf(g_config_path, sizeof(g_config_path), "%.1024s/config.json", g_base_dir);
}

// Helper to set defaults
static void set_defaults(void) {
    // General
    snprintf(g_config.theme, sizeof(g_config.theme), "%s", DEFAULT_THEME);
    g_config.is_dark_mode = DEFAULT_DARK_MODE;
    g_config.show_tutorial_dialog = true;

    // Game Settings
    g_config.game_mode = 1; // Default: PVC
    g_config.play_as = 0;   // Default: White
    g_config.hints_dots = true; 
    g_config.enable_animations = true;
    g_config.enable_sfx = true;
    g_config.enable_live_analysis = false;
    g_config.show_advantage_bar = true;
    g_config.show_mate_warning = true;
    g_config.show_hanging_pieces = true;
    g_config.show_move_rating = true;
    g_config.show_move_rating = true;
    g_config.analysis_use_custom = false;
    
    // Clock Defaults
    g_config.clock_minutes = 0; // Default: No Clock
    g_config.clock_increment = 0;
    
    // AI - Internal
    g_config.int_elo = 1500;
    g_config.int_depth = 10;
    g_config.int_movetime = 500;
    g_config.int_is_advanced = false;
    
    // AI - NNUE
    g_config.nnue_enabled = false;
    g_config.nnue_path[0] = '\0';
    
    // AI - Custom
    g_config.custom_engine_path[0] = '\0';
    g_config.custom_elo = 1500;
    g_config.custom_depth = 10;
    g_config.custom_movetime = 500;
    g_config.custom_is_advanced = false;

    // Board Theme
    snprintf(g_config.board_theme_name, sizeof(g_config.board_theme_name), "%s", "Green & White");
    g_config.light_square_color[0] = '\0';
    g_config.dark_square_color[0] = '\0';

    // Piece Theme
    snprintf(g_config.piece_set, sizeof(g_config.piece_set), "%s", "caliente");
    g_config.white_piece_color[0] = '\0';
    g_config.white_stroke_color[0] = '\0';
    g_config.black_piece_color[0] = '\0';
    g_config.black_stroke_color[0] = '\0';
    g_config.white_stroke_width = 0.5;
    g_config.black_stroke_width = 0.1;
}

static void parse_string_val(char* val_start, char* dest, size_t max_len) {
    val_start++; // skip quote
    char* val_end = strchr(val_start, '\"');
    if (val_end) {
        size_t val_len = val_end - val_start;
        if (val_len >= max_len) val_len = max_len - 1;
        snprintf(dest, max_len, "%.*s", (int)val_len, val_start);
    }
}

static void parse_line(char* line) {
    char key[64] = {0};
    
    // Find key
    char* key_start = strchr(line, '\"');
    if (!key_start) return;
    key_start++;
    char* key_end = strchr(key_start, '\"');
    if (!key_end) return;
    
    size_t key_len = key_end - key_start;
    if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
    snprintf(key, sizeof(key), "%.*s", (int)key_len, key_start);
    
    // Find value (after colon)
    char* colon = strchr(key_end, ':');
    if (!colon) return;
    
    char* val_start = colon + 1;
    while (*val_start == ' ' || *val_start == '\t') val_start++; 
    
    // STRING parsing
    if (*val_start == '\"') {
        if (strcmp(key, "theme") == 0) parse_string_val(val_start, g_config.theme, sizeof(g_config.theme));
        else if (strcmp(key, "nnue_path") == 0) parse_string_val(val_start, g_config.nnue_path, sizeof(g_config.nnue_path));
        else if (strcmp(key, "custom_engine_path") == 0) parse_string_val(val_start, g_config.custom_engine_path, sizeof(g_config.custom_engine_path));
        else if (strcmp(key, "board_theme_name") == 0) parse_string_val(val_start, g_config.board_theme_name, sizeof(g_config.board_theme_name));
        else if (strcmp(key, "light_square_color") == 0) parse_string_val(val_start, g_config.light_square_color, sizeof(g_config.light_square_color));
        else if (strcmp(key, "dark_square_color") == 0) parse_string_val(val_start, g_config.dark_square_color, sizeof(g_config.dark_square_color));
        else if (strcmp(key, "piece_set") == 0) parse_string_val(val_start, g_config.piece_set, sizeof(g_config.piece_set));
        else if (strcmp(key, "white_piece_color") == 0) parse_string_val(val_start, g_config.white_piece_color, sizeof(g_config.white_piece_color));
        else if (strcmp(key, "white_stroke_color") == 0) parse_string_val(val_start, g_config.white_stroke_color, sizeof(g_config.white_stroke_color));
        else if (strcmp(key, "black_piece_color") == 0) parse_string_val(val_start, g_config.black_piece_color, sizeof(g_config.black_piece_color));
        else if (strcmp(key, "black_stroke_color") == 0) parse_string_val(val_start, g_config.black_stroke_color, sizeof(g_config.black_stroke_color));
    } 
    // BOOLEAN
    else if (strncmp(val_start, "true", 4) == 0) {
        if (strcmp(key, "is_dark_mode") == 0) g_config.is_dark_mode = true;
        else if (strcmp(key, "show_tutorial_dialog") == 0) g_config.show_tutorial_dialog = true;
        else if (strcmp(key, "int_is_advanced") == 0) g_config.int_is_advanced = true;
        else if (strcmp(key, "nnue_enabled") == 0) g_config.nnue_enabled = true;
        else if (strcmp(key, "custom_is_advanced") == 0) g_config.custom_is_advanced = true;
        else if (strcmp(key, "hints_dots") == 0) g_config.hints_dots = true;
        else if (strcmp(key, "enable_animations") == 0) g_config.enable_animations = true;
        else if (strcmp(key, "enable_sfx") == 0) g_config.enable_sfx = true;
        else if (strcmp(key, "enable_live_analysis") == 0) g_config.enable_live_analysis = true;
        else if (strcmp(key, "show_advantage_bar") == 0) g_config.show_advantage_bar = true;
        else if (strcmp(key, "show_mate_warning") == 0) g_config.show_mate_warning = true;
        else if (strcmp(key, "show_hanging_pieces") == 0) g_config.show_hanging_pieces = true;
        else if (strcmp(key, "show_move_rating") == 0) g_config.show_move_rating = true;
        else if (strcmp(key, "analysis_use_custom") == 0) g_config.analysis_use_custom = true;
    } 
    else if (strncmp(val_start, "false", 5) == 0) {
        if (strcmp(key, "is_dark_mode") == 0) g_config.is_dark_mode = false;
        else if (strcmp(key, "show_tutorial_dialog") == 0) g_config.show_tutorial_dialog = false;
        else if (strcmp(key, "int_is_advanced") == 0) g_config.int_is_advanced = false;
        else if (strcmp(key, "nnue_enabled") == 0) g_config.nnue_enabled = false;
        else if (strcmp(key, "custom_is_advanced") == 0) g_config.custom_is_advanced = false;
        else if (strcmp(key, "hints_dots") == 0) g_config.hints_dots = false;
        else if (strcmp(key, "enable_animations") == 0) g_config.enable_animations = false;
        else if (strcmp(key, "enable_sfx") == 0) g_config.enable_sfx = false;
        else if (strcmp(key, "enable_live_analysis") == 0) g_config.enable_live_analysis = false;
        else if (strcmp(key, "show_advantage_bar") == 0) g_config.show_advantage_bar = false;
        else if (strcmp(key, "show_mate_warning") == 0) g_config.show_mate_warning = false;
        else if (strcmp(key, "show_hanging_pieces") == 0) g_config.show_hanging_pieces = false;
        else if (strcmp(key, "show_move_rating") == 0) g_config.show_move_rating = false;
        else if (strcmp(key, "analysis_use_custom") == 0) g_config.analysis_use_custom = false;
    }
    // NUMBERS
    else {
        if (strcmp(key, "int_elo") == 0) g_config.int_elo = atoi(val_start);
        else if (strcmp(key, "int_depth") == 0) g_config.int_depth = atoi(val_start);
        else if (strcmp(key, "int_movetime") == 0) g_config.int_movetime = atoi(val_start);
        else if (strcmp(key, "game_mode") == 0) g_config.game_mode = atoi(val_start);
        else if (strcmp(key, "play_as") == 0) g_config.play_as = atoi(val_start);
        else if (strcmp(key, "custom_elo") == 0) g_config.custom_elo = atoi(val_start);
        else if (strcmp(key, "custom_depth") == 0) g_config.custom_depth = atoi(val_start);
        else if (strcmp(key, "custom_movetime") == 0) g_config.custom_movetime = atoi(val_start);
        else if (strcmp(key, "white_stroke_width") == 0) g_config.white_stroke_width = atof(val_start);
        else if (strcmp(key, "white_stroke_width") == 0) g_config.white_stroke_width = atof(val_start);
        else if (strcmp(key, "black_stroke_width") == 0) g_config.black_stroke_width = atof(val_start);
        else if (strcmp(key, "clock_minutes") == 0) g_config.clock_minutes = atoi(val_start);
        else if (strcmp(key, "clock_increment") == 0) g_config.clock_increment = atoi(val_start);
    }
}

bool config_load(void) {
    determine_config_path();
    set_defaults();
    
    FILE* f = fopen(g_config_path, "r");
    if (!f) return false;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* p = strchr(line, '\n');
        if (p) *p = '\0';
        p = strchr(line, '\r');
        if (p) *p = '\0';
        
        parse_line(line);
    }
    
    fclose(f);
    if (debug_mode) {
        printf("Config loaded from %s\n", g_config_path);
        printf("--- [ConfigManager] Config Summary ---\n");
        printf("  Theme: %s\n", g_config.theme);
        printf("  Dark Mode: %s\n", g_config.is_dark_mode ? "true" : "false");
        printf("  Tutorial: %s\n", g_config.show_tutorial_dialog ? "true" : "false");
        printf("  Game Mode: %d\n", g_config.game_mode);
        printf("  Play As: %d\n", g_config.play_as);
        printf("  Hints: %s\n", g_config.hints_dots ? "Dots" : "Squares");
        printf("  Animations: %s\n", g_config.enable_animations ? "ON" : "OFF");
        printf("  SFX: %s\n", g_config.enable_sfx ? "ON" : "OFF");
        printf("  Live Analysis: %s\n", g_config.enable_live_analysis ? "ON" : "OFF");
        printf("  Advantage Bar: %s\n", g_config.show_advantage_bar ? "ON" : "OFF");
        printf("  Mate Warning: %s\n", g_config.show_mate_warning ? "ON" : "OFF");
        printf("  Hanging Pieces: %s\n", g_config.show_hanging_pieces ? "ON" : "OFF");
        printf("  Move Rating: %s\n", g_config.show_move_rating ? "ON" : "OFF");
        printf("  Internal AI: ELO=%d, Depth=%d, MoveTime=%d\n", g_config.int_elo, g_config.int_depth, g_config.int_movetime);
        printf("  NNUE: %s (Path: %s)\n", g_config.nnue_enabled ? "ON" : "OFF", g_config.nnue_path);
        printf("  Custom Engine: %s (ELO=%d)\n", g_config.custom_engine_path, g_config.custom_elo);
        printf("------------------------------\n");
    }
    return true;
}

bool config_save(void) {
    determine_config_path();
    
    FILE* f = fopen(g_config_path, "w");
    if (!f) {
        if (debug_mode) printf("Failed to open config file for writing: %s\n", g_config_path);
        return false;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "    \"theme\": \"%s\",\n", g_config.theme);
    fprintf(f, "    \"is_dark_mode\": %s,\n", g_config.is_dark_mode ? "true" : "false");
    fprintf(f, "    \"show_tutorial_dialog\": %s,\n", g_config.show_tutorial_dialog ? "true" : "false");
    
    fprintf(f, "    \"game_mode\": %d,\n", g_config.game_mode);
    fprintf(f, "    \"play_as\": %d,\n", g_config.play_as);
    fprintf(f, "    \"hints_dots\": %s,\n", g_config.hints_dots ? "true" : "false");
    fprintf(f, "    \"enable_animations\": %s,\n", g_config.enable_animations ? "true" : "false");
    fprintf(f, "    \"enable_sfx\": %s,\n", g_config.enable_sfx ? "true" : "false");
    fprintf(f, "    \"enable_live_analysis\": %s,\n", g_config.enable_live_analysis ? "true" : "false");
    fprintf(f, "    \"show_advantage_bar\": %s,\n", g_config.show_advantage_bar ? "true" : "false");
    fprintf(f, "    \"show_mate_warning\": %s,\n", g_config.show_mate_warning ? "true" : "false");
    fprintf(f, "    \"show_hanging_pieces\": %s,\n", g_config.show_hanging_pieces ? "true" : "false");
    fprintf(f, "    \"show_move_rating\": %s,\n", g_config.show_move_rating ? "true" : "false");
    fprintf(f, "    \"analysis_use_custom\": %s,\n", g_config.analysis_use_custom ? "true" : "false");
    
    fprintf(f, "    \"clock_minutes\": %d,\n", g_config.clock_minutes);
    fprintf(f, "    \"clock_increment\": %d,\n", g_config.clock_increment);

    fprintf(f, "    \"int_elo\": %d,\n", g_config.int_elo);
    fprintf(f, "    \"int_depth\": %d,\n", g_config.int_depth);
    fprintf(f, "    \"int_movetime\": %d,\n", g_config.int_movetime);
    fprintf(f, "    \"int_is_advanced\": %s,\n", g_config.int_is_advanced ? "true" : "false");
    
    fprintf(f, "    \"nnue_enabled\": %s,\n", g_config.nnue_enabled ? "true" : "false");
    fprintf(f, "    \"nnue_path\": \"%s\",\n", g_config.nnue_path);
    
    fprintf(f, "    \"custom_engine_path\": \"%s\",\n", g_config.custom_engine_path);
    fprintf(f, "    \"custom_elo\": %d,\n", g_config.custom_elo);
    fprintf(f, "    \"custom_depth\": %d,\n", g_config.custom_depth);
    fprintf(f, "    \"custom_movetime\": %d,\n", g_config.custom_movetime);
    fprintf(f, "    \"custom_is_advanced\": %s,\n", g_config.custom_is_advanced ? "true" : "false");
    
    fprintf(f, "    \"board_theme_name\": \"%s\",\n", g_config.board_theme_name);
    fprintf(f, "    \"light_square_color\": \"%s\",\n", g_config.light_square_color);
    fprintf(f, "    \"dark_square_color\": \"%s\",\n", g_config.dark_square_color);
    
    fprintf(f, "    \"piece_set\": \"%s\",\n", g_config.piece_set);
    fprintf(f, "    \"white_piece_color\": \"%s\",\n", g_config.white_piece_color);
    fprintf(f, "    \"white_stroke_color\": \"%s\",\n", g_config.white_stroke_color);
    fprintf(f, "    \"black_piece_color\": \"%s\",\n", g_config.black_piece_color);
    fprintf(f, "    \"black_stroke_color\": \"%s\",\n", g_config.black_stroke_color);
    fprintf(f, "    \"white_stroke_width\": %.2f,\n", g_config.white_stroke_width);
    fprintf(f, "    \"black_stroke_width\": %.2f\n", g_config.black_stroke_width);
    
    fprintf(f, "}\n");
    
    fclose(f);
    if (debug_mode) {
        printf("Config saved to %s\n", g_config_path);
        printf("--- [ConfigManager] Config Summary ---\n");
        printf("  Theme: %s\n", g_config.theme);
        printf("  Dark Mode: %s\n", g_config.is_dark_mode ? "true" : "false");
        printf("  Tutorial: %s\n", g_config.show_tutorial_dialog ? "true" : "false");
        printf("  Game Mode: %d\n", g_config.game_mode);
        printf("  Play As: %d\n", g_config.play_as);
        printf("  Hints: %s\n", g_config.hints_dots ? "Dots" : "Squares");
        printf("  Animations: %s\n", g_config.enable_animations ? "ON" : "OFF");
        printf("  SFX: %s\n", g_config.enable_sfx ? "ON" : "OFF");
        printf("  Live Analysis: %s\n", g_config.enable_live_analysis ? "ON" : "OFF");
        printf("  Advantage Bar: %s\n", g_config.show_advantage_bar ? "ON" : "OFF");
        printf("  Mate Warning: %s\n", g_config.show_mate_warning ? "ON" : "OFF");
        printf("  Hanging Pieces: %s\n", g_config.show_hanging_pieces ? "ON" : "OFF");
        printf("  Move Rating: %s\n", g_config.show_move_rating ? "ON" : "OFF");
        printf("  Internal AI: ELO=%d, Depth=%d, MoveTime=%d\n", g_config.int_elo, g_config.int_depth, g_config.int_movetime);
        printf("  NNUE: %s (Path: %s)\n", g_config.nnue_enabled ? "ON" : "OFF", g_config.nnue_path);
        printf("  Custom Engine: %s (ELO=%d)\n", g_config.custom_engine_path, g_config.custom_elo);
        printf("------------------------------\n");
    }
    return true;
}

void config_init(void) {
    determine_config_path();
    config_load();
}

AppConfig* config_get(void) {
    return &g_config;
}

const char* config_get_path(void) {
    determine_config_path();
    return g_config_path;
}

// --- App Themes Implementation ---

#include <limits.h>

#define MAX_CUSTOM_THEMES 50
static AppTheme g_custom_themes[MAX_CUSTOM_THEMES];
static int g_custom_theme_count = 0;
static char g_themes_path[4096] = {0};

static void determine_themes_path(void) {
    if (g_themes_path[0] != '\0') return;
    determine_base_dir();
    snprintf(g_themes_path, sizeof(g_themes_path), "%s/app_themes.json", g_base_dir);
}

// Minimal manual JSON parser for this specific structure
// Function to extract string value by key from a line
static void extract_json_str(const char* line, const char* key, char* dest, size_t dest_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* found = strstr(line, search);
    if (found) {
        const char* val_start = strchr(found, ':');
        if (val_start) {
            val_start = strchr(val_start, '\"');
            if (val_start) {
                val_start++;
                const char* val_end = strchr(val_start, '\"');
                if (val_end) {
                    size_t len = val_end - val_start;
                    if (len >= dest_size) len = dest_size - 1;
                    snprintf(dest, dest_size, "%.*s", (int)len, val_start);
                }
            }
        }
    }
}

void app_themes_init(void) {
    determine_themes_path();
    g_custom_theme_count = 0;
    
    FILE* f = fopen(g_themes_path, "r");
    if (!f) return;
    
    char line[1024];
    AppTheme* current = NULL;
    AppThemeColors* current_colors = NULL;
    
    while (fgets(line, sizeof(line), f)) {
        // Detect start of object
        if (strstr(line, "\"theme_id\"")) {
            if (g_custom_theme_count < MAX_CUSTOM_THEMES) {
                current = &g_custom_themes[g_custom_theme_count++];
                extract_json_str(line, "theme_id", current->theme_id, sizeof(current->theme_id));
            }
        }
        else if (current) {
            if (strstr(line, "\"display_name\"")) extract_json_str(line, "display_name", current->display_name, sizeof(current->display_name));
            else if (strstr(line, "\"light\": {")) current_colors = &current->light;
            else if (strstr(line, "\"dark\": {")) current_colors = &current->dark;
            else if (strstr(line, "},") || strstr(line, "}")) {
               // End of color block or object, handled by context switch or next ID
            }
            else if (current_colors) {
                // Parse colors
                if (strstr(line, "\"base_bg\"")) extract_json_str(line, "base_bg", current_colors->base_bg, sizeof(current_colors->base_bg));
                else if (strstr(line, "\"base_fg\"")) extract_json_str(line, "base_fg", current_colors->base_fg, sizeof(current_colors->base_fg));
                else if (strstr(line, "\"base_panel_bg\"")) extract_json_str(line, "base_panel_bg", current_colors->base_panel_bg, sizeof(current_colors->base_panel_bg));
                else if (strstr(line, "\"base_card_bg\"")) extract_json_str(line, "base_card_bg", current_colors->base_card_bg, sizeof(current_colors->base_card_bg));
                else if (strstr(line, "\"base_entry_bg\"")) extract_json_str(line, "base_entry_bg", current_colors->base_entry_bg, sizeof(current_colors->base_entry_bg));
                else if (strstr(line, "\"base_accent\"")) extract_json_str(line, "base_accent", current_colors->base_accent, sizeof(current_colors->base_accent));
                else if (strstr(line, "\"base_accent_fg\"")) extract_json_str(line, "base_accent_fg", current_colors->base_accent_fg, sizeof(current_colors->base_accent_fg));
                else if (strstr(line, "\"base_success_bg\"")) extract_json_str(line, "base_success_bg", current_colors->base_success_bg, sizeof(current_colors->base_success_bg));
                else if (strstr(line, "\"base_success_text\"")) extract_json_str(line, "base_success_text", current_colors->base_success_text, sizeof(current_colors->base_success_text));
                else if (strstr(line, "\"base_success_fg\"")) extract_json_str(line, "base_success_fg", current_colors->base_success_fg, sizeof(current_colors->base_success_fg));
                else if (strstr(line, "\"success_hover\"")) extract_json_str(line, "success_hover", current_colors->success_hover, sizeof(current_colors->success_hover));
                else if (strstr(line, "\"base_destructive_bg\"")) extract_json_str(line, "base_destructive_bg", current_colors->base_destructive_bg, sizeof(current_colors->base_destructive_bg));
                else if (strstr(line, "\"base_destructive_fg\"")) extract_json_str(line, "base_destructive_fg", current_colors->base_destructive_fg, sizeof(current_colors->base_destructive_fg));
                else if (strstr(line, "\"destructive_hover\"")) extract_json_str(line, "destructive_hover", current_colors->destructive_hover, sizeof(current_colors->destructive_hover));
                else if (strstr(line, "\"border_color\"")) extract_json_str(line, "border_color", current_colors->border_color, sizeof(current_colors->border_color));
                else if (strstr(line, "\"dim_label\"")) extract_json_str(line, "dim_label", current_colors->dim_label, sizeof(current_colors->dim_label));
                else if (strstr(line, "\"tooltip_bg\"")) extract_json_str(line, "tooltip_bg", current_colors->tooltip_bg, sizeof(current_colors->tooltip_bg));
                else if (strstr(line, "\"tooltip_fg\"")) extract_json_str(line, "tooltip_fg", current_colors->tooltip_fg, sizeof(current_colors->tooltip_fg));
                else if (strstr(line, "\"button_bg\"")) extract_json_str(line, "button_bg", current_colors->button_bg, sizeof(current_colors->button_bg));
                else if (strstr(line, "\"button_hover\"")) extract_json_str(line, "button_hover", current_colors->button_hover, sizeof(current_colors->button_hover));
                else if (strstr(line, "\"error_text\"")) extract_json_str(line, "error_text", current_colors->error_text, sizeof(current_colors->error_text));
                else if (strstr(line, "\"capture_bg_white\"")) extract_json_str(line, "capture_bg_white", current_colors->capture_bg_white, sizeof(current_colors->capture_bg_white));
                else if (strstr(line, "\"capture_bg_black\"")) extract_json_str(line, "capture_bg_black", current_colors->capture_bg_black, sizeof(current_colors->capture_bg_black));
            }
        }
    }
    
    fclose(f);
    if (debug_mode) printf("[ConfigManager] Loaded %d custom themes from %s\n", g_custom_theme_count, g_themes_path);
}

AppTheme* app_themes_get_list(int* count) {
    if (count) *count = g_custom_theme_count;
    return g_custom_themes;
}

void app_themes_save_theme(const AppTheme* theme) {
    if (!theme) return;
    
    // Check if exists, update if so
    for (int i = 0; i < g_custom_theme_count; i++) {
        if (strcmp(g_custom_themes[i].theme_id, theme->theme_id) == 0) {
           // Prevent overwriting system theme if somehow a collision happens, check global list
           if (theme_manager_is_system_theme(theme->theme_id)) {
               if (debug_mode) printf("[ConfigManager] Cannot overwrite system theme %s\n", theme->theme_id);
               return; 
           }

            g_custom_themes[i] = *theme;
            app_themes_save_all();
            return;
        }
    }
    
    // Add new (Ensure not system ID)
    if (theme_manager_is_system_theme(theme->theme_id)) {
        if (debug_mode) printf("[Config] Cannot save theme with system ID %s\n", theme->theme_id);
        return;
    }

    if (g_custom_theme_count < MAX_CUSTOM_THEMES) {
        g_custom_themes[g_custom_theme_count++] = *theme;
        app_themes_save_all();
    }
}

void app_themes_delete_theme(const char* id) {
    if (!id) return;
    int idx = -1;
    for (int i = 0; i < g_custom_theme_count; i++) {
        if (strcmp(g_custom_themes[i].theme_id, id) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx != -1) {
        // Shift remaining
        for (int i = idx; i < g_custom_theme_count - 1; i++) {
            g_custom_themes[i] = g_custom_themes[i + 1];
        }
        g_custom_theme_count--;
        app_themes_save_all();
    }
}

static void write_colors_json(FILE* f, const AppThemeColors* c, bool is_last) {
    (void)is_last; // Unused
    fprintf(f, "    \"base_bg\": \"%s\",\n", c->base_bg);
    fprintf(f, "    \"base_fg\": \"%s\",\n", c->base_fg);
    fprintf(f, "    \"base_panel_bg\": \"%s\",\n", c->base_panel_bg);
    fprintf(f, "    \"base_card_bg\": \"%s\",\n", c->base_card_bg);
    fprintf(f, "    \"base_entry_bg\": \"%s\",\n", c->base_entry_bg);
    fprintf(f, "    \"base_accent\": \"%s\",\n", c->base_accent);
    fprintf(f, "    \"base_accent_fg\": \"%s\",\n", c->base_accent_fg);
    fprintf(f, "    \"base_success_bg\": \"%s\",\n", c->base_success_bg);
    fprintf(f, "    \"base_success_text\": \"%s\",\n", c->base_success_text);
    fprintf(f, "    \"base_success_fg\": \"%s\",\n", c->base_success_fg);
    fprintf(f, "    \"success_hover\": \"%s\",\n", c->success_hover);
    fprintf(f, "    \"base_destructive_bg\": \"%s\",\n", c->base_destructive_bg);
    fprintf(f, "    \"base_destructive_fg\": \"%s\",\n", c->base_destructive_fg);
    fprintf(f, "    \"destructive_hover\": \"%s\",\n", c->destructive_hover);
    fprintf(f, "    \"border_color\": \"%s\",\n", c->border_color);
    fprintf(f, "    \"dim_label\": \"%s\",\n", c->dim_label);
    fprintf(f, "    \"tooltip_bg\": \"%s\",\n", c->tooltip_bg);
    fprintf(f, "    \"tooltip_fg\": \"%s\",\n", c->tooltip_fg);
    fprintf(f, "    \"button_bg\": \"%s\",\n", c->button_bg);
    fprintf(f, "    \"button_hover\": \"%s\",\n", c->button_hover);
    fprintf(f, "    \"error_text\": \"%s\",\n", c->error_text);
    fprintf(f, "    \"capture_bg_white\": \"%s\",\n", c->capture_bg_white);
    fprintf(f, "    \"capture_bg_black\": \"%s\"\n", c->capture_bg_black);
}

void app_themes_save_all(void) {
    determine_themes_path();
    
    FILE* f = fopen(g_themes_path, "w");
    if (!f) return;
    
    fprintf(f, "[\n");
    for (int i = 0; i < g_custom_theme_count; i++) {
        AppTheme* t = &g_custom_themes[i];
        fprintf(f, "  {\n");
        fprintf(f, "    \"theme_id\": \"%s\",\n", t->theme_id);
        fprintf(f, "    \"display_name\": \"%s\",\n", t->display_name);
        
        fprintf(f, "    \"light\": {\n");
        write_colors_json(f, &t->light, false);
        fprintf(f, "    },\n");
        
        fprintf(f, "    \"dark\": {\n");
        write_colors_json(f, &t->dark, true);
        fprintf(f, "    }\n");
        
        if (i < g_custom_theme_count - 1) fprintf(f, "  },\n");
        else fprintf(f, "  }\n");
    }
    fprintf(f, "]\n");
    fclose(f);
    if (debug_mode) printf("[ConfigManager] Themes saved to %s\n", g_themes_path);
}

// --- Match History Implementation ---

// Pagination Configuration
#define PAGE_SIZE 20              // Matches per page
#define MAX_CACHED_PAGES 10       // Keep 10 pages in memory (200 entries)
#define PRELOAD_THRESHOLD 5       // Load next page when 5 items from bottom

// Lightweight metadata for fast indexing
typedef struct {
    char id[64];
    int64_t timestamp;  // For sorting
} MatchMetadata;

// Metadata index (all matches, but only ID + timestamp)
typedef struct {
    MatchMetadata* items;
    int count;
    int capacity;
} MatchIndex;

// Cached page of full match entries
typedef struct {
    int page_number;
    MatchHistoryEntry* entries;  // Array of entries
    int entry_count;             // Actual entries in this page
    uint64_t last_access_time;   // For LRU eviction (milliseconds)
} CachePage;

// LRU Cache for match pages
typedef struct {
    CachePage* pages;
    int page_count;
    int max_pages;
} MatchCache;

// Global state
static MatchIndex g_match_index = {0};
static MatchCache g_match_cache = {0};

// Legacy: Keep for backward compatibility
static MatchHistoryEntry* g_history_list = NULL;
static int g_history_count = 0;
static int g_history_capacity = 0;

void match_history_free_entry(MatchHistoryEntry* entry) {
    if (entry && entry->moves_uci) {
        free(entry->moves_uci);
        entry->moves_uci = NULL;
    }
    if (entry && entry->think_time_ms) {
        free(entry->think_time_ms);
        entry->think_time_ms = NULL;
    }
}

// Forward declaration for pagination cache invalidation
static void invalidate_cache(void);

static void save_single_match(MatchHistoryEntry* m) {
    determine_base_dir();
    char matches_dir[4096]; 
    
    // Determine subdirectory based on ID prefix
    if (strncmp(m->id, "import_", 7) == 0) {
        snprintf(matches_dir, sizeof(matches_dir), "%s/matches/imported", g_base_dir);
    } else {
        snprintf(matches_dir, sizeof(matches_dir), "%s/matches", g_base_dir);
    }
    MKDIR(matches_dir);

    char match_path[4096]; 
    snprintf(match_path, sizeof(match_path), "%.2048s/%.256s.json", matches_dir, m->id);
    
    FILE* f = fopen(match_path, "w");
    if (!f) {
        if(debug_mode) printf("[MatchHistory] ERROR: Failed to save match history file: %s\n", match_path);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"id\": \"%s\",\n", m->id);
    fprintf(f, "  \"timestamp\": %lld,\n", (long long)m->timestamp);
    fprintf(f, "  \"created_at_ms\": %lld,\n", (long long)m->created_at_ms);
    fprintf(f, "  \"started_at_ms\": %lld,\n", (long long)m->started_at_ms);
    fprintf(f, "  \"ended_at_ms\": %lld,\n", (long long)m->ended_at_ms);
    fprintf(f, "  \"game_mode\": %d,\n", m->game_mode);
    
    // Clock Settings
    fprintf(f, "  \"clock\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", m->clock.enabled ? "true" : "false");
    fprintf(f, "    \"initial_ms\": %d,\n", m->clock.initial_ms);
    fprintf(f, "    \"increment_ms\": %d\n", m->clock.increment_ms);
    fprintf(f, "  },\n");
    
    fprintf(f, "  \"white\": {\n");
    fprintf(f, "    \"is_ai\": %s, \"elo\": %d, \"depth\": %d, \"movetime\": %d, \"engine_type\": %d, \"engine_path\": \"%s\"\n",
            m->white.is_ai ? "true" : "false", m->white.elo, m->white.depth, m->white.movetime, m->white.engine_type, m->white.engine_path);
    fprintf(f, "  },\n");
    
    fprintf(f, "  \"black\": {\n");
    fprintf(f, "    \"is_ai\": %s, \"elo\": %d, \"depth\": %d, \"movetime\": %d, \"engine_type\": %d, \"engine_path\": \"%s\"\n",
            m->black.is_ai ? "true" : "false", m->black.elo, m->black.depth, m->black.movetime, m->black.engine_type, m->black.engine_path);
    fprintf(f, "  },\n");
    
    fprintf(f, "  \"result\": \"%s\",\n", m->result);
    fprintf(f, "  \"result_reason\": \"%s\",\n", m->result_reason);
    fprintf(f, "  \"move_count\": %d,\n", m->move_count);
    fprintf(f, "  \"moves_uci\": \"%s\",\n", m->moves_uci ? m->moves_uci : "");
    
    // Think Times
    if (m->think_time_count > 0 && m->think_time_ms) {
        fprintf(f, "  \"think_time_ms\": [");
        for (int i = 0; i < m->think_time_count; i++) {
            fprintf(f, "%d%s", m->think_time_ms[i], (i < m->think_time_count - 1) ? ", " : "");
        }
        fprintf(f, "],\n");
    }
    fprintf(f, "  \"start_fen\": \"%s\",\n", m->start_fen);
    fprintf(f, "  \"final_fen\": \"%s\"\n", m->final_fen);
    fprintf(f, "}\n");
    
    fclose(f);
    if (debug_mode){
        printf("[ConfigManager] Match History saved to: %s\n", match_path);
        printf("  \"id\": \"%s\",\n", m->id);
        printf("  \"timestamp\": %lld,\n", (long long)m->timestamp);
        printf("  \"created_at_ms\": %lld,\n", (long long)m->created_at_ms);
        printf("  \"started_at_ms\": %lld,\n", (long long)m->started_at_ms);
        printf("  \"ended_at_ms\": %lld,\n", (long long)m->ended_at_ms);
        printf("  \"game_mode\": %d,\n", m->game_mode);
        
        // Clock Settings
        printf("  \"clock\": {\n");
        printf("    \"enabled\": %s,\n", m->clock.enabled ? "true" : "false");
        printf("    \"initial_ms\": %d,\n", m->clock.initial_ms);
        printf("    \"increment_ms\": %d\n", m->clock.increment_ms);
        printf("  },\n");
        
        printf("  \"white\": {\n");
        printf("    \"is_ai\": %s, \"elo\": %d, \"depth\": %d, \"movetime\": %d, \"engine_type\": %d, \"engine_path\": \"%s\"\n",
                m->white.is_ai ? "true" : "false", m->white.elo, m->white.depth, m->white.movetime, m->white.engine_type, m->white.engine_path);
        printf("  },\n");
        
        printf("  \"black\": {\n");
        printf("    \"is_ai\": %s, \"elo\": %d, \"depth\": %d, \"movetime\": %d, \"engine_type\": %d, \"engine_path\": \"%s\"\n",
                m->black.is_ai ? "true" : "false", m->black.elo, m->black.depth, m->black.movetime, m->black.engine_type, m->black.engine_path);
        printf("  },\n");
        
        printf("  \"result\": \"%s\",\n", m->result);
        printf("  \"result_reason\": \"%s\",\n", m->result_reason);
        printf("  \"move_count\": %d,\n", m->move_count);
        printf("  \"moves_uci\": \"%s\",\n", m->moves_uci ? m->moves_uci : "");
        
        // Think Times
        if (m->think_time_count > 0 && m->think_time_ms) {
            printf("  \"think_time_ms\": [");
            for (int i = 0; i < m->think_time_count; i++) {
                printf("%d%s", m->think_time_ms[i], (i < m->think_time_count - 1) ? ", " : "");
            }
            printf("],\n");
        }
        printf("  \"start_fen\": \"%s\",\n", m->start_fen);
        printf("  \"final_fen\": \"%s\"\n", m->final_fen);
        printf("}\n");
    }
}

// --- NEW: Helper Functions for Pagination ---

// Get current time in milliseconds for LRU tracking
static uint64_t get_time_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

// Compare function for qsort (descending by timestamp)
static int compare_metadata(const void* a, const void* b) {
    const MatchMetadata* ma = (const MatchMetadata*)a;
    const MatchMetadata* mb = (const MatchMetadata*)b;
    if (ma->timestamp > mb->timestamp) return -1;
    if (ma->timestamp < mb->timestamp) return 1;
    return 0;
}

// Helper to scan a directory and add to index
static void scan_directory(const char* dir_path) {
    char search_path[4096];
    
#ifdef _WIN32
    snprintf(search_path, sizeof(search_path), "%.2048s/*.json", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // Expand capacity if needed
            if (g_match_index.count >= g_match_index.capacity) {
                g_match_index.capacity *= 2;
                g_match_index.items = realloc(g_match_index.items, 
                    g_match_index.capacity * sizeof(MatchMetadata));
            }
            
            MatchMetadata* meta = &g_match_index.items[g_match_index.count++];
            
            // Extract ID
            snprintf(meta->id, sizeof(meta->id), "%.63s", fd.cFileName);
            char* ext = strstr(meta->id, ".json");
            if (ext) *ext = '\0';
            
            // Use file modification time as timestamp
            FILETIME ft = fd.ftLastWriteTime;
            ULARGE_INTEGER ull;
            ull.LowPart = ft.dwLowDateTime;
            ull.HighPart = ft.dwHighDateTime;
            meta->timestamp = ull.QuadPart / 10000000ULL - 11644473600ULL; // Convert to Unix time
            
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR* d = opendir(dir_path);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".json")) {
                if (g_match_index.count >= g_match_index.capacity) {
                    g_match_index.capacity *= 2;
                    g_match_index.items = realloc(g_match_index.items, 
                        g_match_index.capacity * sizeof(MatchMetadata));
                }
                
                MatchMetadata* meta = &g_match_index.items[g_match_index.count++];
                snprintf(meta->id, sizeof(meta->id), "%s", dir->d_name);
                char* ext = strstr(meta->id, ".json");
                if (ext) *ext = '\0';
                
                // Get timestamp
                char full_path[4096];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir->d_name);
                struct stat st;
                if (stat(full_path, &st) == 0) {
                    meta->timestamp = st.st_mtime;
                }
            }
        }
        closedir(d);
    }
#endif
}

// Fast scan: only extract ID and timestamp from filename
static void scan_match_files(void) {
    determine_base_dir();
    char matches_dir[4096];
    snprintf(matches_dir, sizeof(matches_dir), "%s/matches", g_base_dir);
    
    // Initialize index
    g_match_index.capacity = 100;
    g_match_index.count = 0;
    if (g_match_index.items) free(g_match_index.items); // Reset
    g_match_index.items = calloc(g_match_index.capacity, sizeof(MatchMetadata));
    
    // Scan root matches
    scan_directory(matches_dir);
    
    // Scan imported matches
    char imported_dir[4096];
    snprintf(imported_dir, sizeof(imported_dir), "%s/matches/imported", g_base_dir);
    scan_directory(imported_dir); // Prefix not stored in ID, ID is filename
    
    // Sort by timestamp (descending - newest first)
    if (g_match_index.count > 0) {
        qsort(g_match_index.items, g_match_index.count, sizeof(MatchMetadata), compare_metadata);
    }
    
    if (debug_mode) {
        printf("[ConfigManager] Fast scan: indexed %d matches total\n", g_match_index.count);
    }
}

void match_history_init(void) {
    // NEW: Fast initialization - only scan filenames
    scan_match_files();
    
    // Initialize cache
    g_match_cache.max_pages = MAX_CACHED_PAGES;
    g_match_cache.page_count = 0;
    g_match_cache.pages = calloc(MAX_CACHED_PAGES, sizeof(CachePage));
    
    // Legacy: Initialize old system for backward compatibility
    g_history_count = 0;
    g_history_capacity = 50;
    g_history_list = calloc(g_history_capacity, sizeof(MatchHistoryEntry));
}


/* Portable strtok_r implementation for Windows/MinGW */
static char* strtok_r_portable(char *str, const char *delim, char **saveptr) {
    char *token;
    if (str == NULL) str = *saveptr;
    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }
    token = str;
    str = str + strcspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
    } else {
        *str = '\0';
        *saveptr = str + 1;
    }
    return token;
}

static void parse_match_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;

    if (g_history_count >= g_history_capacity) {
        g_history_capacity *= 2;
        g_history_list = realloc(g_history_list, g_history_capacity * sizeof(MatchHistoryEntry));
    }

    MatchHistoryEntry* m = &g_history_list[g_history_count++];
    memset(m, 0, sizeof(MatchHistoryEntry));
    // Defaults for back-compat
    m->clock.enabled = false;

    // Force ID from filename to ensure sync (Fix deletion bug)
    // Extract basename: "C:/.../matches/m_123.json" -> "m_123"
    const char* base = strrchr(path, '/');
#ifdef _WIN32
    const char* base_win = strrchr(path, '\\');
    if (base_win > base) base = base_win;
#endif
    
    if (base) base++; // Skip slash
    else base = path;
    
    snprintf(m->id, sizeof(m->id), "%.63s", base);
    
    // Remove .json extension if present
    char* ext = strstr(m->id, ".json");
    if (ext) *ext = '\0';

    int context = 0; // 0: Root, 1: White, 2: Black, 3: Clock
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        // Context switching
        if (strstr(line, "\"white\": {")) context = 1;
        else if (strstr(line, "\"black\": {")) context = 2;
        else if (strstr(line, "\"clock\": {")) context = 3;
        else if (strchr(line, '}') && context != 0) context = 0;

        if (strstr(line, "\"timestamp\"")) {
            char* p = strchr(line, ':');
            if (p) m->timestamp = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (strstr(line, "\"created_at_ms\"")) {
            char* p = strchr(line, ':');
            if (p) m->created_at_ms = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (strstr(line, "\"started_at_ms\"")) {
            char* p = strchr(line, ':');
            if (p) m->started_at_ms = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (strstr(line, "\"ended_at_ms\"")) {
            char* p = strchr(line, ':');
            if (p) m->ended_at_ms = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (context == 3) { // Clock context
            if (strstr(line, "\"initial_ms\"")) {
                 char* p = strchr(line, ':'); if(p) m->clock.initial_ms = atoi(p+1);
            }
            else if (strstr(line, "\"increment_ms\"")) {
                 char* p = strchr(line, ':'); if(p) m->clock.increment_ms = atoi(p+1);
            }
            else if (strstr(line, "\"enabled\"")) {
                 if (strstr(line, "true")) m->clock.enabled = true;
                 else if (strstr(line, "false")) m->clock.enabled = false;
            }
        }
        else if (context == 1 || context == 2) { // Player context (White/Black)
            MatchPlayerConfig* p_cfg = (context == 1) ? &m->white : &m->black;
            
            if (strstr(line, "\"is_ai\"")) {
                if (strstr(line, "true")) p_cfg->is_ai = true;
                else if (strstr(line, "false")) p_cfg->is_ai = false;
            }
            
            if (strstr(line, "\"elo\"")) {
                char* p = strstr(line, "\"elo\"");
                p = strchr(p, ':'); if(p) p_cfg->elo = atoi(p+1);
            }
            
            if (strstr(line, "\"depth\"")) {
                char* p = strstr(line, "\"depth\"");
                p = strchr(p, ':'); if(p) p_cfg->depth = atoi(p+1);
            }
            
            if (strstr(line, "\"movetime\"")) {
                char* p = strstr(line, "\"movetime\"");
                p = strchr(p, ':'); if(p) p_cfg->movetime = atoi(p+1);
            }
            
            if (strstr(line, "\"engine_type\"")) {
                char* p = strstr(line, "\"engine_type\"");
                p = strchr(p, ':'); if(p) p_cfg->engine_type = atoi(p+1);
            }
            
            if (strstr(line, "\"engine_path\"")) {
                extract_json_str(line, "engine_path", p_cfg->engine_path, sizeof(p_cfg->engine_path));
            }
        }
        // Think time array parsing
        else if (strstr(line, "\"think_time_ms\"")) {
            char* start = strchr(line, '[');
            if (start) {
                start++;
                int count = 0;
                char* walk = start;
                while (*walk && *walk != ']') {
                    if (*walk == ',') count++;
                    walk++;
                }
                count += 1;
                
                m->think_time_ms = malloc(count * 2 * sizeof(int));
                m->think_time_count = 0;
                
                char* saved_ptr = NULL;
                char* token = strtok_r_portable(start, ", ]\r\n\"", &saved_ptr);
                while (token) {
                     m->think_time_ms[m->think_time_count++] = atoi(token);
                     token = strtok_r_portable(NULL, ", ]\r\n\"", &saved_ptr);
                }
            }
        }
        else if (strstr(line, "\"game_mode\"")) {
            char* p = strchr(line, ':');
            if (p) m->game_mode = atoi(p + 1);
        }
        else if (strstr(line, "\"result_reason\"")) {
            extract_json_str(line, "result_reason", m->result_reason, sizeof(m->result_reason));
        }
        else if (strstr(line, "\"result\"")) {
            extract_json_str(line, "result", m->result, sizeof(m->result));
        }
        else if (strstr(line, "\"move_count\"")) {
            char* p = strchr(line, ':');
            if (p) m->move_count = atoi(p + 1);
        }
        else if (strstr(line, "\"moves_uci\"")) {
            char val[4096];
            extract_json_str(line, "moves_uci", val, sizeof(val));
            if (m->moves_uci) free(m->moves_uci);
            m->moves_uci = strdup(val);
        }
        else if (strstr(line, "\"start_fen\"")) extract_json_str(line, "start_fen", m->start_fen, sizeof(m->start_fen));
        else if (strstr(line, "\"final_fen\"")) extract_json_str(line, "final_fen", m->final_fen, sizeof(m->final_fen));
    }
    fclose(f);
    // Check this
    if (debug_mode){
        printf("[ConfigManager] Match History loaded from: %s\n", path); 
        printf("  \"id\": \"%s\",\n", m->id);
        printf("  \"timestamp\": %lld,\n", (long long)m->timestamp);
        printf("  \"created_at_ms\": %lld,\n", (long long)m->created_at_ms);
        printf("  \"started_at_ms\": %lld,\n", (long long)m->started_at_ms);
        printf("  \"ended_at_ms\": %lld,\n", (long long)m->ended_at_ms);
        printf("  \"game_mode\": %d,\n", m->game_mode);
        
        // Clock Settings
        printf("  \"clock\": {\n");
        printf("    \"enabled\": %s,\n", m->clock.enabled ? "true" : "false");
        printf("    \"initial_ms\": %d,\n", m->clock.initial_ms);
        printf("    \"increment_ms\": %d,\n", m->clock.increment_ms);
        printf("  },\n");
        
        printf("  \"white\": {\n");
        printf("    \"is_ai\": %s, \"elo\": %d, \"depth\": %d, \"movetime\": %d, \"engine_type\": %d, \"engine_path\": \"%s\"\n",
                m->white.is_ai ? "true" : "false", m->white.elo, m->white.depth, m->white.movetime, m->white.engine_type, m->white.engine_path);
        printf("  },\n");
        
        printf("  \"black\": {\n");
        printf("    \"is_ai\": %s, \"elo\": %d, \"depth\": %d, \"movetime\": %d, \"engine_type\": %d, \"engine_path\": \"%s\"\n",
                m->black.is_ai ? "true" : "false", m->black.elo, m->black.depth, m->black.movetime, m->black.engine_type, m->black.engine_path);
        printf("  },\n");
        
        printf("  \"result\": \"%s\",\n", m->result);
        printf("  \"result_reason\": \"%s\",\n", m->result_reason);
        printf("  \"move_count\": %d,\n", m->move_count);
        printf("  \"moves_uci\": \"%s\",\n", m->moves_uci ? m->moves_uci : "");
        
        // Think Times
        if (m->think_time_count > 0 && m->think_time_ms) {
            printf("  \"think_time_ms\": [");
            for (int i = 0; i < m->think_time_count; i++) {
                printf("%d%s", m->think_time_ms[i], (i < m->think_time_count - 1) ? ", " : "");
            }
            printf("],\n");
        }
        printf("  \"start_fen\": \"%s\",\n", m->start_fen);
        printf("  \"final_fen\": \"%s\"\n", m->final_fen);
        printf("}\n");
    }
}

void match_history_load_all(void) {
    determine_base_dir();
    char matches_dir[4096];
    snprintf(matches_dir, sizeof(matches_dir), "%s/matches", g_base_dir);
    
    // Clear current list
    for (int i = 0; i < g_history_count; i++) match_history_free_entry(&g_history_list[i]);
    g_history_count = 0;

#ifdef _WIN32
    char search_path[4096];
    snprintf(search_path, sizeof(search_path), "%.2048s/*.json", matches_dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            char full_path[4096];
            snprintf(full_path, sizeof(full_path), "%.2048s/%.1024s", matches_dir, fd.cFileName);
            parse_match_file(full_path);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR* d = opendir(matches_dir);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".json")) {
                char full_path[4096];
                snprintf(full_path, sizeof(full_path), "%s/%s", matches_dir, dir->d_name);
                parse_match_file(full_path);
            }
        }
        closedir(d);
    }
#endif
    if(debug_mode) printf("[ConfigManager] Loaded %d matches from %s\n", g_history_count, matches_dir);
}

void match_history_delete(const char* id) {
    if (!id) return;
    determine_base_dir();
    char path[4096];
    snprintf(path, sizeof(path), "%s/matches/%s.json", g_base_dir, id);
    
#ifdef _WIN32
    DeleteFileA(path);
#else
    remove(path);
#endif

    // Remove from memory
    int idx = -1;
    for (int i = 0; i< g_history_count; i++) {
        if (strcmp(g_history_list[i].id, id) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx != -1) {
        match_history_free_entry(&g_history_list[idx]);
        for (int i = idx; i < g_history_count - 1; i++) {
            g_history_list[i] = g_history_list[i+1];
        }
        g_history_count--;
    }
    
    // NEW: Invalidate pagination cache
    invalidate_cache();
    
    if(debug_mode) printf("[ConfigManager] Deleted match: %s\n", path);
}

void match_history_add(MatchHistoryEntry* entry) {
    if (!entry) return;
    if (g_history_count >= g_history_capacity) {
        g_history_capacity *= 2;
        g_history_list = realloc(g_history_list, g_history_capacity * sizeof(MatchHistoryEntry));
    }
    
    MatchHistoryEntry* dest = &g_history_list[g_history_count++];
    *dest = *entry;
    *dest = *entry;
    if (entry->moves_uci) dest->moves_uci = strdup(entry->moves_uci);
    
    save_single_match(dest);
    
    // NEW: Invalidate pagination cache
    invalidate_cache();
}

MatchHistoryEntry* match_history_find_by_id(const char* id) {
    if (!id) return NULL;
    for (int i = 0; i < g_history_count; i++) {
        if (strcmp(g_history_list[i].id, id) == 0) return &g_history_list[i];
    }
    return NULL;
}

MatchHistoryEntry* match_history_get_list(int* count) {
    if (count) *count = g_history_count;
    return g_history_list;
}

// --- NEW: Pagination API Implementation ---

int match_history_get_count(void) {
    return g_match_index.count;
}

// Find cached page or return NULL
static CachePage* find_cached_page(int page_num) {
    for (int i = 0; i < g_match_cache.page_count; i++) {
        if (g_match_cache.pages[i].page_number == page_num) {
            return &g_match_cache.pages[i];
        }
    }
    return NULL;
}

// Find LRU page for eviction
static CachePage* find_lru_page(void) {
    if (g_match_cache.page_count == 0) return NULL;
    
    CachePage* lru = &g_match_cache.pages[0];
    for (int i = 1; i < g_match_cache.page_count; i++) {
        if (g_match_cache.pages[i].last_access_time < lru->last_access_time) {
            lru = &g_match_cache.pages[i];
        }
    }
    return lru;
}

// Free a cached page's entries
static void free_cache_page(CachePage* page) {
    if (!page || !page->entries) return;
    
    for (int i = 0; i < page->entry_count; i++) {
        match_history_free_entry(&page->entries[i]);
    }
    free(page->entries);
    page->entries = NULL;
    page->entry_count = 0;
}

// Load a single match by ID into an entry
static bool load_match_by_id(const char* id, MatchHistoryEntry* entry) {
    determine_base_dir();
    char path[4096];
    snprintf(path, sizeof(path), "%s/matches/%s.json", g_base_dir, id);
    
    FILE* f = fopen(path, "r");
    if (!f) return false;
    
    memset(entry, 0, sizeof(MatchHistoryEntry));
    snprintf(entry->id, sizeof(entry->id), "%s", id);
    entry->clock.enabled = false;
    
    // Parse the file (reuse existing parse_match_file logic)
    int context = 0;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "\"white\": {")) context = 1;
        else if (strstr(line, "\"black\": {")) context = 2;
        else if (strstr(line, "\"clock\": {")) context = 3;
        else if (strchr(line, '}') && context != 0) context = 0;
        
        if (strstr(line, "\"timestamp\"")) {
            char* p = strchr(line, ':');
            if (p) entry->timestamp = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (strstr(line, "\"created_at_ms\"")) {
            char* p = strchr(line, ':');
            if (p) entry->created_at_ms = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (strstr(line, "\"started_at_ms\"")) {
            char* p = strchr(line, ':');
            if (p) entry->started_at_ms = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (strstr(line, "\"ended_at_ms\"")) {
            char* p = strchr(line, ':');
            if (p) entry->ended_at_ms = (int64_t)strtoll(p + 1, NULL, 10);
        }
        else if (context == 3) {
            if (strstr(line, "\"initial_ms\"")) {
                char* p = strchr(line, ':'); if(p) entry->clock.initial_ms = atoi(p+1);
            }
            else if (strstr(line, "\"increment_ms\"")) {
                char* p = strchr(line, ':'); if(p) entry->clock.increment_ms = atoi(p+1);
            }
            else if (strstr(line, "\"enabled\"")) {
                if (strstr(line, "true")) entry->clock.enabled = true;
                else if (strstr(line, "false")) entry->clock.enabled = false;
            }
        }
        else if (context == 1 || context == 2) {
            MatchPlayerConfig* p_cfg = (context == 1) ? &entry->white : &entry->black;
            
            if (strstr(line, "\"is_ai\"")) {
                if (strstr(line, "true")) p_cfg->is_ai = true;
                else if (strstr(line, "false")) p_cfg->is_ai = false;
            }
            if (strstr(line, "\"elo\"")) {
                char* p = strstr(line, "\"elo\"");
                p = strchr(p, ':'); if(p) p_cfg->elo = atoi(p+1);
            }
            if (strstr(line, "\"depth\"")) {
                char* p = strstr(line, "\"depth\"");
                p = strchr(p, ':'); if(p) p_cfg->depth = atoi(p+1);
            }
            if (strstr(line, "\"movetime\"")) {
                char* p = strstr(line, "\"movetime\"");
                p = strchr(p, ':'); if(p) p_cfg->movetime = atoi(p+1);
            }
            if (strstr(line, "\"engine_type\"")) {
                char* p = strstr(line, "\"engine_type\"");
                p = strchr(p, ':'); if(p) p_cfg->engine_type = atoi(p+1);
            }
            if (strstr(line, "\"engine_path\"")) {
                extract_json_str(line, "engine_path", p_cfg->engine_path, sizeof(p_cfg->engine_path));
            }
        }
        else if (strstr(line, "\"game_mode\"")) {
            char* p = strchr(line, ':');
            if (p) entry->game_mode = atoi(p + 1);
        }
        else if (strstr(line, "\"result_reason\"")) {
            extract_json_str(line, "result_reason", entry->result_reason, sizeof(entry->result_reason));
        }
        else if (strstr(line, "\"result\"")) {
            extract_json_str(line, "result", entry->result, sizeof(entry->result));
        }
        else if (strstr(line, "\"move_count\"")) {
            char* p = strchr(line, ':');
            if (p) entry->move_count = atoi(p + 1);
        }
        else if (strstr(line, "\"moves_uci\"")) {
            char val[4096];
            extract_json_str(line, "moves_uci", val, sizeof(val));
            if (entry->moves_uci) free(entry->moves_uci);
            entry->moves_uci = strdup(val);
        }
        else if (strstr(line, "\"start_fen\"")) extract_json_str(line, "start_fen", entry->start_fen, sizeof(entry->start_fen));
        else if (strstr(line, "\"final_fen\"")) extract_json_str(line, "final_fen", entry->final_fen, sizeof(entry->final_fen));
    }
    
    fclose(f);
    return true;
}

MatchHistoryEntry* match_history_get_page(int page_num, int* out_count) {
    // Check cache first
    CachePage* cached = find_cached_page(page_num);
    if (cached) {
        cached->last_access_time = get_time_ms();
        if (out_count) *out_count = cached->entry_count;
        return cached->entries;
    }
    
    // Calculate range for this page
    int start_idx = page_num * PAGE_SIZE;
    if (start_idx >= g_match_index.count) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    
    int end_idx = start_idx + PAGE_SIZE;
    if (end_idx > g_match_index.count) {
        end_idx = g_match_index.count;
    }
    int count = end_idx - start_idx;
    
    // Allocate new page
    CachePage new_page = {0};
    new_page.page_number = page_num;
    new_page.entry_count = count;
    new_page.entries = calloc(count, sizeof(MatchHistoryEntry));
    new_page.last_access_time = get_time_ms();
    
    // Load matches for this page
    for (int i = 0; i < count; i++) {
        const char* id = g_match_index.items[start_idx + i].id;
        if (!load_match_by_id(id, &new_page.entries[i])) {
            // Failed to load, use metadata
            snprintf(new_page.entries[i].id, sizeof(new_page.entries[i].id), "%s", id);
            new_page.entries[i].timestamp = g_match_index.items[start_idx + i].timestamp;
        }
    }
    
    // Insert into cache (evict LRU if needed)
    if (g_match_cache.page_count >= g_match_cache.max_pages) {
        CachePage* lru = find_lru_page();
        if (lru) {
            free_cache_page(lru);
            *lru = new_page;
        }
    } else {
        g_match_cache.pages[g_match_cache.page_count++] = new_page;
    }
    
    if (out_count) *out_count = count;
    
    // Return pointer to cached page
    cached = find_cached_page(page_num);
    return cached ? cached->entries : NULL;
}

// Invalidate cache when matches are added/deleted
static void invalidate_cache(void) {
    for (int i = 0; i < g_match_cache.page_count; i++) {
        free_cache_page(&g_match_cache.pages[i]);
    }
    g_match_cache.page_count = 0;
    
    // Rescan index
    if (g_match_index.items) {
        free(g_match_index.items);
        g_match_index.items = NULL;
        g_match_index.count = 0;
    }
    scan_match_files();
}


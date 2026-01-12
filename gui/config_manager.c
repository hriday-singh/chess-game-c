#include "config_manager.h"
#include "theme_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
static char g_app_name[64] = "HAL Chess";

void config_set_app_param(const char* app_name) {
    if (app_name && strlen(app_name) > 0) {
        strncpy(g_app_name, app_name, sizeof(g_app_name) - 1);
        // Clear config path ensuring it's recalculated
        g_config_path[0] = '\0';
    }
}

static void determine_config_path(void) {
    if (g_config_path[0] != '\0') return;

    const char* home = NULL;
#ifdef _WIN32
    home = getenv("APPDATA");
    if (!home) home = getenv("USERPROFILE");
#else
    home = getenv("XDG_CONFIG_HOME");
    if (!home) home = getenv("HOME");
#endif

    if (!home) {
        strncpy(g_config_path, "config.json", sizeof(g_config_path) - 1);
        return;
    }

    char dir[1024];
#ifdef _WIN32
    snprintf(dir, sizeof(dir), "%s/%s", home, g_app_name);
#else
    snprintf(dir, sizeof(dir), "%s/.config/%s", home, g_app_name);
#endif

    // Create directory, ignore error (might exist)
    MKDIR(dir);
    
    snprintf(g_config_path, sizeof(g_config_path), "%s/config.json", dir);
}

// Helper to set defaults
static void set_defaults(void) {
    // General
    strncpy(g_config.theme, DEFAULT_THEME, sizeof(g_config.theme) - 1);
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
    g_config.analysis_use_custom = false;
    
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
    strncpy(g_config.board_theme_name, "Green & White", sizeof(g_config.board_theme_name) - 1);
    g_config.light_square_color[0] = '\0';
    g_config.dark_square_color[0] = '\0';

    // Piece Theme
    strncpy(g_config.piece_set, "caliente", sizeof(g_config.piece_set) - 1);
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
        strncpy(dest, val_start, val_len);
        dest[val_len] = '\0';
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
    strncpy(key, key_start, key_len);
    
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
        else if (strcmp(key, "black_stroke_width") == 0) g_config.black_stroke_width = atof(val_start);
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
    fclose(f);
    if (debug_mode) {
        printf("Config loaded from %s\n", g_config_path);
        printf("--- [DEBUG] Config Summary ---\n");
        printf("  Theme: %s\n", g_config.theme);
        printf("  Dark Mode: %s\n", g_config.is_dark_mode ? "true" : "false");
        printf("  Tutorial: %s\n", g_config.show_tutorial_dialog ? "true" : "false");
        printf("  Game Mode: %d\n", g_config.game_mode);
        printf("  Play As: %d\n", g_config.play_as);
        printf("  Hints: %s\n", g_config.hints_dots ? "Dots" : "Squares");
        printf("  Animations: %s\n", g_config.enable_animations ? "ON" : "OFF");
        printf("  SFX: %s\n", g_config.enable_sfx ? "ON" : "OFF");
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
    if (debug_mode) printf("[DEBUG] Config saved to %s\n", g_config_path);
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

#define MAX_CUSTOM_THEMES 50
static AppTheme g_custom_themes[MAX_CUSTOM_THEMES];
static int g_custom_theme_count = 0;
static char g_themes_path[2048] = {0}; // Increased to match config_path

static void determine_themes_path(void) {
    if (g_themes_path[0] != '\0') return;
    determine_config_path(); // Ensure base path exists
    
    // Replace config.json with app_themes.json
    // g_config_path is like ".../config.json"
    snprintf(g_themes_path, sizeof(g_themes_path), "%s", g_config_path);
    char* last_slash = strrchr(g_themes_path, '/');
    #ifdef _WIN32
    if (!last_slash) last_slash = strrchr(g_themes_path, '\\');
    #endif
    
    if (last_slash) {
        strcpy(last_slash + 1, "app_themes.json");
    } else {
        // Should not happen if config path is full path
        strcpy(g_themes_path, "app_themes.json");
    }
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
                    strncpy(dest, val_start, len);
                    dest[len] = '\0';
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
    if (debug_mode) printf("[DEBUG] Loaded %d custom themes from %s\n", g_custom_theme_count, g_themes_path);
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
               if (debug_mode) printf("[Config] Cannot overwrite system theme %s\n", theme->theme_id);
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
    if (debug_mode) printf("[DEBUG] Themes saved to %s\n", g_themes_path);
}

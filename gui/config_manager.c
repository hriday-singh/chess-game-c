#include "config_manager.h"
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

static bool is_debug = true;

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

    char dir[2048];
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
    strncpy(g_config.piece_set, "alpha", sizeof(g_config.piece_set) - 1);
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
    } 
    else if (strncmp(val_start, "false", 5) == 0) {
        if (strcmp(key, "is_dark_mode") == 0) g_config.is_dark_mode = false;
        else if (strcmp(key, "show_tutorial_dialog") == 0) g_config.show_tutorial_dialog = false;
        else if (strcmp(key, "int_is_advanced") == 0) g_config.int_is_advanced = false;
        else if (strcmp(key, "nnue_enabled") == 0) g_config.nnue_enabled = false;
        else if (strcmp(key, "custom_is_advanced") == 0) g_config.custom_is_advanced = false;
    }
    // NUMBERS
    else {
        if (strcmp(key, "int_elo") == 0) g_config.int_elo = atoi(val_start);
        else if (strcmp(key, "int_depth") == 0) g_config.int_depth = atoi(val_start);
        else if (strcmp(key, "int_movetime") == 0) g_config.int_movetime = atoi(val_start);
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
    if (is_debug) printf("Config loaded from %s\n", g_config_path);
    return true;
}

bool config_save(void) {
    determine_config_path();
    
    FILE* f = fopen(g_config_path, "w");
    if (!f) {
        printf("Failed to open config file for writing: %s\n", g_config_path);
        return false;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "    \"theme\": \"%s\",\n", g_config.theme);
    fprintf(f, "    \"is_dark_mode\": %s,\n", g_config.is_dark_mode ? "true" : "false");
    fprintf(f, "    \"show_tutorial_dialog\": %s,\n", g_config.show_tutorial_dialog ? "true" : "false");
    
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
    if (is_debug) printf("[DEBUG] Config saved to %s\n", g_config_path);
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

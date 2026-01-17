#include "theme_data.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/stat.h>

#define DEFAULT_FONT_NAME "caliente"
#define DEFAULT_WHITE_STROKE_WIDTH 0.5
#define DEFAULT_BLACK_STROKE_WIDTH 0.1

struct ThemeData {
    // Board colors (RGB 0.0-1.0)
    double lightSquareR, lightSquareG, lightSquareB;
    double darkSquareR, darkSquareG, darkSquareB;
    
    // White piece colors
    double whitePieceR, whitePieceG, whitePieceB;
    double whiteStrokeR, whiteStrokeG, whiteStrokeB;
    double whiteStrokeWidth;
    
    // Black piece colors
    double blackPieceR, blackPieceG, blackPieceB;
    double blackStrokeR, blackStrokeG, blackStrokeB;
    double blackStrokeWidth;
    
    // SVG piece set name
    char* fontName;

    // SVG Cache
    // [owner][type] (0=White, 1=Black)
    cairo_surface_t* pieceCache[2][6];
    char* cachedFontName; // To detect when to clear cache
};

// Forward declarations
static void clear_piece_cache(ThemeData* theme);


bool theme_data_is_standard_font(const char* font_name) {
    if (!font_name) return true;
    if (strcmp(font_name, "Segoe UI Symbol") == 0) return true;
    if (strcmp(font_name, "Default") == 0) return true;
    return false;
}

// Helper: Color to hex string
static void color_to_hex(double r, double g, double b, char* hex, size_t hex_size) {
    int ri = (int)(r * 255.0 + 0.5);
    int gi = (int)(g * 255.0 + 0.5);
    int bi = (int)(b * 255.0 + 0.5);
    snprintf(hex, hex_size, "#%02X%02X%02X", ri, gi, bi);
}

// Helper: Hex string to color
static bool hex_to_color(const char* hex, double* r, double* g, double* b) {
    if (!hex || hex[0] != '#') return false;
    unsigned int ri, gi, bi;
    if (sscanf(hex, "#%02X%02X%02X", &ri, &gi, &bi) == 3) {
        *r = ri / 255.0;
        *g = gi / 255.0;
        *b = bi / 255.0;
        return true;
    }
    return false;
}

// Helper: Extract value from JSON
static char* extract_json_value(const char* json, const char* key) {
    if (!json || !key) return NULL;
    
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char* start = strstr(json, search);
    if (!start) return NULL;
    
    start += strlen(search);
    const char* end = strchr(start, '"');
    if (!end) return NULL;
    
    size_t len = end - start;
    char* result = (char*)malloc(len + 1);
    if (result) {
        snprintf(result, len + 1, "%s", start);
    }
    return result;
}


ThemeData* theme_data_new(void) {
    ThemeData* theme = (ThemeData*)calloc(1, sizeof(ThemeData));
    if (!theme) return NULL;
    
    // Default board colors (Classic Wood)
    theme->lightSquareR = 240.0 / 255.0;
    theme->lightSquareG = 217.0 / 255.0;
    theme->lightSquareB = 181.0 / 255.0;
    theme->darkSquareR = 181.0 / 255.0;
    theme->darkSquareG = 136.0 / 255.0;
    theme->darkSquareB = 99.0 / 255.0;
    
    // Default white piece colors
    theme->whitePieceR = 1.0;
    theme->whitePieceG = 1.0;
    theme->whitePieceB = 1.0;
    theme->whiteStrokeR = 0x22 / 255.0;
    theme->whiteStrokeG = 0x22 / 255.0;
    theme->whiteStrokeB = 0x22 / 255.0;
    theme->whiteStrokeWidth = DEFAULT_WHITE_STROKE_WIDTH;
    
    // Default black piece colors
    theme->blackPieceR = 0x31 / 255.0;
    theme->blackPieceG = 0x2e / 255.0;
    theme->blackPieceB = 0x2b / 255.0;
    theme->blackStrokeR = 0x31 / 255.0;
    theme->blackStrokeG = 0x2e / 255.0;
    theme->blackStrokeB = 0x2b / 255.0;
    theme->blackStrokeWidth = DEFAULT_BLACK_STROKE_WIDTH;
    
    // Default piece set
    theme->fontName = _strdup(DEFAULT_FONT_NAME);
    
    // Init cache
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 6; j++) {
            theme->pieceCache[i][j] = NULL;
        }
    }
    theme->cachedFontName = NULL;
    
    // Set defaults
    theme_data_reset_board_defaults(theme);
    
    return theme;
}

void theme_data_free(ThemeData* theme) {
    if (!theme) return;
    if (theme->fontName) free(theme->fontName);
    clear_piece_cache(theme); // Clear any cached surfaces
    free(theme);
}

const char* theme_data_get_piece_symbol(ThemeData* theme, PieceType type, Player owner) {
    (void)theme;
    if (owner == PLAYER_WHITE) {
        switch (type) {
            case PIECE_KING:   return "♔";
            case PIECE_QUEEN:  return "♕";
            case PIECE_ROOK:   return "♖";
            case PIECE_BISHOP: return "♗";
            case PIECE_KNIGHT: return "♘";
            case PIECE_PAWN:   return "♙";
            default: return "?";
        }
    } else {
        switch (type) {
            case PIECE_KING:   return "♚";
            case PIECE_QUEEN:  return "♛";
            case PIECE_ROOK:   return "♜";
            case PIECE_BISHOP: return "♝";
            case PIECE_KNIGHT: return "♞";
            case PIECE_PAWN:   return "♟";
            default: return "?";
        }
    }
}

// Get piece image path (for SVG themes)
char* theme_data_get_piece_image_path(ThemeData* theme, PieceType type, Player owner) {
    if (!theme || !theme->fontName) return NULL;
    
    if (theme_data_is_standard_font(theme->fontName)) {
        return NULL; // Use text rendering fallback
    }
    
    // It's a piece set folder name (SVG themes)
    // Construct filename: e.g. "wP.svg", "bN.svg"
    char filename[8];
    char colorChar = (owner == PLAYER_WHITE) ? 'w' : 'b';
    char pieceChar = 'P';
    
    switch (type) {
        case PIECE_PAWN:   pieceChar = 'P'; break;
        case PIECE_KNIGHT: pieceChar = 'N'; break;
        case PIECE_BISHOP: pieceChar = 'B'; break;
        case PIECE_ROOK:   pieceChar = 'R'; break;
        case PIECE_QUEEN:  pieceChar = 'Q'; break;
        case PIECE_KING:   pieceChar = 'K'; break;
        default: return NULL;
    }
    
    snprintf(filename, sizeof(filename), "%c%c.svg", colorChar, pieceChar);
    
    // Try to find the file in assets/images/piece/<theme_name>/
    // We need to check current dir, then build dir
    
    // Try local assets first
    char* path = (char*)malloc(1024);
    if (!path) return NULL;
    
    snprintf(path, 1024, "assets/images/piece/%s/%s", theme->fontName, filename);
    struct stat st;
    if (stat(path, &st) == 0) {
        // printf("[ThemeData] Found SVG: %s\n", path);
        return path;
    }
    
    // Try build assets
    snprintf(path, 1024, "build/assets/images/piece/%s/%s", theme->fontName, filename);
    if (stat(path, &st) == 0) {
        // printf("[ThemeData] Found SVG (build): %s\n", path);
        return path;
    }

    printf("[ThemeData] SVG NOT FOUND: %s/%s\n", theme->fontName, filename);
    
    free(path);
    return NULL;
}

static void clear_piece_cache(ThemeData* theme) {
    if (!theme) return;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 6; j++) {
            if (theme->pieceCache[i][j]) {
                cairo_surface_destroy(theme->pieceCache[i][j]);
                theme->pieceCache[i][j] = NULL;
            }
        }
    }
    if (theme->cachedFontName) {
        free(theme->cachedFontName);
        theme->cachedFontName = NULL;
    }
}

cairo_surface_t* theme_data_get_piece_surface(ThemeData* theme, PieceType type, Player owner) {
    if (!theme) return NULL;
    
    if (theme_data_is_standard_font(theme->fontName)) {
        return NULL; // Caller should fallback to text rendering
    }
    
    // Check cache first

    // If font name changed, clear cache
    if (theme->cachedFontName && strcmp(theme->cachedFontName, theme->fontName) != 0) {
        clear_piece_cache(theme);
    }
    if (!theme->cachedFontName) {
        theme->cachedFontName = _strdup(theme->fontName);
    }

    if (theme->pieceCache[owner][type]) {
       return theme->pieceCache[owner][type];
    }
    
    // Load it
    char* path = theme_data_get_piece_image_path(theme, type, owner);
    if (!path) return NULL;
    
    GError* error = NULL;
    // Load from file with scaling (256px height max)
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path, -1, 256, TRUE, &error);
    
    if (pixbuf) {
        // Convert to cairo surface
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);
        cairo_format_t format = gdk_pixbuf_get_has_alpha(pixbuf) ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
        int stride = cairo_format_stride_for_width(format, width);
        
        // Allocate data for cairo surface
        guchar* cairo_data = g_malloc(stride * height);
        
        // Convert pixels
        guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
        int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
        int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
        gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
        
        for (int y = 0; y < height; y++) {
             guchar* src_row = pixels + y * rowstride;
             guint32* dst_row = (guint32*)(cairo_data + y * stride);
             for (int x = 0; x < width; x++) {
                 guchar r = src_row[x * n_channels + 0];
                 guchar g = src_row[x * n_channels + 1];
                 guchar b = src_row[x * n_channels + 2];
                 guchar a = has_alpha ? src_row[x * n_channels + 3] : 255;
                 // ARGB32 (little endian)
                 dst_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
             }
        }
        
        cairo_surface_t* surface = cairo_image_surface_create_for_data(
            cairo_data, format, width, height, stride);
            
        // Attach data to surface to free it when surface is destroyed
        static cairo_user_data_key_t key;
        cairo_surface_set_user_data(surface, &key, cairo_data, (cairo_destroy_func_t)g_free);
        
        theme->pieceCache[owner][type] = surface;
        g_object_unref(pixbuf);
        printf("[ThemeData] Successfully loaded SVG for %d/%d from %s\n", type, owner, path);
    } else {
        if (error) {
            fprintf(stderr, "[ThemeData] FAILED to load SVG: %s (Error: %s)\n", path, error->message);
            g_error_free(error);
        } else {
            fprintf(stderr, "[ThemeData] FAILED to load SVG: %s (Unknown error)\n", path);
        }
    }
    free(path);
    
    return theme->pieceCache[owner][type];
}

// Board color getters/setters
void theme_data_get_light_square_color(ThemeData* theme, double* r, double* g, double* b) {
    if (theme && r && g && b) {
        *r = theme->lightSquareR;
        *g = theme->lightSquareG;
        *b = theme->lightSquareB;
    }
}

void theme_data_get_dark_square_color(ThemeData* theme, double* r, double* g, double* b) {
    if (theme && r && g && b) {
        *r = theme->darkSquareR;
        *g = theme->darkSquareG;
        *b = theme->darkSquareB;
    }
}

void theme_data_set_light_square_color(ThemeData* theme, double r, double g, double b) {
    if (theme) {
        theme->lightSquareR = r;
        theme->lightSquareG = g;
        theme->lightSquareB = b;
    }
}

void theme_data_set_dark_square_color(ThemeData* theme, double r, double g, double b) {
    if (theme) {
        theme->darkSquareR = r;
        theme->darkSquareG = g;
        theme->darkSquareB = b;
    }
}

// White piece color getters/setters
void theme_data_get_white_piece_color(ThemeData* theme, double* r, double* g, double* b) {
    if (theme && r && g && b) {
        *r = theme->whitePieceR;
        *g = theme->whitePieceG;
        *b = theme->whitePieceB;
    }
}

void theme_data_get_white_piece_stroke(ThemeData* theme, double* r, double* g, double* b) {
    if (theme && r && g && b) {
        *r = theme->whiteStrokeR;
        *g = theme->whiteStrokeG;
        *b = theme->whiteStrokeB;
    }
}

double theme_data_get_white_stroke_width(ThemeData* theme) {
    return theme ? theme->whiteStrokeWidth : DEFAULT_WHITE_STROKE_WIDTH;
}

void theme_data_set_white_piece_color(ThemeData* theme, double r, double g, double b) {
    if (theme) {
        theme->whitePieceR = r;
        theme->whitePieceG = g;
        theme->whitePieceB = b;
    }
}

void theme_data_set_white_piece_stroke(ThemeData* theme, double r, double g, double b) {
    if (theme) {
        theme->whiteStrokeR = r;
        theme->whiteStrokeG = g;
        theme->whiteStrokeB = b;
    }
}

void theme_data_set_white_stroke_width(ThemeData* theme, double width) {
    if (theme) {
        theme->whiteStrokeWidth = width;
    }
}

// Black piece color getters/setters
void theme_data_get_black_piece_color(ThemeData* theme, double* r, double* g, double* b) {
    if (theme && r && g && b) {
        *r = theme->blackPieceR;
        *g = theme->blackPieceG;
        *b = theme->blackPieceB;
    }
}

void theme_data_get_black_piece_stroke(ThemeData* theme, double* r, double* g, double* b) {
    if (theme && r && g && b) {
        *r = theme->blackStrokeR;
        *g = theme->blackStrokeG;
        *b = theme->blackStrokeB;
    }
}

double theme_data_get_black_stroke_width(ThemeData* theme) {
    return theme ? theme->blackStrokeWidth : DEFAULT_BLACK_STROKE_WIDTH;
}

void theme_data_set_black_piece_color(ThemeData* theme, double r, double g, double b) {
    if (theme) {
        theme->blackPieceR = r;
        theme->blackPieceG = g;
        theme->blackPieceB = b;
    }
}

void theme_data_set_black_piece_stroke(ThemeData* theme, double r, double g, double b) {
    if (theme) {
        theme->blackStrokeR = r;
        theme->blackStrokeG = g;
        theme->blackStrokeB = b;
    }
}

void theme_data_set_black_stroke_width(ThemeData* theme, double width) {
    if (theme) {
        theme->blackStrokeWidth = width;
    }
}

// Font getter/setter
const char* theme_data_get_font_name(ThemeData* theme) {
    return theme ? theme->fontName : DEFAULT_FONT_NAME;
}

void theme_data_set_font_name(ThemeData* theme, const char* font_name) {
    if (theme && font_name) {
        if (theme->fontName) free(theme->fontName);
        theme->fontName = _strdup(font_name);
        
        // Clear cache when theme changes
        clear_piece_cache(theme);
    }
}


// JSON export/import
char* theme_data_to_board_json(ThemeData* theme) {
    if (!theme) return NULL;
    
    char lightHex[16], darkHex[16];
    color_to_hex(theme->lightSquareR, theme->lightSquareG, theme->lightSquareB, lightHex, sizeof(lightHex));
    color_to_hex(theme->darkSquareR, theme->darkSquareG, theme->darkSquareB, darkHex, sizeof(darkHex));
    
    char* json = (char*)malloc(256);
    if (json) {
        snprintf(json, 256, "{\"light\":\"%s\", \"dark\":\"%s\"}", lightHex, darkHex);
    }
    return json;
}

char* theme_data_to_piece_json(ThemeData* theme) {
    if (!theme) return NULL;
    
    char whiteFillHex[16], whiteStrokeHex[16];
    char blackFillHex[16], blackStrokeHex[16];
    
    color_to_hex(theme->whitePieceR, theme->whitePieceG, theme->whitePieceB, whiteFillHex, sizeof(whiteFillHex));
    color_to_hex(theme->whiteStrokeR, theme->whiteStrokeG, theme->whiteStrokeB, whiteStrokeHex, sizeof(whiteStrokeHex));
    color_to_hex(theme->blackPieceR, theme->blackPieceG, theme->blackPieceB, blackFillHex, sizeof(blackFillHex));
    color_to_hex(theme->blackStrokeR, theme->blackStrokeG, theme->blackStrokeB, blackStrokeHex, sizeof(blackStrokeHex));
    
    // Note: Font data embedding not implemented in C version (would require base64 encoding)
    char* json = (char*)malloc(512);
    if (json) {
        snprintf(json, 512,
            "{\"font\":\"%s\", \"whiteFill\":\"%s\", \"whiteStroke\":\"%s\", \"whiteWidth\":\"%.2f\", "
            "\"blackFill\":\"%s\", \"blackStroke\":\"%s\", \"blackWidth\":\"%.2f\", \"fontData\":\"\"}",
            theme->fontName ? theme->fontName : "",
            whiteFillHex, whiteStrokeHex, theme->whiteStrokeWidth,
            blackFillHex, blackStrokeHex, theme->blackStrokeWidth);
    }
    return json;
}

bool theme_data_load_board_json(ThemeData* theme, const char* json) {
    if (!theme || !json) return false;
    
    char* light = extract_json_value(json, "light");
    char* dark = extract_json_value(json, "dark");
    
    bool success = false;
    if (light) {
        success = hex_to_color(light, &theme->lightSquareR, &theme->lightSquareG, &theme->lightSquareB);
        free(light);
    }
    if (dark) {
        hex_to_color(dark, &theme->darkSquareR, &theme->darkSquareG, &theme->darkSquareB);
        free(dark);
    }
    
    return success;
}

bool theme_data_load_piece_json(ThemeData* theme, const char* json) {
    if (!theme || !json) return false;
    
    char* font = extract_json_value(json, "font");
    char* whiteFill = extract_json_value(json, "whiteFill");
    char* whiteStroke = extract_json_value(json, "whiteStroke");
    char* whiteWidth = extract_json_value(json, "whiteWidth");
    char* blackFill = extract_json_value(json, "blackFill");
    char* blackStroke = extract_json_value(json, "blackStroke");
    char* blackWidth = extract_json_value(json, "blackWidth");
    
    if (font) {
        theme_data_set_font_name(theme, font);
        free(font);
    }
    
    if (whiteFill) {
        hex_to_color(whiteFill, &theme->whitePieceR, &theme->whitePieceG, &theme->whitePieceB);
        free(whiteFill);
    }
    
    if (whiteStroke) {
        hex_to_color(whiteStroke, &theme->whiteStrokeR, &theme->whiteStrokeG, &theme->whiteStrokeB);
        free(whiteStroke);
    }
    
    if (whiteWidth) {
        theme->whiteStrokeWidth = atof(whiteWidth);
        free(whiteWidth);
    }
    
    if (blackFill) {
        hex_to_color(blackFill, &theme->blackPieceR, &theme->blackPieceG, &theme->blackPieceB);
        free(blackFill);
    }
    
    if (blackStroke) {
        hex_to_color(blackStroke, &theme->blackStrokeR, &theme->blackStrokeG, &theme->blackStrokeB);
        free(blackStroke);
    }
    
    if (blackWidth) {
        theme->blackStrokeWidth = atof(blackWidth);
        free(blackWidth);
    }
    
    return true;
}

// Reset to defaults
void theme_data_reset_board_defaults(ThemeData* theme) {
    if (!theme) return;
    theme->lightSquareR = 240.0 / 255.0;
    theme->lightSquareG = 217.0 / 255.0;
    theme->lightSquareB = 181.0 / 255.0;
    theme->darkSquareR = 181.0 / 255.0;
    theme->darkSquareG = 136.0 / 255.0;
    theme->darkSquareB = 99.0 / 255.0;
}

void theme_data_reset_piece_defaults(ThemeData* theme) {
    if (!theme) return;
    
    theme->whitePieceR = 1.0; theme->whitePieceG = 1.0; theme->whitePieceB = 1.0; // White
    theme->whiteStrokeR = 0x22 / 255.0; theme->whiteStrokeG = 0x22 / 255.0; theme->whiteStrokeB = 0x22 / 255.0; // Grey stroke (Startup Default)
    theme->whiteStrokeWidth = DEFAULT_WHITE_STROKE_WIDTH;
    
    theme->blackPieceR = 0x31 / 255.0; theme->blackPieceG = 0x2e / 255.0; theme->blackPieceB = 0x2b / 255.0; // Dark
    theme->blackStrokeR = 0x31 / 255.0; theme->blackStrokeG = 0x2e / 255.0; theme->blackStrokeB = 0x2b / 255.0; // Dark stroke
    theme->blackStrokeWidth = DEFAULT_BLACK_STROKE_WIDTH;
    
    // Reset font to default
    theme_data_set_font_name(theme, DEFAULT_FONT_NAME);
}

void theme_data_reset_piece_colors_only(ThemeData* theme) {
    if (!theme) return;
    
    theme->whitePieceR = 1.0; theme->whitePieceG = 1.0; theme->whitePieceB = 1.0; // White
    theme->whiteStrokeR = 0x22 / 255.0; theme->whiteStrokeG = 0x22 / 255.0; theme->whiteStrokeB = 0x22 / 255.0; // Grey
    theme->whiteStrokeWidth = DEFAULT_WHITE_STROKE_WIDTH;
    
    theme->blackPieceR = 0x31 / 255.0; theme->blackPieceG = 0x2e / 255.0; theme->blackPieceB = 0x2b / 255.0; // Dark
    theme->blackStrokeR = 0x31 / 255.0; theme->blackStrokeG = 0x2e / 255.0; theme->blackStrokeB = 0x2b / 255.0; // Dark
    theme->blackStrokeWidth = DEFAULT_BLACK_STROKE_WIDTH;
    // Do NOT reset font
}

// Apply templates
void theme_data_apply_board_template(ThemeData* theme, const char* template_name) {
    if (!theme || !template_name) return;
    
    if (strcmp(template_name, "Classic Wood") == 0) {
        theme->lightSquareR = 240.0 / 255.0;
        theme->lightSquareG = 217.0 / 255.0;
        theme->lightSquareB = 181.0 / 255.0;
        theme->darkSquareR = 181.0 / 255.0;
        theme->darkSquareG = 136.0 / 255.0;
        theme->darkSquareB = 99.0 / 255.0;
    } else if (strcmp(template_name, "Green & White") == 0) {
        theme->lightSquareR = 238.0 / 255.0;
        theme->lightSquareG = 238.0 / 255.0;
        theme->lightSquareB = 210.0 / 255.0;
        theme->darkSquareR = 118.0 / 255.0;
        theme->darkSquareG = 150.0 / 255.0;
        theme->darkSquareB = 86.0 / 255.0;
    } else if (strcmp(template_name, "Blue Ocean") == 0) {
        theme->lightSquareR = 200.0 / 255.0;
        theme->lightSquareG = 220.0 / 255.0;
        theme->lightSquareB = 240.0 / 255.0;
        theme->darkSquareR = 80.0 / 255.0;
        theme->darkSquareG = 130.0 / 255.0;
        theme->darkSquareB = 180.0 / 255.0;
    } else if (strcmp(template_name, "Dark Mode") == 0) {
        theme->lightSquareR = 150.0 / 255.0;
        theme->lightSquareG = 150.0 / 255.0;
        theme->lightSquareB = 150.0 / 255.0;
        theme->darkSquareR = 50.0 / 255.0;
        theme->darkSquareG = 50.0 / 255.0;
        theme->darkSquareB = 50.0 / 255.0;
    }
}

#include "config_manager.h"

void theme_data_load_config(ThemeData* theme, void* config_struct) {
    if (!theme || !config_struct) return;
    AppConfig* cfg = (AppConfig*)config_struct;
    
    // Load Board Theme
    // If we have a named template (other than "Custom"), try to apply it first
    // BUT we also have specific colors saved, which might override or be exact matches.
    // If the user saved "Green & White" but then tweaked a color, it's technically "Custom" but we might not track that name change in config until saved.
    // If we simply rely on the colors saved in config (which are accurate), we don't need to apply the template by name.
    // The "template name" is mostly for the UI dropdown.
    // However, if colors are missing (empty strings), we should use the name.
    
    if (strlen(cfg->light_square_color) > 0 && strlen(cfg->dark_square_color) > 0) {
        hex_to_color(cfg->light_square_color, &theme->lightSquareR, &theme->lightSquareG, &theme->lightSquareB);
        hex_to_color(cfg->dark_square_color, &theme->darkSquareR, &theme->darkSquareG, &theme->darkSquareB);
    } else {
        // Fallback to template name if colors invalid/missing
        theme_data_apply_board_template(theme, cfg->board_theme_name);
    }
    
    // Load Piece Theme
    if (strlen(cfg->piece_set) > 0) {
        theme_data_set_font_name(theme, cfg->piece_set);
    }
    
    // Colors
    if (strlen(cfg->white_piece_color) > 0) hex_to_color(cfg->white_piece_color, &theme->whitePieceR, &theme->whitePieceG, &theme->whitePieceB);
    if (strlen(cfg->white_stroke_color) > 0) hex_to_color(cfg->white_stroke_color, &theme->whiteStrokeR, &theme->whiteStrokeG, &theme->whiteStrokeB);
    if (strlen(cfg->black_piece_color) > 0) hex_to_color(cfg->black_piece_color, &theme->blackPieceR, &theme->blackPieceG, &theme->blackPieceB);
    if (strlen(cfg->black_stroke_color) > 0) hex_to_color(cfg->black_stroke_color, &theme->blackStrokeR, &theme->blackStrokeG, &theme->blackStrokeB);
    
    theme->whiteStrokeWidth = cfg->white_stroke_width;
    theme->blackStrokeWidth = cfg->black_stroke_width;
    
    // Clear cache because colors/fonts changed
    clear_piece_cache(theme);
}

#ifndef THEME_DATA_H
#define THEME_DATA_H

#include "../game/types.h"
#include <stdbool.h>
#include <cairo.h>

// Forward declaration
typedef struct ThemeData ThemeData;

// Create a new ThemeData instance
ThemeData* theme_data_new(void);

// Free ThemeData
void theme_data_free(ThemeData* theme);

// Get piece symbol based on type and owner (handles custom fonts)
const char* theme_data_get_piece_symbol(ThemeData* theme, PieceType type, Player owner);

// Get piece image path (for SVG themes)
char* theme_data_get_piece_image_path(ThemeData* theme, PieceType type, Player owner);

// Get cached piece surface (lazy loaded)
// Returns a cairo_surface_t* owned by ThemeData (do not destroy)
cairo_surface_t* theme_data_get_piece_surface(ThemeData* theme, PieceType type, Player owner);

// Board colors
void theme_data_get_light_square_color(ThemeData* theme, double* r, double* g, double* b);
void theme_data_get_dark_square_color(ThemeData* theme, double* r, double* g, double* b);
void theme_data_set_light_square_color(ThemeData* theme, double r, double g, double b);
void theme_data_set_dark_square_color(ThemeData* theme, double r, double g, double b);

// Piece colors
void theme_data_get_white_piece_color(ThemeData* theme, double* r, double* g, double* b);
void theme_data_get_white_piece_stroke(ThemeData* theme, double* r, double* g, double* b);
double theme_data_get_white_stroke_width(ThemeData* theme);
void theme_data_set_white_piece_color(ThemeData* theme, double r, double g, double b);
void theme_data_set_white_piece_stroke(ThemeData* theme, double r, double g, double b);
void theme_data_set_white_stroke_width(ThemeData* theme, double width);

void theme_data_get_black_piece_color(ThemeData* theme, double* r, double* g, double* b);
void theme_data_get_black_piece_stroke(ThemeData* theme, double* r, double* g, double* b);
double theme_data_get_black_stroke_width(ThemeData* theme);
void theme_data_set_black_piece_color(ThemeData* theme, double r, double g, double b);
void theme_data_set_black_piece_stroke(ThemeData* theme, double r, double g, double b);
void theme_data_set_black_stroke_width(ThemeData* theme, double width);

// Font
const char* theme_data_get_font_name(ThemeData* theme);
void theme_data_set_font_name(ThemeData* theme, const char* font_name);

// Font management
void theme_data_load_fonts(void);
int theme_data_get_available_font_count(void);
const char* theme_data_get_available_font(int index);

// JSON export/import
char* theme_data_to_board_json(ThemeData* theme);
char* theme_data_to_piece_json(ThemeData* theme);
bool theme_data_load_board_json(ThemeData* theme, const char* json);
bool theme_data_load_piece_json(ThemeData* theme, const char* json);

// Reset to defaults
void theme_data_reset_board_defaults(ThemeData* theme);
void theme_data_reset_piece_defaults(ThemeData* theme);
void theme_data_reset_piece_colors_only(ThemeData* theme);

// Apply templates
void theme_data_apply_board_template(ThemeData* theme, const char* template_name);

// Check if font is standard Unicode
bool theme_data_is_standard_font(const char* fontName);

#endif // THEME_DATA_H


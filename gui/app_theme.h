#ifndef APP_THEME_H
#define APP_THEME_H

#include <stdbool.h>

typedef struct {
    // Surfaces
    char base_bg[32];
    char base_fg[32];
    char base_panel_bg[32];
    char base_card_bg[32];
    char base_entry_bg[32];
    
    // Accent
    char base_accent[32];
    char base_accent_fg[32];
    
    // Success
    char base_success_bg[32];
    char base_success_text[32];
    char base_success_fg[32];
    char success_hover[32];
    
    // Destructive
    char base_destructive_bg[32];
    char base_destructive_fg[32];
    char destructive_hover[32];
    
    // UI Elements
    char border_color[32];
    char dim_label[32];
    char tooltip_bg[32];
    char tooltip_fg[32];
    char button_bg[32];
    char button_hover[32];
    
    // Error
    char error_text[32];
    
    // Capture
    char capture_bg_white[32];
    char capture_bg_black[32];
} AppThemeColors;

typedef struct {
    char theme_id[64];
    char display_name[64];
    
    AppThemeColors light;
    AppThemeColors dark;
} AppTheme;

#endif // APP_THEME_H

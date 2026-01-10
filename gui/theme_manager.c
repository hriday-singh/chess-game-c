#include "theme_manager.h"
#include "config_manager.h"

static GtkCssProvider *global_provider = NULL;
static bool current_is_dark = true;
// Selection: 'A', 'B', 'C', or 'D' (Original)
static char current_theme_variant = 'C'; 

typedef struct {
    char id[64];
    char display_name[64];
    char variant_char;
} ThemeMetadata;

static const ThemeMetadata AVAILABLE_THEMES[] = {
    { "theme_a_slate", "Slate Blue", 'A' },
    { "theme_b_emerald", "Emerald Teal", 'B' },
    { "theme_c_aubergine", "Aubergine Purple", 'C' },
    { "theme_d_mocha_gold", "Mocha Gold", 'D' } // Using user's example name for D
};

static const ThemeMetadata* get_theme_by_char(char c) {
    for (size_t i = 0; i < sizeof(AVAILABLE_THEMES)/sizeof(AVAILABLE_THEMES[0]); i++) {
        if (AVAILABLE_THEMES[i].variant_char == c || AVAILABLE_THEMES[i].variant_char == (c - 32)) {
            return &AVAILABLE_THEMES[i];
        }
    }
    return &AVAILABLE_THEMES[2]; // Default to C if not found
}

static const ThemeMetadata* get_theme_by_id(const char* id) {
    if (!id) return &AVAILABLE_THEMES[2];
    for (size_t i = 0; i < sizeof(AVAILABLE_THEMES)/sizeof(AVAILABLE_THEMES[0]); i++) {
        if (strcmp(AVAILABLE_THEMES[i].id, id) == 0) {
            return &AVAILABLE_THEMES[i];
        }
    }
    return NULL;
}
 

// --- CSS Definitions ---

// Common base CSS (Structure, padding, etc - layout agnostic of color)
// We keep your layout customs here if provided, or rely on existing layout.
// We only define colors in variables.

static const char *CSS_COMMON =
    // --- Core mappings ---
    "@define-color bg_color @base_bg;\n"
    "@define-color fg_color @base_fg;\n"

    // Text Color Sharing
    "@define-color panel_fg @base_fg;\n"
    "@define-color button_fg @base_fg;\n"
    "@define-color entry_fg @base_fg;\n"
    "@define-color popover_fg @base_fg;\n"

    // Background Sharing (default)
    "@define-color panel_bg @base_panel_bg;\n"
    "@define-color card_bg @base_card_bg;\n"
    "@define-color popover_bg @base_panel_bg;\n"
    "@define-color entry_bg @base_entry_bg;\n"

    // Success wiring (theme provides base_success_bg + base_success_text)
    "@define-color success_bg @base_success_bg;\n"
    "@define-color success_text @base_success_text;\n"
    "@define-color success_fg @base_success_fg;\n"

    // Destructive wiring (theme provides base_destructive_bg)
    "@define-color destructive_bg @base_destructive_bg;\n"
    "@define-color destructive_fg @base_destructive_fg;\n"

    // Accent wiring (theme provides base_accent + base_accent_fg)
    "@define-color accent_color @base_accent;\n"
    "@define-color accent_fg @base_accent_fg;\n"

    // Tooltip wiring (theme provides base_tooltip_fg)
    "@define-color tooltip_fg @base_tooltip_fg;\n"

    // Destructive wiring (theme provides base_destructive_fg)
    "@define-color destructive_fg @base_destructive_fg;\n"

    //Hardcoded for safety
    "@define-color close_button_hover #e81123;\n";

// =========================
// THEME A: SLATE BLUE
// =========================

static const char *CSS_LIGHT_THEME_A =
    // Primitives
    "@define-color base_fg #111827;\n"
    "@define-color base_bg #f6f7fb;\n"

    "@define-color base_panel_bg #ffffff;\n"
    "@define-color base_card_bg  #ffffff;\n"
    "@define-color base_entry_bg #ffffff;\n"

    // Unique tokens
    "@define-color border_color #d6dae3;\n"
    "@define-color dim_label #6b7280;\n"
    "@define-color tooltip_bg #0b1220;\n"
    "@define-color tooltip_fg #ffffff;\n"

    "@define-color base_accent #3b82f6;\n"
    "@define-color base_accent_fg #ffffff;\n"

    "@define-color button_bg #eef2f7;\n"
    "@define-color button_hover #e3eaf4;\n"

    "@define-color base_destructive_bg #ef4444;\n"
    "@define-color base_destructive_fg #ffffff;\n"
    "@define-color destructive_hover #dc2626;\n"

    "@define-color base_success_text #15803d;\n"
    "@define-color base_success_bg   #15803d;\n"
    "@define-color base_success_fg   #ffffff;\n"
    "@define-color success_hover #116c32;\n"

    "@define-color error_text #b91c1c;\n"

    // Captures (fixed properly)
    "@define-color capture_bg_white #e9ddff;\n"
    "@define-color capture_bg_black #dbeafe;\n"
;

static const char *CSS_DARK_THEME_A =
    // Primitives
    "@define-color base_fg #e5e7eb;\n"
    "@define-color base_bg #0f1115;\n"

    "@define-color base_panel_bg #151922;\n"
    "@define-color base_card_bg  #1a2030;\n"
    "@define-color base_entry_bg #0f131c;\n"

    // Unique tokens
    "@define-color border_color #2a3142;\n"
    "@define-color dim_label #a1a1aa;\n"
    "@define-color tooltip_bg #000000;\n"
    "@define-color tooltip_fg #ffffff;\n"

    "@define-color base_accent #79a8ff;\n"
    "@define-color base_accent_fg #0b1220;\n"

    "@define-color button_bg #1a2030;\n"
    "@define-color button_hover #232b3f;\n"

    "@define-color base_destructive_bg #ff5a6a;\n"
    "@define-color base_destructive_fg #0b1220;\n"
    "@define-color destructive_hover #e54b5a;\n"

    "@define-color base_success_text #7ce7a8;\n"
    "@define-color base_success_bg   #22c55e;\n"
    "@define-color base_success_fg   #08110c;\n"
    "@define-color success_hover #16a34a;\n"

    "@define-color error_text #ff6b6b;\n"

    "@define-color capture_bg_white #3a2a63;\n"
    "@define-color capture_bg_black #1e3a6b;\n"
;

// =========================
// THEME B: EMERALD TEAL
// =========================

static const char *CSS_LIGHT_THEME_B =
    "@define-color base_fg #0f172a;\n"
    "@define-color base_bg #f7faf9;\n"

    "@define-color base_panel_bg #ffffff;\n"
    "@define-color base_card_bg  #ffffff;\n"
    "@define-color base_entry_bg #ffffff;\n"

    "@define-color border_color #d7e2de;\n"
    "@define-color dim_label #64748b;\n"
    "@define-color tooltip_bg #0b1220;\n"
    "@define-color tooltip_fg #ffffff;\n"

    "@define-color base_accent #0ea5a6;\n"
    "@define-color base_accent_fg #ffffff;\n"

    "@define-color button_bg #eef6f4;\n"
    "@define-color button_hover #e1efeb;\n"

    "@define-color base_destructive_bg #ef4444;\n"
    "@define-color base_destructive_fg #ffffff;\n"
    "@define-color destructive_hover #dc2626;\n"

    "@define-color base_success_text #0f766e;\n"
    "@define-color base_success_bg   #0f766e;\n"
    "@define-color base_success_fg   #ffffff;\n"
    "@define-color success_hover #0b5f59;\n"

    "@define-color error_text #b91c1c;\n"

    "@define-color capture_bg_white #d6f5f2;\n"
    "@define-color capture_bg_black #dcfce7;\n"
;

static const char *CSS_DARK_THEME_B =
    "@define-color base_fg #e5e7eb;\n"
    "@define-color base_bg #0b0f10;\n"

    "@define-color base_panel_bg #111819;\n"
    "@define-color base_card_bg  #172223;\n"
    "@define-color base_entry_bg #0c1415;\n"

    "@define-color border_color #253234;\n"
    "@define-color dim_label #a1a1aa;\n"
    "@define-color tooltip_bg #000000;\n"
    "@define-color tooltip_fg #ffffff;\n"

    "@define-color base_accent #4dd6d6;\n"
    "@define-color base_accent_fg #062023;\n"

    "@define-color button_bg #172223;\n"
    "@define-color button_hover #1f2e30;\n"

    "@define-color base_destructive_bg #ff5a6a;\n"
    "@define-color base_destructive_fg #0b1220;\n"
    "@define-color destructive_hover #e54b5a;\n"

    "@define-color base_success_text #86efac;\n"
    "@define-color base_success_bg   #22c55e;\n"
    "@define-color base_success_fg   #08110c;\n"
    "@define-color success_hover #16a34a;\n"

    "@define-color error_text #ff6b6b;\n"

    "@define-color capture_bg_white #123a3a;\n"
    "@define-color capture_bg_black #173a2a;\n"
;

// =========================
// THEME C: AUBERGINE PURPLE
// =========================

static const char *CSS_LIGHT_THEME_C =
    "@define-color base_fg #111827;\n"
    "@define-color base_bg #faf7fb;\n"

    "@define-color base_panel_bg #ffffff;\n"
    "@define-color base_card_bg  #ffffff;\n"
    "@define-color base_entry_bg #ffffff;\n"

    "@define-color border_color #ddd6e6;\n"
    "@define-color dim_label #6b7280;\n"
    "@define-color tooltip_bg #0b1220;\n"
    "@define-color tooltip_fg #ffffff;\n"

    "@define-color base_accent #7c3aed;\n"
    "@define-color base_accent_fg #ffffff;\n"

    "@define-color button_bg #f2eef8;\n"
    "@define-color button_hover #e7def4;\n"

    "@define-color base_destructive_bg #ef4444;\n"
    "@define-color base_destructive_fg #ffffff;\n"
    "@define-color destructive_hover #dc2626;\n"

    "@define-color base_success_text #166534;\n"
    "@define-color base_success_bg   #16a34a;\n"
    "@define-color base_success_fg   #ffffff;\n"
    "@define-color success_hover #15803d;\n"

    "@define-color error_text #b91c1c;\n"

    "@define-color capture_bg_white #efe3ff;\n"
    "@define-color capture_bg_black #e0e7ff;\n"
;

static const char *CSS_DARK_THEME_C =
    "@define-color base_fg #e5e7eb;\n"
    "@define-color base_bg #0f0c12;\n"

    "@define-color base_panel_bg #16111c;\n"
    "@define-color base_card_bg  #1d1626;\n"
    "@define-color base_entry_bg #120e18;\n"

    "@define-color border_color #2b2336;\n"
    "@define-color dim_label #a1a1aa;\n"
    "@define-color tooltip_bg #000000;\n"
    "@define-color tooltip_fg #ffffff;\n"

    "@define-color base_accent #b892ff;\n"
    "@define-color base_accent_fg #0b1220;\n"

    "@define-color button_bg #1d1626;\n"
    "@define-color button_hover #281f35;\n"

    "@define-color base_destructive_bg #ff5a6a;\n"
    "@define-color base_destructive_fg #0b1220;\n"
    "@define-color destructive_hover #e54b5a;\n"

    "@define-color base_success_text #86efac;\n"
    "@define-color base_success_bg   #22c55e;\n"
    "@define-color base_success_fg   #08110c;\n"
    "@define-color success_hover #16a34a;\n"

    "@define-color error_text #ff6b6b;\n"

    "@define-color capture_bg_white #35204a;\n"
    "@define-color capture_bg_black #1f2b4f;\n"
;

// =========================
// ORIGINAL LIGHT (refactored)
// =========================
static const char *CSS_LIGHT_BASE =
    // Primitives
    "@define-color base_fg #1f2937;\n"
    "@define-color base_bg #fafafa;\n"

    // Surfaces
    "@define-color base_panel_bg #ffffff;\n"
    "@define-color base_card_bg  #ffffff;\n"
    "@define-color base_entry_bg #ffffff;\n"

    // Accent
    "@define-color base_accent #3584e4;\n"
    "@define-color base_accent_fg #ffffff;\n"

    // Buttons
    "@define-color button_bg #f1f5f9;\n"
    "@define-color button_hover #e9eff6;\n"

    // Borders / text
    "@define-color border_color #d1d5db;\n"
    "@define-color dim_label #6b7280;\n"

    // Tooltip
    "@define-color tooltip_bg #111827;\n"
    "@define-color tooltip_fg #ffffff;\n"

    // Destructive
    "@define-color base_destructive_bg #dc3545;\n"
    "@define-color base_destructive_fg #ffffff;\n"
    "@define-color destructive_hover #bb2d3b;\n"

    // Success
    "@define-color base_success_text #2e7d32;\n"
    "@define-color base_success_bg   #2e7d32;\n"
    "@define-color base_success_fg   #ffffff;\n"
    "@define-color success_hover #256528;\n"

    // Error
    "@define-color error_text #c62828;\n"

    // Captures (no longer random)
    "@define-color capture_bg_white #e7f0ff;\n"  // soft blue tint (fits your accent)
    "@define-color capture_bg_black #e9ddff;\n"  // soft lavender tint (related, not loud)
;

// =========================
// ORIGINAL DARK (refactored)
// =========================
static const char *CSS_DARK_BASE =
    // Primitives
    "@define-color base_fg #e7e7e7;\n"
    "@define-color base_bg #121212;\n"

    // Surfaces
    "@define-color base_panel_bg #1a1a1a;\n"
    "@define-color base_card_bg  #1f1f1f;\n"
    "@define-color base_entry_bg #121212;\n"  // same as bg, but explicit for clarity

    // Accent
    "@define-color base_accent #7fb2ff;\n"
    "@define-color base_accent_fg #0f172a;\n"

    // Buttons
    "@define-color button_bg #1f1f1f;\n"
    "@define-color button_hover #2a2a2a;\n"

    // Borders / text
    "@define-color border_color #2f2f2f;\n"
    "@define-color dim_label #a1a1aa;\n"

    // Tooltip
    "@define-color tooltip_bg #000000;\n"
    "@define-color tooltip_fg #ffffff;\n"

    // Destructive
    "@define-color base_destructive_bg #ff5a6a;\n"
    "@define-color base_destructive_fg #0f172a;\n"
    "@define-color destructive_hover #e54b5a;\n"

    // Success
    "@define-color base_success_text #6ee7a6;\n"
    "@define-color base_success_bg   #66bb6a;\n"
    "@define-color base_success_fg   #1e1e1e;\n"
    "@define-color success_hover #57a85b;\n"
    "@define-color capture_bg_white #7810ab;\n"    // Dark bg for White pieces to pop
    "@define-color capture_bg_black #193456;\n"; // Light bg for Black pieces to pop

static const char *CSS_STRUCTURAL =
    /* -------------------- Base surfaces -------------------- */
    "window, .window { background-color: @bg_color; color: @fg_color; }\n"
    "window.csd, .window.csd { border-radius: 12px; }\n"

    /* Robust Headerbar - Use specificity to override system gradients without !important */
    "window.csd headerbar, window.csd .headerbar, headerbar.titlebar, headerbar {\n"
    "  background-color: @bg_color;\n"
    "  background-image: none;\n"
    "  color: @fg_color;\n"
    "  border-bottom: 1px solid @border_color;\n"
    "  box-shadow: none;\n"
    "}\n"
    "window.csd headerbar:backdrop, .headerbar:backdrop {\n"
    "  background-color: @bg_color;\n"
    "  background-image: none;\n"
    "  opacity: 1.0; \n"
    "}\n"
    "headerbar > box, headerbar > widget { background: transparent; }\n"

    /* -------------------- Panels / cards -------------------- */
    ".info-panel { background-color: @panel_bg; color: @panel_fg; border-right: 1px solid @border_color; border-radius: 0 0 0 12px; }\n"
    ".info-label-title { font-weight: 700; color: @dim_label; }\n"
    ".info-label-value { font-size: 1.1em; font-weight: 600; color: @panel_fg; }\n"

    ".card { background-color: @card_bg; border: 1px solid @border_color; border-radius: 10px; }\n"

    /* -------------------- Board & captures -------------------- */
    ".board-frame { border: 2px solid @border_color; border-radius: 12px; margin: 10px; }\n"
    ".capture-box {\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 5px;\n"
    "  padding: 8px;\n"
    "  min-height: 50px;\n"
    "}\n"
    ".capture-box-for-white-pieces { background-color: @capture_bg_white; }\n"
    ".capture-box-for-black-pieces { background-color: @capture_bg_black; }\n"
    ".capture-count {\n"
    "  font-size: 14px;\n"
    "  font-weight: bold;\n"
    "  color: @dim_label;\n"
    "  margin-left: 2px;\n"
    "}\n"

    /*----------------Undo Button ---------------------------------*/
    ".undo-button { color: @fg_color; background: @button_bg; border-radius: 4px; padding: 4px 12px; }\n"

    /*----------------Reset Button ---------------------------------*/
    ".reset-button { color: @fg_color; background: @button_bg; border-radius: 4px; padding: 4px 12px; }\n"

    /* -------------------- Typography helpers -------------------- */
    ".title-1 { font-size: 24px; font-weight: bold; margin-bottom: 8px; color: @fg_color; }\n"
    ".title-2 { font-size: 18px; font-weight: bold; margin-bottom: 12px; color: @fg_color; }\n"
    ".heading { font-weight: 700; font-size: 18px; color: @fg_color; margin-bottom: 8px; }\n"
    ".dim-label { color: @dim_label; opacity: 0.7; }\n"
    ".ai-note { color: @dim_label; font-size: 0.85em; font-style: italic; }\n"

    ".success-text { color: @success_text; font-size: 0.9em; }\n"
    ".error-text { color: @error_text; font-size: 0.9em; }\n"
    ".ai-note { color: @dim_label; font-size: 0.8em; font-style: italic; }\n"

    /* -------------------- Preview Frame -------------------- */
    ".preview-frame { border: 2px solid @border_color; border-radius: 8px; background: @bg_color; box-shadow: 0 2px 8px @border_color; }\n"

    /* Scrollbars - Rounded & Defined */
    "scrollbar { background-color: transparent; border: none; }\n"
    "scrollbar trough { background-color: alpha(@border_color, 0.2); border-radius: 999px; min-width: 10px; min-height: 10px; margin: 2px; }\n"
    "scrollbar slider { background-color: alpha(@fg_color, 0.4); border-radius: 999px; min-width: 8px; min-height: 24px; margin: 1px; border: 1px solid @bg_color; }\n"
    "scrollbar slider:hover { background-color: alpha(@fg_color, 0.6); }\n"
    "scrollbar slider:active { background-color: @accent_color; }\n"
    
    /* -------------------- Sliders (Scale) -------------------- */
    "scale trough { background-color: alpha(@border_color, 0.5); border-radius: 999px; min-height: 6px; min-width: 6px; }\n"
    "scale highlight { background-color: @accent_color; border-radius: 999px; }\n"
    "scale slider { background-color: @fg_color; border-radius: 999px; min-width: 18px; min-height: 18px; margin: -6px; box-shadow: 0 1px 3px rgba(0,0,0,0.3); border: 1px solid @bg_color; }\n"
    "scale slider:hover { background-color: shade(@fg_color, 0.9); }\n"
    
    /* -------------------- Promotion Buttons -------------------- */
    ".promotion-button { background-color: transparent; border: none; box-shadow: none; padding: 0; }\n"
    ".promotion-button:hover { background-color: transparent; border: none; box-shadow: none; }\n"
    ".promotion-button:active { background-color: transparent; border: none; box-shadow: none; }\n"
    ".promotion-button:focus { background-color: transparent; border: none; box-shadow: none; }\n"

    /* -------------------- Header Buttons (Explicitly Transparent) -------------------- */
    ".header-button { background-color: transparent; background-image: none; border: 1px solid @border_color; border-radius: 6px; box-shadow: none; color: @fg_color; }\n"
    ".header-button:hover { background-color: alpha(@fg_color, 0.10); }\n"
    
    /* -------------------- Notebook / Scrollers (Fix white backgrounds) -------------------- */
    "notebook, .notebook { background-color: @bg_color; background-image: none; }\n"
    "notebook contents, .notebook contents { background-color: @bg_color; }\n"
    "notebook stack, .notebook stack { background-color: @bg_color; }\n"
    "notebook header, .notebook header { background-color: @panel_bg; border-bottom: 1px solid @border_color; }\n"
    "notebook header tab { background-color: transparent; border: none; padding: 10px 16px; transition: all 0.2s ease; }\n"
    "notebook header tab:checked { border-bottom: 3px solid @accent_color; color: @accent_color; }\n"
    "notebook header tab:hover:not(:checked) { background-color: alpha(@button_hover, 0.5); }\n"
    "notebook header tab label { font-weight: 600; color: inherit; }\n"
    
    /* AI Dialog specific override */
    ".ai-notebook header tab label { color: @fg_color; }\n"
    ".ai-notebook header tab:checked label { color: @accent_color; }\n"
    
    "scrolledwindow, .scrolled-window { background-color: transparent; }\n"
    "scrolledwindow viewport, .scrolled-window viewport { background-color: transparent; }\n"
    
    /* Special container to force bg */
    ".settings-content { background-color: @bg_color; }\n"


    /* -------------------- Buttons -------------------- */
    "button:not(.titlebutton):not(.window-control):not(.ai-icon-button):not(.image-button):not(.success-action):not(.destructive-action):not(.suggested-action):not(.promotion-button) {\n"
    "  background-color: @button_bg;\n"
    "  background-image: none;\n"
    "  color: @button_fg;\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 8px;\n"
    "  padding: 4px 10px;\n"
    "  min-height: 28px;\n"
    "  box-shadow: none;\n"
    "}\n"
    "button { background-image: none; }\n"

    "button:not(.titlebutton):not(.success-action):not(.destructive-action):not(.suggested-action):hover { background-color: @button_hover; background-image: none; }\n"
    "button:not(.titlebutton):not(.success-action):not(.destructive-action):not(.suggested-action):active { background-color: shade(@button_bg, 0.92); }\n"
    "button:not(.titlebutton):disabled { opacity: 0.55; }\n"
    "\n"
    /* AI Dialog Buttons - specific override after generic to ensure precedence */
    "button.ai-icon-button { background-color: @button_hover; background-image: none; border: 1px solid @border_color; color: @button_fg; border-radius: 5px; padding: 6px; box-shadow: none; }\n"
    "button.ai-icon-button:hover { background-color: @button_hover; color: @accent_color; border-color: @accent_color; }\n"
    "button.ai-icon-button:active { background-color: alpha(@accent_color, 0.2); }\n"

    /* Distinct Checked State (for Toggles) */
    "button:checked {\n"
    "  background-color: @accent_color;\n"
    "  color: @accent_fg;\n"
    "  border-color: @accent_color;\n"
    "}\n"
    "button:checked:hover { background-color: shade(@accent_color, 1.05); }\n"

    /* Ensure classed buttons override the generic button rule */
    "button.success-action {\n"
    "  background-color: @success_bg;\n"
    "  background-image: none;\n"
    "  color: @success_fg;\n"
    "  border: 1px solid @success_bg;\n"
    "}\n"
    "button.success-action:hover { background-color: @success_hover; border-color: @success_hover; }\n"

    "button.destructive-action {\n"
    "  background-color: @destructive_bg;\n"
    "  background-image: none;\n"
    "  color: @destructive_fg;\n"
    "  border: 1px solid @destructive_bg;\n"
    "}\n"
    "button.destructive-action:hover { background-color: @destructive_hover; border-color: @destructive_hover; }\n"

    /* Suggested / destructive - use explicit button class for specificity */
    "button.suggested-action {\n"
    "  background-color: @accent_color;\n"
    "  background-image: none;\n"
    "  color: @accent_fg;\n"
    "  border-color: @accent_color;\n"
    "}\n"
    "button.suggested-action:hover { background-color: shade(@accent_color, 1.05); }\n"
    "button.suggested-action:active { background-color: shade(@accent_color, 0.92); }\n"

    /* -------------------- Dialogs -------------------- */
    "dialog, window.dialog, message-dialog { background-color: @bg_color; color: @fg_color; }\n"
    "window.dialog { padding: 12px; }\n"
    "window.dialog button { margin: 4px; min-width: 80px; padding: 8px 20px; min-height: 32px; }\n"
    "window.dialog button:hover { background: @button_hover; color: inherit; }\n"

    /* -------------------- Entries / inputs -------------------- */
    "entry, textview, spinbutton, searchentry {\n"
    "  background-color: @entry_bg;\n"
    "  background-image: none;\n"
    "  color: @entry_fg;\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 6px;\n"
    "  padding: 6px 8px;\n"
    "  min-height: 30px;\n"
    "}\n"
    "entry:focus, textview:focus, spinbutton:focus, searchentry:focus { border-color: alpha(@accent_color, 0.85); }\n"
    "entry:disabled, textview:disabled { opacity: 0.60; }\n"
    "checkbutton, radiobutton { padding: 4px; }\n"
    "\n"
    /* Spinbutton specific rounding */
    "spinbutton { border-radius: 12px; }\n"
    "spinbutton button { border-radius: 8px; margin: 2px; border: none; box-shadow: none; background-image: none; color: @button_fg; }\n"
    "spinbutton button:hover { background-color: @button_hover; color: @accent_color; }\n"
    "spinbutton button:active { background-color: alpha(@accent_color, 0.2); }\n"
    "\n"

    /* --- Restore native window controls (min/max/close) with subtle hover --- */
    "window.csd headerbar windowcontrols button,\n"
    "window.csd .headerbar windowcontrols button,\n"
    "headerbar windowcontrols button {\n"
    "  background: transparent;\n"
    "  background-image: none;\n"
    "  border: none;\n"
    "  box-shadow: none;\n"
    "  padding: 4px;\n"
    "  margin: 0 2px;\n"
    "  min-height: 24px;\n"
    "  min-width: 24px;\n"
    "  border-radius: 6px;\n"
    "  color: @fg_color;\n"
    "}\n"
    "window.csd headerbar windowcontrols button:not(.close):hover,\n"
    "window.csd .headerbar windowcontrols button:not(.close):hover,\n"
    "headerbar windowcontrols button:not(.close):hover {\n"
    "  background-color: alpha(@fg_color, 0.15);\n"
    "  background-image: none;\n"
    "  box-shadow: none;\n"
    "}\n"
    "headerbar windowcontrols button.close:hover,\n"
    "headerbar windowcontrols button.close-button:hover,\n"
    "windowcontrols > button:last-child:hover,\n"
    "window.csd headerbar windowcontrols button.close:hover,\n"
    "window.csd .headerbar windowcontrols button.close:hover {\n"
    "  background-color: @close_button_hover;\n"
    "  background-image: none;\n"
    "  box-shadow: inset 0 0 100px @close_button_hover;\n"
    "  color: white;\n"
    "}\n"
    "headerbar windowcontrols button.close:hover image,\n"
    "headerbar windowcontrols button.close:hover widget {\n"
    "  color: white;\n"
    "  background-color: transparent;\n"
    "  background-image: none;\n"
    "}\n"
    "window.csd headerbar windowcontrols button.close:active,\n"
    "window.csd .headerbar windowcontrols button.close:active {\n"
    "  background-color: shade(@close_button_hover, 0.9);\n"
    "  background-image: none;\n"
    "  color: white;\n"
    "}\n"
    "window.csd headerbar windowcontrols button:not(.close):active,\n"
    "window.csd .headerbar windowcontrols button:not(.close):active,\n"
    "headerbar windowcontrols button:not(.close):active {\n"
    "  background-color: alpha(@fg_color, 0.25);\n"
    "  background-image: none;\n"
    "}\n"
    "headerbar windowcontrols button image, headerbar windowcontrols button widget {\n"
    "  background: transparent;\n"
    "  background-image: none;\n"
    "}\n"

    /* -------------------- Popovers / tooltips -------------------- */
    "popover { background-color: @popover_bg; color: @popover_fg; border: 1px solid @border_color; border-radius: 12px; }\n"
    "popover contents { background-color: transparent; padding: 4px; }\n"
    "popover button { margin-top: 4px; margin-bottom: 4px; }\n"
    "popover button:not(.suggested-action):not(.destructive-action) { background-color: alpha(@fg_color, 0.05); border: 1px solid alpha(@border_color, 0.5); }\n"
    "popover button:not(.suggested-action):not(.destructive-action):hover { background-color: alpha(@fg_color, 0.1); }\n"
    "tooltip { background-color: @tooltip_bg; color: @tooltip_fg; border-radius: 8px; padding: 6px 8px; }\n"

    /* -------------------- Dropdowns / list views -------------------- */
    "dropdown > button, dropdown button {\n"
    "  background-color: @entry_bg;\n"
    "  background-image: none;\n"
    "  color: @entry_fg;\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 8px;\n"
    "  padding: 4px 10px;\n"
    "  min-height: 30px;\n"
    "}\n"
    "dropdown > button:hover, dropdown button:hover { background-color: @button_hover; }\n"
    "dropdown arrow { color: @entry_fg; }\n"
    "dropdown label { color: inherit; }\n"

    "listview, listbox { background-color: transparent; color: @fg_color; }\n"
    "listview row, listbox row {\n"
    "  padding: 8px;\n"
    "  border-radius: 8px;\n"
    "  margin: 2px 6px;\n"
    "  color: @fg_color;\n"
    "}\n"
    "/* Force labels to use the color, don't rely on inherit */\n"
    "listview label, listbox label, .boxed-list label { color: @fg_color; }\n"
    "listview row:hover, listbox row:hover { background-color: alpha(@button_hover, 0.75); }\n"
    "listview row:selected, listbox row:selected { background-color: @accent_color; color: @accent_fg; }\n"
    "listview row:selected label, listbox row:selected label { color: @accent_fg; }\n"
    "\n"
    "/* Boxed List Specifics (Puzzle List) */\n"
    ".boxed-list { background-color: @bg_color; border-radius: 8px; }\n"
    ".boxed-list row { border-bottom: 1px solid alpha(@border_color, 0.5); }\n"
    ".boxed-list row:last-child { border-bottom: none; }\n"

    /* -------------------- Sidebar / settings -------------------- */
    ".settings-sidebar, .sidebar { background-color: @panel_bg; border-right: 1px solid @border_color; }\n"
    ".settings-content { background-color: @bg_color; }\n"
    ".sidebar row { padding: 10px; border-radius: 6px; margin: 4px; color: @fg_color; }\n"
    ".sidebar row:hover { background-color: alpha(@button_hover, 0.75); }\n"
    ".sidebar row:selected { background-color: @accent_color; color: @accent_fg; font-weight: bold; }\n"

    /* -------------------- Transparent overlay (particles) -------------------- */
    "window.transparent-overlay { background: transparent; box-shadow: none; border: none; }\n"
    "window.transparent-overlay > widget { background: transparent; }\n"

    /* -------------------- Dark Mode Button (Accent Mapping) -------------------- */
    ".dark-mode-button { color: @accent_color; }\n";


static void update_provider(void) {
    if (!global_provider) return;
    
    GString *css = g_string_new(NULL);
    
    // 0. Append Common Variables
    g_string_append(css, CSS_COMMON);

    // 1. Pick Theme Variants
    const char *light_theme = CSS_LIGHT_BASE;
    const char *dark_theme = CSS_DARK_BASE;

    switch (current_theme_variant) {
        case 'A': case 'a':
            light_theme = CSS_LIGHT_THEME_A;
            dark_theme = CSS_DARK_THEME_A;
            break;
        case 'B': case 'b':
            light_theme = CSS_LIGHT_THEME_B;
            dark_theme = CSS_DARK_THEME_B;
            break;
        case 'C': case 'c':
            light_theme = CSS_LIGHT_THEME_C;
            dark_theme = CSS_DARK_THEME_C;
            break;
        case 'D': case 'd':
        default:
            light_theme = CSS_LIGHT_BASE; // Original
            dark_theme = CSS_DARK_BASE;   // Original
            break;
    }

    // 2. Append Selected Theme
    if (current_is_dark) {
        g_string_append(css, dark_theme);
    } else {
        g_string_append(css, light_theme);
    }
    
    // 3. Append Structure
    g_string_append(css, CSS_STRUCTURAL);
    
    // 4. Load
    gtk_css_provider_load_from_string(global_provider, css->str);
    
    g_string_free(css, TRUE);
}

// Done

void theme_manager_init(void) {
    if (global_provider) return;
    
    global_provider = gtk_css_provider_new();
    
    // High priority to override default theme (using USER priority for extreme specificity)
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), 
        GTK_STYLE_PROVIDER(global_provider), 
        GTK_STYLE_PROVIDER_PRIORITY_USER); 
        
    update_provider();
}

void theme_manager_set_dark(bool is_dark) {
    if (current_is_dark == is_dark) return;
    current_is_dark = is_dark;
    
    AppConfig* cfg = config_get();
    if (cfg) cfg->is_dark_mode = is_dark;
    
    update_provider();
}

void theme_manager_toggle(void) {
    theme_manager_set_dark(!current_is_dark);
}

bool theme_manager_is_dark(void) {
    return current_is_dark;
}

void theme_manager_set_variant(char variant) {
    if (current_theme_variant == variant) return;
    current_theme_variant = variant;
    
    AppConfig* cfg = config_get();
    if (cfg) {
        const ThemeMetadata* meta = get_theme_by_char(variant);
        if (meta) {
            strncpy(cfg->theme, meta->id, sizeof(cfg->theme) - 1);
            cfg->theme[sizeof(cfg->theme) - 1] = '\0';
        }
    }
    
    update_provider();
}

char theme_manager_get_variant(void) {
    return current_theme_variant;
}

void theme_manager_set_theme_id(const char* id) {
    const ThemeMetadata* meta = get_theme_by_id(id);
    if (meta) {
        theme_manager_set_variant(meta->variant_char);
    }
}

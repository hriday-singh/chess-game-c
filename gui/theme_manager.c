#include "theme_manager.h"
#include "config_manager.h"

static GtkCssProvider *global_provider = NULL;
static bool current_is_dark = true;

// --- CSS Definitions ---

// Common base CSS (Structure, padding, etc - layout agnostic of color)
// We keep your layout customs here if provided, or rely on existing layout.
// We only define colors in variables.

// Helper to generate CSS from colors
static void append_theme_css(GString* css, const AppThemeColors* c) {
    g_string_append_printf(css, "@define-color base_bg %s;\n", c->base_bg);
    g_string_append_printf(css, "@define-color base_fg %s;\n", c->base_fg);
    g_string_append_printf(css, "@define-color base_panel_bg %s;\n", c->base_panel_bg);
    g_string_append_printf(css, "@define-color base_card_bg %s;\n", c->base_card_bg);
    g_string_append_printf(css, "@define-color base_entry_bg %s;\n", c->base_entry_bg);
    g_string_append_printf(css, "@define-color base_accent %s;\n", c->base_accent);
    g_string_append_printf(css, "@define-color base_accent_fg %s;\n", c->base_accent_fg);
    g_string_append_printf(css, "@define-color base_success_bg %s;\n", c->base_success_bg);
    g_string_append_printf(css, "@define-color base_success_text %s;\n", c->base_success_text);
    g_string_append_printf(css, "@define-color base_success_fg %s;\n", c->base_success_fg);
    g_string_append_printf(css, "@define-color success_hover %s;\n", c->success_hover);
    g_string_append_printf(css, "@define-color base_destructive_bg %s;\n", c->base_destructive_bg);
    g_string_append_printf(css, "@define-color base_destructive_fg %s;\n", c->base_destructive_fg);
    g_string_append_printf(css, "@define-color destructive_hover %s;\n", c->destructive_hover);
    g_string_append_printf(css, "@define-color border_color %s;\n", c->border_color);
    g_string_append_printf(css, "@define-color dim_label %s;\n", c->dim_label);
    g_string_append_printf(css, "@define-color tooltip_bg %s;\n", c->tooltip_bg);
    g_string_append_printf(css, "@define-color tooltip_fg %s;\n", c->tooltip_fg);
    g_string_append_printf(css, "@define-color button_bg %s;\n", c->button_bg);
    g_string_append_printf(css, "@define-color button_hover %s;\n", c->button_hover);
    g_string_append_printf(css, "@define-color error_text %s;\n", c->error_text);
    g_string_append_printf(css, "@define-color capture_bg_white %s;\n", c->capture_bg_white);
    g_string_append_printf(css, "@define-color capture_bg_black %s;\n", c->capture_bg_black);
    // Hardcoded safety
    g_string_append(css, "@define-color bg_color @base_bg;\n");
    g_string_append(css, "@define-color fg_color @base_fg;\n");
    g_string_append(css, "@define-color close_button_hover #e81123;\n");
    g_string_append(css, "@define-color panel_fg @base_fg;\n");
    g_string_append(css, "@define-color button_fg @base_fg;\n");
    g_string_append(css, "@define-color entry_fg @base_fg;\n");
    g_string_append(css, "@define-color popover_fg @base_fg;\n");
    g_string_append(css, "@define-color panel_bg @base_panel_bg;\n");
    g_string_append(css, "@define-color card_bg @base_card_bg;\n");
    g_string_append(css, "@define-color popover_bg @base_panel_bg;\n");
    g_string_append(css, "@define-color entry_bg @base_entry_bg;\n");
    g_string_append(css, "@define-color success_bg @base_success_bg;\n");
    g_string_append(css, "@define-color success_text @base_success_text;\n");
    g_string_append(css, "@define-color success_fg @base_success_fg;\n");
    g_string_append(css, "@define-color destructive_bg @base_destructive_bg;\n");
    g_string_append(css, "@define-color destructive_fg @base_destructive_fg;\n");
    g_string_append(css, "@define-color accent_color @base_accent;\n");
    g_string_append(css, "@define-color accent_fg @base_accent_fg;\n");
}

static const AppTheme SYSTEM_THEMES[] = {
    // Theme A - Slate Blue
    {
        .theme_id = "theme_a_slate", .display_name = "Slate Blue",
        .light = {
            "#f6f7fb", "#111827", "#ffffff", "#ffffff", "#ffffff",
            "#3b82f6", "#ffffff",
            "#15803d", "#15803d", "#ffffff", "#116c32",
            "#ef4444", "#ffffff", "#dc2626",
            "#d6dae3", "#6b7280", "#0b1220", "#ffffff", "#eef2f7", "#e3eaf4",
            "#b91c1c",
            "#e9ddff", "#dbeafe"
        },
        .dark = {
            "#0f1115", "#e5e7eb", "#151922", "#1a2030", "#0f131c",
            "#79a8ff", "#0b1220",
            "#22c55e", "#7ce7a8", "#08110c", "#16a34a",
            "#ff5a6a", "#0b1220", "#e54b5a",
            "#2a3142", "#a1a1aa", "#000000", "#ffffff", "#1a2030", "#232b3f",
            "#ff6b6b",
            "#3a2a63", "#1e3a6b"
        }
    },
    // Theme B - Emerald Teal
    {
        .theme_id = "theme_b_emerald", .display_name = "Emerald Teal",
        .light = {
            "#f7faf9", "#0f172a", "#ffffff", "#ffffff", "#ffffff",
            "#0ea5a6", "#ffffff",
            "#0f766e", "#0f766e", "#ffffff", "#0b5f59",
            "#ef4444", "#ffffff", "#dc2626",
            "#d7e2de", "#64748b", "#0b1220", "#ffffff", "#eef6f4", "#e1efeb",
            "#b91c1c",
            "#d6f5f2", "#dcfce7"
        },
        .dark = {
            "#0b0f10", "#e5e7eb", "#111819", "#172223", "#0c1415",
            "#4dd6d6", "#062023",
            "#22c55e", "#86efac", "#08110c", "#16a34a",
            "#ff5a6a", "#0b1220", "#e54b5a",
            "#253234", "#a1a1aa", "#000000", "#ffffff", "#172223", "#1f2e30",
            "#ff6b6b",
            "#123a3a", "#173a2a"
        }
    },
    // Theme C - Aubergine Purple
    {
        .theme_id = "theme_c_aubergine", .display_name = "Aubergine Purple",
        .light = {
            "#faf7fb", "#111827", "#ffffff", "#ffffff", "#ffffff",
            "#7c3aed", "#ffffff",
            "#16a34a", "#166534", "#ffffff", "#15803d",
            "#ef4444", "#ffffff", "#dc2626",
            "#ddd6e6", "#6b7280", "#0b1220", "#ffffff", "#f2eef8", "#e7def4",
            "#b91c1c",
            "#efe3ff", "#e0e7ff"
        },
        .dark = {
            "#0f0c12", "#e5e7eb", "#16111c", "#1d1626", "#120e18",
            "#b892ff", "#0b1220",
            "#22c55e", "#86efac", "#08110c", "#16a34a",
            "#ff5a6a", "#0b1220", "#e54b5a",
            "#2b2336", "#a1a1aa", "#000000", "#ffffff", "#1d1626", "#281f35",
            "#ff6b6b",
            "#35204a", "#1f2b4f"
        }
    },
    // Theme D - Mocha Gold
    {
        .theme_id = "theme_d_mocha_gold", .display_name = "Mocha Gold",
        .light = {
            "#FAFAFA", "#111827", "#FFFFFF", "#FFFFFF", "#FFFFFF",
            "#D4A017", "#111827",
            "#15803D", "#6EE7A6", "#FFFFFF", "#116C32",
            "#EF4444", "#FFFFFF", "#DC2626",
            "#D1D5DB", "#6B7280", "#111827", "#FFFFFF", "#F1F5F9", "#E9EFF6",
            "#B91C1C",
            "#EFE7DA", "#E7F0FF"
        },
        .dark = {
            "#121212", "#E7E7E7", "#1A1A1A", "#1F1F1F", "#121212",
            "#F1C75B", "#0F172A",
            "#66BB6A", "#6EE7A6", "#1E1E1E", "#57A85B",
            "#FF5A6A", "#0F172A", "#E54B5A",
            "#2F2F2F", "#A1A1AA", "#000000", "#FFFFFF", "#1F1F1F", "#2A2A2A",
            "#FF6B6B",
            "#2F2450", "#24324A"
        }
    },
    // Theme E - Slate Rose
    {
        .theme_id = "theme_e_slate_rose", .display_name = "Slate Rose",
        .light = {
            "#F7F7FA", "#111827", "#FFFFFF", "#FFFFFF", "#FFFFFF",
            "#E11D48", "#FFFFFF",
            "#15803D", "#15803D", "#FFFFFF", "#116C32",
            "#EF4444", "#FFFFFF", "#DC2626",
            "#D7DAE3", "#6B7280", "#111827", "#FFFFFF", "#F1F3F8", "#E7EAF3",
            "#B91C1C",
            "#F6E7EE", "#E7F0FF"
        },
        .dark = {
            "#101218", "#E5E7EB", "#151824", "#1B2131", "#0F131D",
            "#FB7185", "#1F2937",
            "#22C55E", "#86EFAC", "#08110C", "#16A34A",
            "#FF5A6A", "#0B1220", "#E54B5A",
            "#2A3142", "#A1A1AA", "#000000", "#FFFFFF", "#1B2131", "#242C41",
            "#FF6B6B",
            "#3A2030", "#1E2A4A"
        }
    },
    // Theme F - Ocean Mist
    {
        .theme_id = "theme_f_ocean_mist", .display_name = "Ocean Mist",
        .light = {
            "#F5FAFC", "#0F172A", "#FFFFFF", "#FFFFFF", "#FFFFFF",
            "#0284C7", "#FFFFFF",
            "#16A34A", "#166534", "#FFFFFF", "#15803D",
            "#EF4444", "#FFFFFF", "#DC2626",
            "#D6DEE6", "#64748B", "#0B1220", "#FFFFFF", "#EEF6FB", "#E2EEF8",
            "#B91C1C",
            "#E6F2FF", "#E7F0FF"
        },
        .dark = {
            "#0A1014", "#E5E7EB", "#101A20", "#15232C", "#0B1318",
            "#7DD3FC", "#08202A",
            "#22C55E", "#86EFAC", "#08110C", "#16A34A",
            "#FF5A6A", "#0B1220", "#E54B5A",
            "#22313A", "#A1A1AA", "#000000", "#FFFFFF", "#15232C", "#1D3140",
            "#FF6B6B",
            "#14324A", "#1B2B3B"
        }
    },
    // Theme G - Forest Amber
    {
        .theme_id = "theme_g_forest_amber", .display_name = "Forest Amber",
        .light = {
            "#FAFAF7", "#0F172A", "#FFFFFF", "#FFFFFF", "#FFFFFF",
            "#F59E0B", "#111827",
            "#15803D", "#166534", "#FFFFFF", "#116C32",
            "#EF4444", "#FFFFFF", "#DC2626",
            "#DDDCCF", "#6B7280", "#111827", "#FFFFFF", "#F5F3EE", "#EDE8DD",
            "#B91C1C",
            "#F3EBD7", "#E6F2FF"
        },
        .dark = {
            "#0D100C", "#E5E7EB", "#131A12", "#182418", "#0F1510",
            "#FCD34D", "#1F2937",
            "#22C55E", "#86EFAC", "#08110C", "#16A34A",
            "#FF5A6A", "#0B1220", "#E54B5A",
            "#263225", "#A1A1AA", "#000000", "#FFFFFF", "#182418", "#223223",
            "#FF6B6B",
            "#2B2A1C", "#1E2A22"
        }
    },
    // Theme H - Graphite Lime
    {
        .theme_id = "theme_h_graphite_lime", .display_name = "Graphite Lime",
        .light = {
            "#F7F7F7", "#111827", "#FFFFFF", "#FFFFFF", "#FFFFFF",
            "#84CC16", "#111827",
            "#16A34A", "#166534", "#FFFFFF", "#15803D",
            "#EF4444", "#FFFFFF", "#DC2626",
            "#D6D6D6", "#6B7280", "#111827", "#FFFFFF", "#F1F3F5", "#E6EAEE",
            "#B91C1C",
            "#EEF6D8", "#E7F0FF"
        },
        .dark = {
            "#0E0F10", "#E5E7EB", "#141617", "#1A1D1F", "#0E1113",
            "#C7F9A6", "#0B1220",
            "#22C55E", "#86EFAC", "#08110C", "#16A34A",
            "#FF5A6A", "#0B1220", "#E54B5A",
            "#2A2D30", "#A1A1AA", "#000000", "#FFFFFF", "#1A1D1F", "#23282B",
            "#FF6B6B",
            "#2A3A1A", "#1F2A33"
        }
    },
    // Theme I - Sand Cobalt
    {
        .theme_id = "theme_i_sand_cobalt", .display_name = "Sand Cobalt",
        .light = {
            "#FBFAF7", "#111827", "#FFFFFF", "#FFFFFF", "#FFFFFF",
            "#1D4ED8", "#FFFFFF",
            "#15803D", "#166534", "#FFFFFF", "#116C32",
            "#EF4444", "#FFFFFF", "#DC2626",
            "#E0DDD3", "#6B7280", "#111827", "#FFFFFF", "#F5F2EA", "#ECE7DB",
            "#B91C1C",
            "#F2E9D9", "#E7F0FF"
        },
        .dark = {
            "#0F0F12", "#E5E7EB", "#16161B", "#1C1C25", "#12121A",
            "#89B4FF", "#0B1220",
            "#22C55E", "#86EFAC", "#08110C", "#16A34A",
            "#FF5A6A", "#0B1220", "#E54B5A",
            "#2B2B38", "#A1A1AA", "#000000", "#FFFFFF", "#1C1C25", "#252533",
            "#FF6B6B",
            "#2D2452", "#1E2E52"
        }
    },
    {
        .theme_id = "theme_j_sage_ash", .display_name = "Sage Ash",
        .light = {
            "#F7F8F6", "#1F2937", "#FFFFFF", "#FFFFFF", "#FFFFFF",
            "#5D6658", "#FFFFFF",
            "#4D6B57", "#4D6B57", "#FFFFFF", "#415E4B",
            "#E24C4B", "#FFFFFF", "#C83F3E",
            "#D6D9D2", "#6B7280", "#1F2937", "#FFFFFF", "#EEF1EC", "#E3E8E0",
            "#B91C1C",
            "#E7EAE3", "#DDE3DC"
        },
        .dark = {
            "#121411", "#E5E7EB", "#171A16", "#1E221C", "#141712",
            "#7A8575", "#0F172A",
            "#6FA287", "#9AD3B3", "#0B1410", "#5C8E76",
            "#FF6B6B", "#0F172A", "#E25757",
            "#2A2F28", "#A1A1AA", "#000000", "#FFFFFF", "#1E221C", "#262B24",
            "#FF8A8A",
            "#2A3028", "#323A30"
        }
    }
};

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
    ".info-label-title { font-weight: 700; color: @panel_fg; }\n"
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
    ".rounded-border { border: 1px solid @border_color; border-radius: 12px; }\n"

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

    ".ai-notebook header tab { border: none; box-shadow: none; }\n"
    ".ai-notebook header tab:checked {\n"
    "  border: none;\n"
    "  border-bottom: 2px solid @accent_color;\n"
    "  box-shadow: inset 0 -2px 0 0 @accent_color;\n"
    "}\n"

    
    "scrolledwindow, .scrolled-window { background-color: transparent; }\n"
    "scrolledwindow viewport, .scrolled-window viewport { background-color: transparent; }\n"
    
    /* Special container to force bg */
    ".settings-content { background-color: @bg_color; }\n"


    /* -------------------- Buttons -------------------- */
    // "button:not(.titlebutton):not(.window-control):not(.ai-icon-button):not(.image-button):not(.success-action):not(.destructive-action):not(.suggested-action):not(.promotion-button) {\n"
    "button:not(.titlebutton):not(.window-control):not(.ai-icon-button):not(.image-button):not(.success-action):not(.destructive-action):not(.suggested-action):not(.promotion-button):not(.move-text-btn) {\n"
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
    "\n"
    /* Fix for buttons inside listboxes where labels get forced color */
    "button.suggested-action label, button.destructive-action label, button.success-action label { color: inherit; }\n"

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

    /* History Match List - Fix white background */
    ".history-list { background-color: transparent; }\n"
    ".match-row { \n"
    "  background-color: @card_bg; \n"
    "  color: @fg_color; \n"
    "  border: 1px solid @border_color; \n"
    "  border-radius: 8px;\n"
    "  margin-bottom: 8px;\n"
    "  padding: 0;\n"
    "  transition: all 0.15s ease;\n"
    "}\n"
    "button.move-text-btn:hover {\n"
    "  background: alpha(@base_accent, 0.15);\n"
    "}\n"
    "button.move-text-btn.active,\n"
    "button.move-text-btn.active:hover {\n"
    "  background-color: @accent_color;\n"
    "  color: @accent_fg;\n"
    "  font-weight: 800;\n"
    "  border-color: @accent_color;\n"
    "}\n"
    ".match-row:hover { background-color: alpha(@button_hover, 0.4); }\n"
    ".match-row label { color: @fg_color; }\n"

    /* -------------------- Transparent overlay (particles) -------------------- */
    "window.transparent-overlay { background: transparent; box-shadow: none; border: none; }\n"
    "window.transparent-overlay > widget { background: transparent; }\n"
    
    /* -------------------- Standard Components -------------------- */
    "separator { background-color: alpha(@border_color, 0.6); min-height: 1px; min-width: 1px; }\n"
    
    "checkbutton { color: @fg_color; }\n"
    "checkbutton:checked check { background-color: @accent_color; color: @accent_fg; border-color: @accent_color; }\n"
    "checkbutton:hover check { border-color: @accent_color; }\n"
    
    /* -------------------- Media / Replay Buttons -------------------- */
    "button.media-button {\n"
    "  background: transparent;\n"
    "  border: 1px solid alpha(@border_color, 0.8);\n"
    "  border-radius: 6px;\n"
    "  color: @fg_color;\n"
    "  padding: 6px;\n"
    "  transition: all 0.2s ease;\n"
    "}\n"
    "button.media-button:hover {\n"
    "  background: alpha(@accent_color, 0.08);\n"
    "  border-color: @accent_color;\n"
    "  color: @accent_color;\n"
    "}\n"
    "button.media-button:active {\n"
    "  background: alpha(@accent_color, 0.15);\n"
    "}\n"
    "button.media-button:disabled { opacity: 0.3; border-color: @border_color; }\n"
    "button.media-button.suggested-action { background: @accent_color; color: @accent_fg; border-color: @accent_color; }\n"
    "button.media-button.suggested-action:hover { background: shade(@accent_color, 1.1); }\n"

    /* -------------------- Dark Mode Button (Accent Mapping) -------------------- */
    ".dark-mode-button, .accent-color-proxy { color: @accent_color; }\n"

    // --- HAL Chess Console (Right Panel) ---
    // --- Right Side Panel v4 (HAL Console Rail Layout) ---
    " .right-side-panel-v4 { border-left: 1px solid @border_color; background: @panel_bg; }\n"
    " .adv-rail-box { background: alpha(@bg_color, 0.5); padding: 8px 0; border-radius: 50px; margin-left: 8px; }\n"
    " .rail-side-label { font-family: 'Inter', sans-serif; font-size: 10px; font-weight: 800; color: @dim_label; margin: 4px 0; }\n"
    
    " .pos-info-v4 { padding: 16px; }\n"
    " .eval-text-v4 { font-family: 'JetBrains Mono', monospace; font-size: 32px; font-weight: 800; color: @fg_color; }\n"
    " .mate-notice-v4 { background: alpha(@base_destructive_bg, 0.15); color: @base_destructive_bg; font-weight: 900; font-size: 14px; padding: 6px 12px; border-radius: 8px; margin-left: 12px; }\n"
    " .hanging-text-v4 { font-size: 13px; text-transform: uppercase; letter-spacing: 1.2px; color: @dim_label; font-weight: 700; line-height: 1.6; }\n"
    " .analysis-side-lbl-v4 { font-size: 12px; font-weight: 800; color: @accent_color; text-transform: uppercase; letter-spacing: 0.8px; opacity: 0.9; margin-top: 6px; }\n"
    
    " .feedback-zone-v4 { padding: 16px; margin: 12px; border-radius: 14px; border: 1.5px solid alpha(@border_color, 0.6); }\n"
    " .feedback-rating-v4 { font-weight: 800; font-size: 14px; text-transform: uppercase; letter-spacing: 1.2px; margin-bottom: 4px; }\n"
    " .feedback-desc-v4 { font-size: 16px; color: @fg_color; opacity: 1.0; line-height: 1.4; }\n"
    
    " .panel-toggle-btn { border: none; background: transparent; padding: 4px; border-radius: 6px; color: @accent_color; opacity: 1.0; transition: all 0.2s; min-width: 32px; min-height: 32px; }\n"
    " .panel-toggle-btn:hover { background: alpha(@fg_color, 0.05); }\n"
    
    " .feedback-best { background: alpha(@base_success_bg, 0.12); color: @base_success_bg; }\n"
    " .feedback-blunder { background: alpha(@base_destructive_bg, 0.12); color: @base_destructive_bg; }\n"
    " .feedback-mistake { background: rgba(230, 126, 34, 0.1); color: #e67e22; }\n"
    
    " .history-header-v4 { padding: 12px 20px; font-size: 13px; font-weight: 800; text-transform: uppercase; color: @dim_label; letter-spacing: 2px; background: alpha(@fg_color, 0.02); border-top: 1px solid alpha(@border_color, 0.4); border-bottom: 1px solid alpha(@border_color, 0.2); }\n"
    " .move-history-list-v4 { background: transparent; }\n"
    " .move-number-v2 { min-width: 44px; padding: 10px 10px; color: @dim_label; font-size: 13px; font-weight: 700; opacity: 0.8; background: alpha(@bg_color, 0.08); border-right: 1px solid alpha(@border_color, 0.1); }\n"
    " .move-cell-v2 { padding: 4px; min-width: 110px; transition: background 0.15s; border-right: 1px solid alpha(@border_color, 0.05); }\n"
    " .move-text-btn { border: 1.5px solid alpha(@border_color, 0.5); background: alpha(@fg_color, 0.02); border-radius: 20px; padding: 2px 8px; margin: 4px; color: @fg_color; font-family: 'Inter', sans-serif; font-weight: 700; font-size: 14px; transition: all 0.25s; min-height: 32px; }\n"
    " .move-text-btn:hover { background: alpha(@accent_color, 0.15); color: @accent_color; transform: translateY(-1px); border-color: alpha(@accent_color, 0.3); }\n"
    " .move-text-btn.active { background: @accent_color; color: @accent_fg; font-weight: 800; box-shadow: 0 4px 12px alpha(@accent_color, 0.6); border-color: @accent_color; }\n"
    
    " .nav-footer-v4 { padding: 10px; border-top: 1px solid @border_color; background: alpha(@bg_color, 0.1); }\n"
    " .nav-btn-v4 { border: none; background: transparent; opacity: 0.5; color: @fg_color; transition: opacity 0.2s; border-radius: 6px; }\n"
    " .nav-btn-v4:hover { opacity: 1; background: alpha(@fg_color, 0.05); color: @accent_color; }\n"
    
    // --- HAL Chess Console (Right Panel) ---
    ".main-col-v4 { background: @panel_bg; }\n"
    ".right-side-panel-v4 { background: @panel_bg; border-left: 1px solid @border_color; }\n"
    ".right-side-panel-v2 { background: @panel_bg; border-left: 1px solid @border_color; }\n"
    
    // 1. Live Summary Zone
    ".analysis-summary-v2 { padding: 12px; background: @bg_color; }\n"
    ".eval-text-hal { font-family: monospace; font-size: 20px; font-weight: 800; color: @fg_color; }\n"
    ".hanging-text-hal { font-size: 11px; color: @dim_label; text-transform: uppercase; letter-spacing: 0.5px; }\n"
    
    // 2. Feedback Zone
    ".feedback-zone-v2 { padding: 12px; transition: background 0.3s ease; border-radius: 4px; margin: 4px 8px; }\n"
    ".feedback-rating-hal { font-weight: 800; font-size: 13px; text-transform: uppercase; margin-bottom: 2px; }\n"
    ".feedback-desc-hal { font-size: 12px; opacity: 0.9; line-height: 1.3; }\n"
    
    // Severity Tints (Subtle backgrounds)
    ".feedback-best { background: rgba(39, 174, 96, 0.15); color: #27ae60; }\n"
    ".feedback-excellent { background: rgba(39, 174, 96, 0.15); color: #27ae60; }\n"
    ".feedback-good { background: rgba(46, 204, 113, 0.1); color: #2ecc71; }\n"
    ".feedback-inaccuracy { background: rgba(241, 196, 15, 0.1); color: #f1c40f; }\n"
    ".feedback-mistake { background: rgba(230, 126, 34, 0.1); color: #e67e22; }\n"
    ".feedback-blunder { background: rgba(231, 76, 60, 0.15); color: #e74c3c; }\n"
    
    // Rating labels (for History / Info Panel)
    ".rating-best { color: #27ae60; font-weight: bold; }\n"
    ".rating-excellent { color: #27ae60; font-weight: bold; }\n"
    ".rating-good { color: #2ecc71; font-weight: bold; }\n"
    ".rating-inaccuracy { color: #f1c40f; font-weight: bold; }\n"
    ".rating-mistake { color: #e67e22; font-weight: bold; }\n"
    ".rating-blunder { color: #e74c3c; font-weight: bold; }\n"
    
    // 3. Move History Zone
    ".history-header-v2 { padding: 6px 12px; background: rgba(0,0,0,0.03); border-top: 1px solid rgba(0,0,0,0.05); border-bottom: 1px solid rgba(0,0,0,0.05); }\n"
    ".history-header-v2 label { font-size: 10px; font-weight: 800; color: @dim_label; text-transform: uppercase; letter-spacing: 1px; }\n"
    
    ".move-history-row-v2 { border-bottom: 1px solid rgba(0,0,0,0.01); }\n"
    ".move-number-v2 { min-width: 32px; padding: 6px 8px; color: @dim_label; font-size: 11px; background: rgba(0,0,0,0.01); border-right: 1px solid rgba(0,0,0,0.02); }\n"
    ".move-cell-v2 { padding: 6px 8px; min-width: 75px; }\n"
    ".move-cell-v2:hover { background: rgba(0,0,0,0.03); }\n"
    
    // --- Info Panel Refining ---
    ".status-label-v2 { margin-bottom: 12px; padding: 6px 12px; border-radius: 12px; background: rgba(0,0,0,0.03); color: @fg_color; }\n"
    ".capture-box-v2 { background: rgba(0,0,0,0.02); border-radius: 6px; padding: 4px; }\n"
    ".capture-box-v2 flowboxchild { padding: 2px; transition: transform 0.1s ease; }\n"
    ".capture-box-v2 flowboxchild:hover { transform: scale(1.1); }\n"
    
    ".move-history-list { background-color: transparent; padding-bottom: 12px; }\n"
    ".move-history-row { padding: 0; border-bottom: none; transition: background 0.1s; }\n"
    ".move-history-row:hover { background-color: alpha(@accent_color, 0.03); }\n"
    ".move-number { color: @dim_label; font-weight: bold; padding: 4px 4px 4px 12px; min-width: 42px; font-size: 0.85rem; }\n"
    ".move-cell { padding: 2px 4px; }\n"
    ".move-text { font-family: system-ui, -apple-system, sans-serif; font-size: 0.92rem; font-weight: 600; padding: 4px 10px; border-radius: 4px; color: @fg_color; border: none; background: transparent; }\n"
    ".move-text:hover { background-color: alpha(@accent_color, 0.08); }\n"
    ".move-text.active { background-color: alpha(@accent_color, 0.15); color: @accent_color; font-weight: 800; }\n"
    
    ".nav-footer-v2 { padding: 4px; border-top: 1px solid rgba(0,0,0,0.05); background: rgba(0,0,0,0.01); }\n"
    ".nav-btn-v2 { padding: 4px 8px; font-size: 1.1rem; background: transparent; border: none; opacity: 0.7; }\n"
    ".nav-btn-v2:hover { opacity: 1.0; background: rgba(0,0,0,0.05); }\n"
    ".nav-btn-v2:disabled { opacity: 0.2; }\n"

    // ---------- Extra changes for move history row active state ----------
    " .move-history-row-v2.active-row { background-color: alpha(@accent_color, 0.12); border-radius: 10px; }\n"
    " .move-history-row-v2.active-row .move-number-v2 { background-color: alpha(@accent_color, 0.25); color: @accent_fg; }\n"
    " .move-history-row-v2.active-row .move-cell-v2 { background-color: alpha(@accent_color, 0.10); }\n";



static void update_provider(void) {
    if (!global_provider) return;
    
    // Get current config to know which theme to load
    AppConfig* cfg = config_get();
    const char* target_id = cfg ? cfg->theme : "theme_c_aubergine";
    
    const AppTheme* active_theme = NULL;
    
    // 1. Try system themes
    for (size_t i = 0; i < sizeof(SYSTEM_THEMES)/sizeof(SYSTEM_THEMES[0]); i++) {
        if (strcmp(target_id, SYSTEM_THEMES[i].theme_id) == 0) {
            active_theme = &SYSTEM_THEMES[i];
            break;
        }
    }
    
    // 2. Try custom themes if not found
    if (!active_theme) {
        int count = 0;
        AppTheme* customs = app_themes_get_list(&count);
        for (int i = 0; i < count; i++) {
            if (strcmp(customs[i].theme_id, target_id) == 0) {
                active_theme = &customs[i];
                break;
            }
        }
    }
    
    // Fallback
    if (!active_theme) {
        // Fallback to C
        for (size_t i = 0; i < sizeof(SYSTEM_THEMES)/sizeof(SYSTEM_THEMES[0]); i++) {
            if (strcmp(SYSTEM_THEMES[i].theme_id, "theme_c_aubergine") == 0) {
                active_theme = &SYSTEM_THEMES[i];
                break;
            }
        }
        if (!active_theme) active_theme = &SYSTEM_THEMES[0]; // Extra safety
    }
    
    GString *css = g_string_new(NULL);
    
    // 3. Generate Colors
    if (active_theme) {
        if (current_is_dark) append_theme_css(css, &active_theme->dark);
        else append_theme_css(css, &active_theme->light);
    }
    
    // 4. Append Structural CSS
    g_string_append(css, CSS_STRUCTURAL);
    
    // 5. Load
    gtk_css_provider_load_from_string(global_provider, css->str);
    
    g_string_free(css, TRUE);
}

// Done

void theme_manager_init(void) {
    if (global_provider) return;
    
    // Ensure custom themes are loaded
    app_themes_init();
    
    global_provider = gtk_css_provider_new();
    
    // High priority to override default theme
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), 
        GTK_STYLE_PROVIDER(global_provider), 
        GTK_STYLE_PROVIDER_PRIORITY_USER); 
        
    // Set initial state from config
    AppConfig* cfg = config_get();
    if (cfg) {
        current_is_dark = cfg->is_dark_mode;
    }
        
    update_provider();
}

void theme_manager_set_dark(bool is_dark) {
    if (current_is_dark == is_dark) return;
    current_is_dark = is_dark;
    
    AppConfig* cfg = config_get();
    if (cfg) {
        cfg->is_dark_mode = is_dark;
        // We should save config ideally, or rely on caller to save settings
    }
    
    update_provider();
}

void theme_manager_toggle(void) {
    theme_manager_set_dark(!current_is_dark);
}

bool theme_manager_is_dark(void) {
    return current_is_dark;
}

// Check if ID is a system theme
bool theme_manager_is_system_theme(const char* id) {
    if (!id) return false;
    for (size_t i = 0; i < sizeof(SYSTEM_THEMES)/sizeof(SYSTEM_THEMES[0]); i++) {
        if (strcmp(SYSTEM_THEMES[i].theme_id, id) == 0) {
            return true;
        }
    }
    return false;
}

void theme_manager_set_theme_id(const char* id) {
    if (!id) return;
    
    AppConfig* cfg = config_get();
    if (cfg) {
        strncpy(cfg->theme, id, sizeof(cfg->theme) - 1);
        cfg->theme[sizeof(cfg->theme) - 1] = '\0';
    }
    
    update_provider();
}

const AppTheme* theme_manager_get_current_theme(void) {
    AppConfig* cfg = config_get();
    const char* target_id = cfg ? cfg->theme : "theme_c_aubergine";
    
    // Logic similar to update_provider
    
    // 1. System
    for (size_t i = 0; i < sizeof(SYSTEM_THEMES)/sizeof(SYSTEM_THEMES[0]); i++) {
        if (strcmp(target_id, SYSTEM_THEMES[i].theme_id) == 0) {
            return &SYSTEM_THEMES[i];
        }
    }
    
    // 2. Custom
    int count = 0;
    AppTheme* customs = app_themes_get_list(&count);
    for (int i = 0; i < count; i++) {
        if (strcmp(customs[i].theme_id, target_id) == 0) {
            return &customs[i];
        }
    }
    
    // Fallback to C
    for (size_t i = 0; i < sizeof(SYSTEM_THEMES)/sizeof(SYSTEM_THEMES[0]); i++) {
         if (strcmp(SYSTEM_THEMES[i].theme_id, "theme_c_aubergine") == 0) {
             return &SYSTEM_THEMES[i];
         }
    }
    
    return &SYSTEM_THEMES[0];
}

const AppTheme* theme_manager_get_theme_by_id(const char* id) {
    if (!id) return NULL;
    
    // 1. System
    for (size_t i = 0; i < sizeof(SYSTEM_THEMES)/sizeof(SYSTEM_THEMES[0]); i++) {
        if (strcmp(id, SYSTEM_THEMES[i].theme_id) == 0) {
            return &SYSTEM_THEMES[i];
        }
    }
    
    // 2. Custom
    int count = 0;
    AppTheme* customs = app_themes_get_list(&count);
    for (int i = 0; i < count; i++) {
        if (strcmp(customs[i].theme_id, id) == 0) {
            return &customs[i];
        }
    }
    return NULL;
}

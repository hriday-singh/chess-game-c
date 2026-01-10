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
            "#fafafa", "#1f2937", "#ffffff", "#ffffff", "#ffffff",
            "#3584e4", "#ffffff",
            "#2e7d32", "#2e7d32", "#ffffff", "#256528",
            "#dc3545", "#ffffff", "#bb2d3b",
            "#d1d5db", "#6b7280", "#111827", "#ffffff", "#f1f5f9", "#e9eff6",
            "#c62828",
            "#e7f0ff", "#e9ddff"
        },
        .dark = {
            "#121212", "#e7e7e7", "#1a1a1a", "#1f1f1f", "#121212",
            "#7fb2ff", "#0f172a",
            "#66bb6a", "#6ee7a6", "#1e1e1e", "#57a85b",
            "#ff5a6a", "#0f172a", "#e54b5a",
            "#2f2f2f", "#a1a1aa", "#000000", "#ffffff", "#1f1f1f", "#2a2a2a",
            "#ff6b6b",
            "#7810ab", "#193456"
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

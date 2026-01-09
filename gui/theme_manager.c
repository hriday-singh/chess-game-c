#include "theme_manager.h"

static GtkCssProvider *global_provider = NULL;
static bool current_is_dark = false;

// --- CSS Definitions ---

// Common base CSS (Structure, padding, etc - layout agnostic of color)
// We keep your layout customs here if provided, or rely on existing layout.
// Note: User asked NOT to touch sizes/padding. 
// We only define colors in variables.

static const char *CSS_LIGHT =
    "@define-color bg_color #fafafa;\n"
    "@define-color fg_color #1f2937;\n"
    "@define-color panel_bg #ffffff;\n"
    "@define-color panel_fg #1f2937;\n"
    "@define-color border_color #d1d5db;\n"
    "@define-color accent_color #3584e4;\n"
    "@define-color accent_fg #ffffff;\n"
    "@define-color button_bg #f1f5f9;\n"
    "@define-color button_fg #1f2937;\n"
    "@define-color button_hover #e9eff6;\n"
    "@define-color entry_bg #ffffff;\n"
    "@define-color entry_fg #1f2937;\n"
    "@define-color popover_bg #ffffff;\n"
    "@define-color popover_fg #1f2937;\n"
    "@define-color card_bg #ffffff;\n"
    "@define-color dim_label #6b7280;\n"
    "@define-color tooltip_bg #111827;\n"
    "@define-color tooltip_fg #ffffff;\n"
    "@define-color destructive_bg #dc3545;\n"
    "@define-color destructive_fg #ffffff;\n"
    "@define-color destructive_hover #bb2d3b;\n"
    "@define-color success_text #2e7d32;\n"
    "@define-color error_text #c62828;\n"
    "@define-color success_bg #2e7d32;\n"
    "@define-color success_fg #ffffff;\n"
    "@define-color success_hover #256528;\n";

static const char *CSS_DARK =
    "@define-color bg_color #121212;\n"
    "@define-color fg_color #e7e7e7;\n"
    "@define-color panel_bg #1a1a1a;\n"
    "@define-color panel_fg #e7e7e7;\n"
    "@define-color border_color #2f2f2f;\n"
    "@define-color accent_color #7fb2ff;\n"
    "@define-color accent_fg #0f172a;\n"
    "@define-color button_bg #1f1f1f;\n"
    "@define-color button_fg #e7e7e7;\n"
    "@define-color button_hover #2a2a2a;\n"
    "@define-color entry_bg #121212;\n"
    "@define-color entry_fg #e7e7e7;\n"
    "@define-color popover_bg #1a1a1a;\n"
    "@define-color popover_fg #e7e7e7;\n"
    "@define-color card_bg #1f1f1f;\n"
    "@define-color dim_label #a1a1aa;\n"
    "@define-color tooltip_bg #000000;\n"
    "@define-color tooltip_fg #ffffff;\n"
    "@define-color destructive_bg #ff5a6a;\n"
    "@define-color destructive_fg #ffffff;\n"
    "@define-color destructive_hover #e54b5a;\n"
    "@define-color success_text #6ee7a6;\n"
    "@define-color error_text #ff6b6b;\n"
    "@define-color success_bg #66bb6a;\n"
    "@define-color success_fg #1e1e1e;\n"
    "@define-color success_hover #57a85b;\n";

static const char *CSS_STRUCTURAL =
    /* -------------------- Base surfaces -------------------- */
    "window, .window { background-color: @bg_color; color: @fg_color; }\n"
    "window.csd, .window.csd { border-radius: 12px; }\n"

    /* Robust Headerbar - use specificity instead of !important */
    "window headerbar, window headerbar:backdrop, window headerbar.selection-mode {\n"
    "  background-color: @bg_color;\n"
    "  color: @fg_color;\n"
    "  border-bottom: 1px solid @border_color;\n"
    "  box-shadow: none;\n"
    "}\n"
    "headerbar .title, headerbar label { color: @fg_color; }\n"

    /* Make sure common containers don’t introduce their own colors */
    "box, grid, stack, overlay, revealer, scrolledwindow { background: transparent; color: inherit; }\n"

    /* -------------------- Panels / cards -------------------- */
    ".info-panel { background-color: @panel_bg; color: @panel_fg; border-left: 1px solid @border_color; }\n"
    ".info-label-title { font-weight: 700; color: @dim_label; }\n"
    ".info-label-value { font-size: 1.1em; font-weight: 600; color: @panel_fg; }\n"

    ".card { background-color: @card_bg; border: 1px solid @border_color; border-radius: 10px; }\n"

    /* -------------------- Board & captures -------------------- */
    ".board-frame { border: 2px solid @border_color; border-radius: 12px; margin: 10px; }\n"
    ".capture-box {\n"
    "  background-color: @button_bg;\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 8px;\n"
    "  padding: 4px 6px;\n"
    "  min-height: 28px;\n"
    "}\n"
    ".capture-count {\n"
    "  font-size: 10px;\n"
    "  font-weight: 800;\n"
    "  color: @dim_label;\n"
    "  margin-left: 4px;\n"
    "}\n"

    /* -------------------- Typography helpers -------------------- */
    ".title-1 { font-size: 24px; font-weight: 800; margin-bottom: 8px; color: @fg_color; }\n"
    ".title-2 { font-size: 18px; font-weight: 800; margin-bottom: 12px; color: @fg_color; }\n"
    ".heading { font-weight: 800; font-size: 18px; color: @fg_color; margin-bottom: 8px; }\n"
    ".dim-label { color: @dim_label; opacity: 0.85; }\n"
    ".ai-note { color: @dim_label; font-size: 0.85em; font-style: italic; }\n"

    ".success-text { color: @success_text; }\n"
    ".error-text { color: @error_text; }\n"

    /* -------------------- Focus ring (consistent) -------------------- */
    "* { outline-style: solid; outline-width: 0px; outline-offset: 2px; }\n"
    "*:focus-visible { outline-width: 2px; outline-color: alpha(@accent_color, 0.55); }\n"

    /* -------------------- Buttons (Exclude .titlebutton to fix chonkiness) -------------------- */
    "button:not(.titlebutton) {\n"
    "  background-color: @button_bg;\n"
    "  color: @button_fg;\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 8px;\n"
    "  padding: 4px 10px;\n"
    "  min-height: 28px;\n"
    "  box-shadow: none;\n"
    "  transition: background-color 120ms ease, border-color 120ms ease;\n"
    "}\n"

    "button:not(.titlebutton):hover { background-color: @button_hover; }\n"
    "button:not(.titlebutton):active { background-color: shade(@button_bg, 0.92); }\n"
    "button:not(.titlebutton):disabled { opacity: 0.55; }\n"

    /* Restore native looks for titlebuttons */
    "headerbar .titlebutton { padding: 4px; min-height: 24px; min-width: 24px; }\n"

    /* Suggested / destructive - use explicit button class for specificity */
    "button.suggested-action {\n"
    "  background-color: @accent_color;\n"
    "  color: @accent_fg;\n"
    "  border-color: @accent_color;\n"
    "}\n"
    "button.suggested-action:hover { background-color: shade(@accent_color, 1.05); }\n"
    "button.suggested-action:active { background-color: shade(@accent_color, 0.92); }\n"

    "button.destructive-action {\n"
    "  background-color: @destructive_bg;\n"
    "  color: @destructive_fg;\n"
    "  border-color: @destructive_bg;\n"
    "}\n"
    "button.destructive-action:hover { background-color: @destructive_hover; border-color: @destructive_hover; }\n"
    "button.destructive-action:active { background-color: shade(@destructive_hover, 0.92); }\n"

    /* -------------------- Dialogs -------------------- */
    "dialog, window.dialog, message-dialog { background-color: @bg_color; color: @fg_color; }\n"
    "window.dialog { padding: 12px; }\n"
    "window.dialog button { margin: 4px; min-width: 90px; padding: 6px 14px; min-height: 32px; }\n"

    /* -------------------- Entries / inputs -------------------- */
    "entry, textview, spinbutton, searchentry {\n"
    "  background-color: @entry_bg;\n"
    "  color: @entry_fg;\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 8px;\n"
    "  padding: 6px 10px;\n"
    "  min-height: 30px;\n"
    "}\n"
    "entry:focus, textview:focus, spinbutton:focus, searchentry:focus { border-color: alpha(@accent_color, 0.85); }\n"
    "entry:disabled, textview:disabled { opacity: 0.60; }\n"
    "checkbutton, radiobutton { padding: 4px; }\n"

    /* -------------------- Popovers / tooltips -------------------- */
    "popover { background-color: @popover_bg; color: @popover_fg; border: 1px solid @border_color; border-radius: 10px; }\n"
    "popover contents { background-color: @popover_bg; color: @popover_fg; border-radius: 10px; }\n"
    "tooltip { background-color: @tooltip_bg; color: @tooltip_fg; border-radius: 8px; padding: 6px 8px; }\n"

    /* -------------------- Dropdowns / list views -------------------- */
    "dropdown > button {\n"
    "  background-color: @entry_bg;\n"
    "  color: @entry_fg;\n"
    "  border: 1px solid @border_color;\n"
    "  border-radius: 8px;\n"
    "  padding: 4px 10px;\n"
    "  min-height: 30px;\n"
    "}\n"
    "dropdown > button:hover { background-color: @button_hover; }\n"
    "dropdown arrow { color: @entry_fg; }\n"
    "dropdown label { color: inherit; }\n"

    "listview, listbox { background-color: @entry_bg; color: @entry_fg; }\n"
    "listview row, listbox row {\n"
    "  padding: 8px;\n"
    "  border-radius: 8px;\n"
    "  margin: 2px 6px;\n"
    "}\n"
    "listview row:hover, listbox row:hover { background-color: alpha(@button_hover, 0.75); }\n"
    "listview row:selected, listbox row:selected { background-color: @accent_color; color: @accent_fg; }\n"

    /* -------------------- Sidebar / settings -------------------- */
    ".settings-sidebar, .sidebar { background-color: @panel_bg; border-right: 1px solid @border_color; }\n"
    ".settings-content { background-color: @bg_color; }\n"
    ".sidebar row { padding: 10px; border-radius: 10px; margin: 4px 6px; color: @fg_color; }\n"
    ".sidebar row:hover { background-color: alpha(@button_hover, 0.75); }\n"
    ".sidebar row:selected { background-color: @accent_color; color: @accent_fg; font-weight: 800; }\n"

    /* -------------------- Success button -------------------- */
    "button.success-action {\n"
    "  background-color: @success_bg;\n"
    "  color: @success_fg;\n"
    "  border-radius: 8px;\n"
    "  border: 1px solid @success_bg;\n"
    "}\n"
    "button.success-action:hover { background-color: @success_hover; border-color: @success_hover; }\n"
    "button.success-action:active { background-color: shade(@success_bg, 0.92); }\n"
    "button.success-action:disabled { opacity: 0.55; }\n"

    /* -------------------- Transparent overlay (particles) -------------------- */
    "window.transparent-overlay { background: transparent; box-shadow: none; border: none; }\n"
    "window.transparent-overlay > widget { background: transparent; }\n";


static void update_provider(void) {
    if (!global_provider) return;
    
    GString *css = g_string_new(NULL);
    
    // 1. Append Variables
    if (current_is_dark) {
        g_string_append(css, CSS_DARK);
    } else {
        g_string_append(css, CSS_LIGHT);
    }
    
    // 2. Append Structure
    g_string_append(css, CSS_STRUCTURAL);
    
    // 3. Load
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
    update_provider();
}

void theme_manager_toggle(void) {
    theme_manager_set_dark(!current_is_dark);
}

bool theme_manager_is_dark(void) {
    return current_is_dark;
}

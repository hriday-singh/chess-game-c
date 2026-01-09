#include "dark_mode_button.h"
#include <math.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <gdk/win32/gdkwin32.h>
#endif

// --- Configuration Constants ---

// Shape
#define CORNER_ROUNDNESS 0.35 // 0.0=square, 1.0=circle
#define BUTTON_SIZE 36        // Matches typical header bar button size

// Icon
#define ICON_PADDING 8.0
#define ICON_LINE_WIDTH 1.5
#define ICON_GLOW_ALPHA_IDLE 0.15
#define ICON_GLOW_ALPHA_ANIM 0.4
#define BREATHING_AMP 0.1
#define BREATHING_PERIOD_SEC 3.0

// Animation
#define TOGGLE_ANIM_DURATION_MS 900
#define MORPH_START_PROGRESS 0.2
#define MORPH_END_PROGRESS 0.8
#define ROTATION_CLOCKWISE TRUE

// Hearts Particles
#define HEARTS_ENABLED_CLICK TRUE
#define HEARTS_ENABLED_HOVER TRUE
#define HEARTS_COLOR_R 1.0
#define HEARTS_COLOR_G 0.4
#define HEARTS_COLOR_B 0.7
#define HEARTS_COLOR_A 0.7
#define HEARTS_GLOW_STRENGTH 0.5

#define HEARTS_CLICK_BURST_COUNT 30
#define HEARTS_CLICK_BURST_RADIUS 20.0
#define HEARTS_LIFETIME_SEC 2.5
#define HEARTS_HOVER_RATE_PER_SEC 3.0
#define HEARTS_HOVER_MAX_COUNT 10 // Max concurrent concurrent hover hearts to prevent overload

#define HEARTS_MIN_SIZE 4.0
#define HEARTS_MAX_SIZE 9.0
#define HEARTS_SPEED_MIN 10.0
#define HEARTS_SPEED_MAX 30.0

// Overlay & Margins
#define BUTTON_MARGIN 6
#define OVERLAY_PADDING 200
#define MAX_CONCURRENT_BURSTS 3

// Debug
#define DEBUG_DARKBTN_ANIM TRUE


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Data Structures ---

typedef struct {
    double x, y;
    double vx, vy;
    double size;
    double spawn_time; // in seconds (monotonic)
    double lifetime;   // in seconds
    double rotation;
} Particle;

typedef struct {
    double start_time;
    GArray* particles; // Array of Particle
} Burst;

typedef struct {
    // Widget Reference
    GtkWidget* widget;
    
    // Overlay
    GtkWidget* overlay_window; // The transparent serialization window
    GtkWidget* overlay_area;   // Drawing area inside overlay
    
    // State
    gboolean is_dark;
    gboolean is_hovered;
    gboolean enable_hearts; // Master toggle for hearts
    
    // Animation State
    gboolean anim_running;
    double anim_start_time; 
    double anim_progress;   
    
    // Breathing State
    double breathing_time_base; 
    
    // Particle State
    GList* active_bursts;      // List of Burst*
    GArray* hover_particles;   // Continuous hover particles
    double last_hover_emit_time;
    
    // Tick Callback ID
    guint tick_id;
    
    // Time tracking
    double last_frame_time;
    
} DarkModePriv;

// --- Forward Declarations ---

static void dark_mode_button_free_priv(gpointer data);
static gboolean on_tick(GtkWidget* widget, GdkFrameClock* frame_clock, gpointer user_data);
static void start_tick(DarkModePriv* priv);
static void stop_tick_if_idle(DarkModePriv* priv);
static void check_ensure_overlay(DarkModePriv* priv);
static void update_overlay_position(DarkModePriv* priv);
static void spawn_click_burst(DarkModePriv* priv);
static void draw_overlay_particles(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);
static void draw_icon(cairo_t* cr, double w, double h, gboolean is_dark, double progress, double breathing_scale);

// --- Overlay Helpers ---

#ifdef G_OS_WIN32
static void on_overlay_realize(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    GtkNative *native = gtk_widget_get_native(widget);
    if (!native) return;
    
    GdkSurface *surface = gtk_native_get_surface(native);
    if (!surface) return;
    
    HWND hwnd = (HWND)gdk_win32_surface_get_handle(surface);
    if (!hwnd) return;
    
    // Make window Click-Through (TRANSPARENT) and Layered
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}
#endif

static void check_ensure_overlay(DarkModePriv* priv) {
    if (priv->overlay_window) return;
    
    // Create bare window
    priv->overlay_window = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(priv->overlay_window), FALSE);
    gtk_widget_set_focusable(priv->overlay_window, FALSE);
    
    // Styling for transparency - Scope strictly
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, 
        "window.transparent-overlay { background: transparent; box-shadow: none; border: none; } "
        "window.transparent-overlay > widget { background: transparent; }");
    
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER + 200);
    g_object_unref(css);
    
    // Apply class
    gtk_widget_add_css_class(priv->overlay_window, "transparent-overlay");

    // Drawing Area
    priv->overlay_area = gtk_drawing_area_new();
    
    gtk_widget_set_hexpand(priv->overlay_area, TRUE);
    gtk_widget_set_vexpand(priv->overlay_area, TRUE);
    gtk_window_set_child(GTK_WINDOW(priv->overlay_window), priv->overlay_area);
    
    // We bind the draw callback manually since it's a separate widget
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(priv->overlay_area), 
        (GtkDrawingAreaDrawFunc)draw_overlay_particles, priv, NULL);
        
#ifdef G_OS_WIN32
    g_signal_connect(priv->overlay_window, "realize", G_CALLBACK(on_overlay_realize), NULL);
#endif

    // Show
    gtk_widget_set_visible(priv->overlay_window, TRUE);
    // Ensure realized for immediate handle access if possible (though signal handles it)
    gtk_widget_realize(priv->overlay_window);
}


static void update_overlay_position(DarkModePriv* priv) {
#ifdef G_OS_WIN32
    if (!priv->overlay_window || !priv->widget) return;

    // Get Main Window Handle
    GtkNative *native = gtk_widget_get_native(priv->widget);
    if (!native) return;
    
    GdkSurface *surface = gtk_native_get_surface(native);
    if (!surface) return;
    
    HWND hMain = (HWND)gdk_win32_surface_get_handle(surface);
    if (!hMain) return;
    
    // Get Overlay Handle
    GtkNative *overlay_native = gtk_widget_get_native(priv->overlay_window);
    HWND hOverlay = NULL;
    if (overlay_native) {
        GdkSurface *osurf = gtk_native_get_surface(overlay_native);
        if (osurf) hOverlay = (HWND)gdk_win32_surface_get_handle(osurf);
    }
    if (!hOverlay) return;

    // Set Owner to Main (Maintains Z-Order association)
    SetWindowLongPtr(hOverlay, GWLP_HWNDPARENT, (LONG_PTR)hMain);

    // Get Screen metrics
    HMONITOR hMonitor = MonitorFromWindow(hMain, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {0};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMonitor, &mi);
    
    // Check Fullscreen / Maximized
    GdkToplevelState state = gdk_toplevel_get_state(GDK_TOPLEVEL(surface));
    gboolean is_fullscreen = (state & GDK_TOPLEVEL_STATE_FULLSCREEN);
    
    int target_x, target_y, target_w, target_h;
    
    if (is_fullscreen) {
        // In Fullscreen, cover the entire monitor to ensure visibility
        // and allow particles to fly freely.
        target_x = mi.rcMonitor.left;
        target_y = mi.rcMonitor.top;
        target_w = mi.rcMonitor.right - mi.rcMonitor.left;
        target_h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    } else {
        // Standard Mode: Main Rect + Padding
        RECT rMain;
        GetWindowRect(hMain, &rMain);
        
        int w = rMain.right - rMain.left;
        int h = rMain.bottom - rMain.top;
        int pad = OVERLAY_PADDING;
        
        target_x = rMain.left - pad;
        target_y = rMain.top - pad;
        target_w = w + pad * 2;
        target_h = h + pad * 2;
    }
    
    // Apply Size (GTK side)
    gtk_window_set_default_size(GTK_WINDOW(priv->overlay_window), target_w, target_h);

    // Apply Position (Win32 side)
    // Use HWND_TOP instead of HWND_TOPMOST to allow dialogs/popovers to appear above
    // SWP_NOACTIVATE prevents stealing focus from other windows
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    
    // If not visible yet, show it
    if (!gtk_widget_get_visible(priv->overlay_window)) {
         flags |= SWP_SHOWWINDOW;
    }
    
    // Use HWND_TOP (not TOPMOST) so dialogs can appear above the overlay
    SetWindowPos(hOverlay, HWND_TOP, target_x, target_y, target_w, target_h, flags);
#else
    // Fallback for non-Windows (linux etc)
    // ... Minimal implementation preserved ...
    if (!priv->overlay_window || !priv->widget) return;
    GtkWindow* main_win = GTK_WINDOW(gtk_widget_get_root(priv->widget));
    if (GTK_IS_WINDOW(main_win)) {
        gtk_window_set_transient_for(GTK_WINDOW(priv->overlay_window), main_win);
        int w = gtk_widget_get_width(GTK_WIDGET(main_win));
        int h = gtk_widget_get_height(GTK_WIDGET(main_win));
        gtk_window_set_default_size(GTK_WINDOW(priv->overlay_window), w + OVERLAY_PADDING * 2, h + OVERLAY_PADDING * 2);
    }
#endif
}

static void free_burst(gpointer data) {
    Burst* b = (Burst*)data;
    if (b->particles) g_array_free(b->particles, TRUE);
    g_free(b);
}


// --- Helper Functions ---

static double get_monotonic_time(void) {
    return g_get_monotonic_time() / 1000000.0;
}

static double ease_in_out(double t) {
    // Cubic ease in out
    return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2;
}

// --- Lifecycle & Events ---

static void on_enter(GtkEventControllerMotion* controller, double x, double y, gpointer user_data) {
    (void)controller; (void)x; (void)y;
    DarkModePriv* priv = (DarkModePriv*)user_data;
    priv->is_hovered = TRUE;
    start_tick(priv);
}

static void on_leave(GtkEventControllerMotion* controller, gpointer user_data) {
    (void)controller;
    DarkModePriv* priv = (DarkModePriv*)user_data;
    priv->is_hovered = FALSE;
    // Tick will stop naturally if no other animations are running 
    // BUT we need to let existing particles fade out, so we don't force stop here.
    // stop_tick_if_idle will handle it.
}

static void on_click(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    DarkModePriv* priv = (DarkModePriv*)user_data;
    
    if (priv->anim_running) {
        return; // Ignore clicks during animation
    }
    
    // Start Toggle Animation
    priv->anim_running = TRUE;
    priv->anim_start_time = get_monotonic_time(); // Will be refined in tick
    priv->anim_progress = 0.0;
    
    // Set accessibility label immediately for feedback, though state flips at end visually?
    // User requested "At animation end: Set state".
    // But for accessibility, maybe toggle now? Let's stick to visual sync.
    
    // Spawn Click Burst (Concurrent)
    if (priv->enable_hearts && HEARTS_ENABLED_CLICK) {
        spawn_click_burst(priv);
    }
    
    start_tick(priv);
}

// --- Drawing ---

static void draw_heart_shape(cairo_t* cr, double cx, double cy, double size) {
    // Simple heart shape using bezier curves
    // Start at bottom tip
    double half = size / 2.0;
    cairo_move_to(cr, cx, cy + half); 
    
    // Left lobe
    cairo_curve_to(cr, 
        cx - size, cy - size * 0.2, // ctrl1
        cx - size * 0.5, cy - size, // ctrl2
        cx, cy - size * 0.3);       // top center
        
    // Right lobe
    cairo_curve_to(cr, 
        cx + size * 0.5, cy - size, // ctrl1
        cx + size, cy - size * 0.2, // ctrl2
        cx, cy + half);             // back to bottom
        
    cairo_close_path(cr);
}

static void draw_particles_array(cairo_t* cr, GArray* arr, double current_time) {
     if (!arr) return;
    for (guint i = 0; i < arr->len; i++) {
        Particle* p = &g_array_index(arr, Particle, i);
        double age = current_time - p->spawn_time;
        if (age < 0) age = 0;
        double life_pct = age / p->lifetime;
        
        if (life_pct >= 1.0) continue;
        
        double alpha = HEARTS_COLOR_A * (1.0 - (life_pct * life_pct));
        
        cairo_save(cr);
        cairo_translate(cr, p->x, p->y); // p->x,y are relative to button center
        cairo_rotate(cr, p->rotation);
        
        cairo_set_source_rgba(cr, HEARTS_COLOR_R, HEARTS_COLOR_G, HEARTS_COLOR_B, alpha);
        draw_heart_shape(cr, 0, 0, p->size);
        cairo_fill_preserve(cr);
        
        cairo_set_source_rgba(cr, HEARTS_COLOR_R, HEARTS_COLOR_G, HEARTS_COLOR_B, alpha * 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        
        cairo_restore(cr);
    }
}

static void draw_overlay_particles(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area; (void)width; (void)height;
    DarkModePriv* priv = (DarkModePriv*)user_data;
    double now = get_monotonic_time();
    
    // We need to translate to Button Center in Overlay Space.
    // Overlay is MainWin + Padding.
    // So Button Center in Overlay = Button Center in MainWin + PADDING.
    
    // 1. Get Button Center in Main Window
    double btn_w = gtk_widget_get_width(priv->widget);
    double btn_h = gtk_widget_get_height(priv->widget);
    
    // Coordinate Calculation using compute_point (Modern GTK4)
    graphene_point_t p_btn = { .x = btn_w / 2.0f, .y = btn_h / 2.0f };
    graphene_point_t p_target = {0};
    
    GtkWidget* root = GTK_WIDGET(gtk_widget_get_root(priv->widget));
    if (gtk_widget_compute_point(priv->widget, root, &p_btn, &p_target)) {
        double gx = p_target.x;
        double gy = p_target.y;
        
        // On Windows, GetWindowRect includes invisible borders (shadows).
        // GTK Root coordinates start inside this border.
        // Heuristic adjustment based on visual feedback.
        // Previous (8, 8) was slightly Top-Right.
        // Need to move Dot LEFT (decrease X) and DOWN (increase Y).
        
        double win32_offset_x = 0;
        double win32_offset_y = 0;

#ifdef G_OS_WIN32
        // Dynamic Offset based on Window State (Shadows vs No Shadows)
        GdkSurface *surf = gtk_native_get_surface(GTK_NATIVE(root));
        GdkToplevelState state = gdk_toplevel_get_state(GDK_TOPLEVEL(surf));
        gboolean is_max_or_full = (state & (GDK_TOPLEVEL_STATE_MAXIMIZED | GDK_TOPLEVEL_STATE_FULLSCREEN));

        if (!is_max_or_full) {
            // Windowed mode: Shadows are present (~8px on Win10/11)
            // GetWindowRect includes them, GTK Root does not.
            // We need to shift drawing RIGHT/DOWN to match GTK Root.
            // User requested explicit offsets from (5,8) -> (13,10)
            win32_offset_x = 13.0; 
            win32_offset_y = 10.0; 
        } else {
            // Fullscreen/Maximized: No shadows. 
            // GetWindowRect == GTK Root. No offset needed.
            win32_offset_x = 0.0;
            win32_offset_y = 0.0;
        }
#endif


        double ox = gx + OVERLAY_PADDING + win32_offset_x;
        double oy = gy + OVERLAY_PADDING + win32_offset_y;
    
    // DEBUG: Draw RED DOT at calculated center
    /*
    cairo_save(cr);
    cairo_translate(cr, ox, oy);
    cairo_set_source_rgba(cr, 1, 0, 0, 1);
    cairo_arc(cr, 0, 0, 5, 0, 2*M_PI);
    cairo_fill(cr);
    cairo_restore(cr);
    */
    

    cairo_save(cr);
    cairo_translate(cr, ox, oy);
        // if (DEBUG_DARKBTN_ANIM) printf("Draw Overlay: %d hover, %d bursts\n", priv->hover_particles->len, g_list_length(priv->active_bursts));

        draw_particles_array(cr, priv->hover_particles, now);
        
        for (GList* l = priv->active_bursts; l != NULL; l = l->next) {
            Burst* b = (Burst*)l->data;
            draw_particles_array(cr, b->particles, now);
        }
        
        cairo_restore(cr);
    // } // Removed if(TRUE)
    }
}

static void draw_sun_moon(cairo_t* cr, double cx, double cy, double size, gboolean to_dark, double t) {
    // t: 0.0 = Start State, 1.0 = End State
    // If to_dark is TRUE:  Start=Sun, End=Moon
    // If to_dark is FALSE: Start=Moon, End=Sun
    
    // We can normalize this: always draw logic for Sun->Moon, 
    // and if to_dark is FALSE (Moon->Sun), we just invert t (t = 1 - t).
    
    double morph_t = t;
    if (!to_dark) {
        morph_t = 1.0 - t;
    }
    
    // Now morph_t=0 is full Sun, morph_t=1 is full Moon.
    
    double sun_radius = size * 0.35;
    double moon_outer_radius = size * 0.38;
    double ray_len = size * 0.18;
    double ray_start = sun_radius + size * 0.08;
    
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    
    // --- DRAW RAYS (Sun feature) ---
    // Rays retract as we go to Moon (morph_t 0 -> 1)
    if (morph_t < 1.0) {
        double ray_scale = 1.0 - ease_in_out(morph_t); // Retract
        if (ray_scale > 0.01) {
            cairo_save(cr);
            int n_rays = 8;
            for (int i = 0; i < n_rays; i++) {
                double angle = (2 * M_PI * i) / n_rays;
                cairo_save(cr);
                cairo_rotate(cr, angle);
                
                double r_len = ray_len * ray_scale;
                
                // Draw rounded line/rect for ray
                cairo_move_to(cr, ray_start, 0);
                cairo_line_to(cr, ray_start + r_len, 0);
                
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_set_line_width(cr, ICON_LINE_WIDTH);
                
                // Sets color logic
                cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); // Darker for sun rays usually? Or same as icon.
                // Icon color: usually dark gray/black in Light mode, White in Dark mode.
                // Wait, if button is in header (usually light background), default icon is dark.
                // But dark mode toggle implies:
                // Sun (Light Mode) -> Icon is Dark (representing "Current is Light" or "Switch to Dark"?)
                // Usually: Sun = We are in Light Mode. Moon = We are in Dark Mode.
                // Code matches: is_dark=FALSE -> Sun.
                
                if (to_dark) {
                     // Morphing Sun(DarkColor) -> Moon(WhiteColor)?
                     // Usually button icon color contrasts with background.
                     // Assuming standard headerbar: light bg -> dark icon.
                     // If we ignore theme change, we stick to one color strategy.
                     // Let's use standard text color (approx dark gray).
                     cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
                } else {
                     // Moon(DarkColor) -> Sun
                     cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); 
                }
                
                cairo_stroke(cr);
                cairo_restore(cr);
            }
            cairo_restore(cr);
        }
    }
    
    // --- DRAW BODY (Sun Disk -> Moon Crescent) ---
    // Sun: Circle.
    // Moon: Circle minus offset Circle.
    // Transition:
    // We draw the main circle.
    // We "cut out" the shadow circle.
    // Sun: Shadow circle is far away or 0 size.
    // Moon: Shadow circle overlaps.
    
    // Main Body Color
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); // Std icon color
    
    // The main circle grows slightly from Sun to Moon?
    // Sun radius vs Moon outer radius.
    double main_r = sun_radius + (moon_outer_radius - sun_radius) * ease_in_out(morph_t);
    
    // The "Shadow" circle for the crescent cut
    // For Sun (t=0): Shadow is fully outside or effectively creates full circle.
    // For Moon (t=1): Shadow is offset.
    
    // Parametric offset
    // Moon offset usually ~ r * 0.5
    double shadow_r = main_r * 0.85; 
    double max_offset = main_r * 0.6; 
    double start_offset = main_r + shadow_r + 5.0; // Functionally far away
    
    double offset_x = start_offset * (1.0 - ease_in_out(morph_t)) + max_offset * ease_in_out(morph_t);
    
    // Use clip or difference
    // Simplest Cairo way for Crescent:
    // Add outer arc.
    // Add inner arc (shadow) in REVERSE.
    // Fill.
    
    // Note: This only works if the shadow is fully contained or we handle the intersection points perfectly.
    // Easier visual trick: Draw Full Main Circle, then set operator CLEAR and draw Shadow Circle.
    // BUT this clears background too.
    // Better: cairo_push_group(), draw main, set operator DEST_OUT, draw shadow, cairo_pop_group_to_source(), paint.
    
    cairo_push_group(cr);
    
    // Draw Main Circle
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); 
    cairo_arc(cr, 0, 0, main_r, 0, 2 * M_PI);
    cairo_fill(cr);
    
    // Cutout
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
    cairo_set_source_rgba(cr, 0, 0, 0, 1); // Alpha doesn't matter for DEST_OUT
    cairo_arc(cr, offset_x, -main_r * 0.1, shadow_r, 0, 2 * M_PI); // Slight y-offset for tilted moon look
    cairo_fill(cr);
    
    cairo_pop_group_to_source(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);
    
    // Restore
    cairo_restore(cr);
}

static void draw_icon(cairo_t* cr, double w, double h, gboolean start_is_dark, double progress, double breathing_scale) {
    // 1. Setup Transform
    double cx = w / 2.0;
    double cy = h / 2.0;
    double size = fmin(w, h) - ICON_PADDING * 2;
    
    cairo_save(cr);
    
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, breathing_scale, breathing_scale);

    // Rotation
    if (progress > 0.0) {
        double angle = 2 * M_PI * ease_in_out(progress);
        if (ROTATION_CLOCKWISE) cairo_rotate(cr, angle);
        else cairo_rotate(cr, -angle);
    }
    
    // Morph Parameters
    // progress 0..1 covers the whole rotation.
    // morph happens between START and END.
    
    double t_morph = 0.0;
    if (progress < MORPH_START_PROGRESS) t_morph = 0.0;
    else if (progress > MORPH_END_PROGRESS) t_morph = 1.0;
    else {
        t_morph = (progress - MORPH_START_PROGRESS) / (MORPH_END_PROGRESS - MORPH_START_PROGRESS);
    }
    
    // Draw the procedural shape
    // We are morphing FROM start_is_dark TO !start_is_dark.
    // If start_is_dark=TRUE (Moon), we go to Sun. Target is Light. is_to_dark=FALSE.
    // If start_is_dark=FALSE (Sun), we go to Moon. Target is Dark. is_to_dark=TRUE.
    gboolean to_dark = !start_is_dark;
    
    // Since we centered at cx,cy, we effectively draw at 0,0 locally
    cairo_translate(cr, -cx, -cy); // Undo translation for the helper which might expect center logic?
    // Actually draw_sun_moon implementation above expects cx, cy as args.
    
    draw_sun_moon(cr, cx, cy, size, to_dark, t_morph);
    
    cairo_restore(cr);
}


// --- Tick Logic ---

static void spawn_particle_in_array(GArray* arr, double base_x, double base_y, gboolean is_burst) {
     Particle p;
    p.spawn_time = get_monotonic_time();
    p.lifetime = HEARTS_LIFETIME_SEC;
    
    // Random params
    double angle = g_random_double_range(0, 2 * M_PI);
    
    // Start pos relative to CENTER (passed as base_x, base_y)
    double r_start = is_burst ? 5.0 : 12.0; 
    
    p.x = base_x + cos(angle) * r_start;
    p.y = base_y + sin(angle) * r_start;
    
    double speed = g_random_double_range(HEARTS_SPEED_MIN, HEARTS_SPEED_MAX);
    if (is_burst) speed *= 2.0;
    
    p.vx = cos(angle) * speed;
    p.vy = sin(angle) * speed;
    
    p.size = g_random_double_range(HEARTS_MIN_SIZE, HEARTS_MAX_SIZE);
    p.rotation = g_random_double_range(-0.5, 0.5);
    
    g_array_append_val(arr, p);
}

static void spawn_click_burst(DarkModePriv* priv) {
    // 1. Cap concurrent bursts
    if (g_list_length(priv->active_bursts) >= MAX_CONCURRENT_BURSTS) {
        // Drop oldest
        GList* first = g_list_first(priv->active_bursts);
        free_burst(first->data);
        priv->active_bursts = g_list_delete_link(priv->active_bursts, first);
    }

    // 2. Create new burst
    Burst* b = g_new0(Burst, 1);
    b->start_time = get_monotonic_time();
    b->particles = g_array_new(FALSE, FALSE, sizeof(Particle));
    
    for (int i = 0; i < HEARTS_CLICK_BURST_COUNT; i++) {
        // Use HEARTS_CLICK_BURST_RADIUS for initial random spread
        Particle p;
        p.spawn_time = get_monotonic_time();
        p.lifetime = HEARTS_LIFETIME_SEC;
        
        double angle = g_random_double_range(0, 2 * M_PI);
        // Random radius within burst radius
        double r = sqrt(g_random_double()) * HEARTS_CLICK_BURST_RADIUS; 
        
        p.x = cos(angle) * r;
        p.y = sin(angle) * r;
        
        double speed = g_random_double_range(HEARTS_SPEED_MIN, HEARTS_SPEED_MAX);
        // Start velocity outwards
        p.vx = cos(angle) * speed * 2.5; // High burst speed
        p.vy = sin(angle) * speed * 2.5;
        
        p.size = g_random_double_range(HEARTS_MIN_SIZE, HEARTS_MAX_SIZE);
        p.rotation = g_random_double_range(-0.5, 0.5);
        
        g_array_append_val(b->particles, p);
    }
    
    priv->active_bursts = g_list_append(priv->active_bursts, b);
}

static void update_particles_array(GArray* arr, double current_time, double dt) {
    if (!arr) return;
    for (guint i = 0; i < arr->len; i++) {
        Particle* p = &g_array_index(arr, Particle, i);
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        
        if (current_time - p->spawn_time > p->lifetime) {
            g_array_remove_index_fast(arr, i);
            i--;
        }
    }
}

static void update_particles(DarkModePriv* priv, double current_time, double dt) {
    // Update Hover
    update_particles_array(priv->hover_particles, current_time, dt);
    
    // Update Bursts
    GList* l = priv->active_bursts;
    while (l) {
        Burst* b = (Burst*)l->data;
        update_particles_array(b->particles, current_time, dt);
        
        // Remove empty bursts
        if (b->particles->len == 0) {
            GList* next = l->next;
            free_burst(b);
            priv->active_bursts = g_list_delete_link(priv->active_bursts, l);
            l = next;
        } else {
            l = l->next;
        }
    }
}


static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area; // Unused
    DarkModePriv* priv = (DarkModePriv*)user_data;
    double current_time = get_monotonic_time();
    
    // 1. Draw Background (Optional)
    // No background
    
    // 2. Draw Particles (Moved to Overlay)
    // Nothing here

    
    // 3. Draw Icon
    double breathing_scale = 1.0;
    if (!priv->anim_running) {
        double t = current_time + priv->breathing_time_base;
        double phase = (2 * M_PI * t) / BREATHING_PERIOD_SEC;
        breathing_scale = 1.0 + BREATHING_AMP * sin(phase);
    }
    
    gboolean start_is_dark = priv->is_dark;
    double progress = 0.0;
    
    if (priv->anim_running) {
        progress = priv->anim_progress; // 0..1
    }
    
    draw_icon(cr, width, height, start_is_dark, progress, breathing_scale);
}

static gboolean on_tick(GtkWidget* widget, GdkFrameClock* frame_clock, gpointer user_data) {
    // Safety check: Don't run if widget is not realized
    if (!gtk_widget_get_realized(widget)) {
        return G_SOURCE_REMOVE;
    }

    DarkModePriv* priv = (DarkModePriv*)user_data;
    // gdk_frame_clock_get_frame_time returns microseconds.
    gint64 frame_time = gdk_frame_clock_get_frame_time(frame_clock);
    double now = frame_time / 1000000.0;
    
    // Use instance-based dt
    double dt = (priv->last_frame_time == 0) ? 0.016 : (now - priv->last_frame_time);
    priv->last_frame_time = now;
    
    // 1. Update Animation
    if (priv->anim_running) {
        double elapsed = now - priv->anim_start_time;
        double duration = TOGGLE_ANIM_DURATION_MS / 1000.0;
        
        if (elapsed >= duration) {
            priv->anim_progress = 1.0;
            priv->anim_running = FALSE;
            // Commit State Change
            priv->is_dark = !priv->is_dark;
            
            // Update tooltip and accessibility label to match new state
            const char* new_label = priv->is_dark ? "Switch to Light Mode" : "Switch to Dark Mode";
            gtk_widget_set_tooltip_text(widget, new_label);
            gtk_accessible_update_property(GTK_ACCESSIBLE(widget),
                GTK_ACCESSIBLE_PROPERTY_LABEL, new_label,
                -1);

        } else {
            priv->anim_progress = elapsed / duration;
        }
    }
    
    // 2. Overlay Management
    // Only create/update overlay if we actually have particles to draw
    gboolean has_particles = (priv->active_bursts != NULL) || 
                             (priv->hover_particles && priv->hover_particles->len > 0);
    
    if (has_particles) {
        check_ensure_overlay(priv);
        update_overlay_position(priv);
    } else if (priv->overlay_window && gtk_widget_get_visible(priv->overlay_window)) {
        // Hide if no particles (optional optimization)
        gtk_widget_set_visible(priv->overlay_window, FALSE);
    }
    
    // 3. Hover Emission
    if (priv->is_hovered && priv->enable_hearts && HEARTS_ENABLED_HOVER) {
        if (now - priv->last_hover_emit_time > (1.0 / HEARTS_HOVER_RATE_PER_SEC)) {
             spawn_particle_in_array(priv->hover_particles, 0, 0, FALSE);
             priv->last_hover_emit_time = now;
        }
    }
    
    // 4. Update Particles
    update_particles(priv, now, dt);
    
    // 5. Request Redraw
    gtk_widget_queue_draw(widget); // Redraw button (icon breathing)
    if (priv->overlay_area) gtk_widget_queue_draw(priv->overlay_area); // Redraw particles
    
    // 6. Check if we should stop
    stop_tick_if_idle(priv);
    
    if (DEBUG_DARKBTN_ANIM && ((int)now % 5 == 0)) {
       // printf("Tick Running. Bursts: %d\n", g_list_length(priv->active_bursts));
    }

    return G_SOURCE_CONTINUE;
}

static void start_tick(DarkModePriv* priv) {
    if (priv->tick_id == 0) {
        priv->tick_id = gtk_widget_add_tick_callback(priv->widget, on_tick, priv, NULL);
    }
}

static void stop_tick_if_idle(DarkModePriv* priv) {
    if (priv->tick_id == 0) return;
    
    gboolean needs_tick = FALSE;
    
    // Animation running?
    if (priv->anim_running) needs_tick = TRUE;
    
    // Particles alive?
    if (priv->active_bursts || (priv->hover_particles && priv->hover_particles->len > 0)) needs_tick = TRUE;
    
    // Hovers active?
    if (priv->is_hovered && HEARTS_ENABLED_HOVER) needs_tick = TRUE;
    
    // Breathing always active when idle?
    // Prompt: "Breathing continues during hover unless you want to pause; keep it simple: breathing always when idle."
    // Prompt also says: "run breathing using a slower timeout, or keep tick but lightweight."
    // Let's keep tick running if we want smooth breathing always.
    // However, to save battery, maybe only breath on hover?
    // Prompt: "Idle visual rule... icon should have a subtle 'alive' feel via gentle breathing".
    // This implies ALWAYS breathing.
    // Optimization: If nothing else happening, maybe just use a timeout for breathing (low fps?)
    // But for simplicity/smoothness, let's just keep the tick. It's one widget.
    // To properly follow "Idle visual rule", we should probably return TRUE always.
    // BUT "Stopping the tick: Only keep tick callback active while... breathing idle animation is enabled".
    // "Preferred: keep tick but ensure it’s low overhead".
    
    needs_tick = TRUE; // Always run for breathing
    
    if (!needs_tick) {
        gtk_widget_remove_tick_callback(priv->widget, priv->tick_id);
        priv->tick_id = 0;
    }
}

// --- Public API ---

static void dark_mode_button_free_priv(gpointer data) {
    DarkModePriv* priv = (DarkModePriv*)data;
    
    // Stop tick callback first
    if (priv->tick_id > 0) {
        gtk_widget_remove_tick_callback(priv->widget, priv->tick_id);
        priv->tick_id = 0;
    }
    
    // Clean up particles
    if (priv->hover_particles) g_array_free(priv->hover_particles, TRUE);
    if (priv->active_bursts) g_list_free_full(priv->active_bursts, free_burst);
    
    // Properly destroy overlay window
    if (priv->overlay_window) {
        // Hide first to prevent visual glitches
        gtk_widget_set_visible(priv->overlay_window, FALSE);
        // Destroy the window (this will also destroy overlay_area as its child)
        gtk_window_destroy(GTK_WINDOW(priv->overlay_window));
        priv->overlay_window = NULL;
        priv->overlay_area = NULL;
    }
    
    g_free(priv);
}

static void on_unrealize(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    DarkModePriv* priv = (DarkModePriv*)user_data;
    // Early cleanup of resources that depend on GdkSurface or window hierarchy
    
    // Stop tick callback immediately
    if (priv->tick_id > 0) {
        gtk_widget_remove_tick_callback(priv->widget, priv->tick_id);
        priv->tick_id = 0;
    }
    
    // Destroy overlay window while we still have context
    if (priv->overlay_window) {
        gtk_widget_set_visible(priv->overlay_window, FALSE);
        gtk_window_destroy(GTK_WINDOW(priv->overlay_window));
        priv->overlay_window = NULL;
        priv->overlay_area = NULL;
    }
}

GtkWidget* dark_mode_button_new(void) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, BUTTON_SIZE, BUTTON_SIZE);
    gtk_widget_set_margin_start(area, 4);
    gtk_widget_set_margin_end(area, 4);
    
    DarkModePriv* priv = g_new0(DarkModePriv, 1);
    priv->widget = area;
    priv->is_dark = FALSE; // Start Light Mode
    priv->enable_hearts = TRUE; // Default ON
    priv->hover_particles = g_array_new(FALSE, FALSE, sizeof(Particle));
    priv->breathing_time_base = g_random_double_range(0, 100); // Random phase
    
    gtk_widget_set_margin_start(area, BUTTON_MARGIN);
    gtk_widget_set_margin_end(area, BUTTON_MARGIN);
    
    g_object_set_data_full(G_OBJECT(area), "dark_mode_priv", priv, dark_mode_button_free_priv);
    
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, priv, NULL);
    
    // Input Controllers
    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), priv);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));
    
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_enter), priv);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), priv);
    gtk_widget_add_controller(area, motion);
    
    // Tooltip
    gtk_widget_set_tooltip_text(area, "Switch to Dark Mode");
    
    // Start Ticking (for breathing)
    start_tick(priv);
    
    // Connect unrealize for safe early cleanup
    g_signal_connect(area, "unrealize", G_CALLBACK(on_unrealize), priv);
    
    return area;
}

gboolean dark_mode_button_is_dark(GtkWidget* button) {
    DarkModePriv* priv = (DarkModePriv*)g_object_get_data(G_OBJECT(button), "dark_mode_priv");
    if (!priv) return FALSE;
    return priv->is_dark;
}

#include "dark_mode_button.h"
#include <math.h>
#include <stdlib.h>

// --- Configuration Constants ---

// Shape
#define CORNER_ROUNDNESS 0.35 // 0.0=square, 1.0=circle
#define BUTTON_SIZE 36        // Matches typical header bar button size

// Icon
#define ICON_PADDING 8.0
#define ICON_LINE_WIDTH 1.5
#define ICON_GLOW_ALPHA_IDLE 0.15
#define ICON_GLOW_ALPHA_ANIM 0.4
#define BREATHING_AMP 0.05
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

#define HEARTS_CLICK_BURST_COUNT 12
#define HEARTS_CLICK_BURST_RADIUS 20.0
#define HEARTS_LIFETIME_SEC 2.5
#define HEARTS_HOVER_RATE_PER_SEC 3.0
#define HEARTS_HOVER_MAX_COUNT 10 // Max concurrent concurrent hover hearts to prevent overload

#define HEARTS_MIN_SIZE 4.0
#define HEARTS_MAX_SIZE 9.0
#define HEARTS_SPEED_MIN 10.0
#define HEARTS_SPEED_MAX 30.0

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
    // Widget Reference (for queue_draw)
    GtkWidget* widget;
    
    // State
    gboolean is_dark;
    gboolean is_hovered;
    
    // Animation State
    gboolean anim_running;
    double anim_start_time; // monotonic seconds
    double anim_progress;   // 0.0 to 1.0
    
    // Breathing State
    double breathing_time_base; // offset to keep breathing smooth
    
    // Particles
    GArray* particles;
    double last_hover_emit_time;
    gboolean click_burst_active;
    
    // Tick Callback ID
    guint tick_id;
    
} DarkModePriv;

// --- Forward Declarations ---

static void dark_mode_button_free_priv(gpointer data);
static gboolean on_tick(GtkWidget* widget, GdkFrameClock* frame_clock, gpointer user_data);
static void start_tick(DarkModePriv* priv);
static void stop_tick_if_idle(DarkModePriv* priv);
static void spawn_click_burst(DarkModePriv* priv);
static void update_particles(DarkModePriv* priv, double current_time, double dt);
static void draw_particles(cairo_t* cr, DarkModePriv* priv, double current_time);
static void draw_icon(cairo_t* cr, double w, double h, gboolean is_dark, double progress, double breathing_scale);

// --- Helper Functions ---

static double get_monotonic_time(void) {
    return g_get_monotonic_time() / 1000000.0;
}

static double ease_in_out(double t) {
    // Cubic ease in out
    return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2;
}

static double map_range(double val, double in_min, double in_max, double out_min, double out_max) {
    return out_min + (out_max - out_min) * ((val - in_min) / (in_max - in_min));
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
    
    // Spawn Click Burst
    if (HEARTS_ENABLED_CLICK && !priv->click_burst_active) {
        spawn_click_burst(priv);
        priv->click_burst_active = TRUE; // Reset when burst finishes? 
        // Logic: "ignore new click bursts until current burst finishes".
        // Since we can re-click after animation (0.9s) but hearts last 3s,
        // we need to track if particles are still alive from the burst.
        // Simplified: just reset flag when particle count drops to 0 or similar.
        // Actually, let's just use a simple flag that clears after lifetime duration.
    }
    
    start_tick(priv);
}

// --- Drawing ---

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    DarkModePriv* priv = (DarkModePriv*)user_data;
    double current_time = get_monotonic_time();
    
    // 1. Draw Background (Optional / Debug)
    // Design says "Idle: neutral". We can draw a very faint background or none.
    // Let's draw a subtle rounded rect to show interactivity area if needed.
    // Prompt says: "Idle visual rule: button should look neutral."
    // Maybe no background is best, just the icon. The headerbar usually provides button backgrounds on hover,
    // but we disabled traditional hover. So transparent background.
    
    // 2. Draw Particles (underneath or on top? "Aura" implies around/under, "Burst" implies on top.
    // Let's draw them UNDER the icon for the aura feel, but they naturally overlay for burst.
    // Drawing them first puts them behind the icon.
    draw_particles(cr, priv, current_time);
    
    // 3. Draw Icon
    double breathing_scale = 1.0;
    if (!priv->anim_running) {
        double t = current_time + priv->breathing_time_base;
        double phase = (2 * M_PI * t) / BREATHING_PERIOD_SEC;
        breathing_scale = 1.0 + BREATHING_AMP * sin(phase);
    }
    
    // Determine effective visual state and progress
    // During animation: morph from current state to NEXT state.
    // If priv->is_dark == FALSE (Sun), and we animate, we go to Moon.
    // Target state is !priv->is_dark.
    
    gboolean start_is_dark = priv->is_dark;
    double progress = 0.0;
    
    if (priv->anim_running) {
        progress = priv->anim_progress; // 0..1
        // We always draw "Start Icon" -> "End Icon" by parameterizing draw_icon logic
        // But draw_icon handles specific states.
        // Let's pass the "from" state and the progress towards "other" state.
    }
    
    draw_icon(cr, width, height, start_is_dark, progress, breathing_scale);

    // Debug: Frame count or similar? No.
}

static void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

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

static void draw_particles(cairo_t* cr, DarkModePriv* priv, double current_time) {
    if (!priv->particles || priv->particles->len == 0) return;
    
    for (guint i = 0; i < priv->particles->len; i++) {
        Particle* p = &g_array_index(priv->particles, Particle, i);
        double age = current_time - p->spawn_time;
        if (age < 0) age = 0;
        double life_pct = age / p->lifetime;
        
        if (life_pct >= 1.0) continue;
        
        // Alpha fade out
        double alpha = HEARTS_COLOR_A * (1.0 - (life_pct * life_pct)); // Ease out fade
        
        cairo_save(cr);
        
        cairo_translate(cr, p->x, p->y);
        cairo_rotate(cr, p->rotation);
        
        // Color
        cairo_set_source_rgba(cr, HEARTS_COLOR_R, HEARTS_COLOR_G, HEARTS_COLOR_B, alpha);
        
        // Draw Heart
        draw_heart_shape(cr, 0, 0, p->size);
        cairo_fill_preserve(cr);
        
        // Subtle stroke/glow
        cairo_set_source_rgba(cr, HEARTS_COLOR_R, HEARTS_COLOR_G, HEARTS_COLOR_B, alpha * 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        
        cairo_restore(cr);
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

static void spawn_particle(DarkModePriv* priv, gboolean is_burst) {
    if (!priv->particles) return;
    
    Particle p;
    p.spawn_time = get_monotonic_time();
    p.lifetime = HEARTS_LIFETIME_SEC;
    
    // Random params
    double angle = g_random_double_range(0, 2 * M_PI);
    double dist = is_burst ? g_random_double_range(0, HEARTS_CLICK_BURST_RADIUS) : 0;
    
    // Start pos relative to center (0,0)
    double r_start = is_burst ? 5.0 : 12.0; // Burst starts close, Hover starts further out
    
    p.x = cos(angle) * r_start;
    p.y = sin(angle) * r_start;
    
    double speed = g_random_double_range(HEARTS_SPEED_MIN, HEARTS_SPEED_MAX);
    p.vx = cos(angle) * speed;
    p.vy = sin(angle) * speed;
    
    p.size = g_random_double_range(HEARTS_MIN_SIZE, HEARTS_MAX_SIZE);
    p.rotation = g_random_double_range(-0.5, 0.5);
    
    g_array_append_val(priv->particles, p);
}

static void spawn_click_burst(DarkModePriv* priv) {
    for (int i = 0; i < HEARTS_CLICK_BURST_COUNT; i++) {
        // Custom variant of spawn for burst
        Particle p;
        p.spawn_time = get_monotonic_time();
        p.lifetime = HEARTS_LIFETIME_SEC;
        
        double angle = g_random_double_range(0, 2 * M_PI);
        double speed = g_random_double_range(HEARTS_SPEED_MIN * 2.0, HEARTS_SPEED_MAX * 2.0); // Faster burst
        
        p.x = 0; // Start at center
        p.y = 0; 
        
        p.vx = cos(angle) * speed;
        p.vy = sin(angle) * speed;
        
        p.size = g_random_double_range(HEARTS_MIN_SIZE, HEARTS_MAX_SIZE);
        p.rotation = g_random_double_range(-0.5, 0.5);
        
        g_array_append_val(priv->particles, p);
    }
}

static void update_particles(DarkModePriv* priv, double current_time, double dt) {
    if (!priv->particles) return;
    
    for (guint i = 0; i < priv->particles->len; i++) {
        Particle* p = &g_array_index(priv->particles, Particle, i);
        
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        
        // Remove dead
        if (current_time - p->spawn_time > p->lifetime) {
            g_array_remove_index_fast(priv->particles, i);
            i--; // Adjust index
        }
    }
    
    // Check burst flag reset
    if (priv->click_burst_active && priv->particles->len == 0) {
        priv->click_burst_active = FALSE;
    }
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    DarkModePriv* priv = (DarkModePriv*)user_data;
    double current_time = get_monotonic_time();
    
    // 1. Draw Background (Optional)
    // No background
    
    // 2. Draw Particles (Centered)
    cairo_save(cr);
    cairo_translate(cr, width / 2.0, height / 2.0);
    draw_particles(cr, priv, current_time);
    cairo_restore(cr);
    
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
    DarkModePriv* priv = (DarkModePriv*)user_data;
    double current_time = get_monotonic_time();
    // Use frame time ideally but helper is fine? 
    // gdk_frame_clock_get_frame_time returns microseconds.
    // Let's use that for consistency.
    gint64 frame_time = gdk_frame_clock_get_frame_time(frame_clock);
    double now = frame_time / 1000000.0;
    
    // double dt = ...; // Not strictly needed if we use absolute time for anims, but needed for particles integration.
    static double last_time = 0;
    double dt = (last_time == 0) ? 0.016 : (now - last_time);
    last_time = now;
    
    // 1. Update Animation
    if (priv->anim_running) {
        double elapsed = now - priv->anim_start_time;
        double duration = TOGGLE_ANIM_DURATION_MS / 1000.0;
        
        if (elapsed >= duration) {
            priv->anim_progress = 1.0;
            priv->anim_running = FALSE;
            // Commit State Change
            priv->is_dark = !priv->is_dark;
            
            // Accessibility update
            gtk_accessible_update_property(GTK_ACCESSIBLE(widget),
                GTK_ACCESSIBLE_PROPERTY_LABEL, priv->is_dark ? "Switch to Light Mode" : "Switch to Dark Mode",
                -1);

        } else {
            priv->anim_progress = elapsed / duration;
        }
    }
    
    // 2. Hover Emission
    if (priv->is_hovered && HEARTS_ENABLED_HOVER) {
        if (now - priv->last_hover_emit_time > (1.0 / HEARTS_HOVER_RATE_PER_SEC)) {
            // Check max count to avoid flooding
            if (priv->particles->len < HEARTS_HOVER_MAX_COUNT + (priv->click_burst_active ? HEARTS_CLICK_BURST_COUNT : 0)) {
                // Spawn one
                Particle p;
                p.spawn_time = now;
                p.lifetime = HEARTS_LIFETIME_SEC;
                p.size = g_random_double_range(HEARTS_MIN_SIZE, HEARTS_MAX_SIZE * 0.8);
                p.rotation = g_random_double_range(-0.5, 0.5);
                
                // Random pos around center
                double angle = g_random_double_range(0, 2*M_PI);
                double r = g_random_double_range(10, 18);
                p.x = cos(angle) * r;
                p.y = sin(angle) * r;
                
                // Slow outward draft
                double speed = g_random_double_range(5, 15);
                p.vx = cos(angle) * speed;
                p.vy = sin(angle) * speed;
                
                g_array_append_val(priv->particles, p);
                priv->last_hover_emit_time = now;
            }
        }
    }
    
    // 3. Update Particles
    update_particles(priv, now, dt);
    
    // 4. Request Redraw
    gtk_widget_queue_draw(widget);
    
    // 5. Check if we should stop
    stop_tick_if_idle(priv);
    
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
    if (priv->particles && priv->particles->len > 0) needs_tick = TRUE;
    
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
    if (priv->particles) g_array_free(priv->particles, TRUE);
    g_free(priv);
}

GtkWidget* dark_mode_button_new(void) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, BUTTON_SIZE, BUTTON_SIZE);
    gtk_widget_set_margin_start(area, 4);
    gtk_widget_set_margin_end(area, 4);
    
    DarkModePriv* priv = g_new0(DarkModePriv, 1);
    priv->widget = area;
    priv->is_dark = FALSE; // Start Light Mode
    priv->particles = g_array_new(FALSE, FALSE, sizeof(Particle));
    priv->breathing_time_base = g_random_double_range(0, 100); // Random phase
    
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
    
    return area;
}

gboolean dark_mode_button_is_dark(GtkWidget* button) {
    DarkModePriv* priv = (DarkModePriv*)g_object_get_data(G_OBJECT(button), "dark_mode_priv");
    if (!priv) return FALSE;
    return priv->is_dark;
}

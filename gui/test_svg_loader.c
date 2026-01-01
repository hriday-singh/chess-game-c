// Test file for SVG piece loading using GdkPixbuf (no extra dependencies needed)
// GdkPixbuf can load SVGs if librsvg is available, but we'll use it through GTK4

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test loading and rendering an SVG piece using GdkPixbuf
static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    const char* svg_path = (const char*)user_data;
    
    if (!svg_path) {
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);
        return;
    }
    
    // Load SVG using GdkPixbuf (supports SVG if librsvg is available)
    GError* error = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(svg_path, &error);
    
    if (!pixbuf) {
        fprintf(stderr, "Error loading SVG: %s\n", error ? error->message : "Unknown error");
        if (error) {
            fprintf(stderr, "Note: SVG support requires librsvg. Install with: pacman -S mingw-w64-x86_64-librsvg\n");
            g_error_free(error);
        }
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);
        return;
    }
    
    fprintf(stderr, "[DEBUG] on_draw: getting pixbuf dimensions\n");
    // Get pixbuf dimensions
    int pixbuf_width = gdk_pixbuf_get_width(pixbuf);
    int pixbuf_height = gdk_pixbuf_get_height(pixbuf);
    fprintf(stderr, "[DEBUG] on_draw: pixbuf dimensions: %dx%d\n", pixbuf_width, pixbuf_height);
    
    // Scale to fit while maintaining aspect ratio
    double scale_x = (double)width / pixbuf_width;
    double scale_y = (double)height / pixbuf_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;
    
    int scaled_width = (int)(pixbuf_width * scale);
    int scaled_height = (int)(pixbuf_height * scale);
    int offset_x = (width - scaled_width) / 2;
    int offset_y = (height - scaled_height) / 2;
    
    fprintf(stderr, "[DEBUG] on_draw: saving cairo context\n");
    cairo_save(cr);
    fprintf(stderr, "[DEBUG] on_draw: translating and scaling\n");
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale, scale);
    
    fprintf(stderr, "[DEBUG] on_draw: getting pixbuf data\n");
    // Render pixbuf to Cairo context (modern GTK4 way)
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    fprintf(stderr, "[DEBUG] on_draw: rowstride=%d, n_channels=%d, has_alpha=%d, pixels=%p\n", 
            rowstride, n_channels, has_alpha, (void*)pixels);
    
    fprintf(stderr, "[DEBUG] on_draw: converting to ARGB format\n");
    // Convert pixbuf to ARGB32 format for Cairo
    cairo_format_t format = has_alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    int cairo_stride = cairo_format_stride_for_width(format, pixbuf_width);
    fprintf(stderr, "[DEBUG] on_draw: cairo_stride=%d, allocating %d bytes\n", cairo_stride, cairo_stride * pixbuf_height);
    guchar* cairo_data = g_malloc(cairo_stride * pixbuf_height);
    fprintf(stderr, "[DEBUG] on_draw: cairo_data allocated at %p\n", (void*)cairo_data);
    
    fprintf(stderr, "[DEBUG] on_draw: converting pixels\n");
    // Convert RGBA/RGB to ARGB
    for (int y = 0; y < pixbuf_height; y++) {
        guchar* src_row = pixels + y * rowstride;
        guint32* dst_row = (guint32*)(cairo_data + y * cairo_stride);
        
        for (int x = 0; x < pixbuf_width; x++) {
            guchar r = src_row[x * n_channels + 0];
            guchar g = src_row[x * n_channels + 1];
            guchar b = src_row[x * n_channels + 2];
            guchar a = has_alpha ? src_row[x * n_channels + 3] : 255;
            
            // ARGB32 format: AARRGGBB (little-endian)
            dst_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    
    fprintf(stderr, "[DEBUG] on_draw: creating cairo surface\n");
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        cairo_data, format, pixbuf_width, pixbuf_height, cairo_stride);
    fprintf(stderr, "[DEBUG] on_draw: surface created at %p\n", (void*)surface);
    
    if (surface) {
        cairo_status_t status = cairo_surface_status(surface);
        fprintf(stderr, "[DEBUG] on_draw: surface status=%d\n", status);
        if (status == CAIRO_STATUS_SUCCESS) {
            fprintf(stderr, "[DEBUG] on_draw: setting source and painting\n");
            cairo_set_source_surface(cr, surface, 0, 0);
            cairo_paint(cr);
            fprintf(stderr, "[DEBUG] on_draw: paint complete\n");
            // Clear the source before destroying surface
            cairo_set_source_rgba(cr, 0, 0, 0, 0);
        } else {
            fprintf(stderr, "Cairo surface error: %s\n", cairo_status_to_string(status));
        }
    }
    
    fprintf(stderr, "[DEBUG] on_draw: restoring cairo context\n");
    cairo_restore(cr);
    fprintf(stderr, "[DEBUG] on_draw: context restored\n");
    
    fprintf(stderr, "[DEBUG] on_draw: destroying surface\n");
    if (surface) {
        cairo_surface_destroy(surface);
    }
    fprintf(stderr, "[DEBUG] on_draw: surface destroyed\n");
    
    fprintf(stderr, "[DEBUG] on_draw: freeing cairo_data\n");
    g_free(cairo_data);
    fprintf(stderr, "[DEBUG] on_draw: cairo_data freed\n");
    
    fprintf(stderr, "[DEBUG] on_draw: unreffing pixbuf\n");
    g_object_unref(pixbuf);
    fprintf(stderr, "[DEBUG] on_draw: pixbuf unreffed\n");
    fprintf(stderr, "[DEBUG] on_draw: done\n");
}

static void create_window(GtkApplication* app, const char* svg_path) {
    fprintf(stderr, "[DEBUG] create_window: entry, app=%p, svg_path=%p\n", (void*)app, (void*)svg_path);
    if (!app || !svg_path) {
        fprintf(stderr, "[DEBUG] create_window: NULL check failed\n");
        return;
    }
    fprintf(stderr, "[DEBUG] create_window: svg_path='%s'\n", svg_path);
    
    fprintf(stderr, "[DEBUG] create_window: creating window\n");
    // Create window
    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "SVG Loader Test");
    gtk_window_set_default_size(GTK_WINDOW(window), 200, 200);
    gtk_window_set_application(GTK_WINDOW(window), app);
    
    fprintf(stderr, "[DEBUG] create_window: creating drawing area\n");
    // Create drawing area
    GtkWidget* drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(drawing_area), 200);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(drawing_area), 200);
    
    fprintf(stderr, "[DEBUG] create_window: duplicating path\n");
    // Make a copy of the path string for the draw function (will be freed when drawing area is destroyed)
    char* path_copy = g_strdup(svg_path);
    fprintf(stderr, "[DEBUG] create_window: path_copy='%s' at %p\n", path_copy, (void*)path_copy);
    
    fprintf(stderr, "[DEBUG] create_window: setting draw func\n");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, path_copy, (GDestroyNotify)g_free);
    
    fprintf(stderr, "[DEBUG] create_window: setting child\n");
    gtk_window_set_child(GTK_WINDOW(window), drawing_area);
    
    fprintf(stderr, "[DEBUG] create_window: showing window\n");
    gtk_widget_set_visible(window, TRUE);
    fprintf(stderr, "[DEBUG] create_window: done\n");
}

static void on_activate(GtkApplication* app, gpointer user_data) {
    (void)app;
    (void)user_data;
    // Won't be called when files are opened
}

static void on_open(GtkApplication* app, GFile** files, int n_files, const char* hint, gpointer user_data) {
    (void)hint;
    (void)user_data;
    
    fprintf(stderr, "[DEBUG] on_open: entry, n_files=%d\n", n_files);
    if (n_files < 1) {
        fprintf(stderr, "[DEBUG] on_open: no files\n");
        return;
    }
    
    fprintf(stderr, "[DEBUG] on_open: getting file path\n");
    char* svg_path = g_file_get_path(files[0]);
    fprintf(stderr, "[DEBUG] on_open: svg_path=%p\n", (void*)svg_path);
    if (svg_path) {
        fprintf(stderr, "[DEBUG] on_open: svg_path='%s'\n", svg_path);
        create_window(app, svg_path);
        fprintf(stderr, "[DEBUG] on_open: freeing svg_path\n");
        g_free(svg_path);
    }
    fprintf(stderr, "[DEBUG] on_open: done\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <svg_file_path>\n", argv[0]);
        fprintf(stderr, "Example: %s assets/images/piece/alpha/wN.svg\n", argv[0]);
        fprintf(stderr, "\nNote: SVG support requires librsvg. Install with:\n");
        fprintf(stderr, "  pacman -S mingw-w64-x86_64-librsvg\n");
        return 1;
    }
    
    GtkApplication* app = gtk_application_new("com.chessgame.svgtest", G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}

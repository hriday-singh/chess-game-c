#include "board_widget.h"
#include "sound_engine.h"
#include "theme_data.h"
#include "../game/gamelogic.h"
#include "../game/move.h"
#include "../game/piece.h"
#include "../game/types.h"
#include "promotion_dialog.h"
#include <gtk/gtk.h>
#include <graphene.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

// Debug flag - disabled
#define DEBUG_BOARD 0

#if DEBUG_BOARD
#define DBG_PRINT(...) fprintf(stderr, "[BOARD] " __VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

// Define M_PI if not available (C standard doesn't require it)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Board widget state
typedef struct {
    GameLogic* logic;
    ThemeData* theme;  // Theme data for board colors
    GtkWidget* grid;
    GtkWidget* squares[8][8];  // Drawing areas for each square
    GtkWidget* dots[8][8];     // Overlay widgets for move indicators (dots)
    int selectedRow;
    int selectedCol;
    Move** validMoves;  // Array of valid moves for selected piece
    int validMovesCount;
    bool useDots;       // Show dots for legal moves (Chess.com style)
    bool animationsEnabled;
    bool dragMode;      // true = drag-and-drop, false = click-to-move
    // Drag state
    bool isDragging;        // True only if actually dragging (mouse moved while holding)
    bool dragPrepared;      // True if button pressed on piece (but mouse hasn't moved yet)
    int dragSourceRow;
    int dragSourceCol;
    double dragX;
    double dragY;
    double pressStartX;     // Initial press position to detect if mouse moved
    double pressStartY;
    Piece* draggedPiece;
    // Animation state
    bool isAnimating;
    Move* animatingMove;
    Piece* animatingPiece;  // Store piece being animated (before move executes)
    double animProgress;  // 0.0 to 1.0
    guint animTickId;    // Frame tick callback ID
    gint64 animStartTime; // Start time for current animation (microseconds)
    bool animatingFromDrag;  // True if animating from drag position (not from square)
    double animStartX, animStartY;  // Start position for drag drop animation (pixel coords)
    GtkWidget* animOverlay;  // Overlay for animated piece
    GtkCssProvider* cssProvider;  // Single shared CSS provider for efficiency
    bool flipped;  // True if board is flipped (black's perspective)
    
    // Tutorial fields
    bool restrictMoves;
    int allowedStartRow;
    int allowedStartCol;
    int allowedEndRow;
    int allowedEndrow;
    int allowedEndCol;
    bool showTutorialHighlights;
    // Callback for invalid moves (tutorial feedback)
    BoardInvalidMoveCallback invalidMoveCb;
    void* invalidMoveData;
} BoardWidget;

// Cache management moved to ThemeData


// Callback for delayed move sound (when animations are enabled)
// This plays the move sound 200ms into the animation for regular moves only
static gboolean delayed_move_sound_callback(gpointer user_data) {
    (void)user_data;  // Unused parameter
    sound_engine_play(SOUND_MOVE);
    return G_SOURCE_REMOVE;  // One-shot timer
}

// Play appropriate sound for a move (non-blocking, lightweight)
static void play_move_sound(BoardWidget* board, Move* move) {
    if (!board || !move || !board->logic) return;
    
    // Check game end states first (highest priority)
    if (gamelogic_is_checkmate(board->logic, PLAYER_WHITE) || 
        gamelogic_is_checkmate(board->logic, PLAYER_BLACK)) {
        // Determine winner
        Player winner = gamelogic_is_checkmate(board->logic, PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
        if (winner == board->logic->playerSide) {
            sound_engine_play(SOUND_WIN);
        } else {
            sound_engine_play(SOUND_DEFEAT);
        }
        return;
    }
    
    if (gamelogic_is_stalemate(board->logic, PLAYER_WHITE) || 
        gamelogic_is_stalemate(board->logic, PLAYER_BLACK)) {
        sound_engine_play(SOUND_DRAW);
        return;
    }
    
    // Check for check (after move, check the opponent)
    Player opponent = (board->logic->turn == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
    if (gamelogic_is_in_check(board->logic, opponent)) {
        sound_engine_play(SOUND_CHECK);
        return;
    }
    
    // Check move type
    if (move->isCastling) {
        sound_engine_play(SOUND_CASTLES);
    } else if (move->capturedPiece != NULL || move->isEnPassant) {
        sound_engine_play(SOUND_CAPTURE);
    } else {
        // Regular move - play immediately (delay is handled in animate_move if animations are enabled)
        sound_engine_play(SOUND_MOVE);
    }
}

// Convert visual coordinates to logical coordinates (handle board flipping)
static void visual_to_logical(BoardWidget* board, int visualR, int visualC, int* logicalR, int* logicalC) {
    if (board->flipped) {
        *logicalR = 7 - visualR;
        *logicalC = 7 - visualC;
    } else {
        *logicalR = visualR;
        *logicalC = visualC;
    }
}

// Convert logical coordinates to visual coordinates (handle board flipping)
static void logical_to_visual(BoardWidget* board, int logicalR, int logicalC, int* visualR, int* visualC) {
    if (board->flipped) {
        *visualR = 7 - logicalR;
        *visualC = 7 - logicalC;
    } else {
        *visualR = logicalR;
        *visualC = logicalC;
    }
}

// Check if square is in check
static bool is_square_in_check(BoardWidget* board, int r, int c) {
    Piece* piece = board->logic->board[r][c];
    if (!piece || piece->type != PIECE_KING) return false;
    extern bool gamelogic_is_in_check(GameLogic* logic, Player player);
    return gamelogic_is_in_check(board->logic, piece->owner);
}

// Check if square is part of last move
static bool is_last_move_square(BoardWidget* board, int r, int c) {
    Move* lastMove = gamelogic_get_last_move(board->logic);
    if (!lastMove) return false;
    return ((r == lastMove->startRow && c == lastMove->startCol) ||
            (r == lastMove->endRow && c == lastMove->endCol));
}

// Forward declarations (must be before any usage)
static void animate_move(BoardWidget* board, Move* move, void (*on_finished)(void));
static void animate_return_piece(BoardWidget* board);
static void on_drag_press(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
static void on_grid_motion(GtkEventControllerMotion* motion, double x, double y, gpointer user_data);

// Helper to draw piece from cache or fallback to text
static void draw_piece_graphic(cairo_t* cr, BoardWidget* board, PieceType type, Player owner, 
                              double x, double y, double size, double opacity) {
    if (!board || !board->theme) return;

    // Try to draw cached SVG
    // Check if we have a cached surface in ThemeData
    cairo_surface_t* surface = theme_data_get_piece_surface(board->theme, type, owner);
    
    if (surface) {
        // Draw SVG surface
        
        // Calculate scale to fit piece in square
        // We rendered at 256px max height
        // Square size is `size`
        // We want piece to be ~80% of square size? Or full size?
        
        int surf_w = cairo_image_surface_get_width(surface);
        int surf_h = cairo_image_surface_get_height(surface);
        
        double scale_x = (double)size / surf_w;
        double scale_y = (double)size / surf_h;
        double scale = scale_x < scale_y ? scale_x : scale_y;
        
        // Center it around x, y
        double draw_w = surf_w * scale;
        double draw_h = surf_h * scale;
        
        // x, y are center. Top-left is:
        double top_left_x = x - draw_w / 2.0;
        double top_left_y = y - draw_h / 2.0;
        
        cairo_save(cr);
        cairo_translate(cr, top_left_x, top_left_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        
        return;
    }
    
    // Fallback to text rendering
    const char* symbol = theme_data_get_piece_symbol(board->theme, type, owner);
    const char* font_family = theme_data_get_font_name(board->theme);
    if (!font_family) font_family = "Segoe UI Symbol, DejaVu Sans, Sans";
    
    if (!symbol) symbol = "?";
    
    cairo_save(cr);
    
    if (opacity < 1.0) {
        // For drag transparency
        cairo_push_group(cr);
    }

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, font_family);
    pango_font_description_set_size(desc, (int)(size * 0.7 * PANGO_SCALE)); // Adjust relative size to match image
    pango_font_description_set_weight(desc, PANGO_WEIGHT_SEMIBOLD);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, symbol, -1);
    
    int text_width, text_height;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);
    double px = x - text_width / 2.0;
    double py = y - text_height / 2.0;
    
    cairo_move_to(cr, round(px), round(py));
    
    if (owner == PLAYER_WHITE) {
        double r, g, b, sr, sg, sb, cw;
        theme_data_get_white_piece_color(board->theme, &r, &g, &b);
        theme_data_get_white_piece_stroke(board->theme, &sr, &sg, &sb);
        cw = theme_data_get_white_stroke_width(board->theme);
        
        cairo_set_source_rgb(cr, r, g, b);
        pango_cairo_layout_path(cr, layout);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, sr, sg, sb);
        cairo_set_line_width(cr, cw);
        cairo_stroke(cr);
    } else {
        double r, g, b, sr, sg, sb, cw;
        theme_data_get_black_piece_color(board->theme, &r, &g, &b);
        theme_data_get_black_piece_stroke(board->theme, &sr, &sg, &sb);
        cw = theme_data_get_black_stroke_width(board->theme);
        
        cairo_set_source_rgb(cr, r, g, b);
        pango_cairo_layout_path(cr, layout);
        cairo_fill_preserve(cr);
        if (cw > 0.0) {
            cairo_set_source_rgb(cr, sr, sg, sb);
            cairo_set_line_width(cr, cw);
            cairo_stroke(cr);
        } else {
            cairo_new_path(cr);
        }
    }
    
    pango_font_description_free(desc);
    g_object_unref(layout);
    
    if (opacity < 1.0) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, opacity);
    }
    
    cairo_restore(cr);
}

// Draw animated/dragged piece overlay (called during animation or drag)
static void draw_animated_piece(GtkDrawingArea* overlay, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)overlay;
    BoardWidget* board = (BoardWidget*)user_data;
    
    // Set up Cairo
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);
    
    // Draw animated piece
    if (board->isAnimating && board->animatingMove && board->animatingPiece) {
        Move* move = board->animatingMove;
        Piece* piece = board->animatingPiece;
        
        if (piece) {
            int visualStartR, visualStartC, visualEndR, visualEndC;
            logical_to_visual(board, move->startRow, move->startCol, &visualStartR, &visualStartC);
            logical_to_visual(board, move->endRow, move->endCol, &visualEndR, &visualEndC);
            
            double startX = (visualStartC + 0.5) * (width / 8.0);
            double startY = (visualStartR + 0.5) * (height / 8.0);
            double endX = (visualEndC + 0.5) * (width / 8.0);
            double endY = (visualEndR + 0.5) * (height / 8.0);
            
            double t = board->animProgress;
            if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
            double eased = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
            
            double x = startX + (endX - startX) * eased;
            double y = startY + (endY - startY) * eased;
            
            double pieceSize = (width / 8.0);
            draw_piece_graphic(cr, board, piece->type, piece->owner, x, y, pieceSize * 0.85, 1.0);
        }
    }
    
    // Draw dragged piece
    if (board->isDragging && board->draggedPiece) {
        double x = board->dragX;
        double y = board->dragY;
        
        double pieceSize = (width / 8.0);
        draw_piece_graphic(cr, board, board->draggedPiece->type, board->draggedPiece->owner, x, y, pieceSize * 0.85, 0.85);
    }
}

// Draw a square with piece, highlighting, and dots
static void draw_square(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    BoardWidget* board = (BoardWidget*)user_data;
    
    // Safety check - if width/height are 0 or invalid, don't draw
    if (width <= 0 || height <= 0 || !board || !board->logic) {
        return;
    }
    
    int visualR = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "row"));
    int visualC = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "col"));
    
    // Convert visual coordinates to logical coordinates
    int r, c;
    visual_to_logical(board, visualR, visualC, &r, &c);
    
    Piece* piece = board->logic->board[r][c];
    // For isLight, use visual coordinates (what user sees)
    bool isLight = ((visualR + visualC) % 2 == 0);
    
    // Hide piece if it's being animated or dragged
    bool hidePiece = false;
    if (board->isAnimating && board->animatingMove) {
        Move* move = board->animatingMove;
        if ((r == move->startRow && c == move->startCol) ||
            (r == move->endRow && c == move->endCol)) {
            hidePiece = true;
        }
    }
    // Hide piece if it's being dragged (source square)
    if (board->isDragging && board->dragSourceRow == r && board->dragSourceCol == c) {
        hidePiece = true;
    }
    
    // Draw background color
    if (board->selectedRow == r && board->selectedCol == c) {
        // Selected square - yellow highlight
        cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.7);
    } else if (piece && piece->type == PIECE_KING && is_square_in_check(board, r, c)) {
        // King in check - darker red highlight
        cairo_set_source_rgba(cr, 0.7, 0.0, 0.0, 0.85);
    } else if (isLight) {
        // Light square - use theme data if available, otherwise default
        if (board->theme) {
            double r, g, b;
            theme_data_get_light_square_color(board->theme, &r, &g, &b);
            cairo_set_source_rgb(cr, r, g, b);
        } else {
            cairo_set_source_rgb(cr, 0.961, 0.871, 0.730); // Default fallback
        }
    } else {
        // Dark square - use theme data if available, otherwise default
        if (board->theme) {
            double r, g, b;
            theme_data_get_dark_square_color(board->theme, &r, &g, &b);
            cairo_set_source_rgb(cr, r, g, b);
        } else {
            cairo_set_source_rgb(cr, 0.710, 0.533, 0.388); // Default fallback
        }
    }
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    
    // Check if valid move destination (for selected piece OR dragged piece)
    bool isValidDest = false;
    bool isCapture = false;
    int sourceRow = board->selectedRow >= 0 ? board->selectedRow : board->dragSourceRow;
    int sourceCol = board->selectedRow >= 0 ? board->selectedCol : board->dragSourceCol;
    
    if (sourceRow >= 0 && sourceCol >= 0) {
        for (int i = 0; i < board->validMovesCount; i++) {
            if (board->validMoves[i] && 
                board->validMoves[i]->startRow == sourceRow &&
                board->validMoves[i]->startCol == sourceCol &&
                board->validMoves[i]->endRow == r && 
                board->validMoves[i]->endCol == c) {
                isValidDest = true;
                if (piece != NULL) isCapture = true;
                break;
            }
        }
    }
    
    // Draw previous move highlight (yellow tint) - BEFORE valid move indicators
    // So that capture indicators (red) can take over the yellow
    // DISABLE in tutorial mode if requested
    bool isLastMoveSquare = is_last_move_square(board, r, c);
    if (isLastMoveSquare && !board->restrictMoves) {
        double blend = isLight ? 0.70 : 0.55;
        cairo_set_source_rgba(cr, 1.0, 1.0, 0.31, blend); // Yellow tint
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);
    }
    
    // Tutorial Highlights
    if (board->restrictMoves && board->showTutorialHighlights) {
        // Draw Source Green Marker
        if (r == board->allowedStartRow && c == board->allowedStartCol) {
            cairo_set_source_rgba(cr, 0.0, 0.8, 0.0, 0.5); 
            cairo_rectangle(cr, 0, 0, width, height);
            cairo_fill(cr);
            
            // Optional: Draw 'Start' indicator ring
            cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.8);
            cairo_set_line_width(cr, 3.0);
            cairo_rectangle(cr, 2, 2, width-4, height-4);
            cairo_stroke(cr);
        }
        
        // Draw Dest Target Marker
        if (r == board->allowedEndRow && c == board->allowedEndCol) {
           // Draw target bullseye
           double cx = width/2.0;
           double cy = height/2.0;
           
           cairo_set_source_rgba(cr, 0.0, 0.8, 0.0, 0.6);
           cairo_arc(cr, cx, cy, width*0.35, 0, 2*M_PI);
           cairo_fill(cr);
           
           cairo_set_source_rgba(cr, 0.8, 1.0, 0.8, 0.9);
           cairo_set_line_width(cr, 2.0);
           cairo_arc(cr, cx, cy, width*0.25, 0, 2*M_PI);
           cairo_stroke(cr);
        }
    }
    
    // Draw valid move indicator (show if piece is selected OR being dragged)
    bool showValidMoves = (board->selectedRow >= 0) || 
                          (board->isDragging && board->dragSourceRow >= 0);
    if (isValidDest && showValidMoves) {
        if (board->useDots) {
            // Draw dot (Chess.com style)
            double centerX = width / 2.0;
            double centerY = height / 2.0;
            double radius = isCapture ? (width * 0.45) : (width * 0.15);
            
            if (isCapture) {
                // Red circle for capture - darker red, takes over yellow
                cairo_set_source_rgba(cr, 0.7, 0.0, 0.0, 0.85);
            } else {
                // Green circle for empty square
                cairo_set_source_rgba(cr, 0.39, 1.0, 0.39, 0.6);
            }
            cairo_arc(cr, centerX, centerY, radius, 0, 2.0 * M_PI);
            cairo_fill(cr);
        } else {
            // Highlight entire square
            if (isCapture) {
                // Red highlight - darker red, takes over yellow
                cairo_set_source_rgba(cr, 0.7, 0.0, 0.0, 0.85);
            } else {
                cairo_set_source_rgba(cr, 0.39, 1.0, 0.39, 0.6);
            }
            cairo_rectangle(cr, 0, 0, width, height);
            cairo_fill(cr);
        }
    }
    
    // Draw piece (unless it's being dragged or animated)
    if (piece && !hidePiece) {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);
        double pieceSize = width; // Draw piece fills the square (logic handles scaling)
        double centerX = width / 2.0;
        double centerY = height / 2.0;
        
        draw_piece_graphic(cr, board, piece->type, piece->owner, centerX, centerY, pieceSize * 0.85, 1.0);
    }
    
    // Draw rank number (top left) only on first column (visualC == 0)
    // Draw file letter (bottom right) only on last row (visualR == 7)
    double padding = width * 0.08; // 8% padding
    
    // Draw rank number (top left) - only on first visual column
    if (visualC == 0) {
        // Use visual row for rank display (what user sees)
        int rank = board->flipped ? (visualR + 1) : (8 - visualR); // Flipped: 1-8, Normal: 8-1
        char rank_str[2] = { '0' + rank, '\0' };
        
        PangoLayout* rank_layout = pango_cairo_create_layout(cr);
        PangoFontDescription* rank_font = pango_font_description_new();
        pango_font_description_set_family(rank_font, "Sans");
        pango_font_description_set_size(rank_font, (int)(width * 0.12 * PANGO_SCALE)); // 12% of square size
        pango_font_description_set_weight(rank_font, PANGO_WEIGHT_NORMAL);
        pango_layout_set_font_description(rank_layout, rank_font);
        pango_layout_set_text(rank_layout, rank_str, -1);
        
        int rank_width, rank_height;
        pango_layout_get_pixel_size(rank_layout, &rank_width, &rank_height);
        double rank_x = padding;
        double rank_y = padding;
        
        // Use darker contrasting color based on square color for better visibility
        if (isLight) {
            cairo_set_source_rgb(cr, 0.25, 0.25, 0.25); // Darker gray on light squares (was 0.4)
        } else {
            cairo_set_source_rgb(cr, 0.85, 0.85, 0.85); // Slightly darker light gray on dark squares (was 0.9)
        }
        // Use integer pixel alignment for crisp rendering
        cairo_move_to(cr, round(rank_x), round(rank_y));
        pango_cairo_show_layout(cr, rank_layout);
        
        pango_font_description_free(rank_font);
        g_object_unref(rank_layout);
    }
    
    // Draw file letter (bottom right) - only on last visual row
    if (visualR == 7) {
        // Use visual column for file display (what user sees)
        char file = board->flipped ? ('h' - visualC) : ('a' + visualC);
        char file_str[2] = { file, '\0' };
        
        PangoLayout* file_layout = pango_cairo_create_layout(cr);
        PangoFontDescription* file_font = pango_font_description_new();
        pango_font_description_set_family(file_font, "Sans");
        pango_font_description_set_size(file_font, (int)(width * 0.12 * PANGO_SCALE)); // 12% of square size
        pango_font_description_set_weight(file_font, PANGO_WEIGHT_NORMAL);
        pango_layout_set_font_description(file_layout, file_font);
        pango_layout_set_text(file_layout, file_str, -1);
        
        int file_width, file_height;
        pango_layout_get_pixel_size(file_layout, &file_width, &file_height);
        double file_x = width - file_width - padding;
        double file_y = height - file_height - padding;
        
        // Use darker contrasting color based on square color for better visibility
        if (isLight) {
            cairo_set_source_rgb(cr, 0.25, 0.25, 0.25); // Darker gray on light squares (was 0.4)
        } else {
            cairo_set_source_rgb(cr, 0.85, 0.85, 0.85); // Slightly darker light gray on dark squares (was 0.9)
        }
        // Use integer pixel alignment for crisp rendering
        cairo_move_to(cr, round(file_x), round(file_y));
        pango_cairo_show_layout(cr, file_layout);
        
        pango_font_description_free(file_font);
        g_object_unref(file_layout);
    }
    
    // Note: Dragged piece is drawn in overlay, not in individual squares
}

// Update a single square's appearance
static void update_square(BoardWidget* board, int r, int c) {
    GtkWidget* area = board->squares[r][c];
    
    // Queue a redraw
    gtk_widget_queue_draw(area);
}

// Refresh entire board display
static void refresh_board(BoardWidget* board) {
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            update_square(board, r, c);
        }
    }
}

// Free valid moves array
static void free_valid_moves(BoardWidget* board) {
    if (board->validMoves) {
        for (int i = 0; i < board->validMovesCount; i++) {
            if (board->validMoves[i]) {
                move_free(board->validMoves[i]);
            }
        }
        free(board->validMoves);
        board->validMoves = NULL;
        board->validMovesCount = 0;
    }
}

// Drag press handler - prepare for drag (but don't start until mouse moves)
static void on_drag_press(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press;
    BoardWidget* board = (BoardWidget*)user_data;
    
    DBG_PRINT("on_drag_press: x=%.1f y=%.1f isDragging=%d dragPrepared=%d isAnimating=%d turn=%d\n", 
              x, y, board->isDragging, board->dragPrepared, board->isAnimating, board->logic->turn);
    
    // Disable interaction in CvC mode
    if (board->logic->gameMode == GAME_MODE_CVC) {
        DBG_PRINT("  Skipping drag: CvC mode active\n");
        return;
    }

    // Skip if game is over (checkmate/stalemate)
    if (board->logic->isGameOver) {
        DBG_PRINT("  Skipping drag: game is over\n");
        return;
    }
    
    // Don't prepare new drag if currently animating
    if (board->isAnimating) {
        DBG_PRINT("  Currently animating, ignoring drag press\n");
        return;
    }
    
    // Don't prepare new drag if already dragging or prepared
    if (board->isDragging || board->dragPrepared) {
        DBG_PRINT("  Already dragging/prepared, ignoring\n");
        return;
    }
    
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int visualR = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "row"));
    int visualC = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "col"));
    
    // Convert visual coordinates to logical
    int r, c;
    visual_to_logical(board, visualR, visualC, &r, &c);
    
    Piece* piece = board->logic->board[r][c];
    DBG_PRINT("  Square [%d,%d] (visual [%d,%d]) piece=%p turn=%d\n", r, c, visualR, visualC, piece, board->logic->turn);
    
    // Prepare drag if piece belongs to current player (but don't start dragging yet)
    if (piece && piece->owner == board->logic->turn) {
        DBG_PRINT("  Preparing drag from [%d,%d] (will start on motion)\n", r, c);
        board->dragPrepared = true;  // Mark as prepared, but NOT dragging yet
        board->isDragging = false;   // Not dragging until mouse moves
        board->dragSourceRow = r;  // Store logical coordinates
        board->dragSourceCol = c;
        board->draggedPiece = piece;
        
        // Store initial press position (relative to grid)
        graphene_point_t src_point, dst_point;
        graphene_point_init(&src_point, x, y);
        if (gtk_widget_compute_point(widget, board->grid, &src_point, &dst_point)) {
            board->pressStartX = dst_point.x;
            board->pressStartY = dst_point.y;
        } else {
            // Fallback: calculate manually
            int widget_width = gtk_widget_get_width(widget);
            int widget_height = gtk_widget_get_height(widget);
            board->pressStartX = c * widget_width + x;
            board->pressStartY = r * widget_height + y;
        }
        board->dragX = board->pressStartX;
        board->dragY = board->pressStartY;
        
        // Get valid moves for this piece
        typedef struct {
            Move** moves;
            int count;
            int capacity;
        } MoveList;
        
        MoveList* allMoves = (MoveList*)malloc(sizeof(MoveList));
        allMoves->capacity = 128;
        allMoves->count = 0;
        allMoves->moves = (Move**)malloc(sizeof(Move*) * allMoves->capacity);
        
        gamelogic_generate_legal_moves(board->logic, board->logic->turn, allMoves);
        
        free_valid_moves(board);
        board->validMovesCount = 0;
        board->validMoves = (Move**)malloc(sizeof(Move*) * allMoves->count);
        
        for (int i = 0; i < allMoves->count; i++) {
            if (allMoves->moves[i] && 
                allMoves->moves[i]->startRow == r && 
                allMoves->moves[i]->startCol == c) {
                // Removed filtering so all valid moves are added for hints (dots)
                // if (board->restrictMoves) { ... }

                board->validMoves[board->validMovesCount] = 
                    move_create(allMoves->moves[i]->startRow, 
                               allMoves->moves[i]->startCol,
                               allMoves->moves[i]->endRow,
                               allMoves->moves[i]->endCol);
                board->validMoves[board->validMovesCount]->promotionPiece = 
                    allMoves->moves[i]->promotionPiece;
                board->validMoves[board->validMovesCount]->isEnPassant = 
                    allMoves->moves[i]->isEnPassant;  // Preserve en passant flag
                board->validMovesCount++;
            }
        }
        
        // (No strict check here anymore, allowing drag to start so hints are shown)
        
        for (int i = 0; i < allMoves->count; i++) {
            if (allMoves->moves[i]) move_free(allMoves->moves[i]);
        }
        free(allMoves->moves);
        free(allMoves);
        
    } else {
        DBG_PRINT("  Not preparing drag: piece=%p owner=%d turn=%d\n", 
                  piece, piece ? (int)piece->owner : -1, (int)board->logic->turn);
    }
}

// Motion handler for drag tracking (on grid level)
static void on_grid_motion(GtkEventControllerMotion* motion, double x, double y, gpointer user_data) {
    (void)motion;
    BoardWidget* board = (BoardWidget*)user_data;
    
    if (board->logic->gameMode == GAME_MODE_CVC) return;
    
    // If we have a prepared drag but haven't started dragging yet, check if mouse moved
    if (board->dragPrepared && !board->isDragging) {
        // Calculate distance from initial press position
        double dx = x - board->pressStartX;
        double dy = y - board->pressStartY;
        double distance = sqrt(dx * dx + dy * dy);
        
        // If mouse moved more than 5 pixels, start actual dragging
        if (distance > 5.0) {
            DBG_PRINT("on_grid_motion: Mouse moved %.1f pixels, starting drag\n", distance);
            board->isDragging = true;  // NOW we're actually dragging
            
            // Create overlay if needed
            if (!board->animOverlay) {
                board->animOverlay = gtk_drawing_area_new();
                gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(board->animOverlay),
                                              draw_animated_piece, board, NULL);
                gtk_widget_set_hexpand(board->animOverlay, TRUE);
                gtk_widget_set_vexpand(board->animOverlay, TRUE);
                gtk_widget_set_opacity(board->animOverlay, 0.95);
                gtk_widget_set_sensitive(board->animOverlay, FALSE); // Don't block input
                gtk_grid_attach(GTK_GRID(board->grid), board->animOverlay, 0, 0, 8, 8);
            }
            // Show overlay during drag
            gtk_widget_set_visible(board->animOverlay, TRUE);
            
            refresh_board(board);  // Hide original piece
            if (board->animOverlay) {
                gtk_widget_queue_draw(board->animOverlay);
            }
        }
    }
    
    // Update drag position if actually dragging
    if (board->isDragging) {
        board->dragX = x;
        board->dragY = y;
        // Ensure overlay is visible and redraw it
        if (board->animOverlay) {
            gtk_widget_set_visible(board->animOverlay, TRUE);
            gtk_widget_set_opacity(board->animOverlay, 0.95);
            gtk_widget_queue_draw(board->animOverlay);
        }
    }
}

// Release handler (end drag on mouse release)
static void on_release(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    BoardWidget* board = (BoardWidget*)user_data;
    if (board->logic->gameMode == GAME_MODE_CVC) return;

    DBG_PRINT("on_release: isDragging=%d dragPrepared=%d isAnimating=%d x=%.1f y=%.1f\n", 
              board->isDragging, board->dragPrepared, board->isAnimating, x, y);
    
    // If we were dragging, check if we dropped on a valid square
    if (board->isDragging && board->dragSourceRow >= 0) {
        // Use the current drag position (which is in grid coordinates) to find the drop square
        int dropRow = -1, dropCol = -1;
        
        // Convert dragX/dragY (grid coordinates) to square indices
        GtkWidget* grid = board->grid;
        int grid_width = gtk_widget_get_width(grid);
        int grid_height = gtk_widget_get_height(grid);
        
        if (grid_width > 0 && grid_height > 0) {
            int visualDropCol = (int)((board->dragX / grid_width) * 8);
            int visualDropRow = (int)((board->dragY / grid_height) * 8);
            
            // Clamp to valid range
            if (visualDropRow < 0) visualDropRow = 0;
            if (visualDropRow >= 8) visualDropRow = 7;
            if (visualDropCol < 0) visualDropCol = 0;
            if (visualDropCol >= 8) visualDropCol = 7;
            
            // Convert visual coordinates to logical
            visual_to_logical(board, visualDropRow, visualDropCol, &dropRow, &dropCol);
        }
        
        DBG_PRINT("  Drag release at grid coords: dropRow=%d dropCol=%d source=[%d,%d]\n",
                  dropRow, dropCol, board->dragSourceRow, board->dragSourceCol);
        
        // Check if this is a valid destination
        if (dropRow >= 0 && dropCol >= 0) {
            bool isValid = false;
            Move* moveToMake = NULL;
            
            // Get valid moves for dragged piece
            if (board->validMovesCount == 0) {
                // Need to get valid moves first
                typedef struct {
                    Move** moves;
                    int count;
                    int capacity;
                } MoveList;
                
                MoveList* allMoves = (MoveList*)malloc(sizeof(MoveList));
                allMoves->capacity = 128;
                allMoves->count = 0;
                allMoves->moves = (Move**)malloc(sizeof(Move*) * allMoves->capacity);
                
                gamelogic_generate_legal_moves(board->logic, board->logic->turn, allMoves);
                
                board->validMovesCount = 0;
                board->validMoves = (Move**)malloc(sizeof(Move*) * allMoves->count);
                
                for (int i = 0; i < allMoves->count; i++) {
                    if (allMoves->moves[i] && 
                        allMoves->moves[i]->startRow == board->dragSourceRow && 
                        allMoves->moves[i]->startCol == board->dragSourceCol) {
                        
                        // Tutorial Restriction Check
                        if (board->restrictMoves) {
                            if (allMoves->moves[i]->startRow != board->allowedStartRow ||
                                allMoves->moves[i]->startCol != board->allowedStartCol ||
                                allMoves->moves[i]->endRow != board->allowedEndRow ||
                                allMoves->moves[i]->endCol != board->allowedEndCol) {
                                continue;
                            }
                        }
                        
                        board->validMoves[board->validMovesCount] = 
                            move_create(allMoves->moves[i]->startRow, 
                                       allMoves->moves[i]->startCol,
                                       allMoves->moves[i]->endRow,
                                       allMoves->moves[i]->endCol);
                        board->validMoves[board->validMovesCount]->promotionPiece = 
                            allMoves->moves[i]->promotionPiece;
                        board->validMovesCount++;
                    }
                }
                
                for (int i = 0; i < allMoves->count; i++) {
                    if (allMoves->moves[i]) move_free(allMoves->moves[i]);
                }
                free(allMoves->moves);
                free(allMoves);
            }
            
            for (int i = 0; i < board->validMovesCount; i++) {
                if (board->validMoves[i] && 
                    board->validMoves[i]->endRow == dropRow && 
                    board->validMoves[i]->endCol == dropCol) {
                    isValid = true;
                    moveToMake = move_create(board->validMoves[i]->startRow,
                                            board->validMoves[i]->startCol,
                                            board->validMoves[i]->endRow,
                                            board->validMoves[i]->endCol);
                    moveToMake->promotionPiece = board->validMoves[i]->promotionPiece;
                    break;
                }
            }
            
            if (isValid && moveToMake) {
                // Tutorial Restriction Check on Execution
                if (board->restrictMoves) {
                    if (moveToMake->startRow != board->allowedStartRow ||
                        moveToMake->startCol != board->allowedStartCol ||
                        moveToMake->endRow != board->allowedEndRow ||
                        moveToMake->endCol != board->allowedEndCol) {
                        
                        DBG_PRINT("  Tutorial Restriction: Move not allowed\n");
                        // Trigger callback
                        if (board->invalidMoveCb) {
                            board->invalidMoveCb(board->invalidMoveData);
                        }
                        // Treat as invalid move -> cleanup below
                        move_free(moveToMake);
                        moveToMake = NULL; 
                        isValid = false; 
                    }
                }
            }

            if (isValid && moveToMake) {
                DBG_PRINT("  Valid drop: move from [%d,%d] to [%d,%d]\n", 
                          moveToMake->startRow, moveToMake->startCol,
                          moveToMake->endRow, moveToMake->endCol);
                
                // Check if this is a promotion move (pawn reaching last rank)
                Piece* movingPiece = board->logic->board[moveToMake->startRow][moveToMake->startCol];
                if (movingPiece && movingPiece->type == PIECE_PAWN && 
                    (moveToMake->endRow == 0 || moveToMake->endRow == 7)) {
                    // Show promotion dialog
                    GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(board->grid), GTK_TYPE_WINDOW);
                    PieceType selected = promotion_dialog_show(GTK_WINDOW(window), board->theme, movingPiece->owner);
                    if (selected == NO_PROMOTION) {
                        // User cancelled - don't make the move
                        move_free(moveToMake);
                        board->isDragging = false;
                        board->dragPrepared = false;
                        board->draggedPiece = NULL;
                        animate_return_piece(board);
                        return;
                    }
                    moveToMake->promotionPiece = selected;
                }
                
                // Valid move - execute with animation if enabled
                board->isDragging = false;
                board->dragPrepared = false;
                board->draggedPiece = NULL;
                board->dragSourceRow = -1;
                board->dragSourceCol = -1;
                free_valid_moves(board);
                
                // Drag and drop: execute immediately without animation
                DBG_PRINT("  Executing drag drop move immediately (no animation)\n");
                // Hide overlay before executing move
                if (board->animOverlay) {
                    gtk_widget_set_visible(board->animOverlay, FALSE);
                }
                gamelogic_perform_move(board->logic, moveToMake);
                play_move_sound(board, moveToMake);
                move_free(moveToMake);
                board->selectedRow = -1;
                board->selectedCol = -1;
                DBG_PRINT("  Move complete, new turn=%d\n", board->logic->turn);
                refresh_board(board);
                return; // Successfully handled
            }
        }
        
        // Invalid drop or couldn't determine drop location
        DBG_PRINT("  Releasing drag without valid drop\n");
        animate_return_piece(board);
        board->isDragging = false;
        board->dragPrepared = false;
        // Keep valid moves visible for next attempt
    } else if (board->dragPrepared && !board->isDragging) {
        // If we prepared for drag but never actually dragged (just a click), clear it
        // Let on_square_clicked handle the click properly
        DBG_PRINT("  Was just a click, not a drag - clearing prepared drag\n");
        board->dragPrepared = false;
        board->draggedPiece = NULL;
        // Don't clear dragSourceRow/Col yet - on_square_clicked might need them
        // But actually, on_square_clicked will get the square from the widget, so we can clear them
        board->dragSourceRow = -1;
        board->dragSourceCol = -1;
        // Don't free valid moves here - on_square_clicked will handle it
    }
}

// Drop handler
static gboolean on_drop(GtkDropTarget* target, const GValue* value, double x, double y, gpointer user_data) {
    (void)value; (void)x; (void)y;
    BoardWidget* board = (BoardWidget*)user_data;
    if (board->logic->gameMode == GAME_MODE_CVC) return FALSE;
    
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
    int visualR = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "row"));
    int visualC = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "col"));
    
    // Convert visual coordinates to logical
    int r, c;
    visual_to_logical(board, visualR, visualC, &r, &c);
    
    DBG_PRINT("on_drop: [%d,%d] (visual [%d,%d]) isDragging=%d source=[%d,%d] turn=%d\n", 
              r, c, visualR, visualC, board->isDragging, board->dragSourceRow, board->dragSourceCol, board->logic->turn);
    
    if (board->isDragging && board->dragSourceRow >= 0) {
        // Check if this is a valid destination
        bool isValid = false;
        Move* moveToMake = NULL;
        
        // Get valid moves for dragged piece
        if (board->validMovesCount == 0) {
            // Need to get valid moves first
            typedef struct {
                Move** moves;
                int count;
                int capacity;
            } MoveList;
            
            MoveList* allMoves = (MoveList*)malloc(sizeof(MoveList));
            allMoves->capacity = 128;
            allMoves->count = 0;
            allMoves->moves = (Move**)malloc(sizeof(Move*) * allMoves->capacity);
            
            gamelogic_generate_legal_moves(board->logic, board->logic->turn, allMoves);
            
            board->validMovesCount = 0;
            board->validMoves = (Move**)malloc(sizeof(Move*) * allMoves->count);
            
            for (int i = 0; i < allMoves->count; i++) {
                if (allMoves->moves[i] && 
                    allMoves->moves[i]->startRow == board->dragSourceRow && 
                    allMoves->moves[i]->startCol == board->dragSourceCol) {
                    
                    // Tutorial Restriction Check
                    if (board->restrictMoves) {
                        if (allMoves->moves[i]->startRow != board->allowedStartRow ||
                            allMoves->moves[i]->startCol != board->allowedStartCol ||
                            allMoves->moves[i]->endRow != board->allowedEndRow ||
                            allMoves->moves[i]->endCol != board->allowedEndCol) {
                            continue;
                        }
                    }

                    board->validMoves[board->validMovesCount] = 
                        move_create(allMoves->moves[i]->startRow, 
                                   allMoves->moves[i]->startCol,
                                   allMoves->moves[i]->endRow,
                                   allMoves->moves[i]->endCol);
                    board->validMoves[board->validMovesCount]->promotionPiece = 
                        allMoves->moves[i]->promotionPiece;
                    board->validMovesCount++;
                }
            }
            
            for (int i = 0; i < allMoves->count; i++) {
                if (allMoves->moves[i]) move_free(allMoves->moves[i]);
            }
            free(allMoves->moves);
            free(allMoves);
        }
        
        for (int i = 0; i < board->validMovesCount; i++) {
            if (board->validMoves[i] && 
                board->validMoves[i]->endRow == r && 
                board->validMoves[i]->endCol == c) {
                isValid = true;
                moveToMake = move_create(board->validMoves[i]->startRow,
                                        board->validMoves[i]->startCol,
                                        board->validMoves[i]->endRow,
                                        board->validMoves[i]->endCol);
                moveToMake->promotionPiece = board->validMoves[i]->promotionPiece;
                break;
            }
        }
        
        if (isValid && moveToMake) {
            DBG_PRINT("  Valid drop: move from [%d,%d] to [%d,%d]\n", 
                      moveToMake->startRow, moveToMake->startCol,
                      moveToMake->endRow, moveToMake->endCol);
            
            // Check if this is a promotion move (pawn reaching last rank)
            Piece* movingPiece = board->logic->board[moveToMake->startRow][moveToMake->startCol];
            if (movingPiece && movingPiece->type == PIECE_PAWN && 
                (moveToMake->endRow == 0 || moveToMake->endRow == 7)) {
                // Show promotion dialog
                GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(board->grid), GTK_TYPE_WINDOW);
                PieceType selected = promotion_dialog_show(GTK_WINDOW(window), board->theme, movingPiece->owner);
                if (selected == NO_PROMOTION) {
                    // User cancelled - don't make the move
                    move_free(moveToMake);
                    board->isDragging = false;
                    board->dragPrepared = false;
                    board->draggedPiece = NULL;
                    animate_return_piece(board);
                    return FALSE;
                }
                moveToMake->promotionPiece = selected;
            }
            
            if (isValid && moveToMake) {
                // Tutorial Restriction Check on Execution
                if (board->restrictMoves) {
                    if (moveToMake->startRow != board->allowedStartRow ||
                        moveToMake->startCol != board->allowedStartCol ||
                        moveToMake->endRow != board->allowedEndRow ||
                        moveToMake->endCol != board->allowedEndCol) {
                        
                        DBG_PRINT("  Tutorial Restriction: Move not allowed\n");
                        // Trigger callback
                        if (board->invalidMoveCb) {
                            board->invalidMoveCb(board->invalidMoveData);
                        }
                        // Treat as invalid move -> cleanup below
                        move_free(moveToMake);
                        moveToMake = NULL; 
                        isValid = false; 
                        
                        // Return FALSE immediately to prevent falling through to generic invalid handler
                        // The callback has already been triggered above
                        board->isDragging = false;
                        board->dragPrepared = false;
                        animate_return_piece(board);
                        return FALSE;
                    }
                }
            }

            // Valid move - execute with animation if enabled
            board->isDragging = false; // Stop dragging before move
            board->dragPrepared = false;
            board->draggedPiece = NULL;
            board->dragSourceRow = -1;  // Clear source after valid drop
            board->dragSourceCol = -1;
            free_valid_moves(board);
            
            if (board->animationsEnabled) {
                DBG_PRINT("  Starting animation\n");
                animate_move(board, moveToMake, NULL);
            } else {
                DBG_PRINT("  Executing move immediately\n");
                gamelogic_perform_move(board->logic, moveToMake);
                play_move_sound(board, moveToMake);
                move_free(moveToMake);
                // Clear selection after move
                board->selectedRow = -1;
                board->selectedCol = -1;
                DBG_PRINT("  Move complete, new turn=%d\n", board->logic->turn);
                refresh_board(board);
            }
            return TRUE; // Move handled
        } else {
            DBG_PRINT("  Invalid drop\n");
            // Invalid move - return piece to original position
            board->isDragging = false;
            board->dragPrepared = false;
            animate_return_piece(board);
            
            // Remove hardcoded dialog - relying on invalidMoveCb provided by main.c
            // if (board->restrictMoves) { ... } 
            
            // DON'T clear valid moves - keep them visible for next drag attempt
            DBG_PRINT("  Invalid drop, keeping valid moves visible for next attempt\n");
            
            // Only trigger callback here if it wasn't already triggered by specific restriction check
            // (But we handle restriction checks above and return early, so this is just for generic invalid drops)
            if (board->restrictMoves && board->invalidMoveCb) {
                board->invalidMoveCb(board->invalidMoveData);
            }
            
            return FALSE; // Invalid drop
        }
    } else {
        DBG_PRINT("  Drop ignored: not dragging or invalid source\n");
    }
    
    return FALSE;
}

// Square click handler (for click-to-move mode)
static void on_square_clicked(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    BoardWidget* board = (BoardWidget*)user_data;
    
    // Disable interaction in CvC mode
    if (board->logic->gameMode == GAME_MODE_CVC) return;
    
    DBG_PRINT("on_square_clicked: isDragging=%d dragPrepared=%d dragMode=%d isAnimating=%d turn=%d\n", 
              board->isDragging, board->dragPrepared, board->dragMode, board->isAnimating, board->logic->turn);
    
    // Skip if game is over (checkmate/stalemate)
    if (board->logic->isGameOver) {
        DBG_PRINT("  Skipping click: game is over\n");
        return;
    }
    
    // Skip if currently dragging or animating
    if (board->isDragging || board->isAnimating) {
        DBG_PRINT("  Skipping click: dragging=%d or animating=%d\n", board->isDragging, board->isAnimating);
        return;
    }
    
    // Skip if in drag mode (drag will be handled by on_drag_press)
    if (board->dragMode) {
        DBG_PRINT("  Skipping click: drag mode enabled\n");
        return;
    }
    
    // Process clicks immediately on press
    // Note: on_drag_press also runs on press and sets dragPrepared
    // If dragPrepared is true here, it means on_drag_press ran first
    // We'll process the click anyway - if it becomes a drag, the drag handlers will handle it
    // If it's just a click, the release handler will clear dragPrepared
    
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int visualR = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "row"));
    int visualC = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "col"));
    
    // Convert visual coordinates to logical (handle board flipping)
    int logicalR, logicalC;
    visual_to_logical(board, visualR, visualC, &logicalR, &logicalC);
    
    // If no piece selected, try to select one
    if (board->selectedRow < 0) {
        Piece* piece = board->logic->board[logicalR][logicalC];
        if (piece && piece->owner == board->logic->turn) {
            // Select this piece
            board->selectedRow = logicalR;
            board->selectedCol = logicalC;
            
            // Get all legal moves for current player
            typedef struct {
                Move** moves;
                int count;
                int capacity;
            } MoveList;
            
            MoveList* allMoves = (MoveList*)malloc(sizeof(MoveList));
            allMoves->capacity = 128;
            allMoves->count = 0;
            allMoves->moves = (Move**)malloc(sizeof(Move*) * allMoves->capacity);
            
            gamelogic_generate_legal_moves(board->logic, board->logic->turn, allMoves);
            
            // Filter to moves starting from selected square
            free_valid_moves(board);
            board->validMovesCount = 0;
            board->validMoves = (Move**)malloc(sizeof(Move*) * allMoves->count);
            
            for (int i = 0; i < allMoves->count; i++) {
                if (allMoves->moves[i] && 
                    allMoves->moves[i]->startRow == logicalR && 
                    allMoves->moves[i]->startCol == logicalC) {
                    
                    // Allow all moves for hints

                    // Copy the move
                    board->validMoves[board->validMovesCount] = 
                        move_create(allMoves->moves[i]->startRow, 
                                   allMoves->moves[i]->startCol,
                                   allMoves->moves[i]->endRow,
                                   allMoves->moves[i]->endCol);
                    board->validMoves[board->validMovesCount]->promotionPiece = 
                        allMoves->moves[i]->promotionPiece;
                    board->validMovesCount++;
                }
            }

            // Normal case: no moves, deselect
            if (board->validMovesCount == 0) {
                board->selectedRow = -1;
                board->selectedCol = -1;
            }
            
            // Free the temporary list
            for (int i = 0; i < allMoves->count; i++) {
                if (allMoves->moves[i]) {
                    move_free(allMoves->moves[i]);
                }
            }
            free(allMoves->moves);
            free(allMoves);
            
            refresh_board(board);
        }
    } else {
        // Check if this is a valid destination
        bool isValid = false;
        Move* moveToMake = NULL;
        
        for (int i = 0; i < board->validMovesCount; i++) {
            if (board->validMoves[i] && 
                board->validMoves[i]->endRow == logicalR && 
                board->validMoves[i]->endCol == logicalC) {
                isValid = true;
                moveToMake = move_create(board->validMoves[i]->startRow,
                                        board->validMoves[i]->startCol,
                                        board->validMoves[i]->endRow,
                                        board->validMoves[i]->endCol);
                moveToMake->promotionPiece = board->validMoves[i]->promotionPiece;
                break;
            }
        }
        
        if (isValid && moveToMake) {
             // Tutorial Restriction Check on Execution
            if (board->restrictMoves) {
                if (moveToMake->startRow != board->allowedStartRow ||
                    moveToMake->startCol != board->allowedStartCol ||
                    moveToMake->endRow != board->allowedEndRow ||
                    moveToMake->endCol != board->allowedEndCol) {
                    
                    DBG_PRINT("  Tutorial Restriction: Click Move not allowed\n");
                    if (board->invalidMoveCb) {
                        board->invalidMoveCb(board->invalidMoveData);
                    }
                    move_free(moveToMake);
                    moveToMake = NULL;
                    isValid = false;
                }
            }
        }

        if (isValid && moveToMake) {
            DBG_PRINT("  Valid click move: [%d,%d] to [%d,%d]\n",
                      moveToMake->startRow, moveToMake->startCol,
                      moveToMake->endRow, moveToMake->endCol);
            // Clear selection and hints immediately when valid move is clicked
            board->selectedRow = -1;
            board->selectedCol = -1;
            free_valid_moves(board);
            refresh_board(board);  // Update display to remove hints immediately
            // Make the move with animation if enabled
            if (board->animationsEnabled) {
                animate_move(board, moveToMake, NULL);
            } else {
                gamelogic_perform_move(board->logic, moveToMake);
                play_move_sound(board, moveToMake);
                move_free(moveToMake);
                DBG_PRINT("  Move complete, new turn=%d\n", board->logic->turn);
                refresh_board(board);
            }
        } else {
            DBG_PRINT("  Invalid click move, deselecting\n");
            
            // If restricted and clicked a distinct square (not re-clicking same piece), trigger callback
            if (board->restrictMoves && board->invalidMoveCb) {
                // If we clicked strictly outside, or on a piece that wasn't the target
                if (logicalR != board->selectedRow || logicalC != board->selectedCol) {
                    board->invalidMoveCb(board->invalidMoveData);
                }
            }

            // Invalid move - just deselect
            board->selectedRow = -1;
            board->selectedCol = -1;
            free_valid_moves(board);
            refresh_board(board);
        }
    }
}

// Forward declarations (must be before any usage)
static void animate_move(BoardWidget* board, Move* move, void (*on_finished)(void));
static void animate_return_piece(BoardWidget* board);
static void on_drag_press(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
static void on_grid_motion(GtkEventControllerMotion* motion, double x, double y, gpointer user_data);

// Animation timer callback (smooth 60fps animation)
static gboolean animation_tick(gpointer user_data) {
    BoardWidget* board = (BoardWidget*)user_data;
    
    if (!board->isAnimating || !board->animatingMove) {
        board->animTickId = 0;
        board->animStartTime = 0;
        return G_SOURCE_REMOVE;
    }
    
    // Get current time
    gint64 current_time = g_get_monotonic_time();
    
    // Calculate elapsed time in milliseconds
    double elapsed_ms = (current_time - board->animStartTime) / 1000.0;
    // Shorter duration for drag drops (150ms), longer for regular moves (300ms)
    double duration_ms = board->animatingFromDrag ? 150.0 : 300.0;
    board->animProgress = elapsed_ms / duration_ms;
    
    if (board->animProgress >= 1.0) {
        // Animation complete
        board->animProgress = 1.0;
        board->isAnimating = false;
        
        // Execute the move
        Move* move = board->animatingMove;
        DBG_PRINT("animation_tick: Animation complete, executing move\n");
        gamelogic_perform_move(board->logic, move);
        // Check if this was a regular move - if so, sound was already played 200ms into animation
        // For non-regular moves (capture, castling) or special states (check, checkmate), play sound now
        bool isRegularMove = !move->isCastling && 
                             move->capturedPiece == NULL && 
                             !move->isEnPassant;
        if (!isRegularMove) {
            // Play sound for captures, castling, etc. (these play immediately after animation)
            play_move_sound(board, move);
        } else {
            // For regular moves, check for check/checkmate/stalemate after move executes
            // (The move sound was already played 200ms into animation)
            Player opponent = (board->logic->turn == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
            if (gamelogic_is_checkmate(board->logic, PLAYER_WHITE) || 
                gamelogic_is_checkmate(board->logic, PLAYER_BLACK)) {
                Player winner = gamelogic_is_checkmate(board->logic, PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
                if (winner == board->logic->playerSide) {
                    sound_engine_play(SOUND_WIN);
                } else {
                    sound_engine_play(SOUND_DEFEAT);
                }
            } else if (gamelogic_is_stalemate(board->logic, PLAYER_WHITE) || 
                       gamelogic_is_stalemate(board->logic, PLAYER_BLACK)) {
                sound_engine_play(SOUND_DRAW);
            } else if (gamelogic_is_in_check(board->logic, opponent)) {
                sound_engine_play(SOUND_CHECK);
            }
        }
        move_free(move);
        board->animatingMove = NULL;
        // Free the copied piece
        if (board->animatingPiece) {
            piece_free(board->animatingPiece);
            board->animatingPiece = NULL;
        }
        
        // Clean up animation state
        board->animTickId = 0;
        board->animStartTime = 0;
        board->animatingFromDrag = false;
        
        // Clear selection and refresh board
        board->selectedRow = -1;
        board->selectedCol = -1;
        free_valid_moves(board);
        DBG_PRINT("  Animation complete, new turn=%d\n", board->logic->turn);
        
        // Final refresh to show new board state
        refresh_board(board);
        if (board->animOverlay) {
            gtk_widget_queue_draw(board->animOverlay);
            // Ensure overlay doesn't block input - hide it when animation is done
            gtk_widget_set_sensitive(board->animOverlay, FALSE); // Don't block input
            // Also hide it visually when not animating
            gtk_widget_set_visible(board->animOverlay, FALSE);
        }
        
        DBG_PRINT("  Animation cleanup complete, board ready for input (turn=%d, isAnimating=%d)\n", 
                  board->logic->turn, board->isAnimating);
        return G_SOURCE_REMOVE;
    }
    
    // Redraw overlay and board for smooth animation
    if (board->animOverlay) {
        gtk_widget_queue_draw(board->animOverlay);
    }
    refresh_board(board);
    
    return G_SOURCE_CONTINUE;
}

// Start move animation
static void animate_move(BoardWidget* board, Move* move, void (*on_finished)(void)) {
    (void)on_finished; // Callback for future use
    
    // Check if this is a promotion move (pawn reaching last rank)
    Piece* movingPiece = board->logic->board[move->startRow][move->startCol];
    if (movingPiece && movingPiece->type == PIECE_PAWN && 
        (move->endRow == 0 || move->endRow == 7)) {
        // Show promotion dialog
        GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(board->grid), GTK_TYPE_WINDOW);
        PieceType selected = promotion_dialog_show(GTK_WINDOW(window), board->theme, movingPiece->owner);
        if (selected == NO_PROMOTION) {
            // User cancelled - don't make the move
            move_free(move);
            return;
        }
        move->promotionPiece = selected;
    }
    
    if (!board->animationsEnabled) {
        // No animation - execute immediately
        gamelogic_perform_move(board->logic, move);
        play_move_sound(board, move);
        move_free(move);
        return;
    }
    
    board->isAnimating = true;
    board->animatingMove = move_copy(move); // Copy move for animation
    // COPY the piece being animated BEFORE the move executes (not just a pointer!)
    Piece* sourcePiece = board->logic->board[move->startRow][move->startCol];
    if (sourcePiece) {
        board->animatingPiece = piece_copy(sourcePiece);
    } else {
        board->animatingPiece = NULL;
    }
    board->animProgress = 0.0;
    board->animStartTime = g_get_monotonic_time(); // Set start time for this animation
    board->animatingFromDrag = false; // Regular move animation from square
    
    // Schedule move sound to play 200ms into animation (only for regular moves)
    // Check if this is a regular move (not capture, castling, etc.)
    // Note: We can't check for check/checkmate here since move hasn't executed yet
    // Those sounds will play after animation completes if needed
    bool isRegularMove = !move->isCastling && 
                         move->capturedPiece == NULL && 
                         !move->isEnPassant;
    if (isRegularMove) {
        // Schedule move sound to play 200ms into the animation
        g_timeout_add(200, delayed_move_sound_callback, NULL);
    }
    
    // Create overlay for animated piece if it doesn't exist
    if (!board->animOverlay) {
        board->animOverlay = gtk_drawing_area_new();
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(board->animOverlay),
                                      draw_animated_piece, board, NULL);
        gtk_widget_set_hexpand(board->animOverlay, TRUE);
        gtk_widget_set_vexpand(board->animOverlay, TRUE);
        gtk_widget_set_opacity(board->animOverlay, 1.0); // Fully opaque for smooth rendering
        gtk_widget_set_sensitive(board->animOverlay, FALSE); // Don't block input
        gtk_grid_attach(GTK_GRID(board->grid), board->animOverlay, 0, 0, 8, 8);
    }
    // Show overlay during animation and ensure it's on top
    gtk_widget_set_visible(board->animOverlay, TRUE);
    gtk_widget_set_opacity(board->animOverlay, 1.0);
    
    // Stop any existing animation timer
    if (board->animTickId != 0) {
        g_source_remove(board->animTickId);
        board->animTickId = 0;
    }
    
    // Start animation timer with high priority for smooth 60fps (8ms = ~120fps for smoother animation)
    // Use G_PRIORITY_HIGH for smoother, more consistent animation
    board->animTickId = g_timeout_add_full(G_PRIORITY_HIGH, 8, animation_tick, board, NULL);
    
    refresh_board(board);
}

// Animate piece return (for invalid drag)
static void animate_return_piece(BoardWidget* board) {
    DBG_PRINT("animate_return_piece: Resetting drag state\n");
    // Reset drag state and refresh display
    board->isDragging = false;
    board->dragPrepared = false;
    board->draggedPiece = NULL;
    // DON'T clear dragSourceRow/Col - keep them so valid moves stay visible
    // board->dragSourceRow = -1;
    // board->dragSourceCol = -1;
    // Don't clear selection - user might want to try again or click elsewhere
    // DON'T clear valid moves - keep them visible for next drag attempt
    // free_valid_moves(board);
    refresh_board(board);
    if (board->animOverlay) {
        gtk_widget_queue_draw(board->animOverlay);
        // Ensure overlay doesn't block input
        gtk_widget_set_sensitive(board->animOverlay, FALSE);
        // Hide it when not dragging
        gtk_widget_set_visible(board->animOverlay, FALSE);
    }
    DBG_PRINT("  Drag state reset complete, valid moves still visible (turn=%d, isDragging=%d, dragPrepared=%d)\n", 
              board->logic->turn, board->isDragging, board->dragPrepared);
}

// Cleanup function
static void board_widget_destroy(gpointer user_data) {
    BoardWidget* board = (BoardWidget*)user_data;
    if (board) {
        // Handled by ThemeData
        free_valid_moves(board);
        if (board->cssProvider) {
            g_object_unref(board->cssProvider);
        }
        // Stop animations
        if (board->animTickId > 0) {
            g_source_remove(board->animTickId);
            board->animTickId = 0;
        }
        free(board);
    }
}

GtkWidget* board_widget_new(GameLogic* logic) {
    BoardWidget* board = (BoardWidget*)malloc(sizeof(BoardWidget));
    board->logic = logic;
    // Init state
    board->theme = NULL;
    board->grid = NULL;
    board->selectedRow = -1;
    board->selectedCol = -1;
    board->validMoves = NULL;
    board->validMovesCount = 0;
    board->useDots = true;  // Default: show dots (Chess.com style)
    board->animationsEnabled = true;
    board->dragMode = false;  // Default: click-to-move mode
    board->flipped = false;  // Default: white's perspective
    board->isDragging = false;
    board->dragPrepared = false;
    board->dragSourceRow = -1;
    board->dragSourceCol = -1;
    board->dragX = 0;
    board->dragY = 0;
    board->pressStartX = 0;
    board->pressStartY = 0;
    board->draggedPiece = NULL;
    board->isAnimating = false;
    board->animatingMove = NULL;
    board->animProgress = 0.0;
    board->animTickId = 0;
    board->animStartTime = 0;
    board->animatingFromDrag = false;
    board->animStartX = 0;
    board->animStartY = 0;
    board->animOverlay = NULL;
    board->cssProvider = NULL;
    
    // Tutorial fields
    board->restrictMoves = false;
    board->allowedStartRow = -1;
    board->allowedStartCol = -1;
    board->allowedEndRow = -1;
    board->allowedEndCol = -1;
    
    // Create a frame to add border around the board
    GtkWidget* frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "chess-board-frame");
    
    board->grid = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(board->grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(board->grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(board->grid), 0);
    gtk_grid_set_column_spacing(GTK_GRID(board->grid), 0);
    
    // Make grid expand to fill frame
    gtk_widget_set_hexpand(board->grid, TRUE);
    gtk_widget_set_vexpand(board->grid, TRUE);
    
    // Add grid to frame
    gtk_frame_set_child(GTK_FRAME(frame), board->grid);
    
    // Make frame expand to fill available space
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, TRUE);
    
    // Don't set size request on frame - let aspect frame control the sizing
    // The aspect frame will ensure it stays square
    
    // Create 8x8 grid of drawing areas (Cairo-based for full control)
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            GtkWidget* area = gtk_drawing_area_new();
            board->squares[r][c] = area;
            board->dots[r][c] = NULL; // No overlay needed with Cairo
            
            // Ensure drawing area expands to fill grid cell
            gtk_widget_set_hexpand(area, TRUE);
            gtk_widget_set_vexpand(area, TRUE);
            
            g_object_set_data(G_OBJECT(area), "row", GINT_TO_POINTER(r));
            g_object_set_data(G_OBJECT(area), "col", GINT_TO_POINTER(c));
            g_object_set_data(G_OBJECT(area), "board", board);
            
            // Set draw function
            gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), 
                                          draw_square, 
                                          board, NULL);
            
            // Add click gesture (for click-to-move mode) - also handles drag start (simple drag like chess.com)
            GtkGestureClick* gesture = GTK_GESTURE_CLICK(gtk_gesture_click_new());
            gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
            g_signal_connect(gesture, "pressed", G_CALLBACK(on_square_clicked), board);
            // Also use pressed for drag start (simple drag like chess.com - no long press needed)
            g_signal_connect(gesture, "pressed", G_CALLBACK(on_drag_press), board);
            // Add release handler to end drag
            g_signal_connect(gesture, "released", G_CALLBACK(on_release), board);
            gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(gesture));
            
            // Add drop target (for drag-and-drop mode)
            GtkDropTarget* drop_target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_MOVE);
            g_signal_connect(drop_target, "drop", G_CALLBACK(on_drop), board);
            gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drop_target));
            
            gtk_grid_attach(GTK_GRID(board->grid), area, c, r, 1, 1);
        }
    }
    
    // Add motion controller to grid for drag tracking
    GtkEventControllerMotion* grid_motion = GTK_EVENT_CONTROLLER_MOTION(gtk_event_controller_motion_new());
    g_signal_connect(grid_motion, "motion", G_CALLBACK(on_grid_motion), board);
    gtk_widget_add_controller(board->grid, GTK_EVENT_CONTROLLER(grid_motion));
    
    // Set up cleanup - store data on frame instead of grid
    g_object_set_data_full(G_OBJECT(frame), "board-data", board, 
                          board_widget_destroy);
    
    // Store reference to frame in board for CSS
    g_object_set_data(G_OBJECT(board->grid), "board-frame", frame);
    
    // Initial refresh
    refresh_board(board);
    
    return frame; // Return frame instead of grid
}

// Set interaction mode
void board_widget_set_drag_mode(GtkWidget* board_widget, bool drag_mode) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (board) {
        board->dragMode = drag_mode;
        // Clear selection when switching modes
        board->selectedRow = -1;
        board->selectedCol = -1;
        free_valid_moves(board);
        refresh_board(board);
    }
}

// Get interaction mode
bool board_widget_get_drag_mode(GtkWidget* board_widget) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    return board ? board->dragMode : false;
}

// Refresh the board display
void board_widget_refresh(GtkWidget* board_widget) {
    if (!board_widget) {
        return;
    }
    
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    
    if (board) {
        refresh_board(board);
    }
}

// Reset selection (clear selected piece)
void board_widget_reset_selection(GtkWidget* board_widget) {
    // board_widget can be either the frame or the grid
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        // Try getting from grid if board_widget is the frame
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    if (!board) {
        // Try getting from frame if board_widget is the grid
        GtkWidget* frame = (GtkWidget*)g_object_get_data(G_OBJECT(board_widget), "board-frame");
        if (frame) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(frame), "board-data");
        }
    }
    if (board) {
        board->selectedRow = -1;
        board->selectedCol = -1;
        // Clear valid moves
        if (board->validMoves) {
            for (int i = 0; i < board->validMovesCount; i++) {
                if (board->validMoves[i]) {
                    move_free(board->validMoves[i]);
                }
            }
            free(board->validMoves);
            board->validMoves = NULL;
            board->validMovesCount = 0;
        }
        refresh_board(board);
    }
}

// Set board orientation (flip board for black's perspective)
void board_widget_set_flipped(GtkWidget* board_widget, bool flipped) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        // Try getting from grid if board_widget is the frame
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    if (board) {
        board->flipped = flipped;
        refresh_board(board);
    }
}

// Get board orientation
bool board_widget_get_flipped(GtkWidget* board_widget) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    return board ? board->flipped : false;
}

// Set animations enabled
void board_widget_set_animations_enabled(GtkWidget* board_widget, bool enabled) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    if (board) {
        board->animationsEnabled = enabled;
    }
}

// Get animations enabled
bool board_widget_get_animations_enabled(GtkWidget* board_widget) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    return board ? board->animationsEnabled : true;
}

// Set hints mode: true = dots, false = squares
void board_widget_set_hints_mode(GtkWidget* board_widget, bool use_dots) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    if (board) {
        board->useDots = use_dots;
    }
}

void board_widget_set_theme(GtkWidget* board_widget, ThemeData* theme) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    if (board) {
        board->theme = theme;
        board_widget_refresh(board_widget);
    }
}

void board_widget_animate_move(GtkWidget* board_widget, Move* move) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    if (board && move) {
        animate_move(board, move, NULL);
    }
}

// Get hints mode: true = dots, false = squares
bool board_widget_get_hints_mode(GtkWidget* board_widget) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
        }
    }
    return board ? board->useDots : true;
}

// Set nav restriction
void board_widget_set_nav_restricted(GtkWidget* board_widget, bool restricted, int startR, int startC, int endR, int endC) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    // Handle frame/grid data lookup
    if (!board) {
         GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
         if (grid) board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
    }
    if (board) {
        board->restrictMoves = restricted;
        board->allowedStartRow = startR;
        board->allowedStartCol = startC;
        board->allowedEndRow = endR;
        board->allowedEndCol = endC;
        board->showTutorialHighlights = restricted;
        
        // Clear any existing selection or drags to force clean state
        board->selectedRow = -1;
        board->selectedCol = -1;
        board->isDragging = false;
        board_widget_refresh(board_widget);
    }
}

void board_widget_set_invalid_move_callback(GtkWidget* board_widget, BoardInvalidMoveCallback cb, void* data) {
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(board_widget), "board-data");
    if (!board) {
        GtkWidget* grid = gtk_frame_get_child(GTK_FRAME(board_widget));
        if (grid) board = (BoardWidget*)g_object_get_data(G_OBJECT(grid), "board-data");
    }
    if (board) {
        board->invalidMoveCb = cb;
        board->invalidMoveData = data;
    }
}

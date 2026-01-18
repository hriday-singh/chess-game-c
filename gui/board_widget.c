#include "board_widget.h"
#include "sound_engine.h"
#include "theme_data.h"
#include "../game/gamelogic.h"
#include "../game/move.h"
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

// Define M_PI if not available (C standard doesn't require it)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool debug_mode = true;

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
    // Last move highlight (yellow squares)
    int lastMoveFromRow;
    int lastMoveFromCol;
    int lastMoveToRow;
    int lastMoveToCol;
    // Drag state
    bool isDragging;        // True only if actually dragging (mouse moved while holding)
    bool dragPrepared;      // True if button pressed on piece (but mouse hasn't moved yet)
    int dragSourceRow;
    int dragSourceCol;
    double dragX;
    double dragY;
    double pressStartX;     // Initial press position to detect if mouse moved
    double pressStartY;
    // Drag target (hover)
    int dragOverRow;
    int dragOverCol;
    // Animation state
    bool isAnimating;
    Move* animatingMove;
    Move* animCastlingRookMove; // For castling: secondary move for the rook
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
    
    // Callback before human move execution (for state capture)
    BoardPreMoveCallback preMoveCb;
    void* preMoveData;
    bool isInteractive;
} BoardWidget;

// Safe lookup key
#define BOARD_DATA_KEY "board-data"

// Forward declarations (must be before any usage)
static void animate_move(BoardWidget* board, Move* move, void (*on_finished)(void));
static void animate_return_piece(BoardWidget* board);
static void on_drag_press(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
static void on_grid_motion(GtkEventControllerMotion* motion, double x, double y, gpointer user_data);
static void on_release(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
static void refresh_board(BoardWidget* board);
static void free_valid_moves(BoardWidget* board);

/**
 * 6. Centralized lookup helper
 * Finds BoardWidget data from a widget, checking parent/child hierarchy if needed.
 */
static BoardWidget* find_board_data(GtkWidget* widget) {
    if (!widget || !GTK_IS_WIDGET(widget)) return NULL;

    // 1. Check the widget itself
    BoardWidget* board = (BoardWidget*)g_object_get_data(G_OBJECT(widget), BOARD_DATA_KEY);
    if (board) return board;

    // 2. Check immediate parent (common if widget is child of board frame)
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (parent) {
        board = (BoardWidget*)g_object_get_data(G_OBJECT(parent), BOARD_DATA_KEY);
        if (board) return board;
    }

    // 3. Optional: Check child if widget is a frame (common fallback)
    if (GTK_IS_FRAME(widget)) {
        GtkWidget* child = gtk_frame_get_child(GTK_FRAME(widget));
        if (child) {
            board = (BoardWidget*)g_object_get_data(G_OBJECT(child), BOARD_DATA_KEY);
            if (board) return board;
        }
    }

    return NULL; // Not found
}

// Play appropriate sound for a move (non-blocking, lightweight)
static void play_move_sound(BoardWidget* board, Move* move) {
    if (!board || !move || !board->logic) return;
    
    // Check game end states first (highest priority)
    // In Puzzle/Tutorial modes, suppress default game-over sounds (controller handles success/fail)
    bool suppress_game_over_sfx = (board->logic->gameMode == GAME_MODE_PUZZLE || 
                                   board->logic->gameMode == GAME_MODE_TUTORIAL);

    if (!suppress_game_over_sfx && (gamelogic_is_checkmate(board->logic, PLAYER_WHITE) || 
        gamelogic_is_checkmate(board->logic, PLAYER_BLACK))) {
        // Determine winner
        // Check who is at the bottom (The "Player")
        // If flipped=false, White is bottom. If flipped=true, Black is bottom.
        Player bottomPlayer = board->flipped ? PLAYER_BLACK : PLAYER_WHITE;
        Player winner = gamelogic_is_checkmate(board->logic, PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
        
        if (winner == bottomPlayer) {
            sound_engine_play(SOUND_WIN);
            if (debug_mode) printf("[BoardWidget] Playing win sound (Bottom Player Won)\n");
        } else {
            sound_engine_play(SOUND_DEFEAT);
            if (debug_mode) printf("[BoardWidget] Playing defeat sound (Bottom Player Lost)\n");
        }
        return;
    }
    
    if (!suppress_game_over_sfx && (gamelogic_is_stalemate(board->logic, PLAYER_WHITE) || 
        gamelogic_is_stalemate(board->logic, PLAYER_BLACK))) {
        sound_engine_play(SOUND_DRAW);
        if (debug_mode) printf("[BoardWidget] Playing draw sound\n");
        return;
    }
    
    if (gamelogic_is_in_check(board->logic, PLAYER_WHITE) || gamelogic_is_in_check(board->logic, PLAYER_BLACK)) {
        sound_engine_play(SOUND_CHECK);
        if (debug_mode) printf("[BoardWidget] Playing check sound\n");
        return;
    }
    
    // Check move type
    if (move->isCastling) {
        sound_engine_play(SOUND_CASTLES);
        if (debug_mode) printf("[BoardWidget] Playing castling sound\n");
    } else if (move->promotionPiece != NO_PROMOTION) {
        sound_engine_play(SOUND_PROMOTION);
        if (debug_mode) printf("[BoardWidget] Playing promotion sound\n");
    } else if (move->capturedPieceType != NO_PIECE || move->isEnPassant) {
        sound_engine_play(SOUND_CAPTURE);
        if (debug_mode) printf("[BoardWidget] Playing capture sound\n");
    } else {
        // Regular move - play immediately (unless skipped because it was already played delayed)
        // In Puzzle Mode, the main logic handles sounds (Success/Failure), so don't play default move sound
        if (board->logic->gameMode != GAME_MODE_PUZZLE) {
             // Determine who made the move to pick the right sound
             // The move is already made, so 'turn' is now the opponent of the mover.
             // Mover = !board->logic->turn
             Player mover = (board->logic->turn == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
             
             // Check if the mover is a computer
             if (gamelogic_is_computer(board->logic, mover)) {
                 sound_engine_play(SOUND_MOVE_OPPONENT);
                 if (debug_mode) printf("[BoardWidget] Playing move sound for opponent\n");
             } else {
                 sound_engine_play(SOUND_MOVE);
                 if (debug_mode) printf("[BoardWidget] Playing move sound for player\n");
             }
        }
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
    int fromRow = board->lastMoveFromRow;
    int fromCol = board->lastMoveFromCol;
    int toRow = board->lastMoveToRow;
    int toCol = board->lastMoveToCol;

    // Use BoardWidget's lastMove fields for manual override (e.g. during replay animation PRE-completion)
    // If no manual override is set, fallback to logic's actual last move
    // Only use BoardWidget's explicit lastMove fields.
    // We do NOT fallback to logic here because during Replay mode, 
    // the logic's history stack remains full (end of game) while the board state is at the start (Move 0),
    // causing incorrect highlights if we fallback.
    if (fromRow == -1) return false;

    return ((r == fromRow && c == fromCol) ||
            (r == toRow && c == toCol));
}

// Helper to render move to UCI string (e.g. "e2e4")
static void render_move_uci(Move* m, char* buf, size_t size) {
    if (!m || !buf || size < 5) return;
    int r1 = m->from_sq / 8, c1 = m->from_sq % 8;
    int r2 = m->to_sq / 8, c2 = m->to_sq % 8;
    snprintf(buf, size, "%c%d%c%d", 
             'a' + c1, 8 - r1,
             'a' + c2, 8 - r2);
}

// Centralized move execution helper
static void execute_move_with_updates(BoardWidget* board, Move* move) {
    if (!board || !move) return;
    
    // 1. Trigger Pre-Move Callback (for AI rating snapshots etc)
    if (board->preMoveCb) {
        char uci[32];
        render_move_uci(move, uci, sizeof(uci));
        board->preMoveCb(uci, board->preMoveData);
    }

    // 2. Update Game Logic
    bool moved = gamelogic_perform_move(board->logic, move);
    if (debug_mode) {
        printf("[BoardWidget] execute_move_with_updates: Move %d->%d. performed=%d. Logic Turn after: %d\n", 
               move->from_sq, move->to_sq, moved, board->logic->turn);
    }
    if (!moved) {
        // Special Check: Logic might be already updated (Replay Mode)
        // If the destination square already has the piece we expect, force a refresh
        // to ensure the final frame (with piece visible) is drawn.
        int r = move->to_sq / 8;
        int c = move->to_sq % 8;
        Piece* p = board->logic->board[r][c];
        if (p && p->owner == move->mover) {
             refresh_board(board);
        }
        return;
    }
    
    // 2. Play Sound (if enabled)
    // IMPORTANT: Play sound AFTER move so we can detect check/checkmate
    if (board->theme && sound_engine_is_enabled()) {
        play_move_sound(board, move);
    }
    
    // 3. Refresh Board
    // Update internal last move state for highlighting
    board->lastMoveFromRow = move->from_sq / 8;
    board->lastMoveFromCol = move->from_sq % 8;
    board->lastMoveToRow = move->to_sq / 8;
    board->lastMoveToCol = move->to_sq % 8;
    
    refresh_board(board);
    
    // Clear selection/valid moves as move is done
    board->selectedRow = -1;
    board->selectedCol = -1;
    free_valid_moves(board);

    // 4. Analysis and AI orchestration is now handled centrally via 
    // the GameLogic update callback (update_ui_callback in main.c).
    // The previous 'anim-finish-cb' is removed to avoid double-triggering.
}

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
    
    // Fallback to text rendering - ALWAYS use Segoe UI Symbol
    const char* symbol = theme_data_get_piece_symbol(board->theme, type, owner);
    const char* font_family = "Segoe UI Symbol";
    
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
    if (board->isAnimating && board->animatingMove) {
        Move* move = board->animatingMove;
        // Look up piece FRESH from board at start position (before move executes)
        int startRow = move->from_sq / 8, startCol = move->from_sq % 8;
        int endRow = move->to_sq / 8, endCol = move->to_sq % 8;
        Piece* piece = board->logic->board[startRow][startCol];
        
        if (piece) {
            int visualStartR, visualStartC, visualEndR, visualEndC;
            logical_to_visual(board, startRow, startCol, &visualStartR, &visualStartC);
            logical_to_visual(board, endRow, endCol, &visualEndR, &visualEndC);
            
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
        } else {
             // Fallback: Check destination if start is empty (Logic Updated case)
             piece = board->logic->board[endRow][endCol];
             if (piece) {
                int visualStartR, visualStartC, visualEndR, visualEndC;
                logical_to_visual(board, startRow, startCol, &visualStartR, &visualStartC);
                logical_to_visual(board, endRow, endCol, &visualEndR, &visualEndC);
                
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
        
        // Draw SECOND animated piece (Rook during castling)
        if (board->animCastlingRookMove) {
            Move* rookMove = board->animCastlingRookMove;
            int rRook = rookMove->from_sq / 8, cRook = rookMove->from_sq % 8;
            int rRookEnd = rookMove->to_sq / 8, cRookEnd = rookMove->to_sq % 8;
            Piece* piece = board->logic->board[rRook][cRook];
            if (!piece) piece = board->logic->board[rRookEnd][cRookEnd]; // Fallback
            
            if (piece) {
                int visualStartR, visualStartC, visualEndR, visualEndC;
                logical_to_visual(board, rRook, cRook, &visualStartR, &visualStartC);
                logical_to_visual(board, rRookEnd, cRookEnd, &visualEndR, &visualEndC);
                
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
    }
    
    // Draw dragged piece
    if (board->isDragging && board->dragSourceRow >= 0 && board->dragSourceCol >= 0) {
        // Look up piece FRESH from drag source
        Piece* piece = board->logic->board[board->dragSourceRow][board->dragSourceCol];
        if (piece) {
            double x = board->dragX;
            double y = board->dragY;
            
            double pieceSize = (width / 8.0);
            draw_piece_graphic(cr, board, piece->type, piece->owner, x, y, pieceSize * 0.85, 0.85);
        }
    }
}

// Draw a square with piece, highlighting, and dots
static void draw_square(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    BoardWidget* board = (BoardWidget*)user_data;
    
    // Safety check - if width/height are 0 or invalid, don't draw
    if (width <= 0 || height <= 0 || !board || !board->logic) {
        return;
    }
    
    // Extra safety: Don't draw if not mapped (visible)
    if (!gtk_widget_get_mapped(GTK_WIDGET(area))) return;
    
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
        
        int startR = (int)(move->from_sq / 8);
        int startC = (int)(move->from_sq % 8);
        int endR = (int)(move->to_sq / 8);
        int endC = (int)(move->to_sq % 8);
        
        // Hide Piece at START (Standard Mode / Before Logic Update)
        if (r == startR && c == startC) {
            hidePiece = true;
        }
        
        // Hide Piece at DESTINATION (Replay Mode / After Logic Update)
        // If legic is updated early, dest has the piece. We must hide it until animation ends.
        if (r == endR && c == endC) {
             Piece* p = board->logic->board[r][c];
             if (p && p->owner == move->mover) {
                 hidePiece = true;
             }
        }
        
        // Also hide the Rook if castling
        if (board->animCastlingRookMove) {
            // Note: Castling logic update might also need dest hiding, but Rook move is usually secondary
            // and might not trigger the "Double" distraction as much. 
            // The rook start is definitely hidden.
            if (r == (int)(board->animCastlingRookMove->from_sq / 8) && 
                c == (int)(board->animCastlingRookMove->from_sq % 8)) {
                hidePiece = true;
            }
             // Hide Rook Dest if already there
            int rDest = board->animCastlingRookMove->to_sq / 8;
            int cDest = board->animCastlingRookMove->to_sq % 8;
            if (r == rDest && c == cDest) {
                 Piece* p = board->logic->board[r][c];
                 if (p && p->type == PIECE_ROOK && p->owner == move->mover) {
                     hidePiece = true;
                 }
            }
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
                (board->validMoves[i]->from_sq / 8) == sourceRow &&
                (board->validMoves[i]->from_sq % 8) == sourceCol &&
                (board->validMoves[i]->to_sq / 8) == r && 
                (board->validMoves[i]->to_sq % 8) == c) {
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

    // Drag Highlights (Yellow Border + Light Fill to ensure visibility)
    if (board->isDragging) {
        bool isDragSource = (board->dragSourceRow == r && board->dragSourceCol == c);
        bool isDragHover = (board->dragOverRow == r && board->dragOverCol == c);
        
        if (isDragSource || isDragHover) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.3); // Yellow fill (low alpha)
            cairo_rectangle(cr, 2, 2, width - 4, height - 4);
            cairo_fill_preserve(cr); // Fill and keep path for stroke
            
            cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.8); // Yellow border (high alpha)
            cairo_set_line_width(cr, 4.0);
            cairo_stroke(cr);
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
    
    // Prevents drawing if widget is not ready to avoid GTK snapshot warnings
    if (gtk_widget_get_mapped(area) && gtk_widget_get_width(area) > 0 && gtk_widget_get_height(area) > 0) {
        gtk_widget_queue_draw(area);
    }
}

// Refresh entire board display
static void refresh_board(BoardWidget* board) {
    if (!board || !board->grid) return;
    
    // If board itself isn't mapped, don't try to update children
    if (!gtk_widget_get_mapped(board->grid)) return;

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
    
    // Disable interaction in CvC mode or if set as non-interactive (e.g. replay)
    if (board->logic->gameMode == GAME_MODE_CVC || !board->isInteractive) {
        return;
    }

    // Only allow interaction if it's the human's turn in PvC
    if (board->logic->gameMode == GAME_MODE_PVC && gamelogic_is_computer(board->logic, board->logic->turn)) {
        return;
    }
    // Skip if game is over (checkmate/stalemate)
    if (board->logic->isGameOver) {
        return;
    }
    
    // In Puzzle Mode, STRICTLY forbid selecting opponent's pieces
    if (board->logic->gameMode == GAME_MODE_PUZZLE) {
        GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
        int r = -1, c = -1;
        // Optimization: We need r,c here. We can get them early.
        int visualR = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "row"));
        int visualC = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "col"));
        visual_to_logical(board, visualR, visualC, &r, &c);
        
        Piece* p = board->logic->board[r][c];
        if (p && p->owner != board->logic->turn) {
            return; // Silently ignore clicks on opponent pieces
        }
    }
    
    // Don't prepare new drag if currently animating
    if (board->isAnimating) {
        return;
    }
    
    // Don't prepare new drag if already dragging or prepared
    if (board->isDragging || board->dragPrepared) {
        return;
    }
    
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int visualR = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "row"));
    int visualC = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "col"));
    
    // Convert visual coordinates to logical
    int r, c;
    visual_to_logical(board, visualR, visualC, &r, &c);
    
    Piece* piece = board->logic->board[r][c];
    
    // Prepare drag if piece belongs to current player
    if (piece && piece->owner == board->logic->turn) {
        // Start the clock on first interaction/toggle if needed
        gamelogic_start_clock_on_interaction(board->logic);

        board->dragPrepared = true;  // Mark as prepared, but NOT dragging yet
        board->isDragging = false;   // Not dragging until mouse moves
        board->dragSourceRow = r;  // Store logical coordinates
        board->dragSourceCol = c;
        
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
        
        // Get valid moves for this piece using new API
        free_valid_moves(board); // Clear old moves/array
        board->validMoves = gamelogic_get_valid_moves_for_piece(
            board->logic, r, c, &board->validMovesCount);
    }
}

// Motion handler for drag tracking (on grid level)
static void on_grid_motion(GtkEventControllerMotion* motion, double x, double y, gpointer user_data) {
    (void)motion;
    BoardWidget* board = (BoardWidget*)user_data;
    if (board->logic->gameMode == GAME_MODE_CVC || !board->isInteractive) return;
    
    // Only allow interaction if it's the human's turn in PvC
    if (board->logic->gameMode == GAME_MODE_PVC && gamelogic_is_computer(board->logic, board->logic->turn)) {
        return;
    }

    // If we have a prepared drag but haven't started dragging yet, check if mouse moved
    if (board->dragPrepared && !board->isDragging) {
        // Calculate distance from initial press position
        double dx = x - board->pressStartX;
        double dy = y - board->pressStartY;
        double distance = sqrt(dx * dx + dy * dy);
        
        // If mouse moved more than 5 pixels, start actual dragging
        if (distance > 5.0) {
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
        
        // Calculate drag over square
        GtkWidget* grid = board->grid;
        int grid_width = gtk_widget_get_width(grid);
        int grid_height = gtk_widget_get_height(grid);
        
        if (grid_width > 0 && grid_height > 0) {
            int visualCol = (int)((board->dragX / grid_width) * 8);
            int visualRow = (int)((board->dragY / grid_height) * 8);
            
            // Clamp
            if (visualRow < 0) visualRow = 0;
            if (visualRow >= 8) visualRow = 7;
            if (visualCol < 0) visualCol = 0;
            if (visualCol >= 8) visualCol = 7;
            
            int newOverRow, newOverCol;
            visual_to_logical(board, visualRow, visualCol, &newOverRow, &newOverCol);
            
            // If changed, update highlights
            if (newOverRow != board->dragOverRow || newOverCol != board->dragOverCol) {
                // Update old square to remove highlight
                if (board->dragOverRow >= 0 && board->dragOverCol >= 0) {
                    update_square(board, board->dragOverRow, board->dragOverCol);
                }
                
                board->dragOverRow = newOverRow;
                board->dragOverCol = newOverCol;
                
                // Update new square to add highlight
                update_square(board, board->dragOverRow, board->dragOverCol);
            }
        }

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
    if (!board || !board->isInteractive) return;
    if (board->logic->gameMode == GAME_MODE_CVC) return;
    
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
                        
        // Check if this is a valid destination
        if (dropRow >= 0 && dropCol >= 0) {
            bool isValid = false;
            Move* moveToMake = NULL;
            
            // Get valid moves for dragged piece
            if (board->validMovesCount == 0) {
                // Need to get valid moves first (if not cached/dragged from unknown state)
                free_valid_moves(board);
                board->validMoves = gamelogic_get_valid_moves_for_piece(
                    board->logic, board->dragSourceRow, board->dragSourceCol, &board->validMovesCount);
            }
            
            for (int i = 0; i < board->validMovesCount; i++) {
                if (board->validMoves[i] && 
                    (int)(board->validMoves[i]->to_sq / 8) == dropRow && 
                    (int)(board->validMoves[i]->to_sq % 8) == dropCol) {
                    isValid = true;
                    moveToMake = move_copy(board->validMoves[i]);
                    break;
                }
            }
            
            if (isValid && moveToMake) {
                // Tutorial Restriction Check on Execution
                if (board->restrictMoves) {
                    int r1 = moveToMake->from_sq / 8, c1 = moveToMake->from_sq % 8;
                    int r2 = moveToMake->to_sq / 8, c2 = moveToMake->to_sq % 8;
                    if (r1 != board->allowedStartRow ||
                        c1 != board->allowedStartCol ||
                        r2 != board->allowedEndRow ||
                        c2 != board->allowedEndCol) {
                        
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
                // Check if this is a promotion move (pawn reaching last rank)
                int r1 = moveToMake->from_sq / 8, c1 = moveToMake->from_sq % 8;
                int r2 = moveToMake->to_sq / 8;
                Piece* movingPiece = board->logic->board[r1][c1];
                if (movingPiece && movingPiece->type == PIECE_PAWN && 
                    (r2 == 0 || r2 == 7)) {
                    // Show promotion dialog
                    GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(board->grid), GTK_TYPE_WINDOW);
                    PieceType selected = promotion_dialog_show(GTK_WINDOW(window), board->theme, movingPiece->owner);
                    if (selected == NO_PROMOTION) {
                        // User cancelled - don't make the move
                        move_free(moveToMake);
                        board->isDragging = false;
                        board->dragPrepared = false;
                        animate_return_piece(board);
                        return;
                    }
                    moveToMake->promotionPiece = selected;
                }
                
                // Valid move - execute using centralized helper
                board->isDragging = false;
                board->dragPrepared = false;
                board->dragSourceRow = -1;
                board->dragSourceCol = -1;
                board->dragOverRow = -1;
                board->dragOverCol = -1;
                
                 if (board->animOverlay) {
                    gtk_widget_set_visible(board->animOverlay, FALSE);
                }
                
                execute_move_with_updates(board, moveToMake);
                
                move_free(moveToMake);
                return; 
            }
        }
        
        // Invalid drop
        animate_return_piece(board);
        board->isDragging = false;
        board->dragPrepared = false;
    } else if (board->dragPrepared && !board->isDragging) {
        // Was just a click - let on_square_clicked handle it
        board->dragPrepared = false;
        board->dragSourceRow = -1;
        board->dragSourceCol = -1;
        // Don't free valid moves - on_square_clicked will use them
        return; // CRITICAL: Don't interfere with click-to-move logic
    }
}

// Square click handler (for click-to-move mode)
static void on_square_clicked(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    BoardWidget* board = (BoardWidget*)user_data;
    
    // Disable interaction in CvC mode or if set as non-interactive
    if (board->logic->gameMode == GAME_MODE_CVC || !board->isInteractive) return;
    
    // Only allow interaction if it's the human's turn in PvC
    if (board->logic->gameMode == GAME_MODE_PVC && gamelogic_is_computer(board->logic, board->logic->turn)) {
        return;
    }

    // Skip if game is over (checkmate/stalemate)
    if (board->logic->isGameOver) {
        return;
    }
    
    // Skip if currently dragging or animating
    if (board->isDragging || board->isAnimating) {
        return;
    }
    
    // Skip if in drag mode
    if (board->dragMode) {
        return;
    }
    
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int visualR = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "row"));
    int visualC = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "col"));
    
    // Convert visual coordinates to logical (handle board flipping)
    int logicalR, logicalC;
    visual_to_logical(board, visualR, visualC, &logicalR, &logicalC);
    
    // If no piece selected, try to select one
    // Note: gamelogic_get_valid_moves_for_piece now returns empty list if it's not the player's turn or piece.
    if (board->selectedRow < 0 && !board->dragPrepared) {
        // Select this piece  
        board->selectedRow = logicalR;
        board->selectedCol = logicalC;
        
        // Get all legal moves starting from selected square
        free_valid_moves(board);
        board->validMoves = gamelogic_get_valid_moves_for_piece(
            board->logic, logicalR, logicalC, &board->validMovesCount);
        
        // If selection produced no valid moves (e.g. wrong turn or empty square), deselect immediately
        if (board->validMovesCount == 0) {
            board->selectedRow = -1;
            board->selectedCol = -1;
        } else {
             // Valid selection made - Ensure clock is running!
             gamelogic_ensure_clock_running(board->logic);
        }
        
        refresh_board(board);
    } else if (board->selectedRow >= 0 || board->dragSourceRow >= 0) {
        // We have a selection - check if this is a valid destination
        bool isValid = false;
        Move* moveToMake = NULL;
        
        for (int i = 0; i < board->validMovesCount; i++) {
            if (board->validMoves[i] && 
                (int)(board->validMoves[i]->to_sq / 8) == logicalR && 
                (int)(board->validMoves[i]->to_sq % 8) == logicalC) {
                isValid = true;
                moveToMake = move_copy(board->validMoves[i]);
                break;
            }
        }
        
        if (!isValid) {
            // Check if user clicked on another friendly piece (Switch Selection)
            Piece* clickedPiece = board->logic->board[logicalR][logicalC];
            if (clickedPiece && clickedPiece->owner == board->logic->turn) {
                // Switch selection!
                board->selectedRow = logicalR;
                board->selectedCol = logicalC;
                free_valid_moves(board);
                board->validMoves = gamelogic_get_valid_moves_for_piece(
                    board->logic, logicalR, logicalC, &board->validMovesCount);
                
                // Ensure clock running on switch too (redundant if already running but safe)
                if (board->validMovesCount > 0) {
                    gamelogic_ensure_clock_running(board->logic);
                }
                
                refresh_board(board);
                return;
            }
        }
        
        // Tutorial Restriction Check on Execution
        if (isValid && moveToMake && board->restrictMoves) {
            int r1 = moveToMake->from_sq / 8, c1 = moveToMake->from_sq % 8;
            int r2 = moveToMake->to_sq / 8, c2 = moveToMake->to_sq % 8;
            if (r1 != board->allowedStartRow ||
                c1 != board->allowedStartCol ||
                r2 != board->allowedEndRow ||
                c2 != board->allowedEndCol) {
                if (board->invalidMoveCb) {
                    board->invalidMoveCb(board->invalidMoveData);
                }
                move_free(moveToMake);
                moveToMake = NULL;
                isValid = false;
            }
        }

        if (isValid && moveToMake) {
            // Clear selection and hints immediately when valid move is clicked
            board->selectedRow = -1;
            board->selectedCol = -1;
            free_valid_moves(board);
            refresh_board(board);  // Update display to remove hints immediately
            // Valid move - execute with animation (or immediate if disabled)
            animate_move(board, moveToMake, NULL);
            
            // Clear selection after move initiated
            board->selectedRow = -1;
            board->selectedCol = -1;
            free_valid_moves(board);
        } else {           
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
        
        // Execute the move using centralized helper
        Move* move = board->animatingMove;
        execute_move_with_updates(board, move);
        
        move_free(move);
        board->animatingMove = NULL;
        
        // Clean up Rook animation move if present
        if (board->animCastlingRookMove) {
            move_free(board->animCastlingRookMove);
            board->animCastlingRookMove = NULL;
        }
        
        // Clean up animation state
        board->animTickId = 0;
        board->animStartTime = 0;
        board->animatingFromDrag = false;
        
        if (board->animOverlay) {
            gtk_widget_queue_draw(board->animOverlay);
            // Ensure overlay doesn't block input - hide it when animation is done
            gtk_widget_set_sensitive(board->animOverlay, FALSE); // Don't block input
            // Also hide it visually when not animating
            gtk_widget_set_visible(board->animOverlay, FALSE);
        }      
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
    int startRow = move->from_sq / 8, startCol = move->from_sq % 8;
    int endRow = move->to_sq / 8;
    Piece* movingPiece = board->logic->board[startRow][startCol];
    // CRITICAL: Store now - animation will look up piece AFTER it's moved!
    if (movingPiece) move->mover = movingPiece->owner;
    
    // Only show dialog if promotion piece is NOT set (User move)
    // AI moves will already have promotionPiece set
    if (movingPiece && movingPiece->type == PIECE_PAWN && 
        (endRow == 0 || endRow == 7)) {
        
        if (move->promotionPiece == NO_PROMOTION) {
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
    }
    
    if (!board->animationsEnabled) {
        // No animation - execute immediately
        execute_move_with_updates(board, move);
        move_free(move);
        return;
    }
    
    board->isAnimating = true;
    
    // Cleanup previous move if it wasn't finished
    if (board->animatingMove) {
        move_free(board->animatingMove);
        board->animatingMove = NULL;
    }
    
    board->animatingMove = move_copy(move); // Copy move for animation
    board->animProgress = 0.0;
    board->animStartTime = g_get_monotonic_time(); // Set start time for this animation
    board->animatingFromDrag = false; // Regular move animation from square

    // Detect Castling and setup Rook animation
    if (move->isCastling) {
        // Determine Rook's move based on King's move
        int r = move->from_sq / 8;
        int c = move->from_sq % 8; // King start col (e-file, index 4)
        int destC = move->to_sq % 8;
        
        // Ensure we handle both 0-7 and 7-0 row indexing consistently
        // (Move coords satisfy logic directly)
        
        int rRook = r;
        int cRookStart, cRookEnd;
        
        // Determine side based on target column
        if (destC > c) {
            // Kingside
            cRookStart = 7; // h-file
            cRookEnd = 5;   // f-file
        } else {
            // Queenside
            cRookStart = 0; // a-file
            cRookEnd = 3;   // d-file
        }
        
        board->animCastlingRookMove = move_create((uint8_t)(rRook * 8 + cRookStart), (uint8_t)(rRook * 8 + cRookEnd));
        board->animCastlingRookMove->mover = move->mover;
    } else {
        board->animCastlingRookMove = NULL;
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
    // Reset drag state and refresh display
    board->isDragging = false;
    board->dragPrepared = false;
    // DON'T clear dragSourceRow/Col - keep them so valid moves stay visible
    // board->dragSourceRow = -1;
    // board->dragSourceCol = -1;
    board->dragOverRow = -1;
    board->dragOverCol = -1;
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
        // Free animation moves
        if (board->animatingMove) move_free(board->animatingMove);
        if (board->animCastlingRookMove) move_free(board->animCastlingRookMove);
        
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
    board->lastMoveFromRow = -1;
    board->lastMoveFromCol = -1;
    board->lastMoveToRow = -1;
    board->lastMoveToCol = -1;
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
    board->dragOverRow = -1;
    board->dragOverCol = -1;
    board->pressStartX = 0;
    board->pressStartY = 0;
    board->isAnimating = false;
    board->animatingMove = NULL;
    board->animCastlingRookMove = NULL;
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
    board->isInteractive = true;
    
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
            
            gtk_grid_attach(GTK_GRID(board->grid), area, c, r, 1, 1);
        }
    }
    
    // Add motion controller to grid for drag tracking
    GtkEventControllerMotion* grid_motion = GTK_EVENT_CONTROLLER_MOTION(gtk_event_controller_motion_new());
    g_signal_connect(grid_motion, "motion", G_CALLBACK(on_grid_motion), board);
    gtk_widget_add_controller(board->grid, GTK_EVENT_CONTROLLER(grid_motion));
    
    // Set up cleanup - store data on frame instead of grid
    g_object_set_data_full(G_OBJECT(frame), BOARD_DATA_KEY, board, 
                          board_widget_destroy);
    
    // Store reference to frame in board for CSS
    g_object_set_data(G_OBJECT(board->grid), "board-frame", frame);
    
    // Initial refresh
    refresh_board(board);
    
    return frame; // Return frame instead of grid
}

// Set interaction mode
void board_widget_set_drag_mode(GtkWidget* board_widget, bool drag_mode) {
    BoardWidget* board = find_board_data(board_widget);
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
    BoardWidget* board = find_board_data(board_widget);
    return board ? board->dragMode : false;
}

// Refresh the board display
void board_widget_refresh(GtkWidget* board_widget) {
    if (!board_widget) {
        return;
    }
    
    BoardWidget* board = find_board_data(board_widget);
    
    if (board) {
        refresh_board(board);
    }
}

// Reset selection (clear selected piece)
void board_widget_reset_selection(GtkWidget* board_widget) {
    BoardWidget* board = find_board_data(board_widget);
    if (board) {
        board->selectedRow = -1;
        board->selectedCol = -1;
        free_valid_moves(board);
        refresh_board(board);
    }
}

// Set last move for yellow highlight (for replay mode)
void board_widget_set_last_move(GtkWidget* board_widget, int fromRow, int fromCol, int toRow, int toCol) {
    BoardWidget* board = find_board_data(board_widget);
    if (board) {
        board->lastMoveFromRow = fromRow;
        board->lastMoveFromCol = fromCol;
        board->lastMoveToRow = toRow;
        board->lastMoveToCol = toCol;
        refresh_board(board);
    }
}

// Set board orientation (flip board for black's perspective)
void board_widget_set_flipped(GtkWidget* board_widget, bool flipped) {
    BoardWidget* board = find_board_data(board_widget);
    if (board) {
        board->flipped = flipped;
        refresh_board(board);
    }
}

// Get board orientation
bool board_widget_get_flipped(GtkWidget* board_widget) {
    BoardWidget* board = find_board_data(board_widget);
    return board ? board->flipped : false;
}

// Set animations enabled
void board_widget_set_animations_enabled(GtkWidget* board_widget, bool enabled) {
    BoardWidget* board = find_board_data(board_widget);
    if (board) {
        board->animationsEnabled = enabled;
    }
}

// Get animations enabled
bool board_widget_get_animations_enabled(GtkWidget* board_widget) {
    BoardWidget* board = find_board_data(board_widget);
    return board ? board->animationsEnabled : true;
}

// Set hints mode: true = dots, false = squares
void board_widget_set_hints_mode(GtkWidget* board_widget, bool use_dots) {
    BoardWidget* board = find_board_data(board_widget);
    if (board) {
        board->useDots = use_dots;
    }
}

void board_widget_set_theme(GtkWidget* board_widget, ThemeData* theme) {
    BoardWidget* board = find_board_data(board_widget);
    if (board) {
        board->theme = theme;
        board_widget_refresh(board_widget);
    }
}

gboolean board_widget_animate_move(GtkWidget* board_widget, Move* move) {
    // 1. Valid inputs check
    if (!board_widget || !move) {
        g_warning("board_widget_animate_move: Invalid input (widget=%p, move=%p)", board_widget, move);
        return FALSE;
    }
    
    // 2. Thread safety check - ensure we are on main thread
    // Note: GTK calls usually happen on main thread, but as a safety measure for AI callbacks:
    // Ideally we would check thread ID, but g_main_context_default() check is decent proxy if needed.
    // For now, we rely on caller correctness as this is a widget method, 
    // but the robust lookup prevents hard crashes.
    
    // 3. Centralized lookup
    BoardWidget* board = find_board_data(board_widget);
    
    if (!board) {
        g_warning("board_widget_animate_move: Could not find board data on widget type %s", 
                  G_OBJECT_TYPE_NAME(board_widget));
        return FALSE;
    }
    
    // 4. Execute
    animate_move(board, move, NULL);
    return TRUE;
}

// Check if animating
bool board_widget_is_animating(GtkWidget* board_widget) {
    BoardWidget* board = find_board_data(board_widget);
    return board ? board->isAnimating : false;
}

// Get hints mode: true = dots, false = squares
bool board_widget_get_hints_mode(GtkWidget* board_widget) {
    BoardWidget* board = find_board_data(board_widget);
    return board ? board->useDots : true;
}

// Set nav restriction
void board_widget_set_nav_restricted(GtkWidget* board_widget, bool restricted, int startR, int startC, int endR, int endC) {
    BoardWidget* board = find_board_data(board_widget);
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
    BoardWidget* board = find_board_data(board_widget);
    if (!board) return;
    board->invalidMoveCb = cb;
    board->invalidMoveData = data;
}

void board_widget_set_pre_move_callback(GtkWidget* board_widget, BoardPreMoveCallback cb, void* data) {
    BoardWidget* board = find_board_data(board_widget);
    if (!board) return;
    board->preMoveCb = cb;
    board->preMoveData = data;
}

// Helper to check if board is flipped
bool board_widget_is_flipped(GtkWidget* board_widget) {
    return board_widget_get_flipped(board_widget);
}

void board_widget_set_interactive(GtkWidget* board_widget, bool interactive) {
    BoardWidget* board = find_board_data(board_widget);
    if (board) {
        board->isInteractive = interactive;
        if (!interactive) {
            board_widget_reset_selection(board_widget);
        }
    }
}

// Cancel any ongoing animation immediately
void board_widget_cancel_animation(GtkWidget* board_widget) {
    BoardWidget* board = find_board_data(board_widget);
    if (!board) return;

    if (board->animTickId > 0) {
        g_source_remove(board->animTickId);
        board->animTickId = 0;
    }
    
    board->isAnimating = false;
    if (board->animOverlay) {
        gtk_widget_unparent(board->animOverlay); 
        board->animOverlay = NULL;
    }
    
    // Force immediate refresh to ensure pieces act normal
    refresh_board(board);
}
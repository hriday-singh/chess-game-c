#include "gamelogic.h"
#include "move.h"
#include "piece.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations
static bool is_valid_pos(int r, int c);
static void get_pseudo_moves(GameLogic* logic, int r, int c, Piece* p, void* moves_list);
static void add_moves_single_step(GameLogic* logic, int r, int c, int offsets[][2], int num_offsets, void* moves_list, Player owner);
static void add_linear_moves(GameLogic* logic, int r, int c, int dirs[][2], int num_dirs, void* moves_list, Player owner);
static bool can_castle(GameLogic* logic, int r, int kCol, int rCol);

// Simple list for moves (we'll use a dynamic array)
typedef struct {
    Move** moves;
    int count;
    int capacity;
} MoveList;

static MoveList* movelist_create(void) {
    MoveList* list = (MoveList*)malloc(sizeof(MoveList));
    if (list) {
        list->capacity = 32;
        list->count = 0;
        list->moves = (Move**)malloc(sizeof(Move*) * list->capacity);
    }
    return list;
}

static void movelist_add(MoveList* list, Move* move) {
    if (!list || !move) return;
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->moves = (Move**)realloc(list->moves, sizeof(Move*) * list->capacity);
    }
    list->moves[list->count++] = move;
}

static void movelist_free(MoveList* list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        move_free(list->moves[i]);
    }
    free(list->moves);
    free(list);
}

// Generate legal moves for a player
void gamelogic_generate_legal_moves(GameLogic* logic, Player player, void* moves_list) {
    if (!logic) return;
    
    MoveList* pseudo_moves = movelist_create();
    
    // Generate pseudo-legal moves
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            Piece* p = logic->board[r][c];
            if (p && p->owner == player) {
                get_pseudo_moves(logic, r, c, p, pseudo_moves);
            }
        }
    }
    
    // Filter to only legal moves
    MoveList* legal_moves = (MoveList*)moves_list;
    if (!legal_moves) {
        legal_moves = movelist_create();
        // Note: We can't modify the void* parameter, so caller should pass a valid list
    }
    
    // Declare extern for simulate function
    extern bool gamelogic_simulate_move_and_check_safety(GameLogic* logic, Move* m, Player p);
    
    for (int i = 0; i < pseudo_moves->count; i++) {
        Move* m = pseudo_moves->moves[i];
        if (gamelogic_simulate_move_and_check_safety(logic, m, player)) {
            if (legal_moves) {
                movelist_add(legal_moves, move_create(m->startRow, m->startCol, m->endRow, m->endCol));
                Move* new_move = legal_moves->moves[legal_moves->count - 1];
                new_move->promotionPiece = m->promotionPiece;
                new_move->isCastling = m->isCastling;
                new_move->isEnPassant = m->isEnPassant;  // Preserve en passant flag
            }
        }
    }
    
    // Free pseudo moves
    movelist_free(pseudo_moves);
}

// Get pseudo-legal moves for a piece
static void get_pseudo_moves(GameLogic* logic, int r, int c, Piece* p, void* moves_list) {
    MoveList* moves = (MoveList*)moves_list;
    if (!moves || !p) return;
    
    int forward = (p->owner == PLAYER_WHITE) ? -1 : 1;
    
    switch (p->type) {
        case PIECE_PAWN: {
            // Forward move
            if (is_valid_pos(r + forward, c) && logic->board[r + forward][c] == NULL) {
                Move* m = move_create(r, c, r + forward, c);
                movelist_add(moves, m);
                
                // Double move from starting position
                bool isStartRank = (p->owner == PLAYER_WHITE && r == 6) || 
                                  (p->owner == PLAYER_BLACK && r == 1);
                if (!p->hasMoved && isStartRank && is_valid_pos(r + (forward * 2), c) && 
                    logic->board[r + (forward * 2)][c] == NULL) {
                    movelist_add(moves, move_create(r, c, r + (forward * 2), c));
                }
            }
            
            // Capture moves
            int cols[] = {c - 1, c + 1};
            for (int i = 0; i < 2; i++) {
                int targetCol = cols[i];
                if (is_valid_pos(r + forward, targetCol)) {
                    Piece* target = logic->board[r + forward][targetCol];
                    if (target && target->owner != p->owner) {
                        movelist_add(moves, move_create(r, c, r + forward, targetCol));
                    }
                    // En passant
                    // En passant can only be done from the 5th rank (row 3 for white, row 4 for black)
                    if (target == NULL && 
                        ((p->owner == PLAYER_WHITE && r == 3) || (p->owner == PLAYER_BLACK && r == 4)) &&
                        targetCol == logic->enPassantCol && logic->enPassantCol != -1) {
                        Piece* epPawn = logic->board[r][targetCol];
                        if (epPawn && epPawn->type == PIECE_PAWN && epPawn->owner != p->owner) {
                            Move* epMove = move_create(r, c, r + forward, targetCol);
                            epMove->isEnPassant = 1;  // Mark as en passant move
                            movelist_add(moves, epMove);
                        }
                    }
                }
            }
            break;
        }
        
        case PIECE_KNIGHT: {
            int offsets[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, 
                                {1, -2}, {1, 2}, {2, -1}, {2, 1}};
            add_moves_single_step(logic, r, c, offsets, 8, moves, p->owner);
            break;
        }
        
        case PIECE_KING: {
            int offsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, 
                                {0, 1}, {1, -1}, {1, 0}, {1, 1}};
            add_moves_single_step(logic, r, c, offsets, 8, moves, p->owner);
            
            // Castling
            if (!p->hasMoved && !gamelogic_is_in_check(logic, p->owner)) {
                // Kingside
                if (can_castle(logic, r, c, 7)) {
                    Move* m = move_create(r, c, r, 6);
                    m->isCastling = true;
                    movelist_add(moves, m);
                }
                // Queenside
                if (can_castle(logic, r, c, 0)) {
                    Move* m = move_create(r, c, r, 2);
                    m->isCastling = true;
                    movelist_add(moves, m);
                }
            }
            break;
        }
        
        case PIECE_ROOK: {
            int dirs[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
            add_linear_moves(logic, r, c, dirs, 4, moves, p->owner);
            break;
        }
        
        case PIECE_BISHOP: {
            int dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            add_linear_moves(logic, r, c, dirs, 4, moves, p->owner);
            break;
        }
        
        case PIECE_QUEEN: {
            int dirs[8][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, 
                             {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            add_linear_moves(logic, r, c, dirs, 8, moves, p->owner);
            break;
        }
    }
}

// Add single-step moves (Knight, King)
static void add_moves_single_step(GameLogic* logic, int r, int c, int offsets[][2], int num_offsets, void* moves_list, Player owner) {
    MoveList* moves = (MoveList*)moves_list;
    if (!moves) return;
    
    for (int i = 0; i < num_offsets; i++) {
        int nr = r + offsets[i][0];
        int nc = c + offsets[i][1];
        if (is_valid_pos(nr, nc)) {
            Piece* target = logic->board[nr][nc];
            if (target == NULL || target->owner != owner) {
                movelist_add(moves, move_create(r, c, nr, nc));
            }
        }
    }
}

// Add linear moves (Rook, Bishop, Queen)
static void add_linear_moves(GameLogic* logic, int r, int c, int dirs[][2], int num_dirs, void* moves_list, Player owner) {
    MoveList* moves = (MoveList*)moves_list;
    if (!moves) return;
    
    for (int i = 0; i < num_dirs; i++) {
        int nr = r + dirs[i][0];
        int nc = c + dirs[i][1];
        while (is_valid_pos(nr, nc)) {
            Piece* target = logic->board[nr][nc];
            if (target == NULL) {
                movelist_add(moves, move_create(r, c, nr, nc));
            } else {
                if (target->owner != owner) {
                    movelist_add(moves, move_create(r, c, nr, nc));
                }
                break;
            }
            nr += dirs[i][0];
            nc += dirs[i][1];
        }
    }
}

// Check if castling is possible
static bool can_castle(GameLogic* logic, int r, int kCol, int rCol) {
    Piece* rook = logic->board[r][rCol];
    if (!rook || rook->type != PIECE_ROOK || rook->hasMoved) return false;
    
    // Path must be clear
    int start = (kCol < rCol) ? kCol + 1 : rCol + 1;
    int end = (kCol > rCol) ? kCol : rCol;
    for (int i = start; i < end; i++) {
        if (logic->board[r][i] != NULL) return false;
    }
    
    // Safety checks
    Piece* king = logic->board[r][kCol];
    if (!king) return false;
    Player p = king->owner;
    int step = (rCol > kCol) ? 1 : -1;
    
    // Check intermediate square (need to declare extern)
    extern bool gamelogic_is_square_safe(GameLogic* logic, int r, int c, Player p);
    if (!gamelogic_is_square_safe(logic, r, kCol + step, p)) return false;
    
    // Check landing square
    if (!gamelogic_is_square_safe(logic, r, kCol + (step * 2), p)) return false;
    
    return true;
}

// Helper functions
static bool is_valid_pos(int r, int c) {
    return (r >= 0 && r < 8 && c >= 0 && c < 8);
}

// --- Cache Management and UI API ---

void gamelogic_clear_cache(GameLogic* logic) {
    if (!logic) return;
    if (logic->cachedMoves) {
        movelist_free((MoveList*)logic->cachedMoves);
        logic->cachedMoves = NULL;
    }
    logic->cachedPieceRow = -1;
    logic->cachedPieceCol = -1;
}

static Move** move_array_clone(MoveList* list, int* count) {
    if (!list || list->count == 0) {
        *count = 0;
        return NULL;
    }
    *count = list->count;
    Move** arr = (Move**)malloc(sizeof(Move*) * list->count);
    for (int i = 0; i < list->count; i++) {
        arr[i] = move_copy(list->moves[i]);
    }
    return arr;
}

Move** gamelogic_get_valid_moves_for_piece(GameLogic* logic, int row, int col, int* count) {
    if (!logic || !is_valid_pos(row, col)) {
        if (count) *count = 0;
        return NULL;
    }

    // Check cache
    if (logic->cachedMoves && logic->cachedPieceRow == row && logic->cachedPieceCol == col) {
        return move_array_clone((MoveList*)logic->cachedMoves, count);
    }
    
    // Cache miss - regenerate
    gamelogic_clear_cache(logic);
    
    Piece* p = logic->board[row][col];
    if (!p) {
        if (count) *count = 0;
        return NULL;
    }
    
    MoveList* pseudo = movelist_create();
    get_pseudo_moves(logic, row, col, p, pseudo);
    
    MoveList* valid = movelist_create();
    
    extern bool gamelogic_simulate_move_and_check_safety(GameLogic* logic, Move* m, Player p);
    
    for (int i = 0; i < pseudo->count; i++) {
        Move* m = pseudo->moves[i];
        if (gamelogic_simulate_move_and_check_safety(logic, m, p->owner)) {
            Move* clone = move_copy(m);
            // Ensure flags like isEnPassant are preserved
            clone->isEnPassant = m->isEnPassant; 
            clone->promotionPiece = m->promotionPiece;
            movelist_add(valid, clone);
        }
    }
    
    movelist_free(pseudo);
    
    // Store in cache
    logic->cachedMoves = valid;
    logic->cachedPieceRow = row;
    logic->cachedPieceCol = col;
    
    return move_array_clone(valid, count);
}

bool gamelogic_is_move_valid(GameLogic* logic, int startRow, int startCol, int endRow, int endCol) {
    int count = 0;
    Move** moves = gamelogic_get_valid_moves_for_piece(logic, startRow, startCol, &count);
    
    bool valid = false;
    if (moves) {
        for (int i = 0; i < count; i++) {
            if (moves[i]->endRow == endRow && moves[i]->endCol == endCol) {
                valid = true;
                break;
            }
        }
        gamelogic_free_moves_array(moves, count);
    }
    return valid;
}

void gamelogic_free_moves_array(Move** moves, int count) {
    if (!moves) return;
    for (int i = 0; i < count; i++) {
        move_free(moves[i]);
    }
    free(moves);
}


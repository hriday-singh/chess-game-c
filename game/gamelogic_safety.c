#include "gamelogic.h"
#include "piece.h"
#include "move.h"
#include <stdbool.h>
#include <stdlib.h>

// Forward declarations
static bool is_valid_pos(int r, int c);
static Player get_opponent(Player p);
static bool is_threat(GameLogic* logic, int r, int c, Player opp, PieceType type);
static bool is_line_threat(GameLogic* logic, int r, int c, int dr, int dc, Player oppOwner, PieceType t1, PieceType t2);

// Check if a square is safe for a player
bool gamelogic_is_square_safe(GameLogic* logic, int r, int c, Player p) {
    if (!logic || !is_valid_pos(r, c)) return false;
    
    Player opp = get_opponent(p);
    
    // Knight threats
    int kOffsets[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, 
                          {1, -2}, {1, 2}, {2, -1}, {2, 1}};
    for (int i = 0; i < 8; i++) {
        if (is_threat(logic, r + kOffsets[i][0], c + kOffsets[i][1], opp, PIECE_KNIGHT)) {
            return false;
        }
    }
    
    // Sliding threats (Rook, Bishop, Queen)
    int rookDirs[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    for (int i = 0; i < 4; i++) {
        if (is_line_threat(logic, r, c, rookDirs[i][0], rookDirs[i][1], opp, PIECE_ROOK, PIECE_QUEEN)) {
            return false;
        }
    }
    
    int bishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (int i = 0; i < 4; i++) {
        if (is_line_threat(logic, r, c, bishopDirs[i][0], bishopDirs[i][1], opp, PIECE_BISHOP, PIECE_QUEEN)) {
            return false;
        }
    }
    
    // Pawn threats
    int enemyPawnRow = (p == PLAYER_WHITE) ? r - 1 : r + 1;
    if (is_threat(logic, enemyPawnRow, c - 1, opp, PIECE_PAWN)) return false;
    if (is_threat(logic, enemyPawnRow, c + 1, opp, PIECE_PAWN)) return false;
    
    // King threats
    int kingOffsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, 
                             {0, 1}, {1, -1}, {1, 0}, {1, 1}};
    for (int i = 0; i < 8; i++) {
        if (is_threat(logic, r + kingOffsets[i][0], c + kingOffsets[i][1], opp, PIECE_KING)) {
            return false;
        }
    }
    
    return true;
}

// Check if player is in check
bool gamelogic_is_in_check(GameLogic* logic, Player player) {
    if (!logic) return false;
    
    // Find king
    int kr = -1, kc = -1;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            Piece* pc = logic->board[r][c];
            if (pc && pc->owner == player && pc->type == PIECE_KING) {
                kr = r;
                kc = c;
                break;
            }
        }
        if (kr != -1) break;
    }
    
    if (kr == -1) return false;
    
    // Check if king's square is attacked
    return !gamelogic_is_square_safe(logic, kr, kc, player);
}

// Simple move list for checking moves
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

static void movelist_free(MoveList* list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        move_free(list->moves[i]);
    }
    free(list->moves);
    free(list);
}

// Check if player is in checkmate
bool gamelogic_is_checkmate(GameLogic* logic, Player player) {
    if (!logic) return false;
    if (!gamelogic_is_in_check(logic, player)) return false;
    
    // Generate legal moves - if none exist, it's checkmate
    MoveList* moves_list = movelist_create();
    gamelogic_generate_legal_moves(logic, player, moves_list);
    bool is_mate = (moves_list->count == 0);
    movelist_free(moves_list);
    
    return is_mate;
}

// Check if player is in stalemate
bool gamelogic_is_stalemate(GameLogic* logic, Player player) {
    if (!logic) return false;
    if (gamelogic_is_in_check(logic, player)) return false;
    
    // Generate legal moves - if none exist, it's stalemate
    MoveList* moves_list = movelist_create();
    gamelogic_generate_legal_moves(logic, player, moves_list);
    bool is_stale = (moves_list->count == 0);
    movelist_free(moves_list);
    
    return is_stale;
}

// Helper: Check if there's a threat at a position
static bool is_threat(GameLogic* logic, int r, int c, Player opp, PieceType type) {
    if (!is_valid_pos(r, c)) return false;
    Piece* p = logic->board[r][c];
    return (p && p->owner == opp && p->type == type);
}

// Helper: Check for line threats (Rook, Bishop, Queen)
static bool is_line_threat(GameLogic* logic, int r, int c, int dr, int dc, Player oppOwner, PieceType t1, PieceType t2) {
    int nr = r + dr;
    int nc = c + dc;
    
    while (is_valid_pos(nr, nc)) {
        Piece* p = logic->board[nr][nc];
        if (p) {
            if (p->owner == oppOwner && (p->type == t1 || p->type == t2)) {
                return true;
            }
            return false;  // Path blocked
        }
        nr += dr;
        nc += dc;
    }
    return false;
}

// Helper functions
static bool is_valid_pos(int r, int c) {
    return (r >= 0 && r < 8 && c >= 0 && c < 8);
}

static Player get_opponent(Player p) {
    return (p == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
}


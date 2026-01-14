#include "zobrist.h"
#include "gamelogic.h"
#include <stdint.h>
#include <stdbool.h>

static uint64_t piece_keys[64][12]; // type * 2 + color
static uint64_t castling_keys[16];
static uint64_t ep_keys[8];
static uint64_t side_key;
static int initialized = 0;

// Simple 64-bit PRNG
static uint64_t splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

void zobrist_init(void) {
    if (initialized) return;
    
    uint64_t state = 42; // Fixed seed
    
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 12; j++) {
            piece_keys[i][j] = splitmix64(&state);
        }
    }
    
    for (int i = 0; i < 16; i++) {
        castling_keys[i] = splitmix64(&state);
    }
    
    for (int i = 0; i < 8; i++) {
        ep_keys[i] = splitmix64(&state);
    }
    
    side_key = splitmix64(&state);
    
    initialized = 1;
}

uint64_t zobrist_compute(GameLogic* logic) {
    if (!initialized) zobrist_init();
    if (!logic) return 0;
    
    uint64_t hash = 0;
    
    // Board
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            Piece* p = logic->board[r][c];
            if (p) {
                int sq = r * 8 + c;
                int piece_idx = p->type * 2 + p->owner;
                hash ^= piece_keys[sq][piece_idx];
            }
        }
    }
    
    // Turn
    if (logic->turn == PLAYER_BLACK) {
        hash ^= side_key;
    }
    
    // Castling
    hash ^= castling_keys[logic->castlingRights & 0xF];
    
    // En Passant
    // Only include if a pawn can actually capture (as requested for tenfold/threefold repetition)
    // For now we'll implement the simple version and refine later if needed, 
    // or just check for presence of opponent pawns.
    if (logic->enPassantCol != -1) {
        // Strict check: can ANY pawn of the side to move capture it?
        bool can_capture = false;
        int pawnRow = (logic->turn == PLAYER_WHITE) ? 3 : 4;   // e4 or e5
        
        // Check neighbors
        if (logic->enPassantCol > 0) {
            Piece* p = logic->board[pawnRow][logic->enPassantCol - 1];
            if (p && p->type == PIECE_PAWN && p->owner == logic->turn) can_capture = true;
        }
        if (!can_capture && logic->enPassantCol < 7) {
            Piece* p = logic->board[pawnRow][logic->enPassantCol + 1];
            if (p && p->type == PIECE_PAWN && p->owner == logic->turn) can_capture = true;
        }
        
        if (can_capture) {
            hash ^= ep_keys[logic->enPassantCol];
        }
    }
    
    return hash;
}

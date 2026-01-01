#ifndef PIECE_H
#define PIECE_H

#include "types.h"

// Piece value function (equivalent to Java getValue())
static inline int piece_get_value(Piece* p) {
    if (!p) return 0;
    switch (p->type) {
        case PIECE_PAWN: return 100;
        case PIECE_KNIGHT: return 320;
        case PIECE_BISHOP: return 330;
        case PIECE_ROOK: return 500;
        case PIECE_QUEEN: return 900;
        case PIECE_KING: return 20000;
        default: return 0;
    }
}

// Create a new piece
Piece* piece_create(PieceType type, Player owner);

// Copy a piece
Piece* piece_copy(Piece* other);

// Free a piece
void piece_free(Piece* p);

#endif // PIECE_H


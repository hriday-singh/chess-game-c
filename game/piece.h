#ifndef PIECE_H
#define PIECE_H

#include "types.h"

// Create a new piece
Piece* piece_create(PieceType type, Player owner);

// Copy a piece
Piece* piece_copy(Piece* other);

// Free a piece
void piece_free(Piece* p);

#endif // PIECE_H


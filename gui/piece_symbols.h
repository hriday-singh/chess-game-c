#ifndef PIECE_SYMBOLS_H
#define PIECE_SYMBOLS_H

#include "../game/types.h"

// Get Unicode chess piece symbol for display
// Returns the Unicode character string for the given piece type and owner
const char* piece_symbols_get(PieceType type, Player owner);

#endif // PIECE_SYMBOLS_H


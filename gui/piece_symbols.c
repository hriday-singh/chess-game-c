#include "piece_symbols.h"
#include "../game/types.h"

// Unicode chess piece symbols (white and black)
// Order: KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN (matches PieceType enum)
static const char* PIECE_SYMBOLS[2][6] = {
    // White pieces: ♔♕♖♗♘♙
    {"♔", "♕", "♖", "♗", "♘", "♙"},
    // Black pieces: ♚♛♜♝♞♟
    {"♚", "♛", "♜", "♝", "♞", "♟"}
};

// Get piece symbol for display
const char* piece_symbols_get(PieceType type, Player owner) {
    if (type < 0 || type > 5) return "";
    int ownerIdx = (owner == PLAYER_WHITE) ? 0 : 1;
    return PIECE_SYMBOLS[ownerIdx][type];
}


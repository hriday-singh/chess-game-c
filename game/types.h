#ifndef TYPES_H
#define TYPES_H

// Enums converted from Java
typedef enum {
    PLAYER_WHITE = 0,
    PLAYER_BLACK = 1
} Player;

typedef enum {
    PIECE_KING = 0,
    PIECE_QUEEN = 1,
    PIECE_ROOK = 2,
    PIECE_BISHOP = 3,
    PIECE_KNIGHT = 4,
    PIECE_PAWN = 5
} PieceType;

typedef enum {
    GAME_MODE_PVP = 0,
    GAME_MODE_PVC = 1,
    GAME_MODE_CVC = 2,
    GAME_MODE_PUZZLE = 3
} GameMode;

typedef enum {
    AI_TYPE_PRO = 0,
    AI_TYPE_STOCKFISH = 1,
    AI_TYPE_CUSTOM = 2
} AIType;

// Piece structure
typedef struct {
    PieceType type;
    Player owner;
    int hasMoved;
} Piece;

// Move structure
typedef struct {
    int startRow;
    int startCol;
    int endRow;
    int endCol;
    PieceType promotionPiece;
    Piece* capturedPiece;
    int isEnPassant;
    int isCastling;
    int firstMove;
    int rookFirstMove;
    Player mover;
} Move;

// Sentinel value for "no promotion" (PIECE_KING is 0, so 6 is safe)
#define NO_PROMOTION ((PieceType)6)

// Forward declarations
typedef struct GameLogic GameLogic;
typedef struct AIGenome AIGenome;

#endif // TYPES_H


#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

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
    GAME_MODE_PUZZLE = 3,
    GAME_MODE_TUTORIAL = 4
} GameMode;

// Piece structure
typedef struct {
    PieceType type;
    Player owner;
    int hasMoved;
} Piece;

// Sentinel value for "no piece/promotion"
#define NO_PIECE ((PieceType)7)
#define NO_PROMOTION ((PieceType)6)

// Move structure
typedef struct {
    uint8_t from_sq; // 0-63
    uint8_t to_sq;   // 0-63
    PieceType movedPieceType;    // The piece that is moving
    PieceType promotionPiece;
    PieceType capturedPieceType; // PIECE_KING..PIECE_PAWN or NO_PIECE
    int isEnPassant;
    int isCastling;
    int firstMove;       // To restore hasMoved
    int rookFirstMove;   // To restore hasMoved for rook
    Player mover;

    // Undo state
    uint8_t prevCastlingRights;
    int8_t prevEnPassantCol;
    int prevHalfmoveClock;
} Move;

// Position Snapshot
typedef struct {
    uint8_t board[64];      // (PieceType + 1) << 1 | Player, 0 if empty
    uint64_t hasMovedMask;  // Bitmask: bit i is set if square i's piece hasMoved
    Player turn;
    uint8_t castlingRights; // Bits: 1=WK, 2=WQ, 4=BK, 8=BQ
    int8_t enPassantCol;    // -1 if none
    int halfmoveClock;
    int fullmoveNumber;
    uint64_t zobristHash;
} PositionSnapshot;

// Forward declarations
typedef struct GameLogic GameLogic;
typedef struct AIGenome AIGenome;

#endif // TYPES_H


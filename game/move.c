#include "move.h"
#include "types.h"
#include <stdlib.h>

Move* move_create(uint8_t from, uint8_t to) {
    Move* m = (Move*)malloc(sizeof(Move));
    if (m) {
        m->from_sq = from;
        m->to_sq = to;
        m->promotionPiece = NO_PROMOTION;
        m->capturedPieceType = NO_PIECE;
        m->isEnPassant = 0;
        m->isCastling = 0;
        m->firstMove = 0;
        m->rookFirstMove = 0;
        m->mover = PLAYER_WHITE;
        m->prevCastlingRights = 0;
        m->prevEnPassantCol = -1;
        m->prevHalfmoveClock = 0;
    }
    return m;
}

Move* move_copy(Move* src) {
    if (!src) return NULL;
    Move* m = (Move*)malloc(sizeof(Move));
    if (m) {
        *m = *src; // Shallow copy works for value types
    }
    return m;
}

int move_equals(Move* a, Move* b) {
    if (!a || !b) return 0;
    return (a->from_sq == b->from_sq &&
            a->to_sq == b->to_sq &&
            a->promotionPiece == b->promotionPiece);
}

void move_free(Move* m) {
    if (m) {
        free(m);
    }
}


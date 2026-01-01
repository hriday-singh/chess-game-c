#include "move.h"
#include "piece.h"
#include "types.h"
#include <stdlib.h>

Move* move_create(int r1, int c1, int r2, int c2) {
    Move* m = (Move*)malloc(sizeof(Move));
    if (m) {
        m->startRow = r1;
        m->startCol = c1;
        m->endRow = r2;
        m->endCol = c2;
        m->promotionPiece = NO_PROMOTION;  // NO_PROMOTION means no promotion
        m->capturedPiece = NULL;
        m->isEnPassant = 0;
        m->isCastling = 0;
        m->firstMove = 0;
    }
    return m;
}

Move* move_copy(Move* src) {
    if (!src) return NULL;
    Move* m = (Move*)malloc(sizeof(Move));
    if (m) {
        m->startRow = src->startRow;
        m->startCol = src->startCol;
        m->endRow = src->endRow;
        m->endCol = src->endCol;
        m->promotionPiece = src->promotionPiece;
        m->isEnPassant = src->isEnPassant;
        m->isCastling = src->isCastling;
        m->firstMove = src->firstMove;
        // Deep copy captured piece if it exists
        if (src->capturedPiece) {
            m->capturedPiece = piece_copy(src->capturedPiece);
        } else {
            m->capturedPiece = NULL;
        }
    }
    return m;
}

int move_equals(Move* a, Move* b) {
    if (!a || !b) return 0;
    return (a->startRow == b->startRow &&
            a->startCol == b->startCol &&
            a->endRow == b->endRow &&
            a->endCol == b->endCol);
}

void move_free(Move* m) {
    if (m) {
        if (m->capturedPiece) {
            piece_free(m->capturedPiece);
        }
        free(m);
    }
}



#include "move.h"
#include "types.h"
#include <stdlib.h>

Move* move_create(uint8_t from, uint8_t to) {
    Move* m = (Move*)malloc(sizeof(Move));
    if (m) {
        m->from_sq = from;
        m->to_sq = to;
        m->movedPieceType = PIECE_PAWN;
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
        m->prevWhiteTimeMs = 0;
        m->prevBlackTimeMs = 0;
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

void move_to_uci(Move* m, char* buf) {
    if (!m || !buf) return;
    
    int r1 = m->from_sq / 8;
    int c1 = m->from_sq % 8;
    int r2 = m->to_sq / 8;
    int c2 = m->to_sq % 8;
    
    // UCI: file (a-h), rank (1-8). 
    // Internal: row 0 = Rank 8, row 7 = Rank 1.
    // So 'rank' char is '8' - r.
    
    char* ptr = buf;
    *ptr++ = 'a' + c1;
    *ptr++ = '8' - r1;
    *ptr++ = 'a' + c2;
    *ptr++ = '8' - r2;
    
    if (m->promotionPiece != NO_PROMOTION) {
        char pChar = ' ';
        switch(m->promotionPiece) {
            case PIECE_QUEEN: pChar = 'q'; break;
            case PIECE_ROOK: pChar = 'r'; break;
            case PIECE_BISHOP: pChar = 'b'; break;
            case PIECE_KNIGHT: pChar = 'n'; break;
            default: break;
        }
        if (pChar != ' ') *ptr++ = pChar;
    }
    
    *ptr = '\0';
}

#include "piece.h"
#include <stdlib.h>

Piece* piece_create(PieceType type, Player owner) {
    Piece* p = (Piece*)malloc(sizeof(Piece));
    if (p) {
        // Validate inputs to prevent corruption
        if (type < 0 || type > 5) {
            free(p);
            return NULL;
        }
        if (owner != PLAYER_WHITE && owner != PLAYER_BLACK) {
            free(p);
            return NULL;
        }
        p->type = type;
        p->owner = owner;
        p->hasMoved = 0;
    }
    return p;
}

Piece* piece_copy(Piece* other) {
    if (!other) return NULL;
    // Validate source piece to prevent corruption
    if (other->type < 0 || other->type > 5) {
        return NULL;
    }
    if (other->owner != PLAYER_WHITE && other->owner != PLAYER_BLACK) {
        return NULL;
    }
    Piece* p = (Piece*)malloc(sizeof(Piece));
    if (p) {
        p->type = other->type;
        p->owner = other->owner;
        p->hasMoved = other->hasMoved;
    }
    return p;
}

void piece_free(Piece* p) {
    if (p) free(p);
}


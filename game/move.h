#ifndef MOVE_H
#define MOVE_H

#include "types.h"

// Create a new move
Move* move_create(uint8_t from, uint8_t to);

// Copy a move (deep copy, including captured piece if any)
Move* move_copy(Move* src);

// Check if two moves are equal
int move_equals(Move* a, Move* b);

// Free a move
void move_free(Move* m);

#endif // MOVE_H


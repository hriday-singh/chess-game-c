#ifndef ZOBRIST_H
#define ZOBRIST_H

#include <stdint.h>
#include "types.h"

// Initialize Zobrist tables with random values
void zobrist_init(void);

// Compute hash from scratch for a GameLogic instance
uint64_t zobrist_compute(GameLogic* logic);

// Update hash incrementally (not strictly needed for snapshots but good for performance)
// uint64_t zobrist_update_piece(uint64_t current, PieceType type, Player color, int square);

#endif // ZOBRIST_H

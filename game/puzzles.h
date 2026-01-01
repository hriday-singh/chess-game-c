#ifndef PUZZLES_H
#define PUZZLES_H

#include "types.h"

#define MAX_PUZZLE_MOVES 10

typedef struct {
    const char* title;
    const char* description;
    const char* fen;
    const char* solution_moves[MAX_PUZZLE_MOVES]; // UCI format (e.g. "e2e4")
    int solution_length;
    Player turn;
} Puzzle;

// Returns count of available puzzles
int puzzles_get_count(void);

// Returns pointer to puzzle at index (0-based)
// Returns pointer to puzzle at index (0-based)
const Puzzle* puzzles_get_at(int index);

// Initialize puzzle system (and load built-in puzzles)
void puzzles_init(void);

// Convert a puzzle to dynamic storage and add it
// Note: Strings in 'p' are copied.
void puzzles_add_custom(const Puzzle* p);

// Cleanup resources
void puzzles_cleanup(void);

#endif // PUZZLES_H

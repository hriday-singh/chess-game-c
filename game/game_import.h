#ifndef GAME_IMPORT_H
#define GAME_IMPORT_H

#include "gamelogic.h"
#include <stdbool.h>

// Result of an import operation
typedef struct {
    bool success;
    char error_message[256];
    int moves_count;
    char loaded_uci[4096]; // Space-separated UCI moves of the loaded game
    char start_fen[256];   // Detected or default start FEN
    char result[16];       // "1-0", "0-1", "1/2-1/2", "*" or empty
    
    // PGN Metadata
    char white[64];
    char black[64];
    char event[128];
    char date[32];
} GameImportResult;

/**
 * Attempts to import a game from a raw string.
 * The string can be PGN (with tags/comments), UCI move list, or SAN move list.
 * 
 * @param logic A VALID GameLogic instance to use for move validation/generation. 
 *              It will be RESET during this process.
 *              Failed import might leave logic in partial state.
 * @param input The raw input string.
 * @return GameImportResult containing success status and parsed data.
 */
GameImportResult game_import_from_string(GameLogic* logic, const char* input);

#endif // GAME_IMPORT_H

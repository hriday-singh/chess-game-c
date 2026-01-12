#ifndef GAMELOGIC_H
#define GAMELOGIC_H

#include "types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// GameLogic structure (equivalent to Java class)
struct GameLogic {
    // Board state
    Piece* board[8][8];
    
    // Game state
    GameMode gameMode;
    Player turn;
    Player playerSide;
    bool isGameOver;
    char statusMessage[256];
    
    // En passant
    int enPassantCol;  // -1 if none
    
    // History for undo
    void* moveHistory;  // Stack<Move>
    void* enPassantHistory;  // Stack<Integer>
    
    // Cache for single-piece move generation
    void* cachedMoves;      // MoveList* (internal)
    int cachedPieceRow;
    int cachedPieceCol;
    uint64_t cachedVersion;
    
    // Position versioning for cache validation
    uint64_t positionVersion;
    
    // Simulation flag (for AI search)
    bool isSimulation;
    
    // Callbacks
    void (*updateCallback)(void);
};

// Function declarations
GameLogic* gamelogic_create(void);
void gamelogic_free(GameLogic* logic);
void gamelogic_reset(GameLogic* logic);
GameMode gamelogic_get_game_mode(GameLogic* logic);
void gamelogic_set_game_mode(GameLogic* logic, GameMode mode);

// Move generation & validation (UI Decouplings)
void gamelogic_generate_legal_moves(GameLogic* logic, Player player, void* moves_list);
Move** gamelogic_get_valid_moves_for_piece(GameLogic* logic, int row, int col, int* count);
bool gamelogic_is_move_valid(GameLogic* logic, int startRow, int startCol, int endRow, int endCol);
void gamelogic_free_moves_array(Move** moves, int count);

bool gamelogic_perform_move(GameLogic* logic, Move* move);
void gamelogic_undo_move(GameLogic* logic);

// Game state checks
bool gamelogic_is_in_check(GameLogic* logic, Player player);
bool gamelogic_is_checkmate(GameLogic* logic, Player player);
bool gamelogic_is_stalemate(GameLogic* logic, Player player);
bool gamelogic_is_computer(GameLogic* logic, Player player);

// FEN generation
void gamelogic_generate_fen(GameLogic* logic, char* fen, size_t fen_size);
void gamelogic_load_fen(GameLogic* logic, const char* fen);
int gamelogic_get_castling_rights(GameLogic* logic);

// Move validation
bool gamelogic_simulate_move_and_check_safety(GameLogic* logic, Move* m, Player p);

// Get last move
Move* gamelogic_get_last_move(GameLogic* logic);
// Get move at specific index (0-indexed)
Move* gamelogic_get_move_at(GameLogic* logic, int index);
// Get total moves made
int gamelogic_get_move_count(GameLogic* logic);

// Get captured pieces
void gamelogic_get_captured_pieces(GameLogic* logic, Player capturer, void* pieces_list);

// Get status message
const char* gamelogic_get_status_message(GameLogic* logic);

// Get current turn
Player gamelogic_get_turn(GameLogic* logic);

// Get player side
Player gamelogic_get_player_side(GameLogic* logic);

// Reset game (alias for gamelogic_reset)
void gamelogic_reset_game(GameLogic* logic);

// Update game state
void gamelogic_update_game_state(GameLogic* logic);

// Square safety check
bool gamelogic_is_square_safe(GameLogic* logic, int r, int c, Player p);
int gamelogic_count_hanging_pieces(GameLogic* logic, Player player);

// Callbacks
void gamelogic_set_callback(GameLogic* logic, void (*callback)(void));

// Learning
void gamelogic_handle_game_end_learning(GameLogic* logic, Player winner);

// SAN generation
void gamelogic_get_move_san(GameLogic* logic, Move* move, char* san, size_t san_size);

#endif // GAMELOGIC_H


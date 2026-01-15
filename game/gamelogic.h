#ifndef GAMELOGIC_H
#define GAMELOGIC_H

#include "types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// GameLogic structure
struct GameLogic {
    // Board state
    Piece* board[8][8];
    
    // Game state
    GameMode gameMode;
    Player turn;
    Player playerSide;
    bool isGameOver;
    char statusMessage[256];
    char start_fen[256];
    
    // Position attributes
    uint8_t castlingRights; // Bitmask: WK=1, WQ=2, BK=4, BQ=8
    int8_t enPassantCol;    // -1 if none
    int halfmoveClock;
    int fullmoveNumber;
    uint64_t currentHash;
    
    // History for undo
    void* moveHistory;  // Stack<Move>
    
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

// Snapshot and Hashing
void gamelogic_create_snapshot(GameLogic* logic, PositionSnapshot* snap);
void gamelogic_restore_snapshot(GameLogic* logic, const PositionSnapshot* snap);
uint64_t gamelogic_compute_hash(GameLogic* logic);

// Move generation & validation
void gamelogic_generate_legal_moves(GameLogic* logic, Player player, void* moves_list);
Move** gamelogic_get_valid_moves_for_piece(GameLogic* logic, int row, int col, int* count);
Move** gamelogic_get_all_legal_moves(GameLogic* logic, Player player, int* count);
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

// Move validation
bool gamelogic_simulate_move_and_check_safety(GameLogic* logic, Move* m, Player p);

// History access
Move gamelogic_get_last_move(GameLogic* logic);
Move gamelogic_get_move_at(GameLogic* logic, int index);
int gamelogic_get_move_count(GameLogic* logic);

// Captured pieces
void gamelogic_get_captured_pieces(GameLogic* logic, Player capturer, void* pieces_list);

// Status and turn
const char* gamelogic_get_status_message(GameLogic* logic);
Player gamelogic_get_turn(GameLogic* logic);
Player gamelogic_get_player_side(GameLogic* logic);
void gamelogic_update_game_state(GameLogic* logic);

// Square safety
bool gamelogic_is_square_safe(GameLogic* logic, int r, int c, Player p);
int gamelogic_count_hanging_pieces(GameLogic* logic, Player player);

void gamelogic_set_callback(GameLogic* logic, void (*callback)(void));
void gamelogic_handle_game_end_learning(GameLogic* logic, Player winner);

// SAN and PGN
void gamelogic_get_move_uci(GameLogic* logic, Move* move, char* uci, size_t uci_size);

void gamelogic_load_from_uci_moves(GameLogic* logic, const char* moves_uci, const char* start_fen);

// Reconstruct history stack (e.g. for replay)
void gamelogic_rebuild_history(GameLogic* logic, Move** moves, int count);

#endif // GAMELOGIC_H

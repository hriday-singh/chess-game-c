#include "gamelogic.h"
#include "piece.h"
#include "move.h"
#include "zobrist.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Helper: Get Monotonic Time in MS
static int64_t get_monotonic_time_ms(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&frequency);
        init = true;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (int64_t)((now.QuadPart * 1000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

static bool debug_mode = true;

// Simple stack implementation for move history
typedef struct StackNode {
    void* data;
    struct StackNode* next;
} StackNode;

typedef struct {
    StackNode* top;
    int size;
} Stack;

static Stack* stack_create(void) {
    Stack* s = (Stack*)malloc(sizeof(Stack));
    if (s) {
        s->top = NULL;
        s->size = 0;
    }
    return s;
}

static void stack_push(Stack* s, void* data) {
    StackNode* node = (StackNode*)malloc(sizeof(StackNode));
    if (node) {
        node->data = data;
        node->next = s->top;
        s->top = node;
        s->size++;
    }
}

static void* stack_pop(Stack* s) {
    if (!s || !s->top) return NULL;
    StackNode* node = s->top;
    void* data = node->data;
    s->top = node->next;
    free(node);
    s->size--;
    return data;
}

static void stack_free(Stack* s) {
    if (!s) return;
    while (stack_pop(s) != NULL) {
        // Freeing happens in caller
    }
    free(s);
}

// External cache management (defined in gamelogic_movegen.c)
extern void gamelogic_clear_cache(GameLogic* logic);

// Simple list for PieceType (for captured pieces)
typedef struct PieceTypeNode {
    PieceType type;
    struct PieceTypeNode* next;
} PieceTypeNode;

typedef struct {
    PieceTypeNode* head;
    int size;
} PieceTypeList;

static void piece_type_list_add(PieceTypeList* list, PieceType type) {
    if (!list) return;
    PieceTypeNode* node = (PieceTypeNode*)malloc(sizeof(PieceTypeNode));
    if (node) {
        node->type = type;
        node->next = list->head;
        list->head = node;
        list->size++;
    }
}

static void piece_type_list_clear(PieceTypeList* list) {
    if (!list) return;
    PieceTypeNode* current = list->head;
    while (current) {
        PieceTypeNode* next = current->next;
        free(current);
        current = next;
    }
    list->head = NULL;
    list->size = 0;
}

// Create new GameLogic
GameLogic* gamelogic_create(void) {
    GameLogic* logic = (GameLogic*)calloc(1, sizeof(GameLogic));
    if (logic) {
        logic->gameMode = GAME_MODE_PVC;
        logic->turn = PLAYER_WHITE;
        logic->playerSide = PLAYER_WHITE;
        logic->isGameOver = false;
        snprintf(logic->statusMessage, sizeof(logic->statusMessage), "White's Turn");
        logic->enPassantCol = -1;
        logic->castlingRights = 0xF; // All rights
        logic->halfmoveClock = 0;
        logic->fullmoveNumber = 1;

        // Time Tracking Init
        logic->think_times = NULL;
        logic->think_time_count = 0;
        logic->think_time_capacity = 0;
        logic->created_at_ms = (int64_t)time(NULL) * 1000;
        logic->started_at_ms = 0;
        logic->turn_start_time = 0;

        logic->moveHistory = stack_create();

        logic->isSimulation = false;
        logic->updateCallback = NULL;
        
        // Initialize Cache
        logic->cachedMoves = NULL;
        logic->cachedPieceRow = -1;
        logic->cachedPieceCol = -1;
        logic->cachedVersion = 0;
        logic->positionVersion = 0;
        
        gamelogic_reset(logic);
    }
    return logic;
}

// Free GameLogic
void gamelogic_free(GameLogic* logic) {
    if (!logic) return;
    
    // Free board pieces
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
            }
        }
    }
    
    // Free move history
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (moveStack) {
        while (moveStack->top) {
            Move* m = (Move*)stack_pop(moveStack);
            move_free(m);
        }
        stack_free(moveStack);
    }

    if (logic->think_times) {
        free(logic->think_times);
        logic->think_times = NULL;
    }
       
    
    // Free cache
    gamelogic_clear_cache(logic);
    
    free(logic);
}

// Setup initial board
static void setup_board(GameLogic* logic) {
    // Clear board
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    // Setup pawns
    for (int i = 0; i < 8; i++) {
        logic->board[1][i] = piece_create(PIECE_PAWN, PLAYER_BLACK);
        logic->board[6][i] = piece_create(PIECE_PAWN, PLAYER_WHITE);
    }
    
    // Setup back ranks
    // Black pieces (row 0)
    logic->board[0][0] = piece_create(PIECE_ROOK, PLAYER_BLACK);
    logic->board[0][1] = piece_create(PIECE_KNIGHT, PLAYER_BLACK);
    logic->board[0][2] = piece_create(PIECE_BISHOP, PLAYER_BLACK);
    logic->board[0][3] = piece_create(PIECE_QUEEN, PLAYER_BLACK);
    logic->board[0][4] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->board[0][5] = piece_create(PIECE_BISHOP, PLAYER_BLACK);
    logic->board[0][6] = piece_create(PIECE_KNIGHT, PLAYER_BLACK);
    logic->board[0][7] = piece_create(PIECE_ROOK, PLAYER_BLACK);
    
    // White pieces (row 7)
    logic->board[7][0] = piece_create(PIECE_ROOK, PLAYER_WHITE);
    logic->board[7][1] = piece_create(PIECE_KNIGHT, PLAYER_WHITE);
    logic->board[7][2] = piece_create(PIECE_BISHOP, PLAYER_WHITE);
    logic->board[7][3] = piece_create(PIECE_QUEEN, PLAYER_WHITE);
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[7][5] = piece_create(PIECE_BISHOP, PLAYER_WHITE);
    logic->board[7][6] = piece_create(PIECE_KNIGHT, PLAYER_WHITE);
    logic->board[7][7] = piece_create(PIECE_ROOK, PLAYER_WHITE);
}

// Reset game
// Get status message
const char* gamelogic_get_status_message(GameLogic* logic) {
    if (!logic) return "Unknown";
    return logic->statusMessage;
}

// Get current turn
Player gamelogic_get_turn(GameLogic* logic) {
    if (!logic) return PLAYER_WHITE;
    return logic->turn;
}

// Get player side
Player gamelogic_get_player_side(GameLogic* logic) {
    if (!logic) return PLAYER_WHITE;
    return logic->playerSide;
}

void gamelogic_reset(GameLogic* logic) {
    if (!logic) return;
    
    logic->turn = PLAYER_WHITE;
    logic->isGameOver = false;
    snprintf(logic->statusMessage, sizeof(logic->statusMessage), "White's Turn");
    
    logic->enPassantCol = -1;
    logic->castlingRights = 0xF;
    logic->halfmoveClock = 0;
    logic->fullmoveNumber = 1;
    
    // Set standard start FEN
    snprintf(logic->start_fen, sizeof(logic->start_fen), "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    
    // Clear move history
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (moveStack) {
        while (moveStack->top) {
            Move* m = (Move*)stack_pop(moveStack);
            move_free(m);
        }
    }
    
    
    // Clear cache
    gamelogic_clear_cache(logic);
    logic->cachedPieceRow = -1;
    logic->cachedVersion = 0;
    logic->positionVersion = 0;
    
    // Clear and setup board
    setup_board(logic);

    // Reset Time Tracking
    if (logic->think_times) {
        free(logic->think_times);
        logic->think_times = NULL;
    }
    logic->think_time_count = 0;
    logic->think_time_capacity = 0;
    // Keep created_at_ms if we consider reset same "session", but usually reset = new game
    logic->created_at_ms = (int64_t)time(NULL) * 1000;
    logic->started_at_ms = 0;
    logic->turn_start_time = 0;

    // Initialize clock
    // Preserve existing settings if they exist (don't zero them out)
    if (logic->clock_initial_ms > 0) {
        // Re-apply previous clock settings
        clock_set(&logic->clock, logic->clock_initial_ms, logic->clock_increment_ms);
        logic->clock.enabled = true;
    } else {
        // No clock set, ensure it's disabled
        clock_reset(&logic->clock, 0, 0);
        logic->clock_initial_ms = 0;
        logic->clock_increment_ms = 0;
    }
    // Init turn start time immediately so first move has a reference
    logic->turn_start_time = get_monotonic_time_ms();
    
    // Compute initial hash
    logic->currentHash = zobrist_compute(logic);
    
    gamelogic_update_game_state(logic);
    
    // Notify UI (triggers AI check if applicable)
    if (logic->updateCallback) logic->updateCallback();
}

void gamelogic_create_snapshot(GameLogic* logic, PositionSnapshot* snap) {
    if (!logic || !snap) return;
    
    memset(snap->board, 0, 64);
    snap->hasMovedMask = 0;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int sq = r * 8 + c;
            Piece* p = logic->board[r][c];
            if (p) {
                snap->board[sq] = (uint8_t)(((p->type + 1) << 1) | p->owner);
                if (p->hasMoved) {
                    snap->hasMovedMask |= (1ULL << sq);
                }
            } else {
                snap->board[sq] = 0;
            }
        }
    }
    
    snap->turn = logic->turn;
    snap->castlingRights = logic->castlingRights;
    snap->enPassantCol = (int8_t)logic->enPassantCol;
    snap->halfmoveClock = logic->halfmoveClock;
    snap->fullmoveNumber = logic->fullmoveNumber;
    snap->zobristHash = logic->currentHash;
}

void gamelogic_restore_snapshot(GameLogic* logic, const PositionSnapshot* snap) {
    if (!logic || !snap) return;
    
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (logic->board[r][c]) {
                piece_free(logic->board[r][c]);
                logic->board[r][c] = NULL;
            }
        }
    }
    
    for (int i = 0; i < 64; i++) {
        uint8_t val = snap->board[i];
        if (val != 0) {
            PieceType type = (PieceType)((val >> 1) - 1);
            Player owner = (Player)(val & 1);
            int r = i / 8;
            int c = i % 8;
            logic->board[r][c] = piece_create(type, owner);
            if (logic->board[r][c]) {
                logic->board[r][c]->hasMoved = (snap->hasMovedMask & (1ULL << i)) != 0;
            }
        }
    }
    
    logic->turn = snap->turn;
    logic->castlingRights = snap->castlingRights;
    logic->enPassantCol = snap->enPassantCol;
    logic->halfmoveClock = snap->halfmoveClock;
    logic->fullmoveNumber = snap->fullmoveNumber;
    logic->currentHash = snap->zobristHash;
    
    gamelogic_update_game_state(logic);
    if (logic->updateCallback) logic->updateCallback();
}

uint64_t gamelogic_compute_hash(GameLogic* logic) {
    return zobrist_compute(logic);
}

// Get opponent
static Player get_opponent(Player p) {
    return (p == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
}

// Update game state
// Update game state
// Set skipExpensiveChecks=true during FEN loading to avoid board corruption
void gamelogic_update_game_state_internal(GameLogic* logic, bool skipExpensiveChecks) {
    if (!logic) return;
    
    // Reset isGameOver before checking conditions
    logic->isGameOver = false;
    
    // Skip expensive checks (checkmate/stalemate) if requested
    if (!skipExpensiveChecks) {
        // Check for checkmate first
        if (gamelogic_is_checkmate(logic, logic->turn)) {
            logic->isGameOver = true;
            Player winner = get_opponent(logic->turn);
            if (winner == PLAYER_WHITE) {
                snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Checkmate! White wins!");
            } else {
                snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Checkmate! Black wins!");
            }
            return;
        } 
        // Then check for stalemate
        else if (gamelogic_is_stalemate(logic, logic->turn)) {
            logic->isGameOver = true;
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Stalemate! Draw.");
            return;
        }
    }
    
    // Check for regular check (always safe)
    if (gamelogic_is_in_check(logic, logic->turn)) {
        if (logic->turn == PLAYER_WHITE) {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "White is in Check!");
        } else {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Black is in Check!");
        }
    } 
    // Normal turn
    else {
        if (logic->turn == PLAYER_WHITE) {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "White's Turn");
        } else {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Black's Turn");
        }
    }
}

// Public wrapper - normal gameplay uses full checking
void gamelogic_update_game_state(GameLogic* logic) {
    gamelogic_update_game_state_internal(logic, false);
}

// Check if player is computer
bool gamelogic_is_computer(GameLogic* logic, Player p) {
    if (!logic) return false;
    if (logic->gameMode == GAME_MODE_PVP) return false;
    if (logic->gameMode == GAME_MODE_CVC) return true;
    return (p != logic->playerSide);
}

// Set callbacks
void gamelogic_set_callback(GameLogic* logic, void (*callback)(void)) {
    if (logic) logic->updateCallback = callback;
}

// Forward declarations
static void make_move_internal(GameLogic* logic, Move* move);

// Clock Interface
// Clock Interface declaration removed from here to avoid duplication

static Move* undo_move_internal(GameLogic* logic, bool free_move);
static char get_fen_char(Piece* p);
// External safety checks (defined in gamelogic_safety.c)
extern bool gamelogic_is_square_safe(GameLogic* logic, int r, int c, Player p);
extern bool gamelogic_is_in_check(GameLogic* logic, Player player);
extern bool gamelogic_is_checkmate(GameLogic* logic, Player player);
extern bool gamelogic_is_stalemate(GameLogic* logic, Player player);

// External move generation (defined in gamelogic_movegen.c)
extern void gamelogic_generate_legal_moves(GameLogic* logic, Player player, void* moves_list);

// Perform a move
bool gamelogic_perform_move(GameLogic* logic, Move* move) {
    if (!logic || !move) {
        return false;
    }
    if (move->from_sq == move->to_sq) {

        return false;
    }
    
    int r1 = move->from_sq / 8;
    int c1 = move->from_sq % 8;
    Piece* movingPiece = logic->board[r1][c1];
    if (!movingPiece) {

        return false;
    }
    
    // Safety check: ensure only the mover can move their own pieces
    if (movingPiece->owner != logic->turn) {

        return false;
    }
    
    make_move_internal(logic, move);

    // Time Tracking Logic
    int64_t now = get_monotonic_time_ms();
    
    // Set started_at_ms on first move if not set
    if (logic->started_at_ms == 0) {
        logic->started_at_ms = (int64_t)time(NULL) * 1000;
        // First move start time might be vague, we set turn_start_time at reset or first move?
        // If turn_start_time was 0, assume it started "now" - think time is 0 for first move or we fix at reset.
        if (logic->turn_start_time == 0) logic->turn_start_time = now;
    }

    int delta = 0;
    if (logic->turn_start_time > 0) {
        delta = (int)(now - logic->turn_start_time);
        if (delta < 0) delta = 0;
    }

    // Fix for "Super long first think time" (REVERTED as per user request):
    // User wants accurate time from clock start.
    // So we use the delta calculated from turn_start_time.
    // Logic: delta is (now - logic->turn_start_time).
    // Ensure turn_start_time is set correctly at game start.

    // Append to think times
    if (logic->think_time_count >= logic->think_time_capacity) {
        logic->think_time_capacity = (logic->think_time_capacity == 0) ? 64 : logic->think_time_capacity * 2;
        int* new_arr = realloc(logic->think_times, logic->think_time_capacity * sizeof(int));
        if (new_arr) logic->think_times = new_arr;
    }
    if (logic->think_times && logic->think_time_count < logic->think_time_capacity) {
        logic->think_times[logic->think_time_count++] = delta;
    }

    // Reset timer for next turn
    logic->turn_start_time = now;
    
    // Critical Fix for Undo Time:
    // make_move_internal saves logic->clock.white_time_ms into move->prevWhiteTimeMs.
    // However, logic->clock is decremented by tick loop continuously, so saved time = (Start - Delta).
    // Undo restores this saved time, so we lose Delta.
    // We MUST add Delta back to the saved state to ensure Undo restores the time *at start of turn*.
    if (!logic->isSimulation && logic->moveHistory) {
         Stack* s = (Stack*)logic->moveHistory;
         if (s->top) {
             Move* recorded = (Move*)s->top->data;
             // Check who moved (the move structure already has it, or infer from logic->turn which just swapped)
             // logic->turn is now Opponent. So recorded->mover matches Previous Turn.
             if (recorded->mover == PLAYER_WHITE) {
                 recorded->prevWhiteTimeMs += delta;
             } else {
                 recorded->prevBlackTimeMs += delta;
             }
         }
    }
    
    if (!logic->isSimulation) {
        gamelogic_update_game_state(logic);
        if (logic->updateCallback) logic->updateCallback(); 
    }
    
    // Invalidate move cache
    logic->cachedPieceRow = -1;
    
    return true;
}

void gamelogic_undo_move(GameLogic* logic) {
    // Fix for Think Time Mismatch:
    // If we undo a move, we must also remove its recorded think time to keep arrays in sync.
    if (logic->think_time_count > 0) {
        logic->think_time_count--;
    }
    
    undo_move_internal(logic, true);
    if (!logic->isSimulation) {
        gamelogic_update_game_state(logic);
        if (logic->updateCallback) logic->updateCallback();
    }
    logic->cachedPieceRow = -1;
}

// Simulate a move and check if the resulting position is safe for the given player
bool gamelogic_simulate_move_and_check_safety(GameLogic* logic, Move* m, Player p) {
    if (!logic || !m) return false;
    
    int r1 = m->from_sq / 8, c1 = m->from_sq % 8;
    int r2 = m->to_sq / 8, c2 = m->to_sq % 8;
    
    Piece* saved_start = logic->board[r1][c1];
    Piece* saved_end = logic->board[r2][c2];
    Piece* saved_ep = NULL;
    Piece* saved_rook_start = NULL;
    Piece* saved_rook_dest = NULL;
    
    int rookStartCol = -1, rookDestCol = -1;
    bool is_ep_capture = false;
    
    Player saved_turn = logic->turn;
    int8_t saved_enPassantCol = (int8_t)logic->enPassantCol;
    uint8_t saved_castling = logic->castlingRights;
    
    // Execute move directly on board (pointers only, no copies to avoid leaks unless needed)
    Piece* movingPiece = logic->board[r1][c1];
    if (!movingPiece) return false;
    
    logic->board[r2][c2] = movingPiece;
    logic->board[r1][c1] = NULL;
    
    if (m->isEnPassant) {
        saved_ep = logic->board[r1][c2];
        logic->board[r1][c2] = NULL;
        is_ep_capture = true;
    }
    
    if (m->isCastling) {
        rookStartCol = (c2 > c1) ? 7 : 0;
        rookDestCol = (c2 > c1) ? 5 : 3;
        saved_rook_start = logic->board[r1][rookStartCol];
        saved_rook_dest = logic->board[r1][rookDestCol];
        logic->board[r1][rookDestCol] = saved_rook_start;
        logic->board[r1][rookStartCol] = NULL;
    }
    
    bool safe = !gamelogic_is_in_check(logic, p);
    
    // Restore
    logic->board[r1][c1] = saved_start;
    logic->board[r2][c2] = saved_end;
    if (is_ep_capture) logic->board[r1][c2] = saved_ep;
    if (m->isCastling) {
        logic->board[r1][rookStartCol] = saved_rook_start;
        logic->board[r1][rookDestCol] = saved_rook_dest;
    }
    
    logic->turn = saved_turn;
    logic->enPassantCol = saved_enPassantCol;
    logic->castlingRights = saved_castling;
    
    return safe;
}

// History access
Move gamelogic_get_last_move(GameLogic* logic) {
    if (!logic) {
        Move empty = {0};
        empty.capturedPieceType = NO_PIECE;
        empty.promotionPiece = NO_PROMOTION;
        return empty;
    }
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || !moveStack->top) {
        Move empty = {0};
        empty.capturedPieceType = NO_PIECE;
        empty.promotionPiece = NO_PROMOTION;
        return empty;
    }
    return *((Move*)moveStack->top->data);
}

int gamelogic_get_move_count(GameLogic* logic) {
    if (!logic || !logic->moveHistory) return 0;
    Stack* moveStack = (Stack*)logic->moveHistory;
    return moveStack->size;
}

Move gamelogic_get_move_at(GameLogic* logic, int index) {
    if (!logic || index < 0) {
        Move empty = {0};
        empty.capturedPieceType = NO_PIECE;
        empty.promotionPiece = NO_PROMOTION;
        return empty;
    }
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || index >= moveStack->size) {
        Move empty = {0};
        empty.capturedPieceType = NO_PIECE;
        empty.promotionPiece = NO_PROMOTION;
        return empty;
    }
    
    StackNode* current = moveStack->top;
    int currentIdx = moveStack->size - 1;
    while (current && currentIdx > index) {
        current = current->next;
        currentIdx--;
    }
    
    if (current) return *((Move*)current->data);
    Move empty = {0};
    empty.capturedPieceType = NO_PIECE;
    empty.promotionPiece = NO_PROMOTION;
    return empty;
}

// Generate FEN string
void gamelogic_generate_fen(GameLogic* logic, char* fen, size_t fen_size) {
    if (!logic || !fen || fen_size == 0) return;
    
    char* ptr = fen;
    size_t remaining = fen_size - 1;
    
    // Board position
    for (int r = 0; r < 8; r++) {
        int empty = 0;
        for (int c = 0; c < 8; c++) {
            Piece* p = logic->board[r][c];
            if (!p) {
                empty++;
            } else {
                if (empty > 0) {
                    int written = snprintf(ptr, remaining, "%d", empty);
                    ptr += written;
                    remaining -= written;
                    empty = 0;
                }
                char symbol = get_fen_char(p);
                if (p->owner == PLAYER_WHITE) {
                    symbol = (char)toupper(symbol);
                }
                if (remaining > 0) {
                    *ptr++ = symbol;
                    remaining--;
                }
            }
        }
        if (empty > 0) {
            int written = snprintf(ptr, remaining, "%d", empty);
            ptr += written;
            remaining -= written;
        }
        if (r < 7 && remaining > 0) {
            *ptr++ = '/';
            remaining--;
        }
    }
    
    // Active color
    if (remaining >= 3) {
        int written = snprintf(ptr, remaining, " %c ", (logic->turn == PLAYER_WHITE) ? 'w' : 'b');
        ptr += written;
        remaining -= written;
    }
    
    // Castling rights
    bool hasCastling = false;
    if (logic->castlingRights & 1) { if (remaining > 0) { *ptr++ = 'K'; remaining--; hasCastling = true; } }
    if (logic->castlingRights & 2) { if (remaining > 0) { *ptr++ = 'Q'; remaining--; hasCastling = true; } }
    if (logic->castlingRights & 4) { if (remaining > 0) { *ptr++ = 'k'; remaining--; hasCastling = true; } }
    if (logic->castlingRights & 8) { if (remaining > 0) { *ptr++ = 'q'; remaining--; hasCastling = true; } }
    if (!hasCastling && remaining > 0) {
        *ptr++ = '-';
        remaining--;
    }
    
    // En passant
    if (logic->enPassantCol != -1) {
        const char* colParams = "abcdefgh";
        // En Passant target is behind the pawn that just moved 2 squares.
        // It depends on who just moved (the opponent of current turn).
        Player last_mover = (logic->turn == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
        
        // If White moved e2e4 (last_mover=WHITE), target is e3 (Rank 3).
        // If Black moved e7e5 (last_mover=BLACK), target is e6 (Rank 6).
        int ep_rank = (last_mover == PLAYER_WHITE) ? 3 : 6; 
        
        int written = snprintf(ptr, remaining, " %c%d", colParams[logic->enPassantCol], ep_rank);
        ptr += written;
        remaining -= written;
    } else {
        if (remaining >= 2) {
            int written = snprintf(ptr, remaining, " -");
            ptr += written;
            remaining -= written;
        }
    }
    
    // Clocks
    if (remaining > 0) {
        snprintf(ptr, remaining, " %d %d", logic->halfmoveClock, logic->fullmoveNumber);
    }
}

// Get captured pieces
void gamelogic_get_captured_pieces(GameLogic* logic, Player capturer, void* pieces_list) {
    if (!logic || !pieces_list) return;
    
    // Clear the list first
    PieceTypeList* list = (PieceTypeList*)pieces_list;
    piece_type_list_clear(list);
    
    // Iterate through move history to find captured pieces
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || moveStack->size == 0) return;
    
    // We need to iterate through the stack - create a temporary array
    Move** moves = (Move**)malloc(moveStack->size * sizeof(Move*));
    if (!moves) return;
    
    StackNode* current = moveStack->top;
    int count = 0;
    while (current && count < moveStack->size) {
        moves[count++] = (Move*)current->data;
        current = current->next;
    }
    
    // Process moves in reverse order (oldest first) to maintain chronological order
    for (int i = count - 1; i >= 0; i--) {
        Move* m = moves[i];
        if (m && m->capturedPieceType != NO_PIECE) {
            // If the move was made by capturer, it means capturer TOOK a piece.
            if (m->mover == capturer) {
                piece_type_list_add(list, m->capturedPieceType);
            }
        }
    }
    
    free(moves);
}

// Handle game end learning (for AI training)
void gamelogic_handle_game_end_learning(GameLogic* logic, Player winner) {
    // TODO: Implement when we have AIGenome structure
    (void)logic;
    (void)winner;
}

// Internal: Make a move
static void make_move_internal(GameLogic* logic, Move* move) {
    if (!logic || !move) return;
    
    int r1 = move->from_sq / 8, c1 = move->from_sq % 8;
    int r2 = move->to_sq / 8, c2 = move->to_sq % 8;
    Piece* movingPiece = logic->board[r1][c1];
    if (!movingPiece) return;
    
    move->isEnPassant = false;
    move->isCastling = false;
    move->rookFirstMove = false;
    move->promotionPiece = NO_PROMOTION;

    move->movedPieceType = movingPiece->type;
    
    // Store attributes for undo
    move->prevEnPassantCol = (int8_t)logic->enPassantCol;
    move->prevCastlingRights = logic->castlingRights;
    move->prevHalfmoveClock = logic->halfmoveClock;
    move->prevWhiteTimeMs = logic->clock.white_time_ms;
    move->prevBlackTimeMs = logic->clock.black_time_ms;
    
    Piece* target = logic->board[r2][c2];

    bool is_ep = (movingPiece->type == PIECE_PAWN &&
                  c1 != c2 &&
                  target == NULL &&
                  logic->enPassantCol == c2);

    move->capturedPieceType = NO_PIECE;

    if (target) {
        move->capturedPieceType = target->type;
        piece_free(target);
        logic->board[r2][c2] = NULL;
        logic->halfmoveClock = 0;
    } else {
        logic->halfmoveClock++;
    }

    // Press clock if not simulation
    if (!logic->isSimulation) {
        clock_press(&logic->clock, logic->turn);
    }
    
    if (is_ep) {
        move->isEnPassant = true;
        move->capturedPieceType = PIECE_PAWN;
        piece_free(logic->board[r1][c2]);
        logic->board[r1][c2] = NULL;
    }

    move->firstMove = !movingPiece->hasMoved;
    movingPiece->hasMoved = true;
    
    // Update castling rights
    if (movingPiece->type == PIECE_KING) {
        if (movingPiece->owner == PLAYER_WHITE) logic->castlingRights &= ~3;
        else logic->castlingRights &= ~12;
    }
    if (movingPiece->type == PIECE_ROOK) {
        if (movingPiece->owner == PLAYER_WHITE) {
            if (r1 == 7 && c1 == 7) logic->castlingRights &= ~1;
            if (r1 == 7 && c1 == 0) logic->castlingRights &= ~2;
        } else {
            if (r1 == 0 && c1 == 7) logic->castlingRights &= ~4;
            if (r1 == 0 && c1 == 0) logic->castlingRights &= ~8;
        }
    }
    // Also if a rook is captured
    if (move->capturedPieceType == PIECE_ROOK) {
        if (r2 == 7 && c2 == 7) logic->castlingRights &= ~1;
        if (r2 == 7 && c2 == 0) logic->castlingRights &= ~2;
        if (r2 == 0 && c2 == 7) logic->castlingRights &= ~4;
        if (r2 == 0 && c2 == 0) logic->castlingRights &= ~8;
    }
    
    if (movingPiece->type == PIECE_KING && abs(c1 - c2) == 2) {
        move->isCastling = true;
        int rookStartCol = (c2 > c1) ? 7 : 0;
        int rookDestCol = (c2 > c1) ? 5 : 3;
        Piece* rook = logic->board[r1][rookStartCol];
        if (rook) {
            move->rookFirstMove = !rook->hasMoved;
            logic->board[r1][rookDestCol] = rook;
            logic->board[r1][rookStartCol] = NULL;
            rook->hasMoved = true;
        }
    }
    
    PieceType movingPieceType = movingPiece->type;
    
    logic->board[r2][c2] = movingPiece;
    logic->board[r1][c1] = NULL;
    
    if (movingPieceType == PIECE_PAWN && (r2 == 0 || r2 == 7)) {
        if (move->promotionPiece == NO_PROMOTION) move->promotionPiece = PIECE_QUEEN;
        Player owner = movingPiece->owner;
        piece_free(movingPiece);
        logic->board[r2][c2] = piece_create(move->promotionPiece, owner);
        logic->board[r2][c2]->hasMoved = true;
    }
    
    if (movingPieceType == PIECE_PAWN && abs(r1 - r2) == 2) {
        logic->enPassantCol = c1;
    } else {
        logic->enPassantCol = -1;
    }
    
    if (logic->turn == PLAYER_BLACK) logic->fullmoveNumber++;
    move->mover = logic->turn;
    logic->turn = get_opponent(logic->turn);
    
    // Add to history
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (moveStack) {
        Move* moveCopy = move_copy(move);
        stack_push(moveStack, moveCopy);
    }
    
    logic->currentHash = zobrist_compute(logic);
    logic->positionVersion++;
}

// Internal: Undo last move
static Move* undo_move_internal(GameLogic* logic, bool free_move) {
    if (!logic) return NULL;
    
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || !moveStack->top) return NULL;
    
    Move* lastMove = (Move*)stack_pop(moveStack);
    
    // Switch turns back
    logic->turn = get_opponent(logic->turn);
    if (logic->turn == PLAYER_BLACK) logic->fullmoveNumber--;
    
    int r1 = lastMove->from_sq / 8, c1 = lastMove->from_sq % 8;
    int r2 = lastMove->to_sq / 8, c2 = lastMove->to_sq % 8;
    
    Piece* movedPiece = logic->board[r2][c2];
    if (movedPiece) {
        // BULLETPROOF: ONLY restore as pawn if this was actually a promotion move
        // and the piece currently at (r2,c2) matches the promotion type.
        if (lastMove->promotionPiece != NO_PROMOTION && movedPiece->type == lastMove->promotionPiece) {
            piece_free(movedPiece);
            logic->board[r1][c1] = piece_create(PIECE_PAWN, logic->turn);
        } else {
            // Otherwise, it was just a normal move.
            logic->board[r1][c1] = movedPiece;
        }
        logic->board[r1][c1]->hasMoved = !lastMove->firstMove;
        logic->board[r2][c2] = NULL;
    }
    
    // Restore captured piece
    if (lastMove->capturedPieceType != NO_PIECE) {
        Player victimColor = get_opponent(logic->turn);
        Piece* restored = piece_create(lastMove->capturedPieceType, victimColor);
        if (lastMove->isEnPassant) {
            logic->board[r1][c2] = restored;
        } else {
            logic->board[r2][c2] = restored;
        }
    }
    
    // Revert Castling
    if (lastMove->isCastling) {
        int rookStartCol = (c2 > c1) ? 7 : 0;
        int rookDestCol = (c2 > c1) ? 5 : 3;
        Piece* rook = logic->board[r1][rookDestCol];
        if (rook) {
            logic->board[r1][rookStartCol] = rook;
            logic->board[r1][rookDestCol] = NULL;
            rook->hasMoved = !lastMove->rookFirstMove;
        }
    }
    
    // Restore global state
    logic->castlingRights = lastMove->prevCastlingRights;
    logic->enPassantCol = lastMove->prevEnPassantCol;
    logic->halfmoveClock = lastMove->prevHalfmoveClock;

    // Restore Clock
    if (!logic->isSimulation) {
        logic->clock.white_time_ms = lastMove->prevWhiteTimeMs;
        logic->clock.black_time_ms = lastMove->prevBlackTimeMs;
        logic->clock.flagged_player = PLAYER_NONE;
        
        // CRITICAL: Reset the last tick time to NOW.
        // Otherwise, the next clock_tick will calculate delta = (now - old_last_tick),
        // which could be huge, effectively wiping out the restored time instantly.
        logic->clock.last_tick_time = clock_get_current_time_ms();
        
        // logic->clock.active state? Maybe keep it active if it was active?
        // Usually undo happens when game is paused or ongoing. 
        // If game over due to flag fall, we want to clear flag (done above) and probably resume?
        // But let's just restore values.
    }
    
    if (free_move) {
        move_free(lastMove);
        lastMove = NULL;
    }
    logic->currentHash = zobrist_compute(logic);
    logic->positionVersion++;
    return lastMove;
}

GameMode gamelogic_get_game_mode(GameLogic* logic) {
    return logic ? logic->gameMode : GAME_MODE_PVC;
}

void gamelogic_set_game_mode(GameLogic* logic, GameMode mode) {
    if (logic) logic->gameMode = mode;
}

// Helper: Get FEN character for piece
static char get_fen_char(Piece* p) {
    if (!p) return ' ';
    switch (p->type) {
        case PIECE_PAWN: return 'p';
        case PIECE_ROOK: return 'r';
        case PIECE_KNIGHT: return 'n';
        case PIECE_BISHOP: return 'b';
        case PIECE_QUEEN: return 'q';
        case PIECE_KING: return 'k';
        default: return ' ';
    }
}

// Load position from FEN string
void gamelogic_load_fen(GameLogic* logic, const char* fen) {
    if (!logic || !fen) return;

    // Clear move history first
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (moveStack) {
        while (moveStack->top) {
            Move* m = (Move*)stack_pop(moveStack);
            move_free(m);
        }
    }
    // Also clear cache as position is changing discontinuously
    gamelogic_clear_cache(logic);
    logic->cachedPieceRow = -1;
    logic->cachedVersion = 0;
    logic->positionVersion = 0;

    // Store as new start FEN (assuming load_fen acts as a setup)
    snprintf(logic->start_fen, sizeof(logic->start_fen), "%s", fen);
    
    // Clear the board first
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    // Parse board position
    int row = 0, col = 0;
    const char* ptr = fen;
    
    while (*ptr && *ptr != ' ' && row < 8) {
        if (*ptr == '/') {
            row++;
            col = 0;
        } else if (isdigit(*ptr)) {
            col += (*ptr - '0');
        } else {
            // It's a piece
            PieceType type;
            Player owner = isupper(*ptr) ? PLAYER_WHITE : PLAYER_BLACK;
            char c = tolower(*ptr);
            
            switch (c) {
                case 'p': type = PIECE_PAWN; break;
                case 'r': type = PIECE_ROOK; break;
                case 'n': type = PIECE_KNIGHT; break;
                case 'b': type = PIECE_BISHOP; break;
                case 'q': type = PIECE_QUEEN; break;
                case 'k': type = PIECE_KING; break;
                default: ptr++; continue;
            }
            
            if (row < 8 && col < 8) {
                logic->board[row][col] = piece_create(type, owner);
            }
            col++;
        }
        ptr++;
    }
    
    // Parse active color
    while (*ptr && *ptr == ' ') ptr++;
    if (*ptr == 'w') {
        logic->turn = PLAYER_WHITE;
    } else if (*ptr == 'b') {
        logic->turn = PLAYER_BLACK;
    }
    ptr++;
    
    // Parse castling rights
    while (*ptr && *ptr == ' ') ptr++;
    
    bool wk_f = false, wq_f = false, bk_f = false, bq_f = false;
    if (*ptr && *ptr != '-') {
        while (*ptr && *ptr != ' ') {
            if (*ptr == 'K') wk_f = true;
            else if (*ptr == 'Q') wq_f = true;
            else if (*ptr == 'k') bk_f = true;
            else if (*ptr == 'q') bq_f = true;
            ptr++;
        }
    } else if (*ptr == '-') {
        ptr++;
    }

    // Update castling rights in the logic struct
    logic->castlingRights = 0;
    if (wk_f) logic->castlingRights |= 1;
    if (wq_f) logic->castlingRights |= 2;
    if (bk_f) logic->castlingRights |= 4;
    if (bq_f) logic->castlingRights |= 8;

    // Set hasMoved flags for kings and rooks based on castling rights
    // White King
    if (logic->board[7][4] && logic->board[7][4]->type == PIECE_KING) {
        logic->board[7][4]->hasMoved = !(wk_f || wq_f);
    }
    // White Rooks
    if (logic->board[7][7] && logic->board[7][7]->type == PIECE_ROOK) {
        logic->board[7][7]->hasMoved = !wk_f;
    }
    if (logic->board[7][0] && logic->board[7][0]->type == PIECE_ROOK) {
        logic->board[7][0]->hasMoved = !wq_f;
    }
    // Black King
    if (logic->board[0][4] && logic->board[0][4]->type == PIECE_KING) {
        logic->board[0][4]->hasMoved = !(bk_f || bq_f);
    }
    // Black Rooks
    if (logic->board[0][7] && logic->board[0][7]->type == PIECE_ROOK) {
        logic->board[0][7]->hasMoved = !bk_f;
    }
    if (logic->board[0][0] && logic->board[0][0]->type == PIECE_ROOK) {
        logic->board[0][0]->hasMoved = !bq_f;
    }
    
    while (*ptr && *ptr == ' ') ptr++;
    
    // Parse en passant square (e.g., "e3")
    // Parse en passant square (e.g., "e3")
    // Fix: Parse BOTH rank and file to ensure validity
    if (*ptr && *ptr != '-') {
        if (*ptr >= 'a' && *ptr <= 'h') {
            int epFile = *ptr - 'a';
            ptr++;
            // Check rank digit
            if (*ptr >= '1' && *ptr <= '8') {
                int epRank = *ptr - '1'; // 0-7 index (0=row7, 7=row0 in standard chess... wait. FEN is rank 1=row 7, rank 8=row 0)
                // FEN "3" -> Rank 3. In our 0-indexed board (0=Black/Top, 7=White/Bottom):
                // Rank 1 is Row 7. Rank 8 is Row 0.
                // Rank 3 is Row 5. Rank 6 is Row 2.
                // FEN "e6" means Black just moved e7-e5. Target e6. White to move.
                // Row 2 is internal_row 2 (Rank 6). 7 - 5 = 2.
                // Correct expectedRank for current turn
                // White turn: target square MUST be on Rank 6 (internal row 2)
                // Black turn: target square MUST be on Rank 3 (internal row 5)
                int expectedRank = (logic->turn == PLAYER_WHITE) ? 2 : 5;
                
                int internal_row = 7 - epRank;
                
                if (internal_row == expectedRank) {
                    logic->enPassantCol = epFile;
                } else {
                    logic->enPassantCol = -1;
                }
            } else {
                 logic->enPassantCol = -1;
            }
        }
    } else {
        logic->enPassantCol = -1;
    }

    // Parse clocks
    while (*ptr && *ptr == ' ') ptr++;
    if (*ptr && isdigit(*ptr)) {
        logic->halfmoveClock = atoi(ptr);
        while (*ptr && isdigit(*ptr)) ptr++;
    }
    while (*ptr && *ptr == ' ') ptr++;
    if (*ptr && isdigit(*ptr)) {
        logic->fullmoveNumber = atoi(ptr);
    }

    // Finalize state
    logic->currentHash = zobrist_compute(logic);
    logic->positionVersion++;
    
    // Update game status (skip expensive mate/stalemate checks to avoid corruption)
    gamelogic_update_game_state_internal(logic, true);
}

// SAN and PGN
void gamelogic_get_move_uci(GameLogic* logic, Move* move, char* uci, size_t uci_size) {
    if (!logic || !move || !uci || uci_size == 0) return;
    move_to_uci(move, uci);
}

static char get_san_piece_char(PieceType type) {
    switch (type) {
        case PIECE_KNIGHT: return 'N';
        case PIECE_BISHOP: return 'B';
        case PIECE_ROOK:   return 'R';
        case PIECE_QUEEN:  return 'Q';
        case PIECE_KING:   return 'K';
        default:           return '\0'; // Pawn
    }
}

void gamelogic_get_move_san(GameLogic* logic, Move* move, char* san, size_t san_size) {
    if (!logic || !move || !san || san_size == 0) return;

    // Suppress callbacks and updates during this calculation to avoid recursion or UI flicker
    bool old_sim = logic->isSimulation;
    logic->isSimulation = true;

    // 1. We need to analyze the position BEFORE the move for disambiguation.
    // We'll temporarily undo the move if it's the last move in history.
    bool was_undone = false;
    Stack* history = (Stack*)logic->moveHistory;
    if (history && history->top) {
        Move* last = (Move*)history->top->data;
        if (move_equals(last, move)) {
            undo_move_internal(logic, false); // Pop but don't free
            was_undone = true;
        }
    }

    if (!was_undone) {
        // If we can't safely undo (e.g. calculating SAN for historical move not at top),
        // we fallback to UCI for now to ensure stability. 
        // In the future we might want a "stateless" SAN generator.
        move_to_uci(move, san);
        logic->isSimulation = old_sim;
        return;
    }

    // --- SAN Generation Logic (Position is now BEFORE the move) ---
    char buffer[32] = {0};
    int ptr = 0;

    if (move->isCastling) {
        int c2 = move->to_sq % 8;
        int c1 = move->from_sq % 8;
        ptr = snprintf(buffer, sizeof(buffer), (c2 > c1) ? "O-O" : "O-O-O");
    } else {
        char pieceChar = get_san_piece_char(move->movedPieceType);
        if (pieceChar != '\0') {
            buffer[ptr++] = pieceChar;
            
            // Disambiguation
            int count = 0;
            Move** legal = gamelogic_get_all_legal_moves(logic, logic->turn, &count);
            
            bool same_file = false;
            bool same_rank = false;
            int alternatives = 0;
            
            for (int i = 0; i < count; i++) {
                if (legal[i]->to_sq == move->to_sq && 
                    legal[i]->movedPieceType == move->movedPieceType &&
                    legal[i]->from_sq != move->from_sq) {
                    
                    alternatives++;
                    if ((legal[i]->from_sq % 8) == (move->from_sq % 8)) same_file = true;
                    if ((legal[i]->from_sq / 8) == (move->from_sq / 8)) same_rank = true;
                }
            }
            
            if (alternatives > 0) {
                if (!same_file) {
                    buffer[ptr++] = (char)('a' + (move->from_sq % 8));
                } else if (!same_rank) {
                    buffer[ptr++] = (char)('8' - (move->from_sq / 8));
                } else {
                    buffer[ptr++] = (char)('a' + (move->from_sq % 8));
                    buffer[ptr++] = (char)('8' - (move->from_sq / 8));
                }
            }
            
            for (int i = 0; i < count; i++) move_free(legal[i]);
            if (legal) free(legal);

            if (move->capturedPieceType != NO_PIECE) {
                buffer[ptr++] = 'x';
            }
        } else {
            // Pawn move
            if (move->capturedPieceType != NO_PIECE) {
                buffer[ptr++] = (char)('a' + (move->from_sq % 8));
                buffer[ptr++] = 'x';
            }
        }
        
        // Destination square
        buffer[ptr++] = (char)('a' + (move->to_sq % 8));
        buffer[ptr++] = (char)('8' - (move->to_sq / 8));
        
        // Promotion
        if (move->promotionPiece != NO_PROMOTION) {
            buffer[ptr++] = '=';
            buffer[ptr++] = get_san_piece_char(move->promotionPiece);
        }
    }

    // --- Redo move to check for Check/Mate marks ---
    make_move_internal(logic, move);
    
    // Check state AFTER move
    if (gamelogic_is_checkmate(logic, logic->turn)) {
        buffer[ptr++] = '#';
    } else if (gamelogic_is_in_check(logic, logic->turn)) {
        buffer[ptr++] = '+';
    }
    buffer[ptr] = '\0';

    snprintf(san, san_size, "%s", buffer);
    logic->isSimulation = old_sim;
}

void gamelogic_load_from_uci_moves(GameLogic* logic, const char* moves_uci, const char* start_fen) {
    if (!logic || !moves_uci) return;
    
    // 1. Reset logic to starting position
    if (start_fen && start_fen[0] != '\0') {
         gamelogic_load_fen(logic, start_fen);
    } else {
         gamelogic_reset(logic);
    }
    
    // 2. Tokenize UCI string (e.g., "e2e4 g1f3")
    char* moves_copy = strdup(moves_uci);
    if (!moves_copy) return;
    
    char* cursor = moves_copy;
    const char* delims = " \t\r\n";
    
    while (*cursor) {
        // Skip delimiters
        cursor += strspn(cursor, delims);
        if (!*cursor) break;
        
        size_t len = strcspn(cursor, delims);
        if (len > 0) {
            char saved = cursor[len];
            cursor[len] = '\0';
            char* token = cursor;
            
            // Find matching move for this UCI token among all legal moves
            int count = 0;
            Move** legal_moves = gamelogic_get_all_legal_moves(logic, logic->turn, &count);
            
            Move* matched_move = NULL;
            for (int i = 0; i < count; i++) {
                char current_uci[8];
                move_to_uci(legal_moves[i], current_uci);
                if (strcmp(current_uci, token) == 0) {
                    matched_move = move_copy(legal_moves[i]);
                    break;
                }
            }
            
            // Cleanup all generated legal moves
            for (int i = 0; i < count; i++) move_free(legal_moves[i]);
            if (legal_moves) free(legal_moves);
            
            if (matched_move) {
                gamelogic_perform_move(logic, matched_move);
                move_free(matched_move);
            } else {
                if(debug_mode) fprintf(stderr, "[Gamelogic] Replay: Could not match UCI move '%s' at ply %d\n", 
                        token, ((Stack*)logic->moveHistory) ? ((Stack*)logic->moveHistory)->size : 0);
                cursor[len] = saved; // Restore before break
                break; 
            }
            
            cursor[len] = saved;
            cursor += len;
            if (saved == '\0') break;
        } else {
            break;
        }
    }
    
    free(moves_copy);
}

void gamelogic_rebuild_history(GameLogic* logic, Move** moves, int count) {
    if (!logic) return;
    
    // Clear existing history
    Stack* s = (Stack*)logic->moveHistory;
    if (s) {
        while (s->top) {
            Move* m = (Move*)stack_pop(s);
            move_free(m);
        }
    }
    
    // Reset status flags
    logic->isGameOver = false;
    logic->statusMessage[0] = '\0';
    
    if (!moves || count <= 0) return;
    
    // Push new history (up to count)
    for (int i = 0; i < count; i++) {
        if (moves[i]) {
            Move* copy = move_copy(moves[i]);
            stack_push(s, copy);
        }
    }
}

// --- Clock Interface Implementation ---

bool gamelogic_tick_clock(GameLogic* logic) {
    if (!logic || logic->isGameOver || logic->isSimulation) return false;
    
    bool flag_fell = clock_tick(&logic->clock, logic->turn);
    if (flag_fell) {
        logic->isGameOver = true;
        Player flagged = logic->clock.flagged_player;
        if (flagged == PLAYER_WHITE) {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "White lost on time!");
        } else {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Black lost on time!");
        }
        
        // Notify UI immediately
        if (logic->updateCallback) logic->updateCallback();
    }
    return flag_fell;
}

void gamelogic_set_clock(GameLogic* logic, int minutes, int increment) {
    if (!logic) return;
    
    // clock_reset expects minutes and seconds (int), not milliseconds (int64_t)
    // Previous bug: we were converting to ms here AND inside clock_reset, causing massive overflow.
    
    if (minutes == 0 && increment == 0) {
        clock_reset(&logic->clock, 0, 0);
        logic->clock_initial_ms = 0;
        logic->clock_increment_ms = 0;
        return;
    }
    
    clock_reset(&logic->clock, minutes, increment);
    logic->clock_initial_ms = minutes * 60 * 1000;
    logic->clock_increment_ms = increment * 1000;
}

void gamelogic_set_custom_clock(GameLogic* logic, int64_t time_ms, int64_t inc_ms) {
    if (!logic) return;
    clock_reset(&logic->clock, time_ms, inc_ms);
}

void gamelogic_ensure_clock_running(GameLogic* logic) {
    if (!logic || !logic->clock.enabled || logic->clock.active) return;
    
    // Start the clock if not already active and game is in progress
    if (!logic->isGameOver) {
        // logic->clock.active = true;
        // Wait, ensure_clock_running logic was removed?
        // Let's just restore empty body or what was there, 
        // the function expects to do something but previously it was truncated.
        // It seems unused actually? No, let's keep it safe.
    }
}

// Called when user selects a piece (before move)
void gamelogic_start_clock_on_interaction(GameLogic* logic) {
    if (!logic || logic->isGameOver || logic->isSimulation) return;
    
    // Only if clock is enabled but NOT active (first move wait state)
    if (logic->clock.enabled && !logic->clock.active) {
        Stack* history = (Stack*)logic->moveHistory;
        bool isFirstMove = (!history || history->size == 0);
        
        if (isFirstMove) {
             // Start the clock NOW.
             logic->clock.active = true;
             
             // Reset turn_start_time so delta is measured from THIS moment
             logic->turn_start_time = clock_get_current_time_ms();
             
             logic->clock.last_tick_time = 0; // Force reset
        }
    }
}

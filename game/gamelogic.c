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
void gamelogic_update_game_state(GameLogic* logic) {
    if (!logic) return;
    
    // Reset isGameOver before checking conditions
    logic->isGameOver = false;
    
    if (gamelogic_is_checkmate(logic, logic->turn)) {
        logic->isGameOver = true;
        Player winner = get_opponent(logic->turn);
        if (winner == PLAYER_WHITE) {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Checkmate! White wins!");
        } else {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Checkmate! Black wins!");
        }
    } else if (gamelogic_is_stalemate(logic, logic->turn)) {
        logic->isGameOver = true;
        snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Stalemate! Draw.");
    } else if (gamelogic_is_in_check(logic, logic->turn)) {
        if (logic->turn == PLAYER_WHITE) {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "White is in Check!");
        } else {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Black is in Check!");
        }
    } else {
        if (logic->turn == PLAYER_WHITE) {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "White's Turn");
        } else {
            snprintf(logic->statusMessage, sizeof(logic->statusMessage), "Black's Turn");
        }
    }
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
static void undo_move_internal(GameLogic* logic);
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
    
    if (!logic->isSimulation) {
        gamelogic_update_game_state(logic);
        if (logic->updateCallback) logic->updateCallback(); 
    }
    
    // Invalidate move cache
    logic->cachedPieceRow = -1;
    
    return true;
}

void gamelogic_undo_move(GameLogic* logic) {
    if (!logic) return;
    undo_move_internal(logic);
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
Move* gamelogic_get_last_move(GameLogic* logic) {
    if (!logic) return NULL;
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || !moveStack->top) return NULL;
    return (Move*)moveStack->top->data;
}

int gamelogic_get_move_count(GameLogic* logic) {
    if (!logic || !logic->moveHistory) return 0;
    Stack* moveStack = (Stack*)logic->moveHistory;
    return moveStack->size;
}

Move* gamelogic_get_move_at(GameLogic* logic, int index) {
    if (!logic || index < 0) return NULL;
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || index >= moveStack->size) return NULL;
    
    int target = moveStack->size - 1 - index;
    StackNode* current = moveStack->top;
    for (int i = 0; i < target && current; i++) {
        current = current->next;
    }
    return current ? (Move*)current->data : NULL;
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
        int row = (logic->turn == PLAYER_WHITE) ? 2 : 5; // e3 or e6
        int written = snprintf(ptr, remaining, " %c%d", colParams[logic->enPassantCol], 8 - row);
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
    
    // Store attributes for undo
    move->prevEnPassantCol = (int8_t)logic->enPassantCol;
    move->prevCastlingRights = logic->castlingRights;
    move->prevHalfmoveClock = logic->halfmoveClock;
    
    move->capturedPieceType = NO_PIECE;
    if (logic->board[r2][c2]) {
        move->capturedPieceType = logic->board[r2][c2]->type;
        piece_free(logic->board[r2][c2]);
        logic->board[r2][c2] = NULL;
        logic->halfmoveClock = 0;
    } else if (movingPiece->type == PIECE_PAWN) {
        logic->halfmoveClock = 0;
    } else {
        logic->halfmoveClock++;
    }
    
    if (movingPiece->type == PIECE_PAWN && c1 != c2 && logic->board[r2][c2] == NULL) {
        // En passant
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
    
    logic->board[r2][c2] = movingPiece;
    logic->board[r1][c1] = NULL;
    
    if (movingPiece->type == PIECE_PAWN && (r2 == 0 || r2 == 7)) {
        if (move->promotionPiece == NO_PROMOTION) move->promotionPiece = PIECE_QUEEN;
        Player owner = movingPiece->owner;
        piece_free(movingPiece);
        logic->board[r2][c2] = piece_create(move->promotionPiece, owner);
        logic->board[r2][c2]->hasMoved = true;
    }
    
    if (movingPiece->type == PIECE_PAWN && abs(r1 - r2) == 2) {
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
static void undo_move_internal(GameLogic* logic) {
    if (!logic) return;
    
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || !moveStack->top) return;
    
    Move* lastMove = (Move*)stack_pop(moveStack);
    
    // Switch turns back
    logic->turn = get_opponent(logic->turn);
    if (logic->turn == PLAYER_BLACK) logic->fullmoveNumber--;
    
    int r1 = lastMove->from_sq / 8, c1 = lastMove->from_sq % 8;
    int r2 = lastMove->to_sq / 8, c2 = lastMove->to_sq % 8;
    
    Piece* movedPiece = logic->board[r2][c2];
    if (movedPiece) {
        if (lastMove->promotionPiece != NO_PROMOTION) {
            piece_free(movedPiece);
            logic->board[r1][c1] = piece_create(PIECE_PAWN, logic->turn);
        } else {
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
    
    move_free(lastMove);
    logic->currentHash = zobrist_compute(logic);
    logic->positionVersion++;
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
    // Store as new start FEN (assuming load_fen acts as a setup)
    strncpy(logic->start_fen, fen, sizeof(logic->start_fen)-1);
    logic->start_fen[sizeof(logic->start_fen)-1] = '\0';
    
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
    
    // Assume pieces haven't moved unless FEN says otherwise
    // (This is a simplified approach, but better than skipping)
    bool wk = false, wq = false, bk = false, bq = false;
    if (*ptr && *ptr != '-') {
        while (*ptr && *ptr != ' ') {
            if (*ptr == 'K') wk = true;
            else if (*ptr == 'Q') wq = true;
            else if (*ptr == 'k') bk = true;
            else if (*ptr == 'q') bq = true;
            ptr++;
        }
    } else if (*ptr == '-') {
        ptr++;
    }

    // Set hasMoved flags for kings and rooks based on castling rights
    // White
    if (logic->board[7][4] && logic->board[7][4]->type == PIECE_KING) {
        logic->board[7][4]->hasMoved = !(wk || wq);
    }
    if (logic->board[7][7] && logic->board[7][7]->type == PIECE_ROOK) {
        logic->board[7][7]->hasMoved = !wk;
    }
    if (logic->board[7][0] && logic->board[7][0]->type == PIECE_ROOK) {
        logic->board[7][0]->hasMoved = !wq;
    }
    // Black
    if (logic->board[0][4] && logic->board[0][4]->type == PIECE_KING) {
        logic->board[0][4]->hasMoved = !(bk || bq);
    }
    if (logic->board[0][7] && logic->board[0][7]->type == PIECE_ROOK) {
        logic->board[0][7]->hasMoved = !bk;
    }
    if (logic->board[0][0] && logic->board[0][0]->type == PIECE_ROOK) {
        logic->board[0][0]->hasMoved = !bq;
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
                // If it's White's turn, en passant target must be Rank 6 (Row 2), meaning Black just moved double. 
                // Wait. 
                // If White to move, EP target is behind the Black pawn. Black pawn moved to Rank 5. EP Target is Rank 6? 
                // Standard FEN:
                // "e6" means Black just moved e7-e5. Target e6. White to move. Capture on e6.
                // "e3" means White just moved e2-e4. Target e3. Black to move. Capture on e3.
                
                int expectedRank = (logic->turn == PLAYER_WHITE) ? 5 : 2; // Rank 6 (row 2) or Rank 3 (row 5)
                
                // Let's map char '3' to rank index. '3' - '1' = 2.
                // Our rows are: '8'->row 0. '1'->row 7.
                // internal_row = 7 - (char_rank - '1').
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

    // Bump version for cache
    logic->positionVersion++;
    
    // Update game state
    gamelogic_update_game_state(logic);
}

static const char* get_san_piece_char(PieceType type) {
    switch (type) {
        case PIECE_KNIGHT: return "N";
        case PIECE_BISHOP: return "B";
        case PIECE_ROOK:   return "R";
        case PIECE_QUEEN:  return "Q";
        case PIECE_KING:   return "K";
        default: return "";
    }
}

void gamelogic_get_move_san(GameLogic* logic, Move* move, char* san, size_t san_size) {
    if (!logic || !move || !san || san_size == 0) return;

    // Check if we need to temporarily revert the state
    // (If the move passed is the one just played, we are in 'after' state, but need 'before' for SAN context)
    bool was_simulation = logic->isSimulation;
    bool needs_restore = false;
    Move* restore_move = NULL;
    
    Move* last = gamelogic_get_last_move(logic);
    if (last && move_equals(last, move)) {
        // CRITICAL: We are about to UNDO the last move, which will pop it from history and FREE it.
        // The 'move' pointer (which likely points to that history node) will become invalid.
        // We MUST make a copy of the move to use for restoration (redo).
        restore_move = move_copy(move);
        
        logic->isSimulation = true; // Suppress callbacks during revert
        gamelogic_undo_move(logic);
        logic->isSimulation = was_simulation;
        needs_restore = true;
    }

    int r1 = move->from_sq / 8, c1 = move->from_sq % 8;
    int r2 = move->to_sq / 8, c2 = move->to_sq % 8;

    if (move->isCastling) {
        if (c2 > c1) {
            strncpy(san, "O-O", san_size);
        } else {
            strncpy(san, "O-O-O", san_size);
        }
    } else {
        char* ptr = san;
        size_t remaining = san_size - 1;

        Piece* p = logic->board[r1][c1]; 
        // Note: Fallback line removed because we ensure 'p' exists by reverting state if needed.
        // If p is still null here, move is invalid for this state, but we handle gracefully.
        PieceType type = p ? p->type : PIECE_PAWN;

        if (type != PIECE_PAWN) {
            const char* p_char = get_san_piece_char(type);
            int written = snprintf(ptr, remaining, "%s", p_char);
            ptr += written;
            remaining -= written;

            // Disambiguation
            int count = 0;
            // Only generate legal moves if p exists (valid state)
            if (p) {
                Move** legal_moves = gamelogic_get_all_legal_moves(logic, move->mover, &count);
                bool need_file = false;
                bool need_rank = false;
                bool multiple = false;

                for (int i = 0; i < count; i++) {
                    Move* m = legal_moves[i];
                    if (m->from_sq == move->from_sq) continue;
                    
                    // int mr1 = m->from_sq / 8, mc1 = m->from_sq % 8; // unused vars removed
                    int mr2 = m->to_sq / 8, mc2 = m->to_sq % 8;

                    if (mr2 == r2 && mc2 == c2) {
                        Piece* mp = logic->board[m->from_sq / 8][m->from_sq % 8];
                        if (mp && mp->type == type) {
                            multiple = true;
                            if ((m->from_sq % 8) != c1) need_file = true;
                            else need_rank = true;
                        }
                    }
                }
                if (multiple) {
                    if (need_file && remaining > 0) {
                        *ptr++ = 'a' + c1;
                        remaining--;
                    }
                    if (need_rank && remaining > 0) {
                        *ptr++ = '8' - r1;
                        remaining--;
                    }
                }
                gamelogic_free_moves_array(legal_moves, count);
            }
        }

        if (move->capturedPieceType != NO_PIECE) {
            if (type == PIECE_PAWN) {
                if (remaining > 0) {
                    *ptr++ = 'a' + c1;
                    remaining--;
                }
            }
            if (remaining > 0) {
                *ptr++ = 'x';
                remaining--;
            }
        }

        if (remaining >= 2) {
            int written = snprintf(ptr, remaining, "%c%d", 'a' + c2, 8 - r2);
            ptr += written;
            remaining -= written;
        }

        if (move->promotionPiece != NO_PROMOTION) {
            const char* promo_char = get_san_piece_char(move->promotionPiece);
            if (remaining >= 2) {
                int written = snprintf(ptr, remaining, "=%s", promo_char);
                ptr += written;
                remaining -= written;
            }
        }
        *ptr = '\0';
    }

    // Add Check/Checkmate marks
    // Force simulation to avoid hooks during check
    logic->isSimulation = true;
    gamelogic_perform_move(logic, move);
    bool checkmate = gamelogic_is_checkmate(logic, logic->turn);
    bool check = !checkmate && gamelogic_is_in_check(logic, logic->turn);
    gamelogic_undo_move(logic);
    logic->isSimulation = was_simulation;

    if (checkmate) {
        strncat(san, "#", san_size - strlen(san) - 1);
    } else if (check) {
        strncat(san, "+", san_size - strlen(san) - 1);
    }

    // Restore state if we reverted
    if (needs_restore && restore_move) {
        logic->isSimulation = true; // Suppress hooks
        gamelogic_perform_move(logic, restore_move);
        logic->isSimulation = was_simulation;
        move_free(restore_move);
    }
}

void gamelogic_load_from_san_moves(GameLogic* logic, const char* moves_san, const char* start_fen) {
    if (!logic || !moves_san) return;
    
    // 1. Reset logic to starting position
    if (start_fen && start_fen[0] != '\0') {
         gamelogic_load_fen(logic, start_fen);
    } else {
         gamelogic_reset(logic);
    }
    
    // 2. Tokenize SAN string (e.g., "e4 Nf3 O-O")
    char* moves_copy = strdup(moves_san);
    if (!moves_copy) return;
    
    char* token = strtok(moves_copy, " ");
    while (token) {
        // Skip move numbers (e.g. "1.", "2.")
        if (strchr(token, '.')) {
            token = strtok(NULL, " ");
            continue;
        }

        // Find matching move for this SAN among all legal moves
        int count = 0;
        Move** legal_moves = gamelogic_get_all_legal_moves(logic, logic->turn, &count);
        
        Move* matched_move = NULL;
        for (int i = 0; i < count; i++) {
            char current_san[16];
            gamelogic_get_move_san(logic, legal_moves[i], current_san, sizeof(current_san));
            if (strcmp(current_san, token) == 0) {
                matched_move = move_copy(legal_moves[i]);
                break;
            }
        }
        
        // Cleanup all generated legal moves
        for (int i = 0; i < count; i++) move_free(legal_moves[i]);
        if (legal_moves) free(legal_moves);
        
        if (matched_move) {
            // Apply the move to the board
            gamelogic_perform_move(logic, matched_move);
            move_free(matched_move);
        } else {
            fprintf(stderr, "[ERROR] Replay: Could not match SAN move '%s' at ply %d\n", 
                    token, ((Stack*)logic->moveHistory) ? ((Stack*)logic->moveHistory)->size : 0);
            break; 
        }
        
        token = strtok(NULL, " ");
    }
    
    free(moves_copy);
}

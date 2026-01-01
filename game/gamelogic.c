#include "gamelogic.h"
#include "piece.h"
#include "move.h"
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

// Simple list implementation for position history
typedef struct ListNode {
    long long data;
    struct ListNode* next;
} ListNode;

typedef struct {
    ListNode* head;
    ListNode* tail;
    int size;
} List;

static List* list_create(void) {
    List* l = (List*)malloc(sizeof(List));
    if (l) {
        l->head = NULL;
        l->tail = NULL;
        l->size = 0;
    }
    return l;
}

static void list_remove_last(List* l) {
    if (!l || !l->head) return;
    if (l->head == l->tail) {
        free(l->head);
        l->head = l->tail = NULL;
        l->size = 0;
    } else {
        ListNode* current = l->head;
        while (current->next != l->tail) {
            current = current->next;
        }
        free(l->tail);
        l->tail = current;
        current->next = NULL;
        l->size--;
    }
}

static void list_free(List* l) {
    if (!l) return;
    ListNode* current = l->head;
    while (current) {
        ListNode* next = current->next;
        free(current);
        current = next;
    }
    free(l);
}

// Simple list for PieceType (for captured pieces)
typedef struct PieceTypeNode {
    PieceType type;
    struct PieceTypeNode* next;
} PieceTypeNode;

typedef struct {
    PieceTypeNode* head;
    int size;
} PieceTypeList;

// Note: piece_type_list_create is not used here - lists are created in info_panel.c
// This function is kept for potential future use but marked as unused to suppress warning
__attribute__((unused)) static PieceTypeList* piece_type_list_create(void) {
    PieceTypeList* list = (PieceTypeList*)calloc(1, sizeof(PieceTypeList));
    return list;
}

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
        strcpy(logic->statusMessage, "White's Turn");
        logic->vsComputer = true;
        logic->aiDepth = 3;
        logic->whiteAI = AI_TYPE_PRO;
        logic->blackAI = AI_TYPE_PRO;
        logic->whiteDepth = 3;
        logic->blackDepth = 3;
        logic->enPassantCol = -1;
        logic->moveHistory = stack_create();
        logic->enPassantHistory = stack_create();
        logic->positionHistory = list_create();
        logic->cvcRunning = false;
        logic->cvcPaused = false;
        logic->isSimulation = false;
        logic->updateCallback = NULL;
        logic->refreshLayoutCallback = NULL;
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
    
    // Free en passant history
    Stack* epStack = (Stack*)logic->enPassantHistory;
    if (epStack) {
        while (epStack->top) {
            int* ep = (int*)stack_pop(epStack);
            free(ep);
        }
        stack_free(epStack);
    }
    
    // Free position history
    List* posList = (List*)logic->positionHistory;
    if (posList) {
        list_free(posList);
    }
    
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

// Reset game (alias)
void gamelogic_reset_game(GameLogic* logic) {
    gamelogic_reset(logic);
}


void gamelogic_reset(GameLogic* logic) {
    if (!logic) return;
    
    logic->turn = PLAYER_WHITE;
    logic->isGameOver = false;
    strcpy(logic->statusMessage, "White's Turn");
    logic->enPassantCol = -1;
    
    // Clear move history
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (moveStack) {
        while (moveStack->top) {
            Move* m = (Move*)stack_pop(moveStack);
            move_free(m);
        }
    }
    
    // Clear en passant history
    Stack* epStack = (Stack*)logic->enPassantHistory;
    if (epStack) {
        while (epStack->top) {
            int* ep = (int*)stack_pop(epStack);
            free(ep);
        }
    }
    
    // Clear position history
    List* posList = (List*)logic->positionHistory;
    if (posList) {
        list_free(posList);
        logic->positionHistory = list_create();
    }
    
    // Reset statistics
    logic->whiteMoves = 0;
    logic->blackMoves = 0;
    logic->whiteCaptures = 0;
    logic->blackCaptures = 0;
    logic->whiteCastles = 0;
    logic->blackCastles = 0;
    logic->whitePromotions = 0;
    logic->blackPromotions = 0;
    logic->whiteChecks = 0;
    logic->blackChecks = 0;
    logic->gameStartTime = (long long)time(NULL) * 1000;
    
    setup_board(logic);
    gamelogic_update_game_state(logic);
}

// Get opponent
static Player get_opponent(Player p) {
    return (p == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;
}

// Update game state
void gamelogic_update_game_state(GameLogic* logic) {
    if (!logic) return;
    
    if (gamelogic_is_checkmate(logic, logic->turn)) {
        logic->isGameOver = true;
        Player winner = get_opponent(logic->turn);
        if (winner == PLAYER_WHITE) {
            strcpy(logic->statusMessage, "Checkmate! White wins!");
        } else {
            strcpy(logic->statusMessage, "Checkmate! Black wins!");
        }
    } else if (gamelogic_is_stalemate(logic, logic->turn)) {
        logic->isGameOver = true;
        strcpy(logic->statusMessage, "Stalemate! Draw.");
    } else if (gamelogic_is_in_check(logic, logic->turn)) {
        if (logic->turn == PLAYER_WHITE) {
            strcpy(logic->statusMessage, "White is in Check!");
        } else {
            strcpy(logic->statusMessage, "Black is in Check!");
        }
    } else {
        if (logic->turn == PLAYER_WHITE) {
            strcpy(logic->statusMessage, "White's Turn");
        } else {
            strcpy(logic->statusMessage, "Black's Turn");
        }
    }
    
    if (logic->updateCallback) {
        logic->updateCallback();
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

void gamelogic_set_refresh_layout_callback(GameLogic* logic, void (*callback)(void)) {
    if (logic) logic->refreshLayoutCallback = callback;
}

// Forward declarations for internal functions
static void make_move_internal(GameLogic* logic, Move* move);
static void undo_move_internal(GameLogic* logic);
static char get_fen_char(Piece* p);
static Player get_opponent(Player p);

// External move generation (defined in gamelogic_movegen.c)
extern void gamelogic_generate_legal_moves(GameLogic* logic, Player player, void* moves_list);

// External safety checks (defined in gamelogic_safety.c)
extern bool gamelogic_is_square_safe(GameLogic* logic, int r, int c, Player p);
extern bool gamelogic_is_in_check(GameLogic* logic, Player player);
extern bool gamelogic_is_checkmate(GameLogic* logic, Player player);
extern bool gamelogic_is_stalemate(GameLogic* logic, Player player);

// Perform a move
bool gamelogic_perform_move(GameLogic* logic, Move* move) {
    if (!logic || !move) return false;
    if (move->startRow == move->endRow && move->startCol == move->endCol) return false;
    
    Piece* movingPiece = logic->board[move->startRow][move->startCol];
    if (!movingPiece) return false;
    
    Player movingPlayer = logic->turn;
    
    make_move_internal(logic, move);
    
    // Update statistics (only for non-simulation moves)
    if (!logic->isSimulation) {
        // Count moves
        if (movingPlayer == PLAYER_WHITE) {
            logic->whiteMoves++;
        } else {
            logic->blackMoves++;
        }
        
        // Count captures
        if (move->capturedPiece) {
            if (movingPlayer == PLAYER_WHITE) {
                logic->whiteCaptures++;
            } else {
                logic->blackCaptures++;
            }
        }
        
        // Count castling
        if (move->isCastling) {
            if (movingPlayer == PLAYER_WHITE) {
                logic->whiteCastles++;
            } else {
                logic->blackCastles++;
            }
        }
        
        // Count promotions
        if (move->promotionPiece != NO_PROMOTION) {
            if (movingPlayer == PLAYER_WHITE) {
                logic->whitePromotions++;
            } else {
                logic->blackPromotions++;
            }
        }
        
        // Count checks
        Player opponent = logic->turn;  // turn already switched
        if (gamelogic_is_in_check(logic, opponent)) {
            if (movingPlayer == PLAYER_WHITE) {
                logic->whiteChecks++;
            } else {
                logic->blackChecks++;
            }
        }
        
        // Add to position history (Zobrist hash)
        // TODO: Implement Zobrist hashing
        gamelogic_update_game_state(logic);
        if (logic->updateCallback) {
            logic->updateCallback();
        }
    }
    
    if (logic->updateCallback) {
        logic->updateCallback();
    }
    
    return true;
}

// Undo last move
void gamelogic_undo_move(GameLogic* logic) {
    if (!logic) return;
    
    List* posList = (List*)logic->positionHistory;
    if (posList && posList->size > 0) {
        list_remove_last(posList);
    }
    
    undo_move_internal(logic);
    
    if (!logic->isSimulation) {
        gamelogic_update_game_state(logic);
        if (logic->updateCallback) {
            logic->updateCallback();
        }
    }
    
    if (logic->updateCallback) {
        logic->updateCallback();
    }
}

// Simulate move and check safety (for move validation)
bool gamelogic_simulate_move_and_check_safety(GameLogic* logic, Move* m, Player p) {
    if (!logic || !m) return false;
    
    make_move_internal(logic, m);
    bool safe = !gamelogic_is_in_check(logic, p);
    undo_move_internal(logic);
    
    return safe;
}

// Get last move
Move* gamelogic_get_last_move(GameLogic* logic) {
    if (!logic) return NULL;
    Stack* moveStack = (Stack*)logic->moveHistory;
    if (!moveStack || !moveStack->top) return NULL;
    return (Move*)moveStack->top->data;
}

// Get total moves made
int gamelogic_get_move_count(GameLogic* logic) {
    if (!logic || !logic->moveHistory) return 0;
    Stack* moveStack = (Stack*)logic->moveHistory;
    return moveStack->size;
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
    if (remaining > 0) {
        int written = snprintf(ptr, remaining, " %c ", (logic->turn == PLAYER_WHITE) ? 'w' : 'b');
        ptr += written;
        remaining -= written;
    }
    
    // Castling rights
    int castlingRights = gamelogic_get_castling_rights(logic);
    bool hasCastling = false;
    if (castlingRights & 8) {  // WK
        if (remaining > 0) { *ptr++ = 'K'; remaining--; hasCastling = true; }
    }
    if (castlingRights & 4) {  // WQ
        if (remaining > 0) { *ptr++ = 'Q'; remaining--; hasCastling = true; }
    }
    if (castlingRights & 2) {  // BK
        if (remaining > 0) { *ptr++ = 'k'; remaining--; hasCastling = true; }
    }
    if (castlingRights & 1) {  // BQ
        if (remaining > 0) { *ptr++ = 'q'; remaining--; hasCastling = true; }
    }
    if (!hasCastling && remaining > 0) {
        *ptr++ = '-';
        remaining--;
    }
    
    // En passant
    if (logic->enPassantCol != -1) {
        const char* colParams = "abcdefgh";
        int row = (logic->turn == PLAYER_WHITE) ? 2 : 5;
        int written = snprintf(ptr, remaining, " %c%d", colParams[logic->enPassantCol], 8 - row);
        ptr += written;
        remaining -= written;
    } else {
        if (remaining > 0) {
            int written = snprintf(ptr, remaining, " -");
            ptr += written;
            remaining -= written;
        }
    }
    
    // Halfmove and fullmove clocks (placeholders)
    if (remaining > 0) {
        int written = snprintf(ptr, remaining, " 0 1");
        ptr += written;
        remaining -= written;
    }
    
    *ptr = '\0';
}

// Get castling rights as integer
int gamelogic_get_castling_rights(GameLogic* logic) {
    if (!logic) return 0;
    
    int rights = 0;
    Piece* wk = logic->board[7][4];
    Piece* bk = logic->board[0][4];
    
    // White castling
    if (wk && wk->type == PIECE_KING && !wk->hasMoved) {
        Piece* wrk = logic->board[7][7];
        Piece* wrq = logic->board[7][0];
        if (wrk && wrk->type == PIECE_ROOK && !wrk->hasMoved) rights |= 8;  // WK
        if (wrq && wrq->type == PIECE_ROOK && !wrq->hasMoved) rights |= 4;  // WQ
    }
    
    // Black castling
    if (bk && bk->type == PIECE_KING && !bk->hasMoved) {
        Piece* brk = logic->board[0][7];
        Piece* brq = logic->board[0][0];
        if (brk && brk->type == PIECE_ROOK && !brk->hasMoved) rights |= 2;  // BK
        if (brq && brq->type == PIECE_ROOK && !brq->hasMoved) rights |= 1;  // BQ
    }
    
    return rights;
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
        if (m && m->capturedPiece) {
            // If the captured piece belonged to the opponent of capturer, add it
            if (m->capturedPiece->owner != capturer) {
                piece_type_list_add(list, m->capturedPiece->type);
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

// Internal: Make a move (without callbacks or statistics)
static void make_move_internal(GameLogic* logic, Move* move) {
    if (!logic || !move) return;
    
    Piece* movingPiece = logic->board[move->startRow][move->startCol];
    if (!movingPiece) return;
    
    // Record state for undo
    if (logic->board[move->endRow][move->endCol]) {
        move->capturedPiece = piece_copy(logic->board[move->endRow][move->endCol]);
    }
    move->firstMove = !movingPiece->hasMoved;
    
    // En passant capture
    if (movingPiece->type == PIECE_PAWN && move->startCol != move->endCol && 
        logic->board[move->endRow][move->endCol] == NULL) {
        move->isEnPassant = true;
        if (logic->board[move->startRow][move->endCol]) {
            move->capturedPiece = piece_copy(logic->board[move->startRow][move->endCol]);
            piece_free(logic->board[move->startRow][move->endCol]);
            logic->board[move->startRow][move->endCol] = NULL;
        }
    }
    
    // Castling
    if (movingPiece->type == PIECE_KING && abs(move->startCol - move->endCol) == 2) {
        move->isCastling = true;
        int rookStartCol = (move->endCol > move->startCol) ? 7 : 0;
        int rookDestCol = (move->endCol > move->startCol) ? 5 : 3;
        Piece* rook = logic->board[move->startRow][rookStartCol];
        logic->board[move->startRow][rookDestCol] = rook;
        logic->board[move->startRow][rookStartCol] = NULL;
        if (rook) rook->hasMoved = true;
    }
    
    // Move piece
    logic->board[move->endRow][move->endCol] = movingPiece;
    logic->board[move->startRow][move->startCol] = NULL;
    
    // Promotion
    if (movingPiece->type == PIECE_PAWN && (move->endRow == 0 || move->endRow == 7)) {
        if (move->promotionPiece == NO_PROMOTION) move->promotionPiece = PIECE_QUEEN;
        piece_free(movingPiece);
        logic->board[move->endRow][move->endCol] = piece_create(move->promotionPiece, logic->turn);
    }
    
    // Update en passant target
    if (movingPiece->type == PIECE_PAWN && abs(move->startRow - move->endRow) == 2) {
        logic->enPassantCol = move->startCol;
    } else {
        logic->enPassantCol = -1;
    }
    
    movingPiece->hasMoved = true;
    logic->turn = get_opponent(logic->turn);
    
    // Add to history (copy the move so caller can free their copy)
    Stack* moveStack = (Stack*)logic->moveHistory;
    Stack* epStack = (Stack*)logic->enPassantHistory;
    if (moveStack) {
        Move* moveCopy = move_copy(move);
        stack_push(moveStack, moveCopy);
    }
    if (epStack) {
        int* ep = (int*)malloc(sizeof(int));
        *ep = logic->enPassantCol;
        stack_push(epStack, ep);
    }
}

// Internal: Undo last move
static void undo_move_internal(GameLogic* logic) {
    if (!logic) return;
    
    Stack* moveStack = (Stack*)logic->moveHistory;
    Stack* epStack = (Stack*)logic->enPassantHistory;
    if (!moveStack || !moveStack->top) return;
    
    Move* lastMove = (Move*)stack_pop(moveStack);
    if (epStack && epStack->top) {
        int* ep = (int*)stack_pop(epStack);
        logic->enPassantCol = *ep;
        free(ep);
    }
    
    // Get the player who made this move (before we switch turns)
    Player movePlayer = get_opponent(logic->turn);
    
    logic->turn = get_opponent(logic->turn);
    
    Piece* movedPiece = logic->board[lastMove->endRow][lastMove->endCol];
    if (!movedPiece) {
        move_free(lastMove);
        return;
    }
    
    // Revert promotion or move piece back
    if (lastMove->promotionPiece != NO_PROMOTION) {
        piece_free(movedPiece);
        // Use movePlayer (the player who made the move) not logic->turn (which is now the opponent)
        logic->board[lastMove->startRow][lastMove->startCol] = piece_create(PIECE_PAWN, movePlayer);
        logic->board[lastMove->startRow][lastMove->startCol]->hasMoved = true;
    } else {
        logic->board[lastMove->startRow][lastMove->startCol] = movedPiece;
    }
    logic->board[lastMove->endRow][lastMove->endCol] = NULL;
    
    // Restore captured piece
    if (lastMove->capturedPiece) {
        if (lastMove->isEnPassant) {
            logic->board[lastMove->startRow][lastMove->endCol] = piece_copy(lastMove->capturedPiece);
        } else {
            logic->board[lastMove->endRow][lastMove->endCol] = piece_copy(lastMove->capturedPiece);
        }
    }
    
    // Revert castling
    if (lastMove->isCastling) {
        int rookStartCol = (lastMove->endCol > lastMove->startCol) ? 7 : 0;
        int rookDestCol = (lastMove->endCol > lastMove->startCol) ? 5 : 3;
        Piece* rook = logic->board[lastMove->startRow][rookDestCol];
        logic->board[lastMove->startRow][rookStartCol] = rook;
        logic->board[lastMove->startRow][rookDestCol] = NULL;
        if (rook) rook->hasMoved = false;
    }
    
    // Restore hasMoved status
    if (lastMove->firstMove) {
        logic->board[lastMove->startRow][lastMove->startCol]->hasMoved = false;
    }
    
    move_free(lastMove);
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
    
    // Skip castling rights for now (would need to set hasMoved flags)
    while (*ptr && *ptr != ' ') ptr++;
    while (*ptr && *ptr == ' ') ptr++;
    
    // Parse en passant
    if (*ptr && *ptr != '-') {
        if (*ptr >= 'a' && *ptr <= 'h') {
            logic->enPassantCol = *ptr - 'a';
        }
    }
    
    // Update game state
    gamelogic_update_game_state(logic);
}




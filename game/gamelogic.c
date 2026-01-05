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
        logic->statusMessage[0] = '\0';
        strcat(logic->statusMessage, "White's Turn");
        logic->enPassantCol = -1;
        logic->moveHistory = stack_create();
        logic->enPassantHistory = stack_create();
        // logic->positionHistory removed

        logic->isSimulation = false;
        logic->updateCallback = NULL;
        
        // Initialize Cache
        logic->cachedMoves = NULL;
        logic->cachedPieceRow = -1;
        
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
    
    // Clear cache
    gamelogic_clear_cache(logic);
    logic->cachedPieceRow = -1;
    
    // Reset statistics removed
    
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
    
    // Reset isGameOver before checking conditions
    logic->isGameOver = false;
    
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
    if (!logic || !move) return false;
    if (move->startRow == move->endRow && move->startCol == move->endCol) return false;
    
    Piece* movingPiece = logic->board[move->startRow][move->startCol];
    if (!movingPiece) return false;
    
    // Safety check: ensure only the mover can move their own pieces
    if (movingPiece->owner != logic->turn) return false;
    
    make_move_internal(logic, move);
    
    // --- OPTIMIZATION START ---
    if (!logic->isSimulation) {
        if (logic->updateCallback) logic->updateCallback(); 
    }
    // --- OPTIMIZATION END --- 
    
    // Invalidate move cache
    logic->cachedPieceRow = -1;
    
    return true;
}

void gamelogic_undo_move(GameLogic* logic) {
    if (!logic) return;
    
    // No position history, just undo
    undo_move_internal(logic);

    // --- OPTIMIZATION START ---
    if (!logic->isSimulation) {
        gamelogic_update_game_state(logic); 
        if (logic->updateCallback) logic->updateCallback();
    }
    // --- OPTIMIZATION END ---

    // Invalidate move cache
    logic->cachedPieceRow = -1;
}

// Simulate a move and check if the resulting position is safe for the given player
// OPTIMIZED: Only save/restore affected squares to minimize RAM usage
bool gamelogic_simulate_move_and_check_safety(GameLogic* logic, Move* m, Player p) {
    if (!logic || !m) return false;
    
    // Save only affected squares (max 4: start, end, en passant victim, castling rook)
    Piece* saved_start = logic->board[m->startRow][m->startCol] ? piece_copy(logic->board[m->startRow][m->startCol]) : NULL;
    Piece* saved_end = logic->board[m->endRow][m->endCol] ? piece_copy(logic->board[m->endRow][m->endCol]) : NULL;
    Piece* saved_ep = NULL;
    Piece* saved_rook = NULL;
    int rookStartCol = -1, rookDestCol = -1;
    
    Player saved_turn = logic->turn;
    int saved_enPassantCol = logic->enPassantCol;
    
    // Execute move directly on board
    Piece* movingPiece = logic->board[m->startRow][m->startCol];
    if (!movingPiece) {
        if (saved_start) piece_free(saved_start);
        if (saved_end) piece_free(saved_end);
        return false;
    }
    
    // Handle capture at destination
    if (logic->board[m->endRow][m->endCol]) {
        piece_free(logic->board[m->endRow][m->endCol]);
    }
    
    // Move piece
    logic->board[m->endRow][m->endCol] = movingPiece;
    logic->board[m->startRow][m->startCol] = NULL;
    
    // Handle en passant
    if (m->isEnPassant && logic->board[m->startRow][m->endCol]) {
        saved_ep = piece_copy(logic->board[m->startRow][m->endCol]);
        piece_free(logic->board[m->startRow][m->endCol]);
        logic->board[m->startRow][m->endCol] = NULL;
    }
    
    // Handle castling
    if (m->isCastling) {
        rookStartCol = (m->endCol > m->startCol) ? 7 : 0;
        rookDestCol = (m->endCol > m->startCol) ? 5 : 3;
        Piece* rook = logic->board[m->startRow][rookStartCol];
        if (rook) {
            saved_rook = piece_copy(rook);
            logic->board[m->startRow][rookDestCol] = rook;
            logic->board[m->startRow][rookStartCol] = NULL;
        }
    }
    
    // Check safety
    bool safe = !gamelogic_is_in_check(logic, p);
    
    // RESTORE affected squares only
    logic->board[m->startRow][m->startCol] = saved_start;
    if (logic->board[m->endRow][m->endCol]) piece_free(logic->board[m->endRow][m->endCol]);
    logic->board[m->endRow][m->endCol] = saved_end;
    
    if (saved_ep) {
        logic->board[m->startRow][m->endCol] = saved_ep;
    }
    
    if (saved_rook && rookStartCol >= 0) {
        if (logic->board[m->startRow][rookDestCol]) piece_free(logic->board[m->startRow][rookDestCol]);
        logic->board[m->startRow][rookStartCol] = saved_rook;
        logic->board[m->startRow][rookDestCol] = NULL;
    }
    
    logic->turn = saved_turn;
    logic->enPassantCol = saved_enPassantCol;
    
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
    
    // Store previous en passant state for undo
    int prevEnPassantCol = logic->enPassantCol;
    
    // 1. Record state for Undo
    move->capturedPiece = NULL;
    if (logic->board[move->endRow][move->endCol]) {
        // CRITICAL: piece_copy MUST preserve owner correctly!
        Piece* victim = logic->board[move->endRow][move->endCol];
        move->capturedPiece = piece_copy(victim);
        // SAFETY CHECK: Ensure owner was copied correctly
        if (move->capturedPiece && move->capturedPiece->owner != victim->owner) {
            // This should NEVER happen - piece_copy is broken!
            move->capturedPiece->owner = victim->owner;
        }
    }
    move->firstMove = !movingPiece->hasMoved;

    // 2. En Passant Capture Logic
    if (movingPiece->type == PIECE_PAWN && move->startCol != move->endCol && 
        logic->board[move->endRow][move->endCol] == NULL) {
        
        move->isEnPassant = true;
        Piece* victim = logic->board[move->startRow][move->endCol];
        if (victim) {
            // Free any previous capture copy (though shouldn't exist for EP)
            if (move->capturedPiece) piece_free(move->capturedPiece);
            move->capturedPiece = piece_copy(victim);
            piece_free(victim);
            logic->board[move->startRow][move->endCol] = NULL;
        }
    } else if (logic->board[move->endRow][move->endCol]) {
        // Normal capture memory management
        piece_free(logic->board[move->endRow][move->endCol]);
        logic->board[move->endRow][move->endCol] = NULL;
    }
    
    // Castling
    if (movingPiece->type == PIECE_KING && abs(move->startCol - move->endCol) == 2) {
        move->isCastling = true;
        int rookStartCol = (move->endCol > move->startCol) ? 7 : 0;
        int rookDestCol = (move->endCol > move->startCol) ? 5 : 3;
        Piece* rook = logic->board[move->startRow][rookStartCol];
        if (rook) {
            move->rookFirstMove = !rook->hasMoved;
            logic->board[move->startRow][rookDestCol] = rook;
            logic->board[move->startRow][rookStartCol] = NULL;
            rook->hasMoved = true;
        }
    }
    
    // Move piece
    logic->board[move->endRow][move->endCol] = movingPiece;
    logic->board[move->startRow][move->startCol] = NULL;
    
    // Set hasMoved status
    movingPiece->hasMoved = true;
    
    // Promotion
    if (movingPiece->type == PIECE_PAWN && (move->endRow == 0 || move->endRow == 7)) {
        Player pawnOwner = movingPiece->owner;
        if (move->promotionPiece == NO_PROMOTION) move->promotionPiece = PIECE_QUEEN;
        
        // Remove old piece and create new one
        piece_free(movingPiece);
        logic->board[move->endRow][move->endCol] = piece_create(move->promotionPiece, pawnOwner);
        logic->board[move->endRow][move->endCol]->hasMoved = true;
        // After free, we must not access movingPiece anymore
        movingPiece = logic->board[move->endRow][move->endCol];
    }
    
    // 5. Update En Passant target for the NEXT turn
    if (movingPiece->type == PIECE_PAWN && abs(move->startRow - move->endRow) == 2) {
        logic->enPassantCol = move->startCol;
    } else {
        logic->enPassantCol = -1;
    }
    
    move->mover = logic->turn; // Store who made the move
    logic->turn = get_opponent(logic->turn);
    
    // Add to history
    Stack* moveStack = (Stack*)logic->moveHistory;
    Stack* epStack = (Stack*)logic->enPassantHistory;
    if (moveStack) {
        Move* moveCopy = move_copy(move);
        stack_push(moveStack, moveCopy);
    }
    if (epStack) {
        int* ep = (int*)malloc(sizeof(int));
        if (ep) {
            *ep = prevEnPassantCol; // Store PREVIOUS state
            stack_push(epStack, ep);
        }
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
        if (ep) {
            logic->enPassantCol = *ep;
            free(ep);
        }
    }
    
    // Switch turns back
    logic->turn = get_opponent(logic->turn);
    Player movePlayer = logic->turn;
    
    Piece* movedPiece = logic->board[lastMove->endRow][lastMove->endCol];
    if (!movedPiece) {
        // This should theoretically not happen if moves are perfectly matched
        // But if it does, we restored turn and need to check for captures
    } else {
        if (lastMove->promotionPiece != NO_PROMOTION) {
            piece_free(movedPiece);
            logic->board[lastMove->startRow][lastMove->startCol] = piece_create(PIECE_PAWN, movePlayer);
            logic->board[lastMove->startRow][lastMove->startCol]->hasMoved = !lastMove->firstMove;
        } else {
            logic->board[lastMove->startRow][lastMove->startCol] = movedPiece;
        }
        logic->board[lastMove->endRow][lastMove->endCol] = NULL;
    }
    
    // Restore captured piece
    if (lastMove->capturedPiece) {
        // Make a fresh copy to restore
        Piece* restoredPiece = piece_copy(lastMove->capturedPiece);
        if (restoredPiece) {
            if (lastMove->isEnPassant) {
                // En passant: victim was on same row as moving piece, at end column
                logic->board[lastMove->startRow][lastMove->endCol] = restoredPiece;
            } else {
                // Normal capture: victim was at destination square  
                logic->board[lastMove->endRow][lastMove->endCol] = restoredPiece;
            }
        }
    }
    
    // 3. Revert Castling
    if (lastMove->isCastling) {
        int rookStartCol = (lastMove->endCol > lastMove->startCol) ? 7 : 0;
        int rookDestCol = (lastMove->endCol > lastMove->startCol) ? 5 : 3;
        Piece* rook = logic->board[lastMove->startRow][rookDestCol];
        if (rook) {
            logic->board[lastMove->startRow][rookStartCol] = rook;
            logic->board[lastMove->startRow][rookDestCol] = NULL;
            rook->hasMoved = !lastMove->rookFirstMove;
        }
    }

    // 4. Restore "hasMoved" status
    if (lastMove->firstMove && logic->board[lastMove->startRow][lastMove->startCol]) {
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
    if (*ptr && *ptr != '-') {
        if (*ptr >= 'a' && *ptr <= 'h') {
            logic->enPassantCol = *ptr - 'a';
        }
    } else {
        logic->enPassantCol = -1;
    }
    
    // Update game state
    gamelogic_update_game_state(logic);
}




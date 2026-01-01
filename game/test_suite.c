#include "gamelogic.h"
#include "move.h"
#include "piece.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

// MoveList structure (matches gamelogic_movegen.c)
typedef struct {
    Move** moves;
    int count;
    int capacity;
} MoveList;

static MoveList* movelist_create(void) {
    MoveList* list = (MoveList*)malloc(sizeof(MoveList));
    if (list) {
        list->capacity = 32;
        list->count = 0;
        list->moves = (Move**)malloc(sizeof(Move*) * list->capacity);
    }
    return list;
}

static void movelist_free(MoveList* list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        move_free(list->moves[i]);
    }
    free(list->moves);
    free(list);
}

static int tests_passed = 0;
static int tests_failed = 0;

static void assert_condition(bool condition, const char* message) {
    if (condition) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("❌ FAILED: %s\n", message);
    }
}

static void print_board(GameLogic* logic) {
    printf("\nBoard:\n");
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            Piece* p = logic->board[r][c];
            if (p == NULL) {
                printf(". ");
            } else {
                char code;
                switch (p->type) {
                    case PIECE_KING: code = 'K'; break;
                    case PIECE_QUEEN: code = 'Q'; break;
                    case PIECE_ROOK: code = 'R'; break;
                    case PIECE_BISHOP: code = 'B'; break;
                    case PIECE_KNIGHT: code = 'N'; break;
                    case PIECE_PAWN: code = 'P'; break;
                    default: code = '?'; break;
                }
                if (p->owner == PLAYER_BLACK) {
                    code = (char)tolower(code);
                }
                printf("%c ", code);
            }
        }
        printf("\n");
    }
    printf("\n");
}

// Test 1: Initial Setup
static void test_initial_setup(void) {
    GameLogic* logic = gamelogic_create();
    assert_condition(logic != NULL, "GameLogic creation");
    assert_condition(logic->board[6][4] != NULL && logic->board[6][4]->type == PIECE_PAWN, 
                    "White pawn should be at e2");
    assert_condition(logic->board[6][4]->owner == PLAYER_WHITE, "Pawn should be white");
    gamelogic_free(logic);
    printf("✅ Test Initial Setup: Passed\n");
}

// Test 2: Movement and Undo
static void test_movement_and_undo(void) {
    GameLogic* logic = gamelogic_create();
    Move* m = move_create(6, 4, 4, 4); // e2-e4
    gamelogic_perform_move(logic, m);
    move_free(m);
    
    assert_condition(logic->board[6][4] == NULL, "Pawn should have moved from e2");
    assert_condition(logic->board[4][4] != NULL && logic->board[4][4]->type == PIECE_PAWN, 
                    "Pawn should be at e4");
    
    gamelogic_undo_move(logic);
    assert_condition(logic->board[6][4] != NULL && logic->board[6][4]->type == PIECE_PAWN, 
                    "Pawn should be back at e2");
    
    gamelogic_free(logic);
    printf("✅ Test Movement and Undo: Passed\n");
}

// Test 3: En Passant Capture
static void test_en_passant_capture(void) {
    GameLogic* logic = gamelogic_create();
    
    // e4
    Move* m1 = move_create(6, 4, 4, 4);
    gamelogic_perform_move(logic, m1);
    move_free(m1);
    
    // a7-a6 (filler)
    Move* m2 = move_create(1, 0, 2, 0);
    gamelogic_perform_move(logic, m2);
    move_free(m2);
    
    // e5
    Move* m3 = move_create(4, 4, 3, 4);
    gamelogic_perform_move(logic, m3);
    move_free(m3);
    
    // d7-d5 (double move, sets en passant)
    Move* m4 = move_create(1, 3, 3, 3);
    gamelogic_perform_move(logic, m4);
    move_free(m4);
    
    // e5xd6 (En Passant)
    Move* m5 = move_create(3, 4, 2, 3);
    gamelogic_perform_move(logic, m5);
    move_free(m5);
    
    assert_condition(logic->board[3][3] == NULL, "Pawn should be captured via EP");
    assert_condition(logic->board[2][3] != NULL && logic->board[2][3]->type == PIECE_PAWN, 
                    "Pawn should be at d6");
    
    gamelogic_free(logic);
    printf("✅ Test En Passant: Passed\n");
}

// Test 4: Promotion
static void test_promotion(void) {
    GameLogic* logic = gamelogic_create();
    
    // Clear board and setup
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    logic->board[1][0] = piece_create(PIECE_PAWN, PLAYER_WHITE);
    logic->turn = PLAYER_WHITE;
    
    Move* m = move_create(1, 0, 0, 0);
    m->promotionPiece = PIECE_QUEEN;
    gamelogic_perform_move(logic, m);
    move_free(m);
    
    assert_condition(logic->board[0][0] != NULL && logic->board[0][0]->type == PIECE_QUEEN, 
                    "Pawn should promote to Queen");
    
    gamelogic_undo_move(logic);
    assert_condition(logic->board[1][0] != NULL && logic->board[1][0]->type == PIECE_PAWN, 
                    "Should revert to pawn");
    
    gamelogic_free(logic);
    printf("✅ Test Promotion: Passed\n");
}

// Test 5: Castling Through Check
static void test_castling_illegal_through_check(void) {
    GameLogic* logic = gamelogic_create();
    
    // Clear board
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    // Setup: White King on e1, Rook on h1, Black Rook on f8 attacking f1
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[7][7] = piece_create(PIECE_ROOK, PLAYER_WHITE);
    logic->board[0][5] = piece_create(PIECE_ROOK, PLAYER_BLACK); // Attacks f1
    logic->turn = PLAYER_WHITE;
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    // Check if castling move exists (king from e1 to g1)
    // Check moves starting from the King's position (col 4) and ending at the castling square (col 6)
    bool can_castle = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if (m->startCol == 4 && (m->isCastling || m->endCol == 6)) {
            can_castle = true;
            break;
        }
    }
    
    assert_condition(!can_castle, "White King should NOT be able to castle through f1");
    movelist_free(moves);
    gamelogic_free(logic);
    printf("✅ Test Castling Through Check: Passed\n");
}

// Test 6: Castling While In Check
static void test_castling_illegal_while_in_check(void) {
    GameLogic* logic = gamelogic_create();
    
    // Clear board
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    // Setup: White King on e1, Rook on h1, Black Queen checking the King
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[7][7] = piece_create(PIECE_ROOK, PLAYER_WHITE);
    logic->board[0][4] = piece_create(PIECE_QUEEN, PLAYER_BLACK); // Checking the King
    logic->turn = PLAYER_WHITE;
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    bool can_castle = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if (m->endCol == 6) { // g1
            can_castle = true;
            break;
        }
    }
    
    assert_condition(!can_castle, "White should NOT be able to castle while in check");
    movelist_free(moves);
    gamelogic_free(logic);
    printf("✅ Test Castling While In Check: Passed\n");
}

// Test 7: Pin Logic
static void test_piece_pin_logic(void) {
    GameLogic* logic = gamelogic_create();
    
    // Clear board
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    // Setup: White King on e1, White Rook on e3, Black Rook on e8 (pinning the White Rook)
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[5][4] = piece_create(PIECE_ROOK, PLAYER_WHITE); // The pinned piece
    logic->board[0][4] = piece_create(PIECE_ROOK, PLAYER_BLACK); // The pinner
    logic->turn = PLAYER_WHITE;
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    // The Rook on e3 (5,4) is pinned. It should ONLY be allowed to move along the e-file (up/down).
    // It should NOT be allowed to move horizontally.
    bool pin_violated = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if (m->startRow == 5 && m->startCol == 4) {
            if (m->endCol != 4) {
                pin_violated = true;
                printf("ERROR: Pinned Rook moved horizontally from (%d,%d) to (%d,%d)!\n",
                       m->startRow, m->startCol, m->endRow, m->endCol);
                print_board(logic);
                break;
            }
        }
    }
    
    assert_condition(!pin_violated, "Pinned Rook should not move horizontally");
    movelist_free(moves);
    gamelogic_free(logic);
    printf("✅ Test Pin Logic: Passed\n");
}

// Test 8: Stalemate
static void test_stalemate(void) {
    GameLogic* logic = gamelogic_create();
    
    // Clear board
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    // Setup stalemate position: Black King on a1, White Queen on c2, White King on b2
    logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->board[1][2] = piece_create(PIECE_QUEEN, PLAYER_WHITE);  // c2
    logic->board[2][1] = piece_create(PIECE_KING, PLAYER_WHITE);   // b2
    logic->turn = PLAYER_BLACK;
    
    gamelogic_update_game_state(logic);
    
    assert_condition(logic->isGameOver, "Game should be over in stalemate");
    assert_condition(strstr(logic->statusMessage, "Stalemate") != NULL || 
                    strstr(logic->statusMessage, "stalemate") != NULL, 
                    "Status should indicate Stalemate");
    
    gamelogic_free(logic);
    printf("✅ Test Stalemate: Passed\n");
}

// Test 9: En Passant Pin (Advanced)
static void test_en_passant_illegal_pin(void) {
    GameLogic* logic = gamelogic_create();
    
    // Clear board
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (logic->board[i][j]) {
                piece_free(logic->board[i][j]);
                logic->board[i][j] = NULL;
            }
        }
    }
    
    // Setup: King on e1, White Pawn on e5, Black Pawn on d5, Black Rook on e8
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[3][4] = piece_create(PIECE_PAWN, PLAYER_WHITE); // e5
    logic->board[3][3] = piece_create(PIECE_PAWN, PLAYER_BLACK); // d5
    logic->board[0][4] = piece_create(PIECE_ROOK, PLAYER_BLACK); // e8 attacking e-file
    logic->turn = PLAYER_WHITE;
    logic->enPassantCol = 3; // column d (for en passant)
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    // If White takes d6 via En Passant, BOTH pawns leave the e-file.
    // This exposes the King to the Rook on e8. This move MUST be illegal.
    bool ep_allowed = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if (m->startRow == 3 && m->startCol == 4 && m->endRow == 2 && m->endCol == 3) {
            ep_allowed = true;
            break;
        }
    }
    
    assert_condition(!ep_allowed, "En Passant should be illegal if it exposes King via a file-opening pin");
    movelist_free(moves);
    gamelogic_free(logic);
    printf("✅ Test En Passant Pin: Passed\n");
}

int main(void) {
    printf("--- STARTING EXTENSIVE ENGINE TESTS ---\n\n");
    
    test_initial_setup();
    test_movement_and_undo();
    test_en_passant_capture();
    test_promotion();
    test_castling_illegal_through_check();
    test_castling_illegal_while_in_check();
    test_piece_pin_logic();
    test_stalemate();
    test_en_passant_illegal_pin();
    
    printf("\n--- TEST SUMMARY ---\n");
    printf("✅ Tests Passed: %d\n", tests_passed);
    if (tests_failed > 0) {
        printf("❌ Tests Failed: %d\n", tests_failed);
        return 1;
    } else {
        printf("\n✅ ALL EXTENSIVE TESTS PASSED!\n");
        return 0;
    }
}


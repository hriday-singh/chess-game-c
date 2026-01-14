#include "gamelogic.h"
#include "move.h"
#include "piece.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

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
    Move* m = move_create(6 * 8 + 4, 4 * 8 + 4); // e2-e4
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
    Move* m1 = move_create(6 * 8 + 4, 4 * 8 + 4);
    gamelogic_perform_move(logic, m1);
    move_free(m1);
    
    // a7-a6 (filler)
    Move* m2 = move_create(1 * 8 + 0, 2 * 8 + 0);
    gamelogic_perform_move(logic, m2);
    move_free(m2);
    
    // e5
    Move* m3 = move_create(4 * 8 + 4, 3 * 8 + 4);
    gamelogic_perform_move(logic, m3);
    move_free(m3);
    
    // d7-d5 (double move, sets en passant)
    Move* m4 = move_create(1 * 8 + 3, 3 * 8 + 3);
    gamelogic_perform_move(logic, m4);
    move_free(m4);
    
    // e5xd6 (En Passant)
    Move* m5 = move_create(3 * 8 + 4, 2 * 8 + 3);
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
    // Add Kings
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[0][4] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->turn = PLAYER_WHITE;
    
    Move* m = move_create(1 * 8 + 0, 0 * 8 + 0);
    m->promotionPiece = PIECE_QUEEN;
    gamelogic_perform_move(logic, m);
    move_free(m);
    
    assert_condition(logic->board[0][0] != NULL && logic->board[0][0]->type == PIECE_QUEEN, 
                    "Pawn should promote to Queen");
    
    gamelogic_undo_move(logic);
    assert_condition(logic->board[1][0] != NULL && logic->board[1][0]->type == PIECE_PAWN, 
                    "Should revert to pawn");
    assert_condition(logic->board[1][0]->hasMoved == false, 
                    "Pawn should have hasMoved=false after undoing promotion");
    
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
    // Add Black King to avoid segfault
    logic->board[0][4] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->turn = PLAYER_WHITE;
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    // Check if castling move exists (king from e1 to g1)
    // Check moves starting from the King's position (col 4) and ending at the castling square (col 6)
    bool can_castle = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if ((m->from_sq % 8) == 4 && (m->isCastling || (m->to_sq % 8) == 6)) {
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
    // Add Black King to avoid segfault (place it elsewhere)
    logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->turn = PLAYER_WHITE;
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    bool can_castle = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if ((m->to_sq % 8) == 6) { // g1
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
    // Add Black King to avoid segfault (place it elsewhere)
    logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->turn = PLAYER_WHITE;
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    // The Rook on e3 (5,4) is pinned. It should ONLY be allowed to move along the e-file (up/down).
    // It should NOT be allowed to move horizontally.
    bool pin_violated = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if (m->from_sq == (5 * 8 + 4)) {
            if ((m->to_sq % 8) != 4) {
                pin_violated = true;
                printf("ERROR: Pinned Rook moved horizontally from (%d,%d) to (%d,%d)!\n",
                       m->from_sq / 8, m->from_sq % 8, m->to_sq / 8, m->to_sq % 8);
                // print_board(logic);
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
    
    // Ensure Kings are present for game state update
    if (!logic->board[0][0]) logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK);
    if (!logic->board[2][1]) logic->board[2][1] = piece_create(PIECE_KING, PLAYER_WHITE);
    
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
    // Add Black King to avoid segfault
    logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->turn = PLAYER_WHITE;
    logic->enPassantCol = 3; // column d (for en passant)
    
    MoveList* moves = movelist_create();
    gamelogic_generate_legal_moves(logic, PLAYER_WHITE, moves);
    
    // If White takes d6 via En Passant, BOTH pawns leave the e-file.
    // This exposes the King to the Rook on e8. This move MUST be illegal.
    bool ep_allowed = false;
    for (int i = 0; i < moves->count; i++) {
        Move* m = moves->moves[i];
        if (m->from_sq == (3 * 8 + 4) && m->to_sq == (2 * 8 + 3)) {
            ep_allowed = true;
            break;
        }
    }
    
    assert_condition(!ep_allowed, "En Passant should be illegal if it exposes King via a file-opening pin");
    movelist_free(moves);
    gamelogic_free(logic);
    printf("✅ Test En Passant Pin: Passed\n");
}

// Test 10: Promotion Memory Safety
static void test_promotion_memory_safety(void) {
    GameLogic* logic = gamelogic_create();
    
    // Setup for promotion
    for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) {
        if (logic->board[i][j]) { piece_free(logic->board[i][j]); logic->board[i][j] = NULL; }
    }
    
    // Pawn at rank 7, about to promote
    logic->board[1][4] = piece_create(PIECE_PAWN, PLAYER_WHITE);
    logic->turn = PLAYER_WHITE;
    
    Move* m = move_create(1 * 8 + 4, 0 * 8 + 4);
    m->promotionPiece = PIECE_QUEEN;
    
    // Before fix, this might crash or use freed memory if running with sanitizers
    gamelogic_perform_move(logic, m);
    
    assert_condition(logic->board[0][4] != NULL, "Promoted piece should exist");
    assert_condition(logic->board[0][4]->type == PIECE_QUEEN, "Should be a Queen");
    assert_condition(logic->board[0][4]->hasMoved == true, "Promoted piece should have hasMoved=true");
    
    move_free(m);
    gamelogic_free(logic);
    printf("✅ Test Promotion Memory Safety: Passed\n");
}

// Test 11: En Passant Undo State
static void test_en_passant_undo_state(void) {
    GameLogic* logic = gamelogic_create();
    
    // 1. e4 (sets enPassantCol to 4)
    Move* m1 = move_create(6 * 8 + 4, 4 * 8 + 4);
    gamelogic_perform_move(logic, m1);
    assert_condition(logic->enPassantCol == 4, "EP col should be 4 after e4");
    
    // 2. d5
    Move* m2 = move_create(1 * 8 + 3, 3 * 8 + 3);
    gamelogic_perform_move(logic, m2);
    assert_condition(logic->enPassantCol == 3, "EP col should be 3 after d5");
    
    // 3. Undo d5
    gamelogic_undo_move(logic);
    assert_condition(logic->enPassantCol == 4, "EP col should be restored to 4 after undoing d5");
    
    // 4. Undo e4
    gamelogic_undo_move(logic);
    assert_condition(logic->enPassantCol == -1, "EP col should be restored to -1 after undoing e4");
    
    move_free(m1);
    move_free(m2);
    gamelogic_free(logic);
    printf("✅ Test En Passant Undo State: Passed\n");
}

// Test 12: Castling Rook State Undo
static void test_castling_rook_state_undo(void) {
    GameLogic* logic = gamelogic_create();
    
    // Clear board and setup castling
    for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) {
        if (logic->board[i][j]) { piece_free(logic->board[i][j]); logic->board[i][j] = NULL; }
    }
    
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[7][7] = piece_create(PIECE_ROOK, PLAYER_WHITE);
    // Add Black King to avoid segfault in checkmate logic
    logic->board[0][4] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->turn = PLAYER_WHITE;
    
    // 1. Move rook e.g. h1-h2 then back h2-h1 (it has moved now)
    Move* m1 = move_create(7 * 8 + 7, 6 * 8 + 7);
    gamelogic_perform_move(logic, m1);
    
    // Filler move for black
    Move* filler = move_create(0 * 8 + 4, 0 * 8 + 3); 
    gamelogic_perform_move(logic, filler);
    
    Move* m2 = move_create(6 * 8 + 7, 7 * 8 + 7);
    gamelogic_perform_move(logic, m2);
    
    // Filler move for black
    Move* filler2 = move_create(0 * 8 + 3, 0 * 8 + 4);
    gamelogic_perform_move(logic, filler2);
    
    assert_condition(logic->board[7][7]->hasMoved == true, "Rook should have hasMoved=true after moving");
    
    // 2. Try to castle
    Move* castle = move_create(7 * 8 + 4, 7 * 8 + 6);
    castle->isCastling = true;
    gamelogic_perform_move(logic, castle);
    
    Piece* rookAfterCastle = logic->board[7][5];
    assert_condition(rookAfterCastle != NULL && rookAfterCastle->type == PIECE_ROOK, "Rook should be at f1");
    assert_condition(rookAfterCastle->hasMoved == true, "Rook should have moved=true");
    
    // 3. Undo castle
    gamelogic_undo_move(logic);
    Piece* rookRestored = logic->board[7][7];
    assert_condition(rookRestored != NULL && rookRestored->hasMoved == true, "Rook should STILL have hasMoved=true after undoing castle");
    
    move_free(m1);
    move_free(m2);
    move_free(filler);
    move_free(filler2);
    move_free(castle);
    gamelogic_free(logic);
    printf("✅ Test Castling Rook State Undo: Passed\n");
}

// Test 13: FEN Loading Castling Rights
static void test_fen_loading_castling_rights(void) {
    GameLogic* logic = gamelogic_create();
    
    // FEN with white kingside castling ONLY
    const char* fen = "r3k2r/8/8/8/8/8/8/R3K2R w K - 0 1";
    gamelogic_load_fen(logic, fen);
    
    // White King e1
    Piece* wk = logic->board[7][4];
    assert_condition(wk != NULL && wk->hasMoved == false, "White King should not have moved (K in FEN)");
    
    // White Rook h1
    Piece* wrh = logic->board[7][7];
    assert_condition(wrh != NULL && wrh->hasMoved == false, "White Rook h1 should not have moved (K in FEN)");
    
    // White Rook a1
    Piece* wra = logic->board[7][0];
    assert_condition(wra != NULL && wra->hasMoved == true, "White Rook a1 SHOULD have moved (no Q in FEN)");
    
    // Black King e8
    Piece* bk = logic->board[0][4];
    assert_condition(bk != NULL && bk->hasMoved == true, "Black King SHOULD have moved (no kq in FEN)");
    
    gamelogic_free(logic);
    printf("✅ Test FEN Loading Castling Rights: Passed\n");
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
    
    // New tests
    test_promotion_memory_safety();
    test_en_passant_undo_state();
    test_castling_rook_state_undo();
    test_fen_loading_castling_rights();
    
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


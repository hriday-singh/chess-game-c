#include "gamelogic.h"
#include "move.h"
#include "piece.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// Helper to access internal move stack (hacky but needed for black-box testing if we don't expose it)
// We know from gamelogic.c:
// typedef struct StackNode { void* data; struct StackNode* next; } StackNode;
// typedef struct { StackNode* top; int size; } Stack;
// Logic->moveHistory is a Stack*.
// We will replicate these structs here to inspect them.
typedef struct StackNode {
    void* data;
    struct StackNode* next;
} StackNode;

typedef struct {
    StackNode* top;
    int size;
} Stack;

static int tests_passed = 0;
static int tests_failed = 0;

static void assert_condition(bool condition, const char* message) {
    if (condition) {
        tests_passed++;
        printf("✅ PASS: %s\n", message);
    } else {
        tests_failed++;
        printf("❌ FAIL: %s\n", message);
    }
}

// Test 1: Verify UCI generation does NOT destroy Move history pointers
static void test_uci_pointer_stability(void) {
    printf("\n[Test] UCI Pointer Stability\n");
    GameLogic* logic = gamelogic_create();
    
    // e2-e4
    Move* m = move_create(6*8+4, 4*8+4);
    gamelogic_perform_move(logic, m);
    move_free(m); // logic makes a copy
    
    Stack* s = (Stack*)logic->moveHistory;
    Move* moveInHistoryBefore = (Move*)s->top->data;
    printf("  Move ptr before SAN: %p\n", moveInHistoryBefore);
    
    char uci[16];
    gamelogic_get_move_uci(logic, moveInHistoryBefore, uci, sizeof(uci));
    printf("  UCI Generated: %s\n", uci);
    
    Move* moveInHistoryAfter = (Move*)s->top->data;
    printf("  Move ptr after UCI:  %p\n", moveInHistoryAfter);
    
    assert_condition(moveInHistoryBefore == moveInHistoryAfter, "Move pointer should remain identical (no free/malloc)");
    assert_condition(strcmp(uci, "e2e4") == 0, "Output should be UCI 'e2e4'");
    
    gamelogic_free(logic);
}

// Test 2: Verify Capture Integrity through Undo/San
static void test_capture_integrity(void) {
    printf("\n[Test] Capture Integrity\n");
    GameLogic* logic = gamelogic_create();
    
    // Setup: White Rook on a1, Black Pawn on a2
    // Clear board
    for(int i=0; i<8; i++) for(int j=0; j<8; j++) {
        if(logic->board[i][j]) { piece_free(logic->board[i][j]); logic->board[i][j] = NULL; }
    }
    
    logic->board[7][0] = piece_create(PIECE_ROOK, PLAYER_WHITE);
    logic->board[6][0] = piece_create(PIECE_PAWN, PLAYER_BLACK); // Victim
    logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE);
    logic->board[0][4] = piece_create(PIECE_KING, PLAYER_BLACK);
    logic->turn = PLAYER_WHITE;
    
    // Move: Rxa2
    Move* m = move_create(7*8+0, 6*8+0);
    gamelogic_perform_move(logic, m);
    move_free(m);
    
    Stack* s = (Stack*)logic->moveHistory;
    Move* captureMove = (Move*)s->top->data;
    
    assert_condition(captureMove->capturedPieceType == PIECE_PAWN, "Move should record capture of PAWN");
    
    // Generate UCI (this triggers the Peek/Restore logic if any exists, now safe)
    char uci[16];
    gamelogic_get_move_uci(logic, captureMove, uci, sizeof(uci));
    printf("  UCI: %s\n", uci);
    
    Move* captureMoveAfter = (Move*)s->top->data;
    assert_condition(captureMove == captureMoveAfter, "Pointer should be stable");
    
    // KEY CHECK: Did the capture type get preserved?
    // If make_move_internal cleared it and didn't see the piece (because of bad restore), it would be NO_PIECE
    assert_condition(captureMoveAfter->capturedPieceType == PIECE_PAWN, 
                     "Captured Piece Type should still be PAWN after SAN generation");
                     
    // Undo
    gamelogic_undo_move(logic);
    
    // Check Board
    assert_condition(logic->board[6][0] != NULL, "Pawn should be restored");
    if(logic->board[6][0]) {
        assert_condition(logic->board[6][0]->type == PIECE_PAWN, "Restored piece should be PAWN");
        assert_condition(logic->board[6][0]->owner == PLAYER_BLACK, "Restored piece should be BLACK");
    }
    
    assert_condition(logic->board[7][0] != NULL, "Rook should be back");
    if (logic->board[7][0]) {
        assert_condition(logic->board[7][0]->type == PIECE_ROOK, "Rook should be ROOK");
    }
    
    gamelogic_free(logic);
}

// Test 3: Graveyard Accuracy (Move Sequence)
static void test_graveyard_sequence(void) {
    printf("\n[Test] Graveyard Sequence\n");
    GameLogic* logic = gamelogic_create();
    
    // Sequence:
    // 1. e4 d5
    // 2. exd5 (White Pawn captures Black Pawn)
    // 3. ... Qxd5 (Black Queen captures White Pawn)
    
    Move* m1 = move_create(6*8+4, 4*8+4); // e4
    gamelogic_perform_move(logic, m1); move_free(m1);
    
    Move* m2 = move_create(1*8+3, 3*8+3); // d5
    gamelogic_perform_move(logic, m2); move_free(m2);
    
    Move* m3 = move_create(4*8+4, 3*8+3); // exd5
    gamelogic_perform_move(logic, m3); move_free(m3);
    
    // Check M3 capture
    Stack* s = (Stack*)logic->moveHistory;
    Move* capture1 = (Move*)s->top->data;
    assert_condition(capture1->capturedPieceType == PIECE_PAWN, "First capture is PAWN");
    
    Move* m4 = move_create(0*8+3, 3*8+3); // Qxd5
    gamelogic_perform_move(logic, m4); move_free(m4);
    
    // Check M4 capture
    Move* capture2 = (Move*)s->top->data;
    assert_condition(capture2->capturedPieceType == PIECE_PAWN, "Second capture is PAWN");
    
    // Verify Graveyard Content via history iteration
    // The GUI iterates history to find captured pieces.
    // It calls `gamelogic_get_captured_pieces`.
    
    // Helper to count pieces in list manually
    typedef struct PieceTypeNode { PieceType type; struct PieceTypeNode* next; } PieceTypeNode;
    typedef struct { PieceTypeNode* head; int size; } PieceTypeList;
    
    PieceTypeList white_graveyard = {0,0}; // Pieces captured BY White (Black pieces)
    PieceTypeList black_graveyard = {0,0}; // Pieces captured BY Black (White pieces)
    
    gamelogic_get_captured_pieces(logic, PLAYER_WHITE, &white_graveyard);
    gamelogic_get_captured_pieces(logic, PLAYER_BLACK, &black_graveyard);
    
    assert_condition(white_graveyard.size == 1, "White graveyard size should be 1 (Black Pawn)");
    if(white_graveyard.head) {
        assert_condition(white_graveyard.head->type == PIECE_PAWN, "White graveyard item should be PAWN");
    }
    
    assert_condition(black_graveyard.size == 1, "Black graveyard size should be 1 (White Pawn)");
    if(black_graveyard.head) {
        assert_condition(black_graveyard.head->type == PIECE_PAWN, "Black graveyard item should be PAWN");
    }
    
    // Cleanup list manually (since we don't have access to free function here easily without replicating)
    // Actually gamelogic_get_captured_pieces allocates nodes. We should rely on system or just leak in test for now (it's small)
    
    gamelogic_free(logic);
}


int main(void) {
    printf("--- RUNNING EXTENDED REGRESSION TESTS ---\n");
    test_uci_pointer_stability();
    test_capture_integrity();
    test_graveyard_sequence();
    
    printf("\n--- SUMMARY ---\n");
    if (tests_failed == 0) {
        printf("✅ ALL TESTSD PASSED\n");
        return 0;
    } else {
        printf("❌ FAILED: %d tests\n", tests_failed);
        return 1;
    }
}

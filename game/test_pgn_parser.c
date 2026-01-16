#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "game_import.h"
#include "gamelogic.h"
#include "move.h"

// Simple test runner
void run_test(const char* name, const char* input, int expected_moves, const char* expected_uci_substr) {
    printf("Running Test: %s...", name);
    
    GameLogic* logic = gamelogic_create();
    GameImportResult res = game_import_from_string(logic, input);
    
    bool passed = true;
    
    if (!res.success) {
        printf(" FAIL (Import Error: %s)\n", res.error_message);
        passed = false;
    } else if (res.moves_count != expected_moves) {
        printf(" FAIL (Expected %d moves, got %d)\n", expected_moves, res.moves_count);
        passed = false;
    } else {
        if (expected_uci_substr && strstr(res.loaded_uci, expected_uci_substr) == NULL) {
            printf(" FAIL (UCI mismatch. Got: '%s', Expected substr: '%s')\n", res.loaded_uci, expected_uci_substr);
            passed = false;
        }
    }
    
    if (passed) {
        printf(" PASS\n");
    }
    
    gamelogic_free(logic);
}

void test_1_e4_merged() {
    // The specific bug reported: "1.e4" with no space
    run_test("Merged Token e4", "1.e4", 1, "e2e4");
}

void test_1_e4_uci_merged() {
    // Merged UCI style if user provides that?
    run_test("Merged Token UCI", "1.e2e4", 1, "e2e4");
}

void test_1_e4_spaced() {
    run_test("Spaced Token", "1. e4", 1, "e2e4");
}

void test_full_game_fragment() {
    run_test("Game Fragment", "1.e4 e5 2.Nf3 Nc6", 4, "e2e4 e7e5 g1f3 b8c6");
}

void test_complex_pgn() {
    const char* pgn = 
        "[Event \"Test\"]\n"
        "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7\n";
    run_test("Complex PGN", pgn, 10, "e1g1"); // Castling check
}

void test_merged_black_move() {
    // "1.e4 e5 2.Nf3"
    run_test("Merged Black Move Sequence", "1.e4 e5 2.Nf3", 3, "g1f3");
}


void test_san_simple() {
    run_test("SAN Simple (Nf3)", "1. Nf3", 1, "g1f3");
}

void test_san_castling() {
    // Requires setting up a board where castling is legal? 
    // Default board allows it after clearing path.
    // "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. O-O" is valid.
    const char* input = "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. O-O";
    run_test("SAN Castling (O-O)", input, 7, "e1g1");
}

void test_san_promotion() {
    // Needs a board state where promotion is possible.
    // Hard to reach from start POS in few moves for a unit test without fen setup.
    // But game_import supports PGN.
    // PGN can have FEN tag? My parser supports tags [StartFen] or similar?
    // In game_import.c: parse_pgn_tag supports [White], [Black]... does it support FEN?
    // It does NOT support [FEN] tag in the truncated view I saw.
    // It checks White, Black, Event, Date, Result.
    // So I can't easily test promotion from start pos quickly.
    // I'll skip complex setup tests or stick to parser tokenization checks if possible.
    // Actually, I can test parser resilience. If I give "e8=Q", it should try to parse "e8=Q".
    // If board is standard, "e8=Q" is illegal.
    // The test runner checks for move count.
    // So I can't strictly test success without setup.
    // I will skip promotion logic test for now unless I add FEN tag support to importer.
}

void test_san_check_notation() {
    // "1. e4 f5 2. Qh5+ g6"
    run_test("SAN Check (+)", "1. e4 f5 2. Qh5+ g6", 4, "d1h5");
}

void test_san_ambiguity() {
    // "1. Nc3 d5 2. Nd5" (One knight moves) 
    // vs two knights.
    // Setup: 1. Nc3 ... 2. Nd5 ... 3. Nf3 ...
    // Now we have N on d5 and f3.
    // If I move 4. Nb4 and 5. c3??
    // Let's rely on standard opening lines that disambiguate.
    // 1. e4 c5 2. Nf3 Nc6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 e5 6. Ndb5
    // Here Knights are on c3 and b5.
    // If input is "N5c3" it's valid.
    // Let's try simple case: 
    // 1. Nf3 d5
    // 2. Nbd2 (Knights on f3, d2). but wait, d2 pawn is there unless we moved d-pawn.
    // Correct sequence: 1. d4 d5 2. Nf3 Nc6 3. Nbd2
    // d4 moves pawn from d2. d2 is empty.
    // Nf3 (from g1). Nb1 (at b1).
    // Both can move to d2.
    const char* input = "1. d4 d5 2. Nf3 Nc6 3. Nbd2";
    run_test("SAN Ambiguity (Nbd2)", input, 5, "b1d2");
}

void test_uci_input() {
    run_test("UCI Input (e2e4)", "1. e2e4", 1, "e2e4");
}

void test_uci_promotion() {
    // UCI promotion is e7e8q.
    // Again, legality check prevents testing invalid moves.
}

int main() {
    printf("=== PGN/SAN/UCI Parser Test Suite ===\n");
    
    test_1_e4_merged();
    test_1_e4_uci_merged();
    test_1_e4_spaced();
    test_full_game_fragment();
    test_complex_pgn();
    test_merged_black_move();
    
    printf("\n--- New Extended Tests ---\n");
    test_san_simple();
    test_san_castling();
    test_san_check_notation();
    test_san_ambiguity();
    test_uci_input();
    
    printf("\n=== Tests Complete ===\n");
    return 0;
}

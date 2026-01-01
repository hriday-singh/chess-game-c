# Testing Guide

## Running Tests

### Basic Test

```bash
make test
```

Runs a simple test that checks:

- GameLogic creation
- Move generation
- FEN generation

### Full Test Suite

```bash
make test-suite
```

Runs comprehensive tests matching the Java ChessTest.java:

1. ✅ **Initial Setup** - Verifies starting position
2. ✅ **Movement and Undo** - Tests move execution and undo
3. ✅ **En Passant Capture** - Tests en passant mechanics
4. ✅ **Promotion** - Tests pawn promotion
5. ✅ **Castling Through Check** - Ensures castling is blocked when king passes through check
6. ✅ **Castling While In Check** - Ensures castling is blocked when in check
7. ✅ **Pin Logic** - Tests that pinned pieces can't move to expose king
8. ✅ **Stalemate** - Tests stalemate detection
9. ✅ **En Passant Pin** - Advanced test: en passant blocked if it exposes king

## Expected Output

```
--- STARTING EXTENSIVE ENGINE TESTS ---

✅ Test Initial Setup: Passed
✅ Test Movement and Undo: Passed
✅ Test En Passant: Passed
✅ Test Promotion: Passed
✅ Test Castling Through Check: Passed
✅ Test Castling While In Check: Passed
✅ Test Pin Logic: Passed
✅ Test Stalemate: Passed
✅ Test En Passant Pin: Passed

--- TEST SUMMARY ---
✅ Tests Passed: 9
❌ Tests Failed: 0

✅ ALL EXTENSIVE TESTS PASSED!
```

## If Tests Fail

If a test fails, it will print:

- ❌ FAILED: [error message]
- The board state (if applicable)
- Which test failed

Fix the issue in the GameLogic implementation and re-run tests.

## Test Files

- `game/main_test.c` - Basic functionality test
- `game/test_suite.c` - Comprehensive test suite (matches Java tests)

# Ready to Compile with Make! 🚀

## Quick Start (MSYS2 MinGW 64-bit Terminal)

1. **Open MSYS2 MinGW 64-bit terminal** (NOT regular MSYS2!)

2. **Navigate to project:**

```bash
cd /c/Users/clash/OneDrive/Desktop/Codes/Java/Hriday\ Chess/ChessGameC
```

3. **Build:**

```bash
make
```

4. **Run tests:**

```bash
# Basic test
make test

# Full comprehensive test suite
make test-suite
```

## What This Tests

### Basic Test (`make test`)

- ✅ GameLogic creation and initialization
- ✅ Move generation (should generate ~20 moves for starting position)
- ✅ FEN string generation
- ✅ Memory management

### Full Test Suite (`make test-suite`)

Matches all tests from Java ChessTest.java:

1. ✅ Initial Setup
2. ✅ Movement and Undo
3. ✅ En Passant Capture
4. ✅ Promotion
5. ✅ Castling Through Check
6. ✅ Castling While In Check
7. ✅ Pin Logic
8. ✅ Stalemate
9. ✅ En Passant Pin (Advanced)

## Expected Output

### Build Output:

```
Compiling game/gamelogic.c...
Compiling game/gamelogic_movegen.c...
Compiling game/gamelogic_safety.c...
Compiling game/main_test.c...
Compiling game/move.c...
Compiling game/piece.c...
Compiling game/test_suite.c...
Linking build/chessgamec_test.exe...
Linking build/test_suite.exe...
Build complete! Run: build/chessgamec_test.exe or build/test_suite.exe
```

### Test Suite Output:

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

## Makefile Commands

- `make` or `make all` - Build all test executables
- `make clean` - Remove all build files
- `make test` - Build and run basic test
- `make test-suite` - Build and run comprehensive test suite

## If You Get Errors

### Missing dependencies

```bash
pacman -S make
pacman -S mingw-w64-x86_64-gcc
```

### "make: command not found"

- Make sure you're in **MinGW 64-bit** terminal
- Install: `pacman -S make`

### "gcc: command not found"

- Install: `pacman -S mingw-w64-x86_64-gcc`

### Compilation errors

- Check that all `game/*.c` files are present
- Run `make clean` and try again

### Link errors

- Check that all object files were created in `build/obj/`
- Verify all source files compile individually

### Runtime errors (Error 127)

- Make sure you're running from the correct directory
- Try: `./build/test_suite.exe` (with full path)
- Check file permissions

## Next Steps After Successful Compilation

1. ✅ Game logic works - **DONE**
2. ⏳ Add GUI (GTK4)
3. ⏳ Integrate Stockfish
4. ⏳ Add training system

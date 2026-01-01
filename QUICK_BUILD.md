# Quick Build Guide (MSYS2 with Make)

## Step 1: Open MSYS2 MinGW 64-bit Terminal

**Important**: Use **MSYS2 MinGW 64-bit** terminal, NOT regular MSYS2!

## Step 2: Navigate to Project

```bash
cd /c/Users/clash/OneDrive/Desktop/Codes/Java/Hriday\ Chess/ChessGameC
```

## Step 3: Build

```bash
make
```

That's it! The Makefile will:

- Create `build/obj/` directory for object files
- Compile all game logic files
- Link them into `build/chessgamec_test.exe`

## Step 4: Run Tests

```bash
# Basic test
make test

# Full comprehensive test suite (matches Java ChessTest.java)
make test-suite
```

Or directly:

```bash
./build/chessgamec_test.exe
./build/test_suite.exe
```

## Clean Build

To remove all build files:

```bash
make clean
```

## Expected Output

```
Compiling game/gamelogic.c...
Compiling game/gamelogic_movegen.c...
Compiling game/gamelogic_safety.c...
Compiling game/main_test.c...
Compiling game/move.c...
Compiling game/piece.c...
Linking build/chessgamec_test.exe...
Build complete! Run: build/chessgamec_test.exe
```

Then when you run it:

```
Testing GameLogic...
GameLogic created successfully!
Status: White's Turn
Turn: White
Generated XX legal moves for White
FEN: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
Test passed!
```

## If You Get Errors

### "make: command not found"

- Make sure you're in **MinGW 64-bit** terminal
- Install: `pacman -S make`

### "gcc: command not found"

- Install: `pacman -S mingw-w64-x86_64-gcc`

### Compilation errors

- Check that all `game/*.c` files are present
- Make sure you have gcc installed: `pacman -S mingw-w64-x86_64-gcc`

### Link errors

- Check that all object files were created in `build/obj/`
- Run `make clean` and try again

## Makefile Targets

- `make` or `make all` - Build all test executables
- `make clean` - Remove all build files
- `make test` - Build and run basic test
- `make test-suite` - Build and run comprehensive test suite (9 tests)

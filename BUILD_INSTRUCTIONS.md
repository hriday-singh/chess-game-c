# Build Instructions for MSYS2

## Prerequisites

Make sure you have installed in MSYS2:

```bash
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-gtk4  # Optional for now (GUI not implemented yet)
pacman -S mingw-w64-x86_64-pkg-config  # Optional (for GTK4)
pacman -S make
```

## Building

### Using MSYS2 MinGW 64-bit Terminal

1. Open **MSYS2 MinGW 64-bit** terminal (not regular MSYS2)

2. Navigate to the project:

```bash
cd /c/Users/clash/OneDrive/Desktop/Codes/Java/Hriday\ Chess/ChessGameC
```

3. Build:

```bash
make
```

That's it! The Makefile will:

- Create `build/obj/` directory for object files
- Compile all game logic files
- Link them into test executables

## Running Tests

### Basic Test

```bash
make test
```

### Comprehensive Test Suite

```bash
make test-suite
```

This runs all tests matching the Java ChessTest.java:

- Initial setup
- Movement and undo
- En passant capture
- Promotion
- Castling through check
- Castling while in check
- Pin logic
- Stalemate
- En passant pin (advanced)

## Clean Build

To remove all build files:

```bash
make clean
```

## Troubleshooting

### "make: command not found"

- Make sure you're in **MinGW 64-bit** terminal
- Install: `pacman -S make`

### "gcc: command not found"

- Install: `pacman -S mingw-w64-x86_64-gcc`

### Compilation errors

- Check that all `game/*.c` files are present
- Run `make clean` and try again
- Verify gcc is installed: `gcc --version`

### Link errors

- Check that all object files were created in `build/obj/`
- Verify all source files compile individually

## Current Status

The project currently has:

- ✅ Game logic (C files) - **FULLY IMPLEMENTED**
- ✅ Test suite - **ALL TESTS PASSING**
- ✅ Stockfish source (C++ files) - ready for integration
- ⏳ GUI (not yet implemented)
- ⏳ Training system (not yet implemented)

The game logic is complete and tested. You can build and run tests without GTK4.

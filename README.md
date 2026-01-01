# ChessGameC - Chess Engine with GUI and Training

This is a complete conversion of the Java chess game to C, integrated with Stockfish engine.

## Structure

- `src/` - Stockfish engine source code
- `game/` - Chess game logic (converted from Java)
- `gui/` - GUI implementation (GTK4)
- `training/` - AI training system

## Dependencies

### Required Libraries:

1. **GTK4** (for GUI) - Download from: https://www.gtk.org/docs/installations/windows/

   - Or use MSYS2: `pacman -S mingw-w64-x86_64-gtk4`
   - Or use vcpkg: `vcpkg install gtk`

2. **Make** (build system)

   - Windows (MSYS2): `pacman -S make`
   - Linux: Usually pre-installed
   - macOS: Usually pre-installed

3. **C/C++ Compiler**
   - Windows (MSYS2): `pacman -S mingw-w64-x86_64-gcc`
   - Linux: `sudo apt-get install build-essential gcc g++`
   - macOS: `xcode-select --install`

## Building

### Quick Build (MSYS2 MinGW 64-bit Terminal)

```bash
cd /c/Users/clash/OneDrive/Desktop/Codes/Java/Hriday\ Chess/ChessGameC
make
```

### Available Make Targets

- `make` or `make all` - Build all test executables
- `make clean` - Remove all build files
- `make test` - Build and run basic test
- `make test-suite` - Build and run comprehensive test suite

## Testing

```bash
# Run basic test
make test

# Run full test suite (matches Java ChessTest.java)
make test-suite
```

## Features

- ✅ Complete chess game logic (fully tested)
- ✅ Full Stockfish engine integration (not external process)
- ⏳ Chess GUI with theming (GTK4)
- ⏳ AI training system
- ⏳ UCI protocol support
- ⏳ Custom AI profiles

## Current Status

- ✅ Game logic fully implemented and tested
- ✅ Build system (Makefile)
- ✅ Comprehensive test suite
- ⏳ GUI implementation (pending)
- ⏳ Stockfish integration (pending)
- ⏳ Training system (pending)

# Installation Guide for ChessGameC

## Required Libraries and Tools

### 1. GTK4 (GUI Framework) - Optional for now

**Windows:**

- Download GTK4 from: https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer
- Or use MSYS2: `pacman -S mingw-w64-x86_64-gtk4`
- Or use vcpkg: `vcpkg install gtk`

**Linux:**

```bash
sudo apt-get install libgtk-4-dev  # Ubuntu/Debian
sudo dnf install gtk4-devel        # Fedora
```

**macOS:**

```bash
brew install gtk4
```

### 2. Make (Build System)

**Windows (MSYS2):**

```bash
pacman -S make
```

**Linux:**

- Usually pre-installed

**macOS:**

- Usually pre-installed

### 3. C/C++ Compiler

**Windows (MSYS2):**

```bash
pacman -S mingw-w64-x86_64-gcc
```

**Linux:**

```bash
sudo apt-get install build-essential gcc g++
```

**macOS:**

```bash
xcode-select --install
```

### 4. pkg-config (for finding GTK4 when needed)

**Windows:**

- Usually comes with GTK4 installation
- Or install via MSYS2: `pacman -S mingw-w64-x86_64-pkg-config`

**Linux/macOS:**

- Usually pre-installed

## Building

### MSYS2 MinGW 64-bit Terminal

```bash
cd /c/Users/clash/OneDrive/Desktop/Codes/Java/Hriday\ Chess/ChessGameC
make
```

### Build Targets

- `make` - Build all executables
- `make clean` - Clean build files
- `make test` - Run basic test
- `make test-suite` - Run comprehensive test suite

## Project Structure

```
ChessGameC/
├── src/              # Stockfish engine source (already copied)
├── game/             # Chess game logic (converted from Java)
│   ├── types.h       # Enums and basic types
│   ├── piece.h/c     # Piece structure
│   ├── move.h/c      # Move structure
│   ├── gamelogic.h/c # Main game engine
│   ├── gamelogic_movegen.c  # Move generation
│   ├── gamelogic_safety.c   # Safety checks
│   ├── main_test.c   # Basic test
│   └── test_suite.c  # Comprehensive tests
├── gui/              # GUI implementation (GTK4) - pending
├── training/         # AI training system - pending
└── Makefile          # Build configuration
```

## Current Status

✅ Project structure created
✅ Stockfish source copied
✅ Basic types and structures converted
✅ GameLogic fully implemented
✅ Comprehensive test suite
⏳ GUI conversion pending
⏳ Stockfish integration pending
⏳ Training system conversion pending

## Next Steps

1. ✅ GameLogic implementation (DONE)
2. ⏳ Create GUI using GTK4
3. ⏳ Integrate Stockfish engine directly
4. ⏳ Convert training system
5. ⏳ Test and debug

# ChessGameC

A modern, high-performance Chess Engine and GUI written in C11, featuring a custom engine integrated with Stockfish, GTK4-based UI, and an immutable state architecture.

## Prerequisites & Installation Guide

This project is built for **Windows** using the **MSYS2 MinGW-w64** environment. Follow these steps strictly if you are setting this up from scratch.

### 1. Install MSYS2

1.  Download the **installer** from [msys2.org](https://www.msys2.org/).
2.  Run the installer and follow the default installation steps (usually installs to `C:\msys64`).
3.  When finished, check "Run MSYS2 now" or open **"MSYS2 MSYS"** from your start menu.
4.  Update the package database by running:
    ```bash
    pacman -Syu
    ```
5.  If it asks to close the terminal, close it and open **"MSYS2 MinGW 64-bit"** (IMPORTANT: Always use the _MinGW 64-bit_ terminal for development, not UCRT64 or Clang64 unless you know what you are doing).

### 2. Install Dependencies

Open your **MSYS2 MinGW 64-bit** terminal and run the following command to install all required libraries (GTK4, GCC, Make, etc.):

```bash
pacman -S mingw-w64-x86_64-gtk4 mingw-w64-x86_64-gcc make mingw-w64-x86_64-gdb mingw-w64-x86_64-pkg-config mingw-w64-x86_64-librsvg
```

- `gtk4`: The GUI toolkit.
- `gcc`: The C compiler.
- `make`: The build tool.
- `gdb`: The debugger.
- `pkg-config`: Helper to find library paths.
- `librsvg`: Required for loading SVG chess pieces.

### 3. Setup VS Code (Optional but Recommended)

1.  Install [Visual Studio Code](https://code.visualstudio.com/).
2.  Install the **C/C++ Extension** by Microsoft.
3.  Add the MinGW64 `bin` folder to your Windows PATH:
    - Open Windows Search -> "Edit the system environment variables".
    - Click "Environment Variables".
    - Under "System variables", select "Path" and click "Edit".
    - Click "New" and add: `C:\msys64\mingw64\bin`
    - This allows VS Code to automatically find `gcc.exe` and `gdb.exe`.

## Building the Project

1.  Open **MSYS2 MinGW 64-bit**.
2.  Navigate to the project folder:

    ```bash
    # Adjust to your actual path
    cd /path/to/ChessGameC
    ```

    _Note: In MSYS2, Windows drives are accessed like `/c/`, `/d/`, etc._

3.  Compile the game:
    ```bash
    make
    ```

    - This creates `build/HalChess.exe`.

### Using a Custom NNUE Network

If you wish to use a different neural network for the engine:

1.  Download or train your `.nnue` file.
2.  Place the file inside the `src/` directory.
3.  Open `src/evaluate.h` and change `EvalFileDefaultName` to match your new filename.
4.  Run `make clean` and then `make` to recompile with the new network.

### Build Commands

| Command           | Description                                                                |
| :---------------- | :------------------------------------------------------------------------- |
| `make`            | Compiles the main game executable.                                         |
| `make clean`      | Removes all compiled object files and executables (use if build is stuck). |
| `make test`       | Compiles and runs the basic unit tests.                                    |
| `make test-suite` | Runs the comprehensive test suite.                                         |

## Creating a Redistributable Installer

To create a standalone installer that you can share with others (who don't have MSYS2 installed):

1.  Run the distribution command:
    ```bash
    make dist
    ```
2.  What this does:
    - Compiles a fresh Release build.
    - Collects all necessary DLLs (GTK4, libc, etc.) automatically using `ldd`.
    - Bundles assets (images, sounds, fonts).
    - Compiles the custom installer (`HalChessSetup.exe`).

3.  **Output**: You will find the final installer at `dist/HalChessSetup.exe`.
    - You can send this single `.exe` file to anyone, and they can install and play the game without needing MSYS2.

## Project Structure

- `game/` - **Core Logic**: Pure C code for chess rules, move generation, and board state. Zero GUI dependencies.
- `gui/` - **User Interface**: GTK4 code that handles windows, widgets, and input.
- `src/` - **Stockfish**: The integrated chess engine source (C++).
- `installer/` - Source code for the custom installer.
- `assets/` - Images, sounds, and CSS themes.

## Troubleshooting

- **"d3d11.dll not found" or graphics issues**:
  - Ensure your GPU drivers are up to date. GTK4 uses OpenGL/Vulkan.
- **Images/Icons missing**:
  - Make sure `librsvg` is installed (`pacman -S mingw-w64-x86_64-librsvg`).
- **Build fails after git pull**:
  - Always run `make clean` before `make` after fetching new changes to ensure a clean build.

## Contributing

We welcome contributions from the community! This project is open source, and we value your help in improving it.

1.  **Fork** the repository.
2.  **Clone** your fork to your local machine.
3.  **Create a branch** for your feature or bug fix.
4.  **Make changes** and ensure the code compiles and tests pass.
5.  **Submit a Pull Request** describing your changes in detail.

Please ensure appropriate credit is given when using or modifying this codebase. If you use major portions of this logic, a link back to this repository is appreciated.

## License

This project is licensed under the **MIT License**.

See the [LICENSE](LICENSE) file for details. You are free to use, modify, and distribute this software, provided the original copyright notice and permission notice are included in all copies or substantial portions of the software.

## Download

https://drive.google.com/drive/folders/1j_E8OpiE6kUWG57YqLceTH_ic4t6yBn9?usp=sharing

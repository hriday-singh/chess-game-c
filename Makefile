# Makefile for ChessGameC
# For MSYS2 MinGW 64-bit

CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g

# GTK4 (for GUI)
GTK_CFLAGS := $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk4 2>/dev/null)

# Directories
SRCDIR = game
GUIDIR = gui
SFDIR = src
BUILDDIR = build
OBJDIR = $(BUILDDIR)/obj

# Source files
GAME_SOURCES = $(wildcard $(SRCDIR)/*.c)
GAME_OBJECTS = $(GAME_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

GUI_SOURCES = $(wildcard $(GUIDIR)/*.c)
# Exclude icon_test.c, test_svg_loader.c, and standalone test programs from main GUI build
GUI_SOURCES := $(filter-out $(GUIDIR)/icon_test.c $(GUIDIR)/test_svg_loader.c $(GUIDIR)/test_focus_chain.c $(GUIDIR)/test_right_panel.c, $(GUI_SOURCES))
GUI_OBJECTS = $(GUI_SOURCES:$(GUIDIR)/%.c=$(OBJDIR)/gui_%.o)

# Stockfish sources (exclude main.cpp)
SF_SOURCES = $(SFDIR)/benchmark.cpp $(SFDIR)/bitboard.cpp $(SFDIR)/evaluate.cpp \
             $(SFDIR)/misc.cpp $(SFDIR)/movegen.cpp $(SFDIR)/movepick.cpp \
             $(SFDIR)/position.cpp $(SFDIR)/search.cpp $(SFDIR)/thread.cpp \
             $(SFDIR)/timeman.cpp $(SFDIR)/tt.cpp $(SFDIR)/uci.cpp \
             $(SFDIR)/ucioption.cpp $(SFDIR)/tune.cpp $(SFDIR)/syzygy/tbprobe.cpp \
             $(SFDIR)/nnue/nnue_accumulator.cpp $(SFDIR)/nnue/nnue_misc.cpp \
             $(SFDIR)/nnue/features/half_ka_v2_hm.cpp $(SFDIR)/nnue/network.cpp \
             $(SFDIR)/engine.cpp $(SFDIR)/score.cpp $(SFDIR)/memory.cpp \
             $(SRCDIR)/ai_engine.cpp

# Stockfish objects - handle subdirectories by flattening for simplicity or creating dirs
SF_OBJECTS = $(OBJDIR)/sf_benchmark.o $(OBJDIR)/sf_bitboard.o $(OBJDIR)/sf_evaluate.o \
             $(OBJDIR)/sf_misc.o $(OBJDIR)/sf_movegen.o $(OBJDIR)/sf_movepick.o \
             $(OBJDIR)/sf_position.o $(OBJDIR)/sf_search.o $(OBJDIR)/sf_thread.o \
             $(OBJDIR)/sf_timeman.o $(OBJDIR)/sf_tt.o $(OBJDIR)/sf_uci.o \
             $(OBJDIR)/sf_ucioption.o $(OBJDIR)/sf_tune.o $(OBJDIR)/sf_tbprobe.o \
             $(OBJDIR)/sf_nnue_accumulator.o $(OBJDIR)/sf_nnue_misc.o \
             $(OBJDIR)/sf_half_ka_v2_hm.o $(OBJDIR)/sf_network.o \
             $(OBJDIR)/sf_engine_sf.o $(OBJDIR)/sf_score.o $(OBJDIR)/sf_memory.o \
             $(OBJDIR)/sf_ai_engine.o

# Test executables
TEST_TARGET = $(BUILDDIR)/chessgamec_test.exe
TEST_SUITE_TARGET = $(BUILDDIR)/test_suite.exe

# GUI executable
GUI_TARGET = $(BUILDDIR)/HalChess.exe

# SVG loader test
SVG_TEST_TARGET = $(BUILDDIR)/test_svg_loader.exe

# Default target - build GUI executable (MUST be first target in Makefile)
all: $(GUI_TARGET)

# Build everything including tests
all-tests: $(TEST_TARGET) $(TEST_SUITE_TARGET) $(GUI_TARGET)

# Create build directories
$(BUILDDIR):
	-@mkdir $(BUILDDIR)

$(OBJDIR): | $(BUILDDIR)
	-@mkdir $(subst /,\,$(OBJDIR))

# Stockfish specific flags
SF_FLAGS = -O3 -DNDEBUG -DIS_64BIT -DUSE_SSE2 -DUSE_POPCNT -DUSE_PTHREADS -msse2 -mpopcnt

# Compile game C files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

# Compile GUI C files (with GTK4 flags)
$(OBJDIR)/gui_%.o: $(GUIDIR)/%.c | $(OBJDIR)
	@echo "Compiling GUI $<..."
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -I$(SRCDIR) -I$(GUIDIR) -c $< -o $@

# Compile Stockfish C++ files
$(OBJDIR)/sf_%.o: $(SFDIR)/%.cpp | $(OBJDIR)
	@echo "Compiling Stockfish $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) -I$(SFDIR) -c $< -o $@

$(OBJDIR)/sf_tbprobe.o: $(SFDIR)/syzygy/tbprobe.cpp | $(OBJDIR)
	@echo "Compiling Stockfish $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) -I$(SFDIR) -c $< -o $@

$(OBJDIR)/sf_nnue_accumulator.o: $(SFDIR)/nnue/nnue_accumulator.cpp | $(OBJDIR)
	@echo "Compiling Stockfish $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) -I$(SFDIR) -c $< -o $@

$(OBJDIR)/sf_nnue_misc.o: $(SFDIR)/nnue/nnue_misc.cpp | $(OBJDIR)
	@echo "Compiling Stockfish $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) -I$(SFDIR) -c $< -o $@

$(OBJDIR)/sf_half_ka_v2_hm.o: $(SFDIR)/nnue/features/half_ka_v2_hm.cpp | $(OBJDIR)
	@echo "Compiling Stockfish $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) -I$(SFDIR) -c $< -o $@

$(OBJDIR)/sf_network.o: $(SFDIR)/nnue/network.cpp | $(OBJDIR)
	@echo "Compiling Stockfish $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) -I$(SFDIR) -c $< -o $@

$(OBJDIR)/sf_engine_sf.o: $(SFDIR)/engine.cpp | $(OBJDIR)
	@echo "Compiling Stockfish $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) -I$(SFDIR) -c $< -o $@

$(OBJDIR)/sf_ai_engine.o: $(SRCDIR)/ai_engine.cpp | $(OBJDIR)
	@echo "Compiling Stockfish bridge $<..."
	$(CXX) $(CXXFLAGS) $(SF_FLAGS) $(GTK_CFLAGS) -I$(SFDIR) -I$(SRCDIR) -c $< -o $@

# Basic test executable (exclude test_suite.c and move_validation_test.c)
TEST_OBJECTS = $(filter-out $(OBJDIR)/test_suite.o $(OBJDIR)/move_validation_test.o $(OBJDIR)/test_extended.o, $(GAME_OBJECTS))

$(TEST_TARGET): $(TEST_OBJECTS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $^ -o $@

# Test suite executable (exclude main_test.c and move_validation_test.c)
TEST_SUITE_OBJECTS = $(filter-out $(OBJDIR)/main_test.o $(OBJDIR)/move_validation_test.o $(OBJDIR)/test_extended.o, $(GAME_OBJECTS))

$(OBJDIR)/test_suite.o: $(SRCDIR)/test_suite.c | $(OBJDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(TEST_SUITE_TARGET): $(TEST_SUITE_OBJECTS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $^ -o $@
	@echo "Build complete! Run: $(TEST_TARGET) or $(TEST_SUITE_TARGET)"

# Extended test suite (Regression tests)
TEST_EXTENDED_TARGET = $(BUILDDIR)/test_extended.exe
TEST_EXTENDED_OBJ = $(OBJDIR)/test_extended.o

$(OBJDIR)/test_extended.o: $(SRCDIR)/test_extended.c | $(OBJDIR)
	@echo "Compiling extended tests $<..."
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(TEST_EXTENDED_TARGET): $(TEST_EXTENDED_OBJ) $(filter-out $(OBJDIR)/test_suite.o $(OBJDIR)/main_test.o $(OBJDIR)/move_validation_test.o, $(GAME_OBJECTS))
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $^ -o $@

test-extended: $(TEST_EXTENDED_TARGET)
	@echo "Running Extended Regression Tests..."
	./$(TEST_EXTENDED_TARGET)

# Resource file for Windows icon
RC_FILE = chess.rc
RES_OBJ = $(OBJDIR)/chess_res.o

# Build resource file
$(RES_OBJ): $(RC_FILE) assets/images/icon/icon.ico | $(OBJDIR)
	@echo "Compiling resources..."
	windres $(RC_FILE) -O coff -o $(RES_OBJ)

# GUI executable (links game + GUI + GTK4)
GAME_OBJS_FOR_GUI = $(filter-out $(OBJDIR)/main_test.o $(OBJDIR)/test_suite.o $(OBJDIR)/move_validation_test.o $(OBJDIR)/test_extended.o $(OBJDIR)/test_pgn_parser.o, $(GAME_OBJECTS))
GUI_OBJS_FOR_GUI = $(filter-out $(OBJDIR)/gui_icon_test.o, $(GUI_OBJECTS))

# Force all dependencies to be built before linking
$(GUI_TARGET): $(GAME_OBJS_FOR_GUI) $(GUI_OBJS_FOR_GUI) $(SF_OBJECTS) $(RES_OBJ) | $(OBJDIR)
	@echo "Linking $@ (GUI + Stockfish + Icon)..."
	@echo "  Game objects: $(words $(GAME_OBJS_FOR_GUI)) files"
	@echo "  GUI objects: $(words $(GUI_OBJS_FOR_GUI)) files"
	@echo "  Stockfish objects: $(words $(SF_OBJECTS)) files"
	@echo "  Total object files: $(words $^)"
	$(CXX) $(CXXFLAGS) $^ $(GTK_LIBS) -o $@
	@echo "Copying resources..."
	@if [ -f icon.png ]; then cp icon.png $(BUILDDIR)/icon.png; fi
	@if [ -d assets ]; then \
		mkdir -p $(BUILDDIR)/assets/audio; \
		cp -r assets/audio/* $(BUILDDIR)/assets/audio/ 2>/dev/null || true; \
	fi

# Clean build files (Windows-compatible)
clean:
	@echo "Cleaning..."
	@-rm -rf $(BUILDDIR) 2>/dev/null || true
	@echo "Clean complete!"

# Run basic test
test: $(TEST_TARGET)
	@echo "Running basic test..."
	./$(TEST_TARGET)

# Run full test suite
test-suite: $(TEST_SUITE_TARGET)
	@echo "Running full test suite..."
	./$(TEST_SUITE_TARGET)

# Build only GUI (skip tests)
gui: $(GUI_TARGET)

# SVG loader test executable (uses GdkPixbuf which is part of GTK4)
$(SVG_TEST_TARGET): $(GUIDIR)/test_svg_loader.c | $(BUILDDIR)
	@echo "Building SVG loader test..."
	@echo "Note: SVG support requires librsvg. Install with: pacman -S mingw-w64-x86_64-librsvg"
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -I$(SRCDIR) -I$(GUIDIR) $< -o $@ $(GTK_LIBS)

# Build SVG test target
test-svg: $(SVG_TEST_TARGET)
	@echo "SVG test built: $(SVG_TEST_TARGET)"
	@echo "Run with: ./$(SVG_TEST_TARGET) assets/images/piece/alpha/wN.svg"

# Focus Chain Test
FOCUS_TEST_TARGET = $(BUILDDIR)/test_focus_chain.exe

$(FOCUS_TEST_TARGET): $(GUIDIR)/test_focus_chain.c $(GUIDIR)/gui_utils.c | $(BUILDDIR)
	@echo "Building Focus Chain Test..."
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -I$(GUIDIR) $^ -o $@ $(GTK_LIBS)

test-focus: $(FOCUS_TEST_TARGET)
	@echo "Running Focus Chain Test..."
	./$(FOCUS_TEST_TARGET)


# Reproduction test target
REPRO_TARGET = $(BUILDDIR)/repro_perform_move.exe

test-repro: $(GAME_OBJECTS)
	@echo "Building reproduction test..."
	$(CC) $(CFLAGS) -I. -I$(SRCDIR) repro_perform_move.c $(filter-out $(OBJDIR)/test_suite.o $(OBJDIR)/main_test.o, $(GAME_OBJECTS)) -o $(REPRO_TARGET)
	@echo "Running reproduction test..."
	./$(REPRO_TARGET)

# AI Stress Test
AI_STRESS_TARGET = $(BUILDDIR)/ai_stress_test.exe

# Objects needed for AI stress test (game logic + AI components)
AI_STRESS_GAME_OBJS = $(filter-out $(OBJDIR)/main_test.o $(OBJDIR)/test_suite.o $(OBJDIR)/move_validation_test.o, $(GAME_OBJECTS))
AI_STRESS_GUI_OBJS = $(OBJDIR)/gui_ai_controller.o $(OBJDIR)/gui_ai_dialog.o $(OBJDIR)/gui_config_manager.o $(OBJDIR)/gui_theme_manager.o $(OBJDIR)/gui_gui_utils.o
AI_STRESS_TEST_OBJ = $(OBJDIR)/ai_stress_test.o

# Compile the stress test C file
$(AI_STRESS_TEST_OBJ): ai_stress_test.c | $(OBJDIR)
	@echo "Compiling AI stress test..."
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -I. -I$(SRCDIR) -I$(GUIDIR) -c ai_stress_test.c -o $(AI_STRESS_TEST_OBJ)

test-ai-stress: $(AI_STRESS_GAME_OBJS) $(AI_STRESS_GUI_OBJS) $(SF_OBJECTS) $(AI_STRESS_TEST_OBJ) | $(BUILDDIR)
	@echo "Linking AI stress test (with real AI controller)..."
	@echo "  Game objects: $(words $(AI_STRESS_GAME_OBJS)) files"
	@echo "  GUI objects: $(words $(AI_STRESS_GUI_OBJS)) files"
	@echo "  Stockfish objects: $(words $(SF_OBJECTS)) files"
	$(CXX) $(CXXFLAGS) $(AI_STRESS_TEST_OBJ) $(AI_STRESS_GAME_OBJS) $(AI_STRESS_GUI_OBJS) $(SF_OBJECTS) -o $(AI_STRESS_TARGET) $(GTK_LIBS)
	@echo "Running AI stress test..."
	./$(AI_STRESS_TARGET)

# PGN Parser Test
test-pgn: $(GAME_OBJECTS)
	@echo "Building PGN parser test..."
	$(CC) $(CFLAGS) -I. -I$(SRCDIR) $(SRCDIR)/test_pgn_parser.c $(filter-out $(OBJDIR)/test_suite.o $(OBJDIR)/main_test.o $(OBJDIR)/test_extended.o $(OBJDIR)/test_pgn_parser.o, $(GAME_OBJECTS)) -o $(BUILDDIR)/test_pgn_parser.exe
	@echo "Running PGN parser test..."
	./$(BUILDDIR)/test_pgn_parser.exe

# Phony targets
# Installer / Distribution Targets

DIST_DIR = dist
STAGE_DIR = $(DIST_DIR)/stage/HalChess
PAYLOAD_ZIP = $(DIST_DIR)/payload.zip

# Stage: Create directory, copy game, assets, and runtime dependencies
stage: $(GUI_TARGET)
	@echo "Staging for distribution..."
	@mkdir -p $(STAGE_DIR)
	@mkdir -p $(STAGE_DIR)/assets
	@cp $(GUI_TARGET) $(STAGE_DIR)/
	@cp -r assets/* $(STAGE_DIR)/assets/
	@echo "Copying runtime DLLs (using ldd)..."
	@ldd $(GUI_TARGET) | grep '/mingw64/' | awk '{print $$3}' | sort | uniq | xargs -I {} cp "{}" $(STAGE_DIR)/
	@echo "Copying GDK Pixbuf SVG Loader and dependencies..."
	@mkdir -p $(STAGE_DIR)/lib/gdk-pixbuf-2.0/2.10.0/loaders
	@cp /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders/pixbufloader_svg.dll $(STAGE_DIR)/lib/gdk-pixbuf-2.0/2.10.0/loaders/
	@ldd /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders/pixbufloader_svg.dll | grep '/mingw64/' | awk '{print $$3}' | sort | uniq | xargs -I {} cp "{}" $(STAGE_DIR)/
	@echo "Generating loaders.cache..."
	@gdk-pixbuf-query-loaders /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders/pixbufloader_svg.dll | sed -E "s|.*[\\\\/]lib[\\\\/]gdk-pixbuf|lib/gdk-pixbuf|g" | sed "s|\\\\|/|g" > $(STAGE_DIR)/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache
	@echo "Copying D-Bus session daemon..."
	@mkdir -p $(STAGE_DIR)/bin
	@cp /mingw64/bin/gdbus.exe $(STAGE_DIR)/bin/ 2>/dev/null || true
	@echo "Copying GTK icon theme (Adwaita)..."
	@mkdir -p $(STAGE_DIR)/share/icons
	@cp -r /mingw64/share/icons/Adwaita $(STAGE_DIR)/share/icons/
	@echo "Copying GTK schemas..."
	@mkdir -p $(STAGE_DIR)/share/glib-2.0/schemas
	@cp /mingw64/share/glib-2.0/schemas/gschemas.compiled $(STAGE_DIR)/share/glib-2.0/schemas/
	@echo "Creating launcher script..."
	@echo '@echo off' > $(STAGE_DIR)/HalChess_Launcher.bat
	@echo 'set "APPDIR=%~dp0"' >> $(STAGE_DIR)/HalChess_Launcher.bat
	@echo 'set "GDK_PIXBUF_MODULEDIR=%APPDIR%lib\gdk-pixbuf-2.0\2.10.0\loaders"' >> $(STAGE_DIR)/HalChess_Launcher.bat
	@echo 'set "GDK_PIXBUF_MODULE_FILE=%APPDIR%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"' >> $(STAGE_DIR)/HalChess_Launcher.bat
	@echo 'set "XDG_DATA_DIRS=%APPDIR%share"' >> $(STAGE_DIR)/HalChess_Launcher.bat
	@echo 'start "" "%%APPDIR%%HalChess.exe" %%*' >> $(STAGE_DIR)/HalChess_Launcher.bat
	@echo "Staging complete at $(STAGE_DIR)"

# Payload: Zip the staging directory
payload: stage
	@echo "Creating payload.zip..."
	@powershell -Command "Compress-Archive -Path '$(STAGE_DIR)/*' -DestinationPath '$(PAYLOAD_ZIP)' -Force"
	@echo "Payload created at $(PAYLOAD_ZIP)"

# Dist: Build installers
dist: payload unified_installer
	@echo "Distribution build complete."

# Unified Installer
UNIFIED_SRC = installer/main.c \
              installer/src/install_logic.c \
              installer/src/payload_utils.c \
              installer/src/zip_extract.c \
              installer/src/path_utils.c \
              installer/lib/miniz.c

# Reuse setup resource because it just contains the payload and icon
UNIFIED_RES = installer/resources/installer_setup.rc
UNIFIED_RES_OBJ = $(BUILDDIR)/installer_unified.res
UNIFIED_TARGET = $(DIST_DIR)/HalChessSetup.exe

# Compile Resource
$(UNIFIED_RES_OBJ): $(UNIFIED_RES) $(PAYLOAD_ZIP)
	@echo "Compiling Installer resources..."
	@windres $(UNIFIED_RES) -O coff -o $(UNIFIED_RES_OBJ)

# Build Unified Installer
unified_installer: $(UNIFIED_TARGET)

$(UNIFIED_TARGET): $(UNIFIED_SRC) $(UNIFIED_RES_OBJ)
	@echo "Building Unified Installer..."
	$(CC) $(CFLAGS) -mwindows -Iinstaller/src -Iinstaller/lib $(UNIFIED_SRC) $(UNIFIED_RES_OBJ) -o $@ -lshlwapi -luser32 -lshell32 -lole32 -luuid -lcomdlg32 -lcomctl32
	@echo "Installer created at $@"

.PHONY: all all-tests clean test test-suite gui test-svg test-focus test-ai-stress test-pgn stage payload dist unified_installer

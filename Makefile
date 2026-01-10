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
# Exclude icon_test.c, and test_svg_loader.c from main GUI build
# test_svg_loader.c is a standalone test program
GUI_SOURCES := $(filter-out $(GUIDIR)/icon_test.c $(GUIDIR)/test_svg_loader.c, $(GUI_SOURCES))
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
GUI_TARGET = $(BUILDDIR)/chessgame_gui.exe

# SVG loader test
SVG_TEST_TARGET = $(BUILDDIR)/test_svg_loader.exe

# GdkPixbuf is included with GTK4, no extra flags needed for SVG test
# (SVG support requires librsvg to be installed, but GdkPixbuf will use it if available)

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
TEST_OBJECTS = $(filter-out $(OBJDIR)/test_suite.o $(OBJDIR)/move_validation_test.o, $(GAME_OBJECTS))

$(TEST_TARGET): $(TEST_OBJECTS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $^ -o $@

# Test suite executable (exclude main_test.c and move_validation_test.c)
TEST_SUITE_OBJECTS = $(filter-out $(OBJDIR)/main_test.o $(OBJDIR)/move_validation_test.o, $(GAME_OBJECTS))

$(OBJDIR)/test_suite.o: $(SRCDIR)/test_suite.c | $(OBJDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(TEST_SUITE_TARGET): $(TEST_SUITE_OBJECTS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $^ -o $@
	@echo "Build complete! Run: $(TEST_TARGET) or $(TEST_SUITE_TARGET)"

# Resource file for Windows icon
RC_FILE = chess.rc
RES_OBJ = $(OBJDIR)/chess_res.o

# Build resource file
$(RES_OBJ): $(RC_FILE) assets/images/icon/icon.ico | $(OBJDIR)
	@echo "Compiling resources..."
	windres $(RC_FILE) -O coff -o $(RES_OBJ)

# GUI executable (links game + GUI + GTK4)
GAME_OBJS_FOR_GUI = $(filter-out $(OBJDIR)/main_test.o $(OBJDIR)/test_suite.o $(OBJDIR)/move_validation_test.o, $(GAME_OBJECTS))
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

# Where MSYS2 MinGW64 binaries live (DLLs)
MINGW_PREFIX := $(shell pkg-config --variable=prefix gtk4 2>/dev/null)
MINGW_BIN    := $(MINGW_PREFIX)/bin

# Robust Standalone Distribution
DIST_DIR = $(BUILDDIR)/standalone

# Helper to copy DLLs using ldd
define copy_deps
	@echo "Scanning and copying DLL dependencies..."
	@ldd "$(1)" | grep -iE "\/mingw64\/bin\/.*\.dll|\/usr\/bin\/.*\.dll" | awk '{print $$3}' | sort -u | while read dll; do \
		if [ -f "$$dll" ]; then \
			cp -Wu "$$dll" "$(2)/"; \
			echo "  [DLL] $$(basename "$$dll")"; \
		fi; \
	done
endef

copy-dlls: $(GUI_TARGET)
	@mkdir -p "$(DIST_DIR)"
	$(call copy_deps,$(GUI_TARGET),$(DIST_DIR))
.PHONY: copy-dlls

standalone: $(GUI_TARGET)
	@echo "========================================"
	@echo "Creating Standalone Release: $(DIST_DIR)"
	@echo "========================================"
	
	@# 1. Clean and Create Directory
	@rm -rf $(DIST_DIR)
	@mkdir -p $(DIST_DIR)
	
	@# 2. Copy Executable
	@echo "-> Copying executable..."
	@cp $(GUI_TARGET) $(DIST_DIR)/
	
	@# 3. Copy Assets (Recursively)
	@echo "-> Copying assets/ directory..."
	@mkdir -p $(DIST_DIR)/assets
	@cp -r assets/* $(DIST_DIR)/assets/
	
	@# 4. Copy Icon (if exists outside assets)
	@if [ -f icon.png ]; then cp icon.png $(DIST_DIR)/; fi
	
	@# 5. Copy DLLs
	$(call copy_deps,$(GUI_TARGET),$(DIST_DIR))
	
	@# 6. Setup GDK Pixbuf Loaders (Required for images)
	@echo "-> Setting up GDK Pixbuf loaders..."
	@PIXBUF_DIR="$$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0 2>/dev/null)"; \
	PIXBUF_QUERY="$$(pkg-config --variable=gdk_pixbuf_query_loaders gdk-pixbuf-2.0 2>/dev/null)"; \
	if [ -d "$$PIXBUF_DIR" ]; then \
		mkdir -p "$(DIST_DIR)/lib/gdk-pixbuf-2.0/2.10.0/loaders"; \
		cp -r "$$PIXBUF_DIR/"*.dll "$(DIST_DIR)/lib/gdk-pixbuf-2.0/2.10.0/loaders/" 2>/dev/null || true; \
		if [ -x "$$PIXBUF_QUERY" ]; then \
			"$$PIXBUF_QUERY" > "$(DIST_DIR)/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"; \
			sed -i 's|.*/lib/|lib/|g' "$(DIST_DIR)/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"; \
		fi; \
	fi
	
	@# 7. Setup GLib Schemas (Required for GTK settings)
	@echo "-> Copying GLib schemas..."
	@SCHEMAS_DIR="$$(pkg-config --variable=prefix glib-2.0 2>/dev/null)/share/glib-2.0/schemas"; \
	if [ -d "$$SCHEMAS_DIR" ]; then \
		mkdir -p "$(DIST_DIR)/share/glib-2.0/schemas"; \
		cp "$$SCHEMAS_DIR/gschemas.compiled" "$(DIST_DIR)/share/glib-2.0/schemas/" 2>/dev/null || true; \
	fi
	
	@# 8. Create Run Script (Sets up environment variables for portability)
	@echo "-> Creating run_gui.bat..."
	@printf "@echo off\r\nsetlocal\r\nset \"DIR=%%~dp0\"\r\nset \"PATH=%%DIR%%;%%PATH%%\"\r\nset \"GDK_PIXBUF_MODULE_FILE=%%DIR%%lib\\gdk-pixbuf-2.0\\2.10.0\\loaders.cache\"\r\nset \"GSETTINGS_SCHEMA_DIR=%%DIR%%share\\glib-2.0\\schemas\"\r\nstart \"\" \"%%DIR%%chessgame_gui.exe\"\r\n" > "$(DIST_DIR)/run_gui.bat"
	
	@echo "========================================"
	@echo "Standalone build complete!"
	@echo "Location: $(DIST_DIR)"
	@echo "Run: double-click 'run_gui.bat' inside that folder."
	@echo "========================================"

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

# Phony targets
.PHONY: all all-tests clean test test-suite standalone gui test-svg

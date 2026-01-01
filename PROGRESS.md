# Conversion Progress - Java to C

## Implemented (Awaiting Verification)

1. **Project Structure**
   - Created `ChessGameC/` folder
   - Copied Stockfish source code to `src/`
   - Created folder structure: `game/`, `gui/`, `training/`

2. **Basic Types & Structures**
   - `types.h` - All enums (Player, PieceType, GameMode, AIType)
   - `piece.h/c` - Piece structure and functions
   - `move.h/c` - Move structure and functions
   - `gamelogic.h` - GameLogic structure definition

3. **GameLogic Implementation**
   - Basic structure and initialization
   - Board setup
   - Stack/List utilities for history
   - Move generation for all piece types
   - Move validation and legal move filtering
   - Check/Checkmate/Stalemate detection
   - FEN generation
   - Castling logic (with safety checks)
   - En passant logic
   - Undo functionality
   - Statistics tracking

4. **Build System**
   - `Makefile` - Make build configuration
   - Documentation files (README, INSTALLATION, TESTING)

5. **Testing**
   - Basic test (`main_test.c`)
   - Comprehensive test suite (`test_suite.c`) - matches Java ChessTest.java
   - All 16 tests passing in test suite

## ⏳ Pending Features Roadmap

### 1. Playing Strength & Engines
- [ ] **Stockfish Integration**
    - [ ] Clean integration (engine selection, time control, multi-PV).
    - [ ] Allow "Engine A" (Internal/Stockfish) vs "Engine B" (External UCI).
    - [ ] PGN logging for matches.
    - [ ] Optimize Stockfish: Select ELO, depth, move time approximation.
- [ ] **Analysis Capabilities**
    - [ ] Expose depth, eval, best line, nodes, time.
    - [ ] "Engine Bar" (evaluation graph/bar) on the side.
    - [ ] Support custom NNUE (load via menu/cmd).
    - [ ] Make in-house Stockfish do game review and stats after every match.

### 2. Training & Practice Tools
- [ ] **Tactics Mode**
    - [ ] Load PGN/JSON puzzles.
    - [ ] "You to move" feedback, correctness check, rating/streak tracking.
- [ ] **Analysis Tools**
    - [ ] **Blunder/Accuracy Analysis**: Mark Brilliant/Best/Inaccuracy/Mistake/Blunder with explanations.
    - [ ] **Guess the Move**: Hide engine move, user guesses, score based on closeness.
    - [ ] **Threat Mode**: Show opponent's best reply if you pass.

### 3. Game Management & Formats
- [ ] **PGN Support**: Full save/load (Event, Site, Date, ECO, etc.).
- [ ] **Opening Explorer**: Show ECO opening name as moves are played.
- [ ] **Repertoire Support**: Mark moves as "repertoire", drill random positions.
- [ ] **Interoperability**:
    - [ ] Import PGNs from Chess.com/Lichess.
    - [ ] Export annotated games (comments/diagrams) to PGN.
    - [ ] Export/Import in `.chesspiece` (JSON) format.

### 4. Analysis & Visualization
- [ ] **Visual Aids**
    - [ ] Move graph (evaluation over time).
    - [ ] Arrow overlays (Green=Best, Red=Blunder).
    - [ ] Heat maps / Attack maps (toggle for attacked/defended squares).
- [ ] **Piece Editor**: Implement using the SVG system.

### 5. Time Controls & Practical Play
- [ ] **Clocks**: Presets (Bullet/Blitz/Rapid/Classical) + Increment/Delay.
- [ ] **Controls**: Premoves, Move confirmation option.
- [ ] **Opening Training Mode**: Engine plays from book, starts thinking only after book ends.
- [ ] **User Interface**
    - [ ] Keyboard shortcuts (New game, Flip board, Toggle eval, Start/Stop).
    - [ ] Focus management: After popups, return focus to main app.

### 6. Customisation & Theming
- [x] **Themes**:
    - [x] Piece sets: Multiple SVG themes (Classic, Alpha, Minimal).
    - [ ] Board textures: Wood, Marble, Flat colors.
    - [ ] Dark mode support.
- [x] **Optimization**:
    - [x] Use `#define MAX_PIECE_SETS 100`.
    - [x] Optimize SVG size (Removed unused sets).
    - [ ] Unified piece type for promotion and graveyard.

### 7. Performance & Efficiency
- [ ] **Benchmarks for size and memory consumption**.
- [ ] **Makefile Optimization**: Reduce size, remove unnecessary files.
- [ ] **Low-Spec Mode**: Disable animations, simplify themes, limit threads/hash.
- [ ] **Auto-save**: Prevent data loss on crash.
- [ ] **Code Constants**:
    - [ ] `#define MAX_STROKE_WIDTH 4.0`
    - [ ] `#define DEFAULT_WHITE_STROKE_WIDTH 0.4`
    - [ ] `#define DEFAULT_BLACK_STROKE_WIDTH 0.0`

### 8. Educational / Beginner-Friendly
- [x] **Interactive Tutorial**:
    - [x] Step-by-step interactive guide (Pawn to Mate).
    - [x] Restricted movement logic.
    - [x] Dialog instructions and focus management.
    - [x] 2-second delay between steps.
    - [x] Onboarding bubble for new users.
- [ ] **Puzzles Mode**:
    - [ ] 10 Famous/Popular Puzzles.
    - [ ] Puzzle specific Info Panel (Title, Description).
    - [ ] Validation logic (Correct/Incorrect feedback).
    - [ ] Auto-play opponent response.
- [ ] **Move Hints**: Tooltips ("Fork", "Pin", "Discovered Attack").
- [ ] **Difficulty Levels**: Labels (Beginner/Intermediate/Adv) wrapping engine settings.
- [ ] **Beginner Aids**: "Show legal moves", "Show attacked squares".

### 9. Deprioritized / Dropped
- [X] **In-house AI Training System (Genetic AI)**: Dropped in favor of Stockfish focus.
- [X] **Complex Multi-engine Training**: Postponed until core analysis is stable.

## File Structure Plan

```
ChessGameC/
├── src/                    # Stockfish engine
├── game/                   # Chess logic (C implementation)
├── gui/                    # GUI (GTK4)
│   ├── components/         # Reusable widgets
│   ├── dialogs/           # Menus and popups
│   └── assets/            # SVGs and themes
├── analysis/               # Analysis engine integration
└── Makefile
```

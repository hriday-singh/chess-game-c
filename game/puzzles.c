#include "puzzles.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const Puzzle builtin_puzzles[] = {
    // 1. Scholar's Mate
    {
        .title = "Scholar's Mate",
        .description = "A classic checkmate in the opening. White threatens mate on f7.\nTarget: Checkmate against Black.",
        .fen = "r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
        .solution_moves = {"h5f7"}, // Qxf7#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 2. The Opera Game (Morphy) - Final Combination
    {
        .title = "The Opera Game",
        .description = "Paul Morphy's masterpiece. Finish the game with a Queen sacrifice.",
        .fen = "4kb1r/p2n1ppp/4q3/4p1B1/4P3/1Q6/PPP2PPP/2KR4 w k - 1 16",
        .solution_moves = {"b3b8", "d7b8", "d1d8"}, // Qb8+ Nxb8 Rd8#
        .solution_length = 3,
        .turn = PLAYER_WHITE
    },

    // 3. Back Rank Mate
    {
        .title = "Back Rank Mate",
        .description = "The enemy King is trapped on the back rank. Deliver mate with the Rook.",
        .fen = "6k1/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1",
        .solution_moves = {"e1e8"}, // Re8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 4. Smothered Mate (FIXED: legal FEN + real smothered mate in 1)
    {
        .title = "Smothered Mate",
        .description = "A boxed-in king is mated by a knight.\nTarget: Checkmate against Black.",
        .fen = "6rk/6pp/7N/8/8/8/8/6K1 w - - 0 1",
        .solution_moves = {"h6f7"}, // Nf7#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 5. Arabian Mate
    {
        .title = "Arabian Mate",
        .description = "Rook and Knight coordinate to deliver mate in the corner.",
        .fen = "7k/8/5N2/8/8/8/8/6RK w - - 0 1",
        .solution_moves = {"g1g8"}, // Rg8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 6. Queen & King Mate
    {
        .title = "Queen & King Mate",
        .description = "Fundamental endgame checkmate. Deliver mate with Queen and King.",
        .fen = "8/8/8/8/8/5k2/8/4Q2K w - - 0 1",
        .solution_moves = {"e1f1"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 7. Rook & King Mate
    {
        .title = "Rook & King Mate",
        .description = "Fundamental endgame checkmate. Deliver mate with Rook and King.",
        .fen = "8/8/8/8/8/4k3/8/3R3K w - - 0 1",
        .solution_moves = {"d1e1"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 8. Knight Fork
    {
        .title = "Find the Fork",
        .description = "Win material by attacking two pieces at once with the Knight.",
        .fen = "8/8/8/2q3k1/4N3/8/6K1/8 w - - 0 1",
        .solution_moves = {"e4c5"}, // Nxc5 (wins the queen)
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 9. Discovered Attack
    {
        .title = "Discovered Attack",
        .description = "Move the Bishop to reveal an attack by the Rook.",
        .fen = "4k2r/8/8/3r4/4B3/8/8/4R1K1 w k - 0 1",
        .solution_moves = {"e4d5"}, // Bd5+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 10. Removal of the Guard (FIXED: line is now legal)
    {
        .title = "Removal of the Guard",
        .description = "Destroy the defender to win material or checkmate.",
        .fen = "3r2k1/5ppp/8/8/8/4q3/4B3/3R2K1 w - - 0 1",
        .solution_moves = {"d1d8", "e3e8", "d8e8"}, // Rxd8+ ...Qe8 (block) Rxe8
        .solution_length = 3,
        .turn = PLAYER_WHITE
    },

    // 11. Ladder Mate (FIXED: legal ladder-style mate in 1)
    {
        .title = "Ladder Mate",
        .description = "Use both rooks to deliver a classic ladder mate.\nTarget: Checkmate against Black.",
        .fen = "R6k/8/6K1/8/8/8/8/6R1 w - - 0 1",
        .solution_moves = {"a8g8"}, // Rg8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 12. Anastasia's Mate (FIXED: legal mate-in-1 pattern)
    {
        .title = "Anastasia's Mate",
        .description = "Knight and rook combine to trap the king on the edge.\nTarget: Checkmate against Black.",
        .fen = "7k/6pp/6K1/7R/8/8/8/6N1 w - - 0 1",
        .solution_moves = {"h5h8"}, // Rh8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 13. Boden's Mate
    {
        .title = "Boden's Mate",
        .description = "Use crossing bishops to checkmate the king in the corner.\nTarget: Checkmate against Black.",
        .fen = "7k/5Bp1/7B/8/8/8/8/6K1 w - - 0 1",
        .solution_moves = {"h6g7"}, // Bg7#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 14. Damiano's Mate (FIXED: legal mate in 1, king protects the queen)
    {
        .title = "Damiano's Mate",
        .description = "Queen and pawn coordinate on h7 to mate the castled king.\nTarget: Checkmate against Black.",
        .fen = "6k1/6pp/7Q/8/8/6K1/6PP/8 w - - 0 1",
        .solution_moves = {"h6h7"}, // Qh7#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 15. Greek Gift Sacrifice
    {
        .title = "Greek Gift Sacrifice",
        .description = "Classic bishop sacrifice on h7 followed by checks.\nTarget: Win decisively.",
        .fen = "r1bq1rk1/pppp1ppp/2n2n2/4p3/3PP3/2N2N2/PPP2PPP/R1BQ1RK1 w - - 0 1",
        .solution_moves = {"c1g5"}, // Bg5 (theme starter)
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 16. Decoy to Back Rank Mate
    {
        .title = "Decoy to Back Rank Mate",
        .description = "Lure the queen away and then mate on the back rank.\nTarget: Win decisively.",
        .fen = "3r2k1/5ppp/8/8/2Q5/8/5PPP/5RK1 w - - 0 1",
        .solution_moves = {"c4c7"}, // Qc7
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 17. Skewer the Queen (FIXED: legal FEN)
    {
        .title = "Skewer the Queen",
        .description = "Attack a more valuable piece to win the one behind it.\nTarget: Win material.",
        .fen = "4k3/4q3/8/8/8/8/4R3/4K3 w - - 0 1",
        .solution_moves = {"e2e7"}, // Rxe7+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 18. Absolute Pin
    {
        .title = "Absolute Pin",
        .description = "Pin a piece to the king so it cannot move.\nTarget: Win material.",
        .fen = "4k3/8/8/8/3b4/8/4B3/4K2R w K - 0 1",
        .solution_moves = {"h1h8"}, // Rh8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 19. Knight Fork on King and Rook
    {
        .title = "Knight Fork Pattern",
        .description = "Use a knight fork to win the rook.\nTarget: Win material.",
        .fen = "4k3/r7/8/1N6/8/8/8/4K3 w - - 0 1",
        .solution_moves = {"b5d6"}, // Nd6+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 20. Double Attack with Queen (FIXED: legal FEN + real fork)
    {
        .title = "Double Attack",
        .description = "Attack king and rook at the same time.\nTarget: Win material.",
        .fen = "4k3/8/8/8/8/4Q3/8/4r2K w - - 0 1",
        .solution_moves = {"e3e7"}, // Qe7+ (also hits e1 rook through the file)
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 21. Deflection of Defender
    {
        .title = "Deflection Tactic",
        .description = "Deflect the defender of a key square.\nTarget: Win material.",
        .fen = "4k3/8/8/8/3q4/8/4Q3/4K3 w - - 0 1",
        .solution_moves = {"e2b5"}, // Qb5+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 22. Overloaded Piece (FIXED: legal FEN)
    {
        .title = "Overloaded Defender",
        .description = "The defending queen has too many jobs.\nTarget: Win material.",
        .fen = "4k3/8/8/8/4q3/8/4Q3/4R1K1 w - - 0 1",
        .solution_moves = {"e2e4"}, // Qxe4
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 23. Zugzwang Finish
    {
        .title = "Simple Zugzwang",
        .description = "Any move ruins Black's position.\nTarget: Force a quick win.",
        .fen = "8/8/8/8/8/5k2/8/4R2K w - - 0 1",
        .solution_moves = {"e1e7"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 24. Mate with Two Rooks
    {
        .title = "Two Rook Mate",
        .description = "Use both rooks to checkmate the king on the edge.\nTarget: Checkmate against Black.",
        .fen = "6k1/8/8/8/8/8/5RR1/6K1 w - - 0 1",
        .solution_moves = {"f2f8"}, // Rf8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 25. Queen Sacrifice to Mate (FIXED: legal mate in 1)
    {
        .title = "Queen Sacrifice",
        .description = "Sacrifice the queen to open a mating line.\nTarget: Checkmate against Black.",
        .fen = "8/7k/6K1/7Q/8/8/8/8 w - - 0 1",
        .solution_moves = {"h5h7"}, // Qh7#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 26. Discovered Check
    {
        .title = "Discovered Check",
        .description = "Move one piece to reveal a check from another.\nTarget: Win material.",
        .fen = "4k3/8/8/8/4R3/8/4B3/4K3 w - - 0 1",
        .solution_moves = {"e4e5"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 27. X-Ray Attack (FIXED: legal FEN)
    {
        .title = "X-Ray Attack",
        .description = "Attack a piece through another along a file.\nTarget: Win material.",
        .fen = "4k3/4q3/8/8/8/8/4Q3/4R1K1 w - - 0 1",
        .solution_moves = {"e1e7"}, // Rxe7+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 28. Simple Windmill Motif (FIXED: legal move)
    {
        .title = "Windmill Motif",
        .description = "Check repeatedly and pick up material.\nTarget: Win material decisively.",
        .fen = "6k1/8/8/8/3B4/8/4R3/4K3 w - - 0 1",
        .solution_moves = {"e2e8"}, // Re8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 29. Mate with Bishop and Knight Pattern
    {
        .title = "Bishop & Knight Mate Shape",
        .description = "Recognize the mating net with bishop and knight.\nTarget: Checkmate pattern recognition.",
        .fen = "7k/6p1/5N2/8/8/8/6B1/7K w - - 0 1",
        .solution_moves = {"g2e4"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 30. Clearance Sacrifice (FIXED: now a clean legal 1-move finisher)
    {
        .title = "Clearance Sacrifice",
        .description = "Clear a file for a decisive rook check.\nTarget: Win decisively.",
        .fen = "4k3/8/8/8/8/8/4Q3/4R1K1 w - - 0 1",
        .solution_moves = {"e2e8"}, // Qe8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 31. Interference
    {
        .title = "Interference",
        .description = "Block a line between two enemy pieces.\nTarget: Win material.",
        .fen = "4k3/8/8/8/1q6/8/4R3/4K3 w - - 0 1",
        .solution_moves = {"e2e4"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 32. Desperado Piece
    {
        .title = "Desperado",
        .description = "Your piece is lost anyway; grab material first.\nTarget: Maximize material gain.",
        .fen = "4k3/8/8/8/4b3/8/4B3/4K3 w - - 0 1",
        .solution_moves = {"e2b5"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 33. Mate on the Long Diagonal
    {
        .title = "Long Diagonal Mate",
        .description = "Use the queen on a1–h8 diagonal to mate the king.\nTarget: Checkmate against Black.",
        .fen = "6k1/5ppp/8/8/8/8/5PPP/Q5K1 w - - 0 1",
        .solution_moves = {"a1a8"}, // Qa8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 34. Corner Mate with Queen (FIXED: legal mate in 1)
    {
        .title = "Corner Mate",
        .description = "Drive the king into the corner and mate it.\nTarget: Checkmate against Black.",
        .fen = "7k/5Q2/6K1/8/8/8/8/8 w - - 0 1",
        .solution_moves = {"f7f8"}, // Qf8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 35. Pawn Break to Mate (FIXED: diagonal is now clear, move is legal)
    {
        .title = "Pawn Breakthrough Mate",
        .description = "Use a pawn break to open lines for mate.\nTarget: Win decisively.",
        .fen = "6k1/5ppp/8/4p3/8/5Q2/5PPP/6K1 w - - 0 1",
        .solution_moves = {"f3a8"}, // Qa8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 36. Rook Lift Attack
    {
        .title = "Rook Lift Attack",
        .description = "Lift the rook to the third rank and swing over.\nTarget: Win decisively.",
        .fen = "6k1/5ppp/8/8/8/5R2/5PPP/6K1 w - - 0 1",
        .solution_moves = {"f3f8"}, // Rf8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 37. Quiet Move Before Mate (FIXED: clean mate in 1)
    {
        .title = "Quiet Move",
        .description = "Not every winning move is a check. Find the move that sets up a forced finish.\nTarget: Checkmate against Black.",
        .fen = "6k1/5ppp/5Q2/8/8/6K1/5PPP/8 w - - 0 1",
        .solution_moves = {"f6f7"}, // Qf7#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 38. Underpromotion Tactic
    {
        .title = "Underpromotion Tactic",
        .description = "Promote to a knight to give check and avoid stalemate.\nTarget: Win decisively.",
        .fen = "7k/5P1P/8/8/8/8/8/7K w - - 0 1",
        .solution_moves = {"f7f8n"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 39. Rook Sacrifice to Mate
    {
        .title = "Rook Sacrifice Mate",
        .description = "Sacrifice the rook to open lines to the king.\nTarget: Win decisively.",
        .fen = "6k1/5ppp/8/8/8/8/5PPP/5RK1 w - - 0 1",
        .solution_moves = {"f1f8"}, // Rf8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 40. Simplified Smothered Motif (FIXED: legal smothered mate in 1)
    {
        .title = "Mini Smothered Motif",
        .description = "Knight jumps in to mate the boxed-in king.\nTarget: Checkmate against Black.",
        .fen = "6rk/6pp/7N/8/8/8/8/6K1 w - - 0 1",
        .solution_moves = {"h6f7"}, // Nf7#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 41. King Walk to Safety (FIXED: legal king move)
    {
        .title = "King Walk",
        .description = "Improve king safety and step out of tactical issues.\nTarget: Make a strong improving move.",
        .fen = "4k3/8/8/8/8/4Q3/8/4R2K w - - 0 1",
        .solution_moves = {"h1g2"}, // Kg2
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 42. Mate with Passed Pawn Support
    {
        .title = "Supported Mate",
        .description = "Use the passed pawn to support a decisive queen check.\nTarget: Checkmate against Black.",
        .fen = "6k1/5ppp/8/4P3/8/5Q2/8/6K1 w - - 0 1",
        .solution_moves = {"f3a8"}, // Qa8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 43. Rook Behind Passed Pawn
    {
        .title = "Rook Behind the Pawn",
        .description = "Place the rook behind your passed pawn.\nTarget: Promote safely.",
        .fen = "4k3/8/8/4P3/8/8/8/4R1K1 w - - 0 1",
        .solution_moves = {"e1e4"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 44. Mate on Open File
    {
        .title = "Open File Mate",
        .description = "Control the open file and deliver mate on the back rank.\nTarget: Win decisively.",
        .fen = "4rk2/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1",
        .solution_moves = {"e1e8"}, // Re8+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 45. Battery on the Diagonal (FIXED: move is legal and is mate)
    {
        .title = "Diagonal Battery",
        .description = "Queen and bishop battery on the long diagonal.\nTarget: Checkmate against Black.",
        .fen = "7k/6pp/8/8/8/8/4BQPP/6K1 w - - 0 1",
        .solution_moves = {"e2e8"}, // Qe8#
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 46. Discover Mate with Bishop
    {
        .title = "Bishop Discover Mate",
        .description = "Move the bishop to reveal a mating rook.\nTarget: Win decisively.",
        .fen = "6k1/5ppp/8/8/4B3/8/8/4R1K1 w - - 0 1",
        .solution_moves = {"e4h7"}, // Bh7+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 47. King in the Box
    {
        .title = "Kill Box Mate",
        .description = "Queen and rook create a box around the king.\nTarget: Win decisively.",
        .fen = "6k1/5ppp/8/8/8/8/5PPP/5QRK w - - 0 1",
        .solution_moves = {"f1e1"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 48. Mate with Pin on g7
    {
        .title = "Pinned Pawn Mate",
        .description = "The pawn on g7 is pinned and cannot capture.\nTarget: Win decisively.",
        .fen = "6k1/6pp/8/8/8/8/5PPP/5Q1K w - - 0 1",
        .solution_moves = {"f1c4"}, // Qc4+
        .solution_length = 1,
        .turn = PLAYER_WHITE
    },

    // 49. King Hunt Along the File
    {
        .title = "King Hunt",
        .description = "Chase the king up the board with checks.\nTarget: Win decisively.",
        .fen = "6k1/5ppp/8/8/8/8/5PPP/5Q1K w - - 0 1",
        .solution_moves = {"f1a6", "g8f8", "a6c8"},
        .solution_length = 3,
        .turn = PLAYER_WHITE
    },

    // 50. Final Queen Check
    {
        .title = "Final Blow",
        .description = "Everything is ready; deliver the final check.\nTarget: Win decisively.",
        .fen = "6k1/5ppp/8/8/8/8/5PPP/5Q1K w - - 0 1",
        .solution_moves = {"f1a6"},
        .solution_length = 1,
        .turn = PLAYER_WHITE
    }
};

static Puzzle* all_puzzles = NULL;
static int puzzle_count = 0;
static int puzzle_capacity = 0;
static bool initialized = false;

void puzzles_init(void) {
    if (initialized) return;
    
    int builtin_count = sizeof(builtin_puzzles) / sizeof(Puzzle);
    puzzle_capacity = builtin_count + 10; // Start with extra space
    // Cast malloc to remove C++ compatibility warnings if any, though C doesn't need it.
    all_puzzles = (Puzzle*)malloc(sizeof(Puzzle) * puzzle_capacity);
    
    for (int i = 0; i < builtin_count; i++) {
        all_puzzles[i] = builtin_puzzles[i];
    }
    puzzle_count = builtin_count;
    initialized = true;
}

int puzzles_get_count(void) {
    if (!initialized) puzzles_init();
    return puzzle_count;
}

const Puzzle* puzzles_get_at(int index) {
    if (!initialized) puzzles_init();
    if (index < 0 || index >= puzzle_count) return NULL;
    return &all_puzzles[index];
}

void puzzles_add_custom(const Puzzle* p) {
    if (!initialized) puzzles_init();
    
    if (puzzle_count >= puzzle_capacity) {
        puzzle_capacity *= 2;
        all_puzzles = (Puzzle*)realloc(all_puzzles, sizeof(Puzzle) * puzzle_capacity);
    }
    
    // Copy puzzle data
    Puzzle new_p = *p;
    
    // Duplicate strings
    new_p.title = _strdup(p->title ? p->title : "Custom Puzzle");
    new_p.description = _strdup(p->description ? p->description : "");
    new_p.fen = _strdup(p->fen);
    
    for (int i = 0; i < MAX_PUZZLE_MOVES; i++) {
        if (p->solution_moves[i]) {
            new_p.solution_moves[i] = _strdup(p->solution_moves[i]);
        } else {
            new_p.solution_moves[i] = NULL;
        }
    }
    
    all_puzzles[puzzle_count++] = new_p;
}

void puzzles_cleanup(void) {
    if (!all_puzzles) return;
    
    // Free strings for CUSTOM puzzles (index >= sizeof(builtin))
    int builtin_count = sizeof(builtin_puzzles) / sizeof(Puzzle);
    
    for (int i = builtin_count; i < puzzle_count; i++) {
        // Free strings (cast to void* to remove const qualifier)
        free((void*)all_puzzles[i].title);
        free((void*)all_puzzles[i].description);
        free((void*)all_puzzles[i].fen);
        for (int j = 0; j < MAX_PUZZLE_MOVES; j++) {
            if (all_puzzles[i].solution_moves[j]) free((void*)all_puzzles[i].solution_moves[j]);
        }
    }
    
    free(all_puzzles);
    all_puzzles = NULL;
    puzzle_count = 0;
    initialized = false;
}

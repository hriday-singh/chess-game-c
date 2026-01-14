#include "game/gamelogic.h"
#include "game/move.h"
#include "game/piece.h"
#include <stdio.h>
#include <stdbool.h>

int main() {
    printf("--- Reproducing perform_move behavior ---\n");
    
    // 1. Create Logic
    printf("Creating GameLogic...\n");
    GameLogic* logic = gamelogic_create();
    if (!logic) {
        printf("Failed to create logic.\n");
        return 1;
    }
    gamelogic_reset(logic);

    printf("Initial Turn: %d (0=White, 1=Black)\n", logic->turn);

    // 2. Inspect Source Square (e2 -> 6,4)
    int r1 = 6, c1 = 4;
    int from_sq = r1 * 8 + c1;
    Piece* p = logic->board[r1][c1];
    
    if (p) {
        printf("Piece at (%d,%d): Type=%d, Owner=%d\n", r1, c1, p->type, p->owner);
    } else {
        printf("ERROR: No piece at (%d,%d)!\n", r1, c1);
    }

    // 3. Inspect Dest Square (e4 -> 4,4)
    int r2 = 4, c2 = 4;
    int to_sq = r2 * 8 + c2;
    
    printf("Attempting move from %d to %d...\n", from_sq, to_sq);

    // 4. Create Move
    Move* m = move_create(from_sq, to_sq);
    if (!m) {
        printf("Failed to create move.\n");
        return 1;
    }

    // 5. Perform Move
    bool result = gamelogic_perform_move(logic, m);
    
    if (result) {
        printf("SUCCESS: gamelogic_perform_move returned true.\n");
        if (logic->board[r2][c2]) {
            printf("Verified: Piece is now at destination.\n");
        } else {
            printf("FAILED: perform_move true, but destination is empty!\n");
        }
        if (logic->board[r1][c1] == NULL) {
            printf("Verified: Source square is empty.\n");
        } else {
            printf("FAILED: Source square still has piece!\n");
        }
    } else {
        printf("FAILURE: gamelogic_perform_move returned false.\n");
    }

    // Cleanup
    move_free(m);
    gamelogic_free(logic);
    
    return 0;
}

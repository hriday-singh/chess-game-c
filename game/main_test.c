#include "gamelogic.h"
#include "move.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("Testing GameLogic...\n");
    
    GameLogic* game = gamelogic_create();
    if (!game) {
        printf("ERROR: Failed to create GameLogic!\n");
        return 1;
    }
    
    printf("GameLogic created successfully!\n");
    printf("Status: %s\n", game->statusMessage);
    printf("Turn: %s\n", (game->turn == PLAYER_WHITE) ? "White" : "Black");
    
    // Test move generation
    // MoveList structure (matches gamelogic_movegen.c)
    typedef struct {
        Move** moves;
        int count;
        int capacity;
    } MoveList;
    
    MoveList* moves = (MoveList*)malloc(sizeof(MoveList));
    moves->capacity = 32;
    moves->count = 0;
    moves->moves = (Move**)malloc(sizeof(Move*) * moves->capacity);
    
    gamelogic_generate_legal_moves(game, PLAYER_WHITE, moves);
    printf("Generated %d legal moves for White\n", moves->count);
    
    // Test FEN generation
    char fen[256];
    gamelogic_generate_fen(game, fen, sizeof(fen));
    printf("FEN: %s\n", fen);
    
    // Cleanup moves
    for (int i = 0; i < moves->count; i++) {
        move_free(moves->moves[i]);
    }
    free(moves->moves);
    free(moves);
    
    gamelogic_free(game);
    printf("Test passed!\n");
    return 0;
}

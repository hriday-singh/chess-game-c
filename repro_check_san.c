#include "game/gamelogic.h"
#include "game/piece.h"
#include "game/move.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("--- Reproducing Discovered Check SAN Generation ---\n");
    
    // 1. Create Logic
    GameLogic* logic = gamelogic_create();
    if (!logic) {
        printf("Failed to create logic.\n");
        return 1;
    }
    
    // Clear board manually
    for(int i=0; i<8; i++) 
        for(int j=0; j<8; j++) 
            if(logic->board[i][j]) { 
                piece_free(logic->board[i][j]); 
                logic->board[i][j] = NULL; 
            }

    // Setup Discovered Check Loop
    // White King at h1 (7, 7)
    logic->board[7][7] = piece_create(PIECE_KING, PLAYER_WHITE);
    
    // Black Rook at h8 (0, 7)
    logic->board[0][7] = piece_create(PIECE_ROOK, PLAYER_BLACK);
    
    // Black King at h7 (1, 7) - blocking the rook
    logic->board[1][7] = piece_create(PIECE_KING, PLAYER_BLACK);
    
    logic->turn = PLAYER_BLACK; // Black to move
    logic->playerSide = PLAYER_WHITE; // Irrelevant for this test, but good for consistency
    logic->isGameOver = false;

    // Move Black King to g7 (1, 6) -> Unmasks Rook -> Check
    // Coordinates:
    // from: 1,7 (1*8 + 7 = 15)
    // to:   1,6 (1*8 + 6 = 14)
    
    int from_sq = 15;
    int to_sq = 14; 
    
    Move* move = move_create(from_sq, to_sq);
    move->capturedPieceType = NO_PIECE;
    move->promotionPiece = NO_PROMOTION;
    move->isEnPassant = false;
    move->isCastling = false;
    // Mover is set inside perform, but SAN gen reads it? 
    // SAN gen reads from move->mover usually? Check gamelogic code.
    // Actually gamelogic_get_move_san calls gamelogic_get_all_legal_moves(logic, move->mover...)
    // So we MUST set mover.
    move->mover = PLAYER_BLACK; 
    
    // NOTE: In the real engine, move->firstMove is set by mk_move. 
    // Here we must set it if we want move_equals to work perfectly, 
    // but for SAN gen of a hypothetical move, it might not matter unless it affects castling notation.
    // King move doesn't affect castling notation directly (O-O is separate).
    
    char san[16];
    gamelogic_get_move_san(logic, move, san, sizeof(san));
    
    printf("Generated SAN: '%s'\n", san);
    
    int result = 0;
    if (strcmp(san, "Kg7+") == 0) {
        printf("SUCCESS: Discovered check detected.\n");
    } else {
        printf("FAILURE: Expected 'Kg7+', got '%s'\n", san);
        result = 1;
    }

    move_free(move);
    gamelogic_free(logic);
    return result;
}

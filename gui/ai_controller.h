#ifndef AI_CONTROLLER_H
#define AI_CONTROLLER_H

#include "ai_dialog.h"
#include "ai_engine.h"
#include "gamelogic.h"
#include "config_manager.h"

typedef struct _AiController AiController;

// Callback for when the AI has calculated a move
typedef void (*AiMoveReadyCallback)(Move* move, gpointer user_data);

// Analysis/Evaluation Statistics to broadcast
typedef struct {
    int score;            // White perspective centipawns. Mate mapped to +/-30000 internally.
    bool is_mate;         // White perspective
    int mate_distance;    // Positive = White mates, Negative = Black mates
    const char* best_move;

    // Rating / Toast Info (NULL if no rating update)
    const char* rating_label;   // Best/Excellent/Good/Inaccuracy/Mistake/Blunder
    const char* rating_reason;

    int move_number;
    char* fen;            // FEN this analysis belongs to

    // Hanging Pieces Count
    int white_hanging;
    int black_hanging;
} AiStats;

// Callback for evaluation updates
typedef void (*AiEvalUpdateCallback)(const AiStats* stats, gpointer user_data);

// Constructor
AiController* ai_controller_new(GameLogic* logic, AiDialog* ai_dialog);
void ai_controller_free(AiController* controller);

// Request an AI move for the current position
void ai_controller_request_move(AiController* controller,
                                bool use_custom,
                                AiDifficultyParams params,
                                const char* custom_path,
                                AiMoveReadyCallback callback,
                                gpointer user_data);

// Stop any ongoing thinking
void ai_controller_stop(AiController* controller);

// Check if AI is currently thinking
bool ai_controller_is_thinking(AiController* controller);

// Set search parameters (depth, time, etc.)
void ai_controller_set_params(AiController* controller, AiDifficultyParams params);

#endif // AI_CONTROLLER_H

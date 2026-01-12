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
    int score;
    bool is_mate;
    int mate_distance;
    const char* best_move;
    
    // Rating / Toast Info (NULL if no rating update)
    const char* rating_label; 
    const char* rating_reason;
    int move_number;
    
    // Hanging Pieces Count
    int white_hanging;
    int black_hanging;
} AiStats;

// Callback for evaluation updates
typedef void (*AiEvalUpdateCallback)(const AiStats* stats, gpointer user_data);

// Constructor no longer requires RightSidePanel, but needs game logic and settings
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

// Get current evaluation (last known)
void ai_controller_get_evaluation(AiController* controller, int* score, bool* is_mate);

// Set search parameters (depth, time, etc.)
void ai_controller_set_params(AiController* controller, AiDifficultyParams params);

// Set evaluation update callback
void ai_controller_set_eval_callback(AiController* controller, AiEvalUpdateCallback callback, gpointer user_data);

// Enable/disable NNUE
void ai_controller_set_nnue(AiController* controller, bool enabled, const char* path);

// Marks that the next evaluation should count towards a rating (move just made)
void ai_controller_set_rating_pending(AiController* controller, bool pending);

// Analysis Control
bool ai_controller_start_analysis(AiController* controller, bool use_custom, const char* custom_path);
void ai_controller_stop_analysis(AiController* controller, bool free_engine);

#endif // AI_CONTROLLER_H

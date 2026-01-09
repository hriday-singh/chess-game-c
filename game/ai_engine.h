#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include <stdbool.h>

typedef struct {
    int depth;         // derived from ELO in ELO-mode, or from advanced mode
    int move_time_ms;  // derived from ELO in ELO-mode, or from advanced mode
} AiDifficultyParams;

// Opaque handle for an engine instance
typedef struct EngineHandle EngineHandle;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the internal Stockfish engine.
 */
EngineHandle* ai_engine_init_internal(void);

/**
 * Initializes an external engine from a binary path.
 */
EngineHandle* ai_engine_init_external(const char* binary_path);

/**
 * Cleans up and shuts down the engine instance.
 */
void ai_engine_cleanup(EngineHandle* handle);

/**
 * Sends a UCI command to the engine.
 */
void ai_engine_send_command(EngineHandle* handle, const char* command);

/**
 * Non-blocking check for a response from the engine.
 * Returns a string that must be freed with ai_engine_free_response, or NULL.
 */
char* ai_engine_try_get_response(EngineHandle* handle);

/**
 * Frees a response string returned by ai_engine_try_get_response.
 */
void ai_engine_free_response(char* response);

/**
 * Blocking wait for the 'bestmove' response after a 'go' command.
 */
char* ai_engine_wait_for_bestmove(EngineHandle* handle);

/**
 * Tests if a binary is a valid UCI engine.
 * Returns true if the engine responds to "uci" with "uciok".
 */
bool ai_engine_test_binary(const char* binary_path);

/**
 * Sends a 'setoption name <name> value <value>' command.
 */
void ai_engine_set_option(EngineHandle* handle, const char* name, const char* value);

/**
 * Maps ELO rating to Stockfish parameters.
 */
AiDifficultyParams ai_get_difficulty_params(int elo);

#ifdef __cplusplus
}
#endif

#endif // AI_ENGINE_H

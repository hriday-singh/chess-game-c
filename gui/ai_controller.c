#include "ai_controller.h"
#include <string.h>
#include <stdio.h>
#include "move.h"
#include <stdbool.h>

static bool debug_mode = false;

struct _AiController {
    GameLogic* logic;
    AiDialog* ai_dialog;

    EngineHandle* internal_engine;
    EngineHandle* custom_engine;

    bool ai_thinking;

    // Callbacks
    AiMoveReadyCallback move_cb;
    gpointer move_cb_data;

    bool destroyed;
    guint64 think_gen;
};

typedef struct {
    AiController* controller;
    char* fen;
    AiDifficultyParams params;
    EngineHandle* engine;
    char* nnue_path;
    bool nnue_enabled;
    AiMoveReadyCallback callback;
    gpointer user_data;
    guint64 gen;
} AiTaskData;

typedef struct {
    AiController* controller;
    char* fen;
    char* bestmove;
    AiMoveReadyCallback callback;
    gpointer user_data;
    guint64 gen;
} AiResultData;

static int g_ai_move_delay_ms = 250;

static gboolean apply_ai_move_idle(gpointer user_data) {
    AiResultData* result = (AiResultData*)user_data;
    if (!result) return FALSE;

    AiController* controller = result->controller;

    if (!controller || controller->destroyed || !controller->logic) goto cleanup;

    if (result->gen != controller->think_gen) goto cleanup;

    char current_fen[256];
    gamelogic_generate_fen(controller->logic, current_fen, sizeof(current_fen));

    if (strcmp(current_fen, result->fen) != 0) {
        controller->ai_thinking = false;
        goto cleanup;
    }

    if (result->bestmove) {
        const char* move_ptr = result->bestmove;

        if (strcmp(move_ptr, "(none)") == 0 || strcmp(move_ptr, "0000") == 0) {
            controller->ai_thinking = false;
            goto cleanup;
        }

        if (strlen(move_ptr) >= 4) {
            int c1 = move_ptr[0] - 'a', r1 = 8 - (move_ptr[1] - '0');
            int c2 = move_ptr[2] - 'a', r2 = 8 - (move_ptr[3] - '0');
            Move* m = move_create((uint8_t)(r1 * 8 + c1), (uint8_t)(r2 * 8 + c2));
            if (strlen(move_ptr) >= 5) {
                switch (move_ptr[4]) {
                    case 'q': m->promotionPiece = PIECE_QUEEN; break;
                    case 'r': m->promotionPiece = PIECE_ROOK; break;
                    case 'b': m->promotionPiece = PIECE_BISHOP; break;
                    case 'n': m->promotionPiece = PIECE_KNIGHT; break;
                }
            }
            controller->ai_thinking = false;
            if (result->callback) result->callback(m, result->user_data);
            else move_free(m);
        }
    } else {
        controller->ai_thinking = false;
    }

cleanup:
    g_free(result->fen);
    g_free(result->bestmove);
    g_free(result);
    return FALSE;
}

static gpointer ai_think_thread(gpointer user_data) {
    AiTaskData* data = (AiTaskData*)user_data;
    AiController* controller = data->controller;

    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", data->fen);

    if (debug_mode) printf("[AI Thread] Engine Setup: FEN=%s\n", data->fen);

    if (data->nnue_enabled && data->nnue_path) {
        ai_engine_set_option(data->engine, "Use NNUE", "true");
        ai_engine_set_option(data->engine, "EvalFile", data->nnue_path);
    }

    // Stop and drain stale output
    ai_engine_send_command(data->engine, "stop");

    if (debug_mode) printf("[AI Thread] Draining engine output...\n");
    int drained = 0;
    while (true) {
        char* stale = ai_engine_try_get_response(data->engine);
        if (!stale) break;
        if (debug_mode && drained < 5) printf("[AI Thread] Drained stale: %s", stale);
        drained++;
        ai_engine_free_response(stale);
    }
    if (debug_mode && drained > 0) printf("[AI Thread] Drained %d stale messages.\n", drained);

    ai_engine_send_command(data->engine, pos_cmd);

    char go_cmd[128];
    if (data->params.depth > 0) snprintf(go_cmd, sizeof(go_cmd), "go depth %d", data->params.depth);
    else snprintf(go_cmd, sizeof(go_cmd), "go movetime %d", data->params.move_time_ms);
    ai_engine_send_command(data->engine, go_cmd);

    if (debug_mode) printf("[AI Thread] Thinking Command Sent: %s\n", go_cmd);

    char* bestmove_str = ai_engine_wait_for_bestmove(data->engine);

    // Stale gen check
    if (!controller || controller->destroyed || data->gen != controller->think_gen) {
        if (debug_mode) {
            printf("[AI Thread] Result discarded: Stale generation (Data: %lu, Ctrl: %lu). Bestmove: %s\n",
                   (unsigned long)data->gen,
                   (unsigned long)(controller ? controller->think_gen : 0),
                   bestmove_str ? bestmove_str : "NULL");
        }
        if (bestmove_str) ai_engine_free_response(bestmove_str);
        g_free(data->fen);
        if (data->nnue_path) g_free(data->nnue_path);
        g_free(data);
        return NULL;
    }

    if (debug_mode) printf("[AI Thread] Received Bestmove for FEN '%s': %s\n", data->fen, bestmove_str ? bestmove_str : "NULL");

    if (bestmove_str && strlen(bestmove_str) > 9) {
        AiResultData* result = g_new0(AiResultData, 1);
        result->controller = controller;
        result->fen = data->fen;
        result->bestmove = g_strdup(bestmove_str + 9);
        result->callback = data->callback;
        result->user_data = data->user_data;
        result->gen = data->gen;

        ai_engine_free_response(bestmove_str);
        if (data->nnue_path) g_free(data->nnue_path);
        g_free(data);

        g_timeout_add(g_ai_move_delay_ms, apply_ai_move_idle, result);
    } else {
        if (controller && !controller->destroyed && data->gen == controller->think_gen) {
            controller->ai_thinking = false;
        }
        if (bestmove_str) ai_engine_free_response(bestmove_str);
        g_free(data->fen);
        if (data->nnue_path) g_free(data->nnue_path);
        g_free(data);
    }

    return NULL;
}

AiController* ai_controller_new(GameLogic* logic, AiDialog* ai_dialog) {
    if (!logic || !ai_dialog) return NULL;

    AiController* controller = g_new0(AiController, 1);
    controller->logic = logic;
    controller->ai_dialog = ai_dialog;

    return controller;
}

void ai_controller_free(AiController* controller) {
    if (!controller) return;

    controller->destroyed = true;

    ai_controller_stop(controller);

    if (controller->internal_engine) ai_engine_cleanup(controller->internal_engine);
    if (controller->custom_engine) ai_engine_cleanup(controller->custom_engine);

    g_free(controller);
}

void ai_controller_request_move(AiController* controller,
                                bool use_custom,
                                AiDifficultyParams params,
                                const char* custom_path,
                                AiMoveReadyCallback callback,
                                gpointer user_data) {
    if (controller->ai_thinking) return;

    char* fen = g_malloc(256);
    gamelogic_generate_fen(controller->logic, fen, 256);

    EngineHandle* engine = NULL;
    if (use_custom) {
        if (!controller->custom_engine && custom_path) {
            controller->custom_engine = ai_engine_init_external(custom_path);
        }
        engine = controller->custom_engine;
    } else {
        if (!controller->internal_engine) {
            controller->internal_engine = ai_engine_init_internal();
        }
        engine = controller->internal_engine;
    }

    if (debug_mode) {
        printf("[AI Controller] Request Move: Custom=%d, CustomPath=%s, Depth=%d, Time=%d\n",
               use_custom, custom_path ? custom_path : "NULL", params.depth, params.move_time_ms);
    }

    if (!engine) {
        g_free(fen);
        return;
    }

    controller->ai_thinking = true;
    controller->think_gen++;

    AiTaskData* data = g_new0(AiTaskData, 1);
    data->controller = controller;
    data->fen = fen;
    data->params = params;
    data->engine = engine;
    data->callback = callback;
    data->user_data = user_data;
    data->gen = controller->think_gen;

    bool nnue_enabled = false;
    const char* nn_path = ai_dialog_get_nnue_path(controller->ai_dialog, &nnue_enabled);
    if (nn_path) data->nnue_path = g_strdup(nn_path);
    data->nnue_enabled = nnue_enabled;

    GThread* thread = g_thread_new("ai-think", ai_think_thread, data);
    g_thread_unref(thread);
}

void ai_controller_stop(AiController* controller) {
    if (!controller) return;

    controller->think_gen++;

    if (controller->internal_engine) ai_engine_send_command(controller->internal_engine, "stop");
    if (controller->custom_engine) ai_engine_send_command(controller->custom_engine, "stop");

    controller->ai_thinking = false;
}

bool ai_controller_is_thinking(AiController* controller) {
    return controller->ai_thinking;
}

void ai_controller_set_params(AiController* controller, AiDifficultyParams params) {
    (void)controller; (void)params;
}
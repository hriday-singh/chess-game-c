#include "ai_controller.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "move.h"
#include <stdbool.h>

static bool debug_mode = false;

struct _AiController {
    GameLogic* logic;
    AiDialog* ai_dialog;
    
    EngineHandle* internal_engine;
    EngineHandle* custom_engine;
    EngineHandle* analysis_engine;
    
    bool ai_thinking;
    bool analysis_running;
    int last_score;
    bool last_is_mate;
    int last_mate_distance;
    
    // Callbacks
    AiMoveReadyCallback move_cb;
    gpointer move_cb_data;
    
    AiEvalUpdateCallback eval_cb;
    gpointer eval_cb_data;
    
    // Rating State
    double last_eval;
    bool rating_pending;
    
    // State to avoid unnecessary engine restarts
    bool analysis_is_custom;
    char* analysis_custom_path; 
};

typedef struct {
    AiController* controller;
    int score;
    bool is_mate;
    int mate_distance;
    char* best_move;
} AiDispatchData;

// Thread-safe UI update
static gboolean dispatch_eval_update(gpointer user_data) {
    AiDispatchData* data = (AiDispatchData*)user_data;
    AiController* ctrl = data->controller;
    
    // Check validity
    if (!ctrl) {
        g_free(data->best_move);
        g_free(data);
        return FALSE;
    }
    
    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis) {
        g_free(data->best_move);
        g_free(data);
        return FALSE;
    }

    AiStats stats = {0};
    stats.score = data->score;
    stats.is_mate = data->is_mate;
    stats.mate_distance = data->mate_distance;
    stats.best_move = data->best_move;

    double evaluation = (double)data->score / 100.0;
    
    // Rating Logic
    if (ctrl->rating_pending && cfg->show_move_rating) {
        int count = gamelogic_get_move_count(ctrl->logic);
        stats.move_number = count - 1;
        double loss = 0;
        if (count % 2 == 1) { // White just moved
            loss = ctrl->last_eval - evaluation;
        } else if (count > 0) { // Black just moved
            loss = evaluation - ctrl->last_eval;
        }

        const char* rating = "Good";
        const char* reason = NULL;

        if (loss > 3.0) { rating = "Blunder"; reason = "Lost major advantage"; }
        else if (loss > 1.5) { rating = "Mistake"; reason = "Poor strategy"; }
        else if (loss > 0.6) { rating = "Inaccuracy"; }
        else if (loss < 0.1) { rating = "Best"; }

        if (data->is_mate && abs(data->mate_distance) <= 5) reason = "Allowed mate threat";
        
        int w_h = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_WHITE);
        int b_h = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_BLACK);
        if ((count % 2 == 1 && w_h > 0) || (count % 2 == 0 && b_h > 0)) {
            reason = "Hung piece";
        }

        stats.rating_label = rating;
        stats.rating_reason = reason;
        
        ctrl->rating_pending = false;
    }
    ctrl->last_eval = evaluation;

    // Hanging Pieces
    if (cfg->show_hanging_pieces) {
        stats.white_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_WHITE);
        stats.black_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_BLACK);
    }

    // Dispatch via callback
    if (ctrl->eval_cb) {
        ctrl->eval_cb(&stats, ctrl->eval_cb_data);
    }
    
    g_free(data->best_move);
    g_free(data);
    return FALSE;
}

typedef struct {
    AiController* controller;
    char* fen;
    AiDifficultyParams params;
    EngineHandle* engine;
    char* nnue_path;
    bool nnue_enabled;
    AiMoveReadyCallback callback;
    gpointer user_data;
} AiTaskData;

typedef struct {
    AiController* controller;
    char* fen;
    char* bestmove;
    AiMoveReadyCallback callback;
    gpointer user_data;
} AiResultData;

static int g_ai_move_delay_ms = 250;

static gboolean apply_ai_move_idle(gpointer user_data) {
    AiResultData* result = (AiResultData*)user_data;
    AiController* controller = result->controller;
    
    char current_fen[256];
    gamelogic_generate_fen(controller->logic, current_fen, sizeof(current_fen));
    
    if (strcmp(current_fen, result->fen) != 0) {
        controller->ai_thinking = false;
        goto cleanup;
    }

    if (result->bestmove) {
        const char* move_ptr = result->bestmove;
        if (strlen(move_ptr) >= 4) {
            int c1 = move_ptr[0] - 'a', r1 = 8 - (move_ptr[1] - '0');
            int c2 = move_ptr[2] - 'a', r2 = 8 - (move_ptr[3] - '0');
            Move* m = move_create(r1, c1, r2, c2);
            if (strlen(move_ptr) >= 5) {
                switch (move_ptr[4]) {
                    case 'q': m->promotionPiece = PIECE_QUEEN; break;
                    case 'r': m->promotionPiece = PIECE_ROOK; break;
                    case 'b': m->promotionPiece = PIECE_BISHOP; break;
                    case 'n': m->promotionPiece = PIECE_KNIGHT; break;
                }
            }
            controller->ai_thinking = false;
            if (result->callback) {
                result->callback(m, result->user_data);
            }
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
    
    ai_engine_send_command(data->engine, "ucinewgame");
    
    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", data->fen);
    ai_engine_send_command(data->engine, pos_cmd);
    
    if (data->nnue_enabled && data->nnue_path) {
        ai_engine_set_option(data->engine, "Use NNUE", "true");
        ai_engine_set_option(data->engine, "EvalFile", data->nnue_path);
    }
    
    char go_cmd[128];
    if (data->params.depth > 0) {
        snprintf(go_cmd, sizeof(go_cmd), "go depth %d", data->params.depth);
    } else {
        snprintf(go_cmd, sizeof(go_cmd), "go movetime %d", data->params.move_time_ms);
    }
    ai_engine_send_command(data->engine, go_cmd);
    
    if (debug_mode) printf("[AI Thread] Thinking Command Sent: %s\n", go_cmd);

    char* bestmove_str = ai_engine_wait_for_bestmove(data->engine);
    
    if (bestmove_str && strlen(bestmove_str) > 9) {
        AiResultData* result = g_new0(AiResultData, 1);
        result->controller = controller;
        result->fen = data->fen; 
        result->bestmove = g_strdup(bestmove_str + 9);
        result->callback = data->callback;
        result->user_data = data->user_data;
        
        ai_engine_free_response(bestmove_str);
        if (data->nnue_path) g_free(data->nnue_path);
        g_free(data);
        
        g_timeout_add(g_ai_move_delay_ms, apply_ai_move_idle, result);
    } else {
        controller->ai_thinking = false;
        ai_engine_free_response(bestmove_str);
        g_free(data->fen);
        if (data->nnue_path) g_free(data->nnue_path);
        g_free(data);
    }
    
    return NULL;
}

static void parse_info_line(AiController* controller, const char* line) {
    if (!line) return;
    
    // Search for "score cp" or "score mate"
    const char* score_ptr = strstr(line, "score ");
    if (score_ptr) {
        score_ptr += 6; // skip "score "
        if (strncmp(score_ptr, "cp ", 3) == 0) {
            controller->last_score = atoi(score_ptr + 3);
            controller->last_is_mate = false;
            controller->last_mate_distance = 0;
        } else if (strncmp(score_ptr, "mate ", 5) == 0) {
            controller->last_mate_distance = atoi(score_ptr + 5);
            controller->last_score = (controller->last_mate_distance > 0) ? 30000 : -30000;
            controller->last_is_mate = true;
        }
        
        // Search for PV (Principal Variation) for best move
        char last_pv[16] = "";
        const char* pv_ptr = strstr(line, " pv ");
        if (pv_ptr) {
            pv_ptr += 4; // skip " pv "
            int i = 0;
            while (pv_ptr[i] && pv_ptr[i] != ' ' && i < 15) {
                last_pv[i] = pv_ptr[i];
                i++;
            }
            last_pv[i] = '\0';
        }

        if (last_pv[0] != '\0') {
             // We could store best move if needed
        }

        // Dispatch to main thread
        AiDispatchData* dispatch = g_new0(AiDispatchData, 1);
        dispatch->controller = controller;
        dispatch->score = controller->last_score;
        dispatch->is_mate = controller->last_is_mate;
        dispatch->mate_distance = controller->last_mate_distance;
        dispatch->best_move = (last_pv[0] != '\0') ? g_strdup(last_pv) : NULL;
        
        if (debug_mode) printf("[AI Controller] Parsed Info: Score=%d, Mate=%d, BestMove=%s\n", 
                      controller->last_score, controller->last_mate_distance, last_pv);
        
        g_idle_add(dispatch_eval_update, dispatch);
    }
}

static gpointer ai_analysis_thread(gpointer user_data) {
    AiController* controller = (AiController*)user_data;
    
    while (controller->analysis_running) {
        if (controller->ai_thinking) {
            g_usleep(100000); // Wait 100ms if engine is busy with move
            continue;
        }
        
        char* response = ai_engine_try_get_response(controller->analysis_engine);
        if (response) {
            // Debug every line
            if (debug_mode) printf("[AI Thread] Piped: %s", response); 

            if (strncmp(response, "info ", 5) == 0) {
                parse_info_line(controller, response);
            }
            ai_engine_free_response(response);
        } else {
            g_usleep(50000); // 50ms polling
        }
    }
    
    return NULL;
}

AiController* ai_controller_new(GameLogic* logic, AiDialog* ai_dialog) {
    AiController* ctrl = g_new0(AiController, 1);
    ctrl->logic = logic;
    ctrl->ai_dialog = ai_dialog;
    return ctrl;
}

void ai_controller_free(AiController* controller) {
    if (!controller) return;
    controller->analysis_running = false;
    if (controller->internal_engine) ai_engine_cleanup(controller->internal_engine);
    if (controller->custom_engine) ai_engine_cleanup(controller->custom_engine);
    if (controller->analysis_engine) ai_engine_cleanup(controller->analysis_engine);
    if (controller->analysis_custom_path) g_free(controller->analysis_custom_path);
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
    
    if (debug_mode) printf("[AI Controller] Request Move: Custom=%d, CustomPath=%s, Depth=%d, Time=%d\n",
                  use_custom, custom_path ? custom_path : "NULL", params.depth, params.move_time_ms);
    
    if (!engine) {
        g_free(fen);
        return;
    }
    
    controller->ai_thinking = true;
    
    AiTaskData* data = g_new0(AiTaskData, 1);
    data->controller = controller;
    data->fen = fen;
    data->params = params;
    data->engine = engine;
    data->callback = callback;
    data->user_data = user_data;
    
    // Check NNUE settings from dialog
    bool nnue_enabled = false;
    const char* nn_path = ai_dialog_get_nnue_path(controller->ai_dialog, &nnue_enabled);
    if (nn_path) data->nnue_path = g_strdup(nn_path);
    data->nnue_enabled = nnue_enabled;
    
    g_thread_new("ai-think", ai_think_thread, data);
}

void ai_controller_stop(AiController* controller) {
    if (controller->ai_thinking) {
        if (controller->internal_engine) ai_engine_send_command(controller->internal_engine, "stop");
        if (controller->custom_engine) ai_engine_send_command(controller->custom_engine, "stop");
    }
}

#include "config_manager.h"

// Analysis Controls
bool ai_controller_start_analysis(AiController* controller, bool use_custom, const char* custom_path) {
    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis) return false;

    // Check if we can reuse the existing engine
    if (debug_mode) printf("[AI Controller] Start Analysis. UseCustom=%d, Path=%s\n", use_custom, custom_path ? custom_path : "NULL");
    bool reuse_engine = false;
    if (controller->analysis_engine && controller->analysis_running) {
        if (use_custom == controller->analysis_is_custom) {
            if (!use_custom) {
                reuse_engine = true; // Internal stays internal
            } else if (custom_path && controller->analysis_custom_path && 
                       strcmp(custom_path, controller->analysis_custom_path) == 0) {
                reuse_engine = true; // Same custom engine path
            }
        }
    }

    if (reuse_engine) {
        // Just stop calculation but keep process alive
        ai_engine_send_command(controller->analysis_engine, "stop");
    } else {
        // Full restart needed
        if (controller->analysis_running) {
            ai_controller_stop_analysis(controller);
        }
    
        if (use_custom && custom_path) {
            controller->analysis_engine = ai_engine_init_external(custom_path);
            if (controller->analysis_custom_path) g_free(controller->analysis_custom_path);
            controller->analysis_custom_path = g_strdup(custom_path);
            controller->analysis_is_custom = true;
        } else {
            controller->analysis_engine = ai_engine_init_internal();
            controller->analysis_is_custom = false;
        }
        
        if (!controller->analysis_engine) return false;
        
        // Initial setup for new engine
        ai_engine_send_command(controller->analysis_engine, "uci");
        ai_engine_send_command(controller->analysis_engine, "isready");
        ai_engine_send_command(controller->analysis_engine, "ucinewgame");
    }
    
    controller->analysis_running = true;
    
    char fen[256];
    gamelogic_generate_fen(controller->logic, fen, 256);
    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", fen);
    ai_engine_send_command(controller->analysis_engine, pos_cmd);
    ai_engine_send_command(controller->analysis_engine, pos_cmd);
    ai_engine_send_command(controller->analysis_engine, "go infinite");
    
    if (debug_mode) printf("[AI Controller] Analysis started: %s\n", pos_cmd);
    
    // Only start thread if not already running (reusing engine doesn't kill thread, 
    // BUT we need to be careful. ai_controller_stop_analysis sets analysis_running=false
    // which exits the loop. So if we reuse, we must ensure loop continues or restart it.
    // My previous logic for reuse: "Just stop calculation". 
    // The thread 'ai_analysis_thread' loops on 'controller->analysis_running'.
    // If we kept 'analysis_running=true', the thread is still alive.
    // So we don't need to start a new thread.
    
    if (!reuse_engine) {
        g_thread_new("ai-analysis", ai_analysis_thread, controller);
    }
    return true;
}

void ai_controller_stop_analysis(AiController* controller) {
    if (debug_mode) printf("[AI Controller] Analysis Stopped.\n");
    controller->analysis_running = false;
    if (controller->analysis_engine) {
        ai_engine_send_command(controller->analysis_engine, "stop");
        ai_engine_cleanup(controller->analysis_engine);
        controller->analysis_engine = NULL;
    }
}

void ai_controller_set_nnue(AiController* controller, bool enabled, const char* path) {
    if (controller->analysis_engine) {
        ai_engine_set_option(controller->analysis_engine, "Use NNUE", enabled ? "true" : "false");
        if (enabled && path) {
            ai_engine_set_option(controller->analysis_engine, "EvalFile", path);
        }
    }
}

bool ai_controller_is_thinking(AiController* controller) {
    return controller->ai_thinking;
}

void ai_controller_get_evaluation(AiController* controller, int* score, bool* is_mate) {
    if (score) *score = controller->last_score;
    if (is_mate) *is_mate = controller->last_is_mate;
}

void ai_controller_set_params(AiController* controller, AiDifficultyParams params) {
    (void)controller; (void)params;
}

void ai_controller_set_eval_callback(AiController* controller, AiEvalUpdateCallback callback, gpointer user_data) {
    if (!controller) return;
    controller->eval_cb = callback;
    controller->eval_cb_data = user_data;
}

void ai_controller_set_rating_pending(AiController* controller, bool pending) {
    if (controller) controller->rating_pending = pending;
}

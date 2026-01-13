#include "ai_controller.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "move.h"
#include <stdbool.h>

static bool debug_mode = true;

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
    char last_analysis_fen[256]; 
    
    // Thread management
    GThread* analysis_thread;
    
    // Rating Baseline
    double eval_before_move;
    bool rating_scan_active;
    
    // Throttling
    gint64 last_dispatch_time;
    int last_dispatch_score;
    bool last_dispatch_mate;
    
    // Safety
    bool destroyed;
    guint64 think_gen;
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
    if (!data) return FALSE;
    
    AiController* ctrl = data->controller;
    
    // Check validity
    if (!ctrl) {
        g_free(data->best_move);
        g_free(data);
        return FALSE;
    }
    
    if (ctrl->destroyed) {
        g_free(data->best_move);
        g_free(data);
        return FALSE;
    }
    
    // Critical Guard: Live Analysis Enabled?
    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis) {
        g_free(data->best_move);
        g_free(data);
        return FALSE;
    }
    
    // Prepare stats
    AiStats stats = {0};
    stats.score = data->score;
    stats.is_mate = data->is_mate;
    stats.mate_distance = data->mate_distance;
    
    // Safety: Duplicate string for the callback to own/use violently if it wants
    // Then we free our own copy regardless.
    if (data->best_move) {
        stats.best_move = g_strdup(data->best_move);
    }
    
    // Hanging Pieces (only if enabled)
    if (cfg && cfg->show_hanging_pieces && ctrl->logic) {
        stats.white_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_WHITE);
        stats.black_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_BLACK);
    }
    
    // Rating Logic
    if (ctrl->eval_cb) {
        // Apply Feature Toggles
        if (cfg && !cfg->show_mate_warning) {
             stats.is_mate = false;
             stats.mate_distance = 0;
        }
        
        ctrl->eval_cb(&stats, ctrl->eval_cb_data);
    }
    
    // Cleanup
    g_free((char*)stats.best_move); // Free the copy we made for the callback
    g_free(data->best_move); // Free the original from thread
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
    
    // Safety Guard
    if (!controller || controller->destroyed || !controller->logic) {
        goto cleanup;
    }
    
    // Generation Guard (Stale result check)
    if (result->gen != controller->think_gen) {
        goto cleanup;
    }
    
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
            } else {
                move_free(m); 
            }
        }
    } else {
        controller->ai_thinking = false;
    }
    
    // Restart analysis if it was running (and using shared engine, or just to update position)
    if (controller->analysis_running) {
        // Delay slightly to let things settle? No, just fire.
        // We need to re-invoke start logic to send new position and 'go infinite'
        // We use the cached settings
        ai_controller_start_analysis(controller, controller->analysis_is_custom, controller->analysis_custom_path);
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
    
    // ai_engine_send_command(data->engine, "ucinewgame"); // Optimization: Removed per user request
    
    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", data->fen);
    
    if (debug_mode) printf("[AI Thread] Engine Setup: FEN=%s\n", data->fen);

    if (data->nnue_enabled && data->nnue_path) {
        ai_engine_set_option(data->engine, "Use NNUE", "true");
        ai_engine_set_option(data->engine, "EvalFile", data->nnue_path);
        // ai_engine_send_command(data->engine, "isready"); // Ensure options applied
    }
    
    // Safety: If sharing engine with analysis, ensure analysis is stopped.
    // AND DRAIN the output queue to ensure we don't pick up a stale bestmove from the analysis stop!
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
    if (data->params.depth > 0) {
        snprintf(go_cmd, sizeof(go_cmd), "go depth %d", data->params.depth);
    } else {
        snprintf(go_cmd, sizeof(go_cmd), "go movetime %d", data->params.move_time_ms);
    }
    ai_engine_send_command(data->engine, go_cmd);
    
    if (debug_mode) printf("[AI Thread] Thinking Command Sent: %s\n", go_cmd);

    // Debug: Check if engine is still valid
    if (controller && controller->destroyed) {
         if (debug_mode) printf("[AI Thread] Controller destroyed immediately after send!\n");
    }

    char* bestmove_str = ai_engine_wait_for_bestmove(data->engine);
    
    // Stale result check (optimization)
    if (!controller || controller->destroyed || data->gen != controller->think_gen) {
        if (debug_mode) printf("[AI Thread] Result discarded: Stale generation (Data: %lu, Ctrl: %lu). Bestmove: %s\n", (unsigned long)data->gen, (unsigned long)(controller ? controller->think_gen : 0), bestmove_str ? bestmove_str : "NULL");
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

static void parse_info_line(AiController* controller, char* line) {
    if (!controller || !line) return;
    
    // Standard UCI parsing: "info score cp 50 mate 0 pv e2e4 ..."
    int score = 0;
    bool is_mate = false;
    int mate_dist = 0;
    char best_move[32] = {0};
    bool found_score = false;
    
    char* p;
    // Simple manual parsing to avoid strict tok checks
    if ((p = strstr(line, "score cp"))) {
        sscanf(p + 9, "%d", &score);
        found_score = true;
    } else if ((p = strstr(line, "score mate"))) {
        sscanf(p + 11, "%d", &mate_dist);
        is_mate = true;
        // Fix: Set large score for mate
        score = (mate_dist > 0) ? 30000 : -30000;
        found_score = true;
    }
    
    if (found_score) {
        if ((p = strstr(line, " pv "))) {
             char* m = p + 4;
             int i = 0;
             while (*m && *m != ' ' && i < 31) {
                 best_move[i++] = *m++;
             }
             best_move[i] = '\0';
        }
    }
    
    if (!found_score) return;
    
    // --- Throttling Logic ---
    gint64 now = g_get_monotonic_time();
    bool urgent = false;
    
    if (is_mate != controller->last_dispatch_mate) urgent = true;
    if (!urgent && abs(score - controller->last_dispatch_score) > 15) urgent = true;
    
    // 200ms throttle
    if (!urgent && (now - controller->last_dispatch_time) < 200000) {
        return; 
    }
    
    // Dispatch
    controller->last_dispatch_time = now;
    controller->last_dispatch_score = score;
    controller->last_dispatch_mate = is_mate;
    controller->last_score = score; // Sync for other threads/accessors
    controller->last_is_mate = is_mate;
    controller->last_mate_distance = mate_dist;
    
    AiDispatchData* data = g_new0(AiDispatchData, 1);
    data->controller = controller;
    data->score = score;
    data->is_mate = is_mate;
    data->mate_distance = mate_dist;
    if (best_move[0]) data->best_move = g_strdup(best_move);
    
    g_idle_add(dispatch_eval_update, data);
}

static gpointer ai_analysis_thread(gpointer user_data) {
    AiController* controller = (AiController*)user_data;
    
    while (controller->analysis_running) {
        if (controller->ai_thinking) {
            g_usleep(100000); // Wait 100ms if engine is busy with move
            continue;
        }
        
        if (!controller->analysis_engine) {
            g_usleep(50000); 
            continue; 
        }
        
        char* response = ai_engine_try_get_response(controller->analysis_engine);
        if (response) {
            // Debug every line
            // if (debug_mode) printf("[AI Analysis] Piped: %s", response);  

            if (strncmp(response, "info ", 5) == 0) {
                parse_info_line(controller, response);
            }
            ai_engine_free_response(response);
        } else {
            g_usleep(50000); // 50ms polling
        }
    }
    
    if (debug_mode) printf("[AI Analysis] Thread exiting...\n");
    return NULL;
}

static void on_ai_settings_changed(void* user_data) {
    AiController* controller = (AiController*)user_data;
    if (!controller) return;
    
    AppConfig* cfg = config_get();
    if (cfg && cfg->enable_live_analysis) {
        // Safe restart/update
        ai_controller_start_analysis(controller, cfg->analysis_use_custom, cfg->custom_engine_path);
    } else {
        ai_controller_stop_analysis(controller, true); // Clean release
    }
}

// Constructor
AiController* ai_controller_new(GameLogic* logic, AiDialog* ai_dialog) {
    if (!logic || !ai_dialog) return NULL;
    
    AiController* controller = g_new0(AiController, 1);
    controller->logic = logic;
    controller->ai_dialog = ai_dialog;
    
    // Listen for changes
    ai_dialog_set_settings_changed_callback(ai_dialog, on_ai_settings_changed, controller);
    
    // Init values
    controller->last_dispatch_score = 999999;
    
    return controller;
}

void ai_controller_free(AiController* controller) {
    if (!controller) return;
    
    controller->destroyed = true; // Mark as destroying to prevent callbacks
    
    // Safety: Stop threads first!
    ai_controller_stop(controller); // Stops move thinking
    ai_controller_stop_analysis(controller, true); // Stops analysis AND cleans up engine
    
    // internal_engine and custom_engine are independent from analysis_engine now
    if (controller->internal_engine) {
        ai_engine_cleanup(controller->internal_engine);
    }
    
    if (controller->custom_engine) {
        ai_engine_cleanup(controller->custom_engine);
    }
    
    // analysis_engine was cleaned up by stop_analysis(..., true)
    
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
    
    // Set flag IMMEDIATELY to stop analysis thread from interfering
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
    
    // Check NNUE settings from dialog
    bool nnue_enabled = false;
    const char* nn_path = ai_dialog_get_nnue_path(controller->ai_dialog, &nnue_enabled);
    if (nn_path) data->nnue_path = g_strdup(nn_path);
    data->nnue_enabled = nnue_enabled;
    
    // Start Thread
    // controller->ai_thinking = true; // Removed redundant assignment
    GThread* thread = g_thread_new("ai-think", ai_think_thread, data);
    g_thread_unref(thread);
}

void ai_controller_stop(AiController* controller) {
    if (!controller) return;
    
    controller->think_gen++; // Always invalidate pending results
    
    if (controller->internal_engine) ai_engine_send_command(controller->internal_engine, "stop");
    if (controller->custom_engine) ai_engine_send_command(controller->custom_engine, "stop");
    
    controller->ai_thinking = false;
}

#include "config_manager.h"

// Analysis Controls
// Start continuous analysis (Independent Engine Instance)
bool ai_controller_start_analysis(AiController* controller, bool use_custom, const char* custom_path) {
    if (debug_mode) printf("[AI Controller] Start Analysis Requested (Custom=%d)\n", use_custom);
    if (!controller) return false;
    
    // 0. Guard: Configuration Check
    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis) {
        if (controller->analysis_running) {
            ai_controller_stop_analysis(controller, true); // Always free on disable
        }
        return false;
    }

    // 1. Validate inputs
    if (use_custom && (!custom_path || strlen(custom_path) == 0)) {
        return false; 
    }
    
    // 2. Generate Current FEN
    char current_fen[256];
    gamelogic_generate_fen(controller->logic, current_fen, 256);
    
    // 3. Check for Reuse Suitability
    bool engine_exists = (controller->analysis_engine != NULL);
    bool thread_active = (controller->analysis_thread != NULL);
    bool type_match = (use_custom == controller->analysis_is_custom);
    bool path_match = true;
    
    // Fix: Strict path match logic
    if (use_custom) {
        path_match = (controller->analysis_custom_path && strcmp(custom_path, controller->analysis_custom_path) == 0);
    } else {
        path_match = true; // Internal engine always matches internal request
    }
    
    bool can_reuse_engine = (engine_exists && thread_active && type_match && path_match);
    
    // 4. Check for Position Match (Optimization)
    if (can_reuse_engine) {
        if (strcmp(controller->last_analysis_fen, current_fen) == 0 && controller->analysis_running) {
             return true; // Exact match, do nothing
        }
    }
    
    // 5. Initialize Engine (if reuse not possible)
    if (!can_reuse_engine) {
        // Strict Stop & Free if we are switching engines
        
        if (engine_exists || controller->analysis_running) {
             ai_controller_stop_analysis(controller, true); // Free the old engine
        }
        
        EngineHandle* new_engine = NULL;
        
        // Start New Instance OR Share Internal
        if (use_custom) {
            new_engine = ai_engine_init_external(custom_path);
        } else {
            // SHARED INTERNAL ENGINE LOGIC
            if (!controller->internal_engine) {
                controller->internal_engine = ai_engine_init_internal();
            }
            new_engine = controller->internal_engine;
        }
        
        controller->analysis_engine = new_engine;
        
        if (!controller->analysis_engine) return false;
        
        // Init Commands
        // If shared, maybe don't reset? But ucinewgame is good.
        // For internal, ucinewgame might clear hash?
        // Let's send it.
        if (use_custom || !controller->internal_engine) {
             // Only if distinct? No, always init.
        }
        
        ai_engine_send_command(controller->analysis_engine, "uci");
        // ai_engine_send_command(controller->analysis_engine, "isready");
        // ai_engine_send_command(controller->analysis_engine, "ucinewgame");
        
        // Update Metadata
        controller->analysis_is_custom = use_custom;
        if (controller->analysis_custom_path) g_free(controller->analysis_custom_path);
        controller->analysis_custom_path = use_custom ? g_strdup(custom_path) : NULL;
        
        // Start Thread
        controller->analysis_running = true;
        controller->analysis_thread = g_thread_new("ai-analysis", ai_analysis_thread, controller);
    } else {
        // Reusing: ensure running
        controller->analysis_running = true;
    }

    // 6. Send Position
    g_strlcpy(controller->last_analysis_fen, current_fen, 256);
    
    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", current_fen);
    
    ai_engine_send_command(controller->analysis_engine, "stop");
    ai_engine_send_command(controller->analysis_engine, pos_cmd);
    ai_engine_send_command(controller->analysis_engine, "go infinite");
    
    if (debug_mode) printf("[AI Controller] Analysis Started successfully.\n");
    return true;
}

void ai_controller_stop_analysis(AiController* controller, bool free_engine) {
    if (!controller) return;
    
    // Logical Stop
    controller->analysis_running = false;
    
    // Stop Command
    if (controller->analysis_engine) {
        ai_engine_send_command(controller->analysis_engine, "stop");
    }
    
    // Join Thread
    if (controller->analysis_thread) {
        g_thread_join(controller->analysis_thread);
        controller->analysis_thread = NULL;
    }
    
    // Cleanup Engine if requested
    if (free_engine && controller->analysis_engine) {
        
        // Shared Engine Guard: Don't free if it's the internal singleton
        bool is_shared = (controller->analysis_engine == controller->internal_engine);
        
        if (!is_shared) {
            ai_engine_cleanup(controller->analysis_engine);
        } else {
             if (debug_mode) printf("[AI Controller] Skipping cleanup of shared internal engine.\n");
        }
        
        controller->analysis_engine = NULL;
        
        // Clear state to prevent stale reuse
        controller->last_analysis_fen[0] = '\0';
        controller->analysis_is_custom = false;
        if (controller->analysis_custom_path) {
            g_free(controller->analysis_custom_path);
            controller->analysis_custom_path = NULL;
        }
    }
    // If not freeing, we leave controller->analysis_engine populated for potential reuse
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

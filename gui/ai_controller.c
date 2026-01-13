#include "ai_controller.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "move.h"
#include <stdbool.h>

static bool debug_mode = true;

typedef struct {
    char fen[256];
    int score;           // White perspective centipawns. Mate mapped to +/- 30000.
    bool is_mate;
    int mate_dist;       // White perspective sign after normalization.
    char best_move[32];
    bool valid;
} AiSnapshot;

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

    // snapshots for move rating
    AiSnapshot before_move_snapshot;
    AiSnapshot current_snapshot;             // Dispatched/UI snapshot
    AiSnapshot latest_unthrottled_snapshot;  // Updated on every parse
    bool rating_pending;

    // State to avoid unnecessary engine restarts
    bool analysis_is_custom;
    char* analysis_custom_path;
    char last_analysis_fen[256];

    // Thread management
    GThread* analysis_thread;

    // Throttling
    gint64 last_dispatch_time;
    int last_dispatch_score;
    bool last_dispatch_mate;
    int last_dispatch_mate_dist;

    // Mate Warning Stability
    gint64 mate_expiry_time;
    int last_mate_dist_stable;

    // Sticky Move Rating
    gint64 rating_expiry_time;
    const char* last_rating_label;
    const char* last_rating_reason;

    // Safety
    bool destroyed;
    guint64 think_gen;
};

typedef struct {
    AiController* controller;
    char* fen;
    int score;
    bool is_mate;
    int mate_distance;
    char* best_move;
} AiDispatchData;

// Helper: clamp
static int clamp_i(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// Helper: eval in mover perspective
// Input score must already be White perspective.
static int mover_perspective_eval(bool white_moved, int white_score) {
    // If white moved: mover eval = white score
    // If black moved: mover eval = -white score
    return white_moved ? white_score : -white_score;
}

// Thread-safe UI update
static gboolean dispatch_eval_update(gpointer user_data) {
    AiDispatchData* data = (AiDispatchData*)user_data;
    if (!data) return FALSE;

    AiController* ctrl = data->controller;

    // Check validity
    if (!ctrl || ctrl->destroyed) {
        g_free(data->fen);
        g_free(data->best_move);
        g_free(data);
        return FALSE;
    }

    // Live Analysis Enabled?
    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis) {
        g_free(data->fen);
        g_free(data->best_move);
        g_free(data);
        return FALSE;
    }

    // FEN Matching Guard: analysis belongs to current board position
    char current_fen[256];
    if (ctrl->logic) {
        gamelogic_generate_fen(ctrl->logic, current_fen, sizeof(current_fen));
        if (data->fen && strcmp(current_fen, data->fen) != 0) {
            if (debug_mode) {
                printf("[AI Controller] Discarding stale eval: Data FEN=%s, Current FEN=%s\n",
                       data->fen, current_fen);
            }
            g_free(data->fen);
            g_free(data->best_move);
            g_free(data);
            return FALSE;
        }
    }

    // Update Controller Global State (synced to current position)
    ctrl->last_score = data->score;
    ctrl->last_is_mate = data->is_mate;
    ctrl->last_mate_distance = data->mate_distance;

    // Update snapshots (for UI + rating)
    g_strlcpy(ctrl->current_snapshot.fen, data->fen, 256);
    ctrl->current_snapshot.score = data->score;
    ctrl->current_snapshot.is_mate = data->is_mate;
    ctrl->current_snapshot.mate_dist = data->mate_distance;
    if (data->best_move) g_strlcpy(ctrl->current_snapshot.best_move, data->best_move, 32);
    ctrl->current_snapshot.valid = true;

    // Sticky Mate Logic
    gint64 now = g_get_monotonic_time();
    if (data->is_mate) {
        ctrl->mate_expiry_time = now + 1500000; // 1.5s
        ctrl->last_mate_dist_stable = data->mate_distance;
    }

    // Prepare stats for UI
    AiStats stats = (AiStats){0};
    stats.score = data->score;
    stats.is_mate = data->is_mate;
    stats.mate_distance = data->mate_distance;
    stats.fen = g_strdup(data->fen);

    // Handle sticky mate
    if (!stats.is_mate && now < ctrl->mate_expiry_time) {
        stats.is_mate = true;
        stats.mate_distance = ctrl->last_mate_dist_stable;
    }

    if (data->best_move) {
        stats.best_move = g_strdup(data->best_move);
    }

    // Hanging Pieces
    if (cfg && cfg->show_hanging_pieces && ctrl->logic) {
        stats.white_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_WHITE);
        stats.black_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_BLACK);
    }

    // ---------------- Move Rating ----------------
    if (ctrl->rating_pending && ctrl->before_move_snapshot.valid) {

        // Sanity: must not be the same position
        if (strcmp(ctrl->before_move_snapshot.fen, data->fen) == 0) {
            if (debug_mode) {
                printf("[AI Analysis] Rating skipped: before_fen == after_fen\n");
            }
            ctrl->rating_pending = false;
        } else {

            // Determine mover from BEFORE position (side to move in parent FEN)
            bool white_moved = (strstr(ctrl->before_move_snapshot.fen, " w ") != NULL);

            // Evaluate whether we should rate this move (only human moves)
            bool should_rate = true;
            if (ctrl->logic->gameMode == GAME_MODE_PVC) {
                Player mover = white_moved ? PLAYER_WHITE : PLAYER_BLACK;
                if (ctrl->logic->playerSide != mover) should_rate = false;
            } else if (ctrl->logic->gameMode == GAME_MODE_CVC) {
                should_rate = false;
            }

            // Compute mover-perspective evals (mate already mapped to +/-30000)
            int before_white = clamp_i(ctrl->before_move_snapshot.score, -30000, 30000);
            int after_white  = clamp_i(data->score, -30000, 30000);

            // Also clamp non-mate huge cp to keep things stable
            before_white = clamp_i(before_white, -2000, 2000);
            after_white  = clamp_i(after_white, -2000, 2000);

            int before_mover = mover_perspective_eval(white_moved, before_white);
            int after_mover  = mover_perspective_eval(white_moved, after_white);

            // Positive loss = worse for mover. Negative = improvement for mover.
            int loss_cp = before_mover - after_mover;

            // penalty used for rating: we only punish mistakes (don’t treat improvement as “Best”)
            int penalty_cp = (loss_cp > 0) ? loss_cp : 0;

            if (debug_mode) {
                printf("[AI Analysis] Move Analysis:\n");
                printf("  mover=%s\n", white_moved ? "White" : "Black");
                printf("  before_white=%d after_white=%d\n", before_white, after_white);
                printf("  before_mover=%d after_mover=%d\n", before_mover, after_mover);
                printf("  loss=%d penalty=%d\n", loss_cp, penalty_cp);
            }

            if (should_rate) {
                // Thresholds on penalty (only positive)
                if (penalty_cp <= 15) stats.rating_label = "Best";
                else if (penalty_cp <= 60) stats.rating_label = "Good";
                else if (penalty_cp <= 180) stats.rating_label = "Mistake";
                else stats.rating_label = "Blunder";

                // Reasons
                if (penalty_cp > 60) {
                    // If after position has mate against mover, call it out
                    // (mate_distance is White perspective; but score already mapped.
                    // We'll use mover perspective eval: if after_mover is very negative it implies danger.)
                    if (data->is_mate) {
                        stats.rating_reason = "Allowed mate threat";
                    } else {
                        stats.rating_reason = "Suboptimal move";
                    }
                }

                if (debug_mode) {
                    printf("[AI Analysis] Move Analysis: Rating=%s, Reason=%s\n",
                           stats.rating_label,
                           stats.rating_reason ? stats.rating_reason : "(null)");
                }

                // Sticky
                ctrl->last_rating_label = stats.rating_label;
                ctrl->last_rating_reason = stats.rating_reason;
                ctrl->rating_expiry_time = g_get_monotonic_time() + 1500000;
            }

            ctrl->rating_pending = false;
        }
    }

    // Apply Sticky Rating
    if (!stats.rating_label && g_get_monotonic_time() < ctrl->rating_expiry_time) {
        stats.rating_label = ctrl->last_rating_label;
        stats.rating_reason = ctrl->last_rating_reason;
    }

    // Move Number
    if (ctrl->logic) {
        stats.move_number = gamelogic_get_move_count(ctrl->logic);
    }

    if (ctrl->eval_cb) {
        if (cfg && !cfg->show_mate_warning) {
            stats.is_mate = false;
            stats.mate_distance = 0;
        } else {
            // Enforce Mate-in-5 UI constraint
            if (stats.is_mate && abs(stats.mate_distance) > 5) {
                stats.is_mate = false;
                stats.mate_distance = 0;
            }
        }
        ctrl->eval_cb(&stats, ctrl->eval_cb_data);
    }

    // Cleanup
    g_free((char*)stats.best_move);
    g_free((char*)stats.fen);
    g_free(data->fen);
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
            if (result->callback) result->callback(m, result->user_data);
            else move_free(m);
        }
    } else {
        controller->ai_thinking = false;
    }

    // Restart analysis once after AI move (this should be the only restart for AI moves)
    if (controller->analysis_running) {
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

static void parse_info_line(AiController* controller, char* line) {
    if (!controller || !line) return;

    int score = 0;
    bool is_mate = false;
    int mate_dist = 0;
    char best_move[32] = {0};
    bool found_score = false;

    char* p;
    if ((p = strstr(line, "score cp"))) {
        sscanf(p + 9, "%d", &score);
        found_score = true;
    } else if ((p = strstr(line, "score mate"))) {
        sscanf(p + 11, "%d", &mate_dist);
        is_mate = true;
        score = (mate_dist > 0) ? 30000 : -30000;
        found_score = true;
    }

    if (!found_score) return;

    // Normalize: UCI score is side-to-move. Convert to White perspective.
    bool black_to_move = (strstr(controller->last_analysis_fen, " b ") != NULL);
    if (black_to_move) {
        score = -score;
        mate_dist = -mate_dist;
    }

    if ((p = strstr(line, " pv "))) {
        char* m = p + 4;
        int i = 0;
        while (*m && *m != ' ' && i < 31) best_move[i++] = *m++;
        best_move[i] = '\0';
    }

    // Update Unthrottled Snapshot
    controller->latest_unthrottled_snapshot.score = score;
    controller->latest_unthrottled_snapshot.is_mate = is_mate;
    controller->latest_unthrottled_snapshot.mate_dist = mate_dist;
    g_strlcpy(controller->latest_unthrottled_snapshot.fen, controller->last_analysis_fen, 256);
    controller->latest_unthrottled_snapshot.valid = true;

    // Throttling
    gint64 now = g_get_monotonic_time();
    bool urgent = false;

    if (is_mate != controller->last_dispatch_mate) urgent = true;
    if (is_mate && mate_dist != controller->last_dispatch_mate_dist) urgent = true;
    if (!urgent && abs(score - controller->last_dispatch_score) > 15) urgent = true;

    if (!urgent && (now - controller->last_dispatch_time) < 200000) return;

    controller->last_dispatch_time = now;
    controller->last_dispatch_score = score;
    controller->last_dispatch_mate = is_mate;
    controller->last_dispatch_mate_dist = mate_dist;

    AiDispatchData* data = g_new0(AiDispatchData, 1);
    data->controller = controller;
    data->fen = g_strdup(controller->last_analysis_fen);
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
            g_usleep(100000);
            continue;
        }

        if (!controller->analysis_engine) {
            g_usleep(50000);
            continue;
        }

        char* response = ai_engine_try_get_response(controller->analysis_engine);
        if (response) {
            if (strncmp(response, "info ", 5) == 0) {
                parse_info_line(controller, response);
            }
            ai_engine_free_response(response);
        } else {
            g_usleep(50000);
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
        ai_controller_start_analysis(controller, cfg->analysis_use_custom, cfg->custom_engine_path);
    } else {
        ai_controller_stop_analysis(controller, true);
    }
}

AiController* ai_controller_new(GameLogic* logic, AiDialog* ai_dialog) {
    if (!logic || !ai_dialog) return NULL;

    AiController* controller = g_new0(AiController, 1);
    controller->logic = logic;
    controller->ai_dialog = ai_dialog;

    ai_dialog_set_settings_changed_callback(ai_dialog, on_ai_settings_changed, controller);

    controller->last_dispatch_score = 999999;
    controller->last_analysis_fen[0] = '\0';

    return controller;
}

void ai_controller_free(AiController* controller) {
    if (!controller) return;

    controller->destroyed = true;

    ai_controller_stop(controller);
    ai_controller_stop_analysis(controller, true);

    if (controller->internal_engine) ai_engine_cleanup(controller->internal_engine);
    if (controller->custom_engine) ai_engine_cleanup(controller->custom_engine);

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

#include "config_manager.h"

static void drain_engine_output(EngineHandle* engine) {
    if (!engine) return;
    char* resp;
    while ((resp = ai_engine_try_get_response(engine)) != NULL) {
        ai_engine_free_response(resp);
    }
}

bool ai_controller_start_analysis(AiController* controller, bool use_custom, const char* custom_path) {
    if (debug_mode) printf("[AI Controller] Start Analysis Requested (Custom=%d)\n", use_custom);
    if (!controller) return false;

    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis) {
        if (controller->analysis_running) ai_controller_stop_analysis(controller, true);
        return false;
    }

    if (use_custom && (!custom_path || strlen(custom_path) == 0)) return false;

    char current_fen[256];
    gamelogic_generate_fen(controller->logic, current_fen, 256);

    bool engine_exists = (controller->analysis_engine != NULL);
    bool thread_active = (controller->analysis_thread != NULL);
    bool type_match = (use_custom == controller->analysis_is_custom);
    bool path_match = true;

    if (use_custom) {
        path_match = (controller->analysis_custom_path && strcmp(custom_path, controller->analysis_custom_path) == 0);
    } else {
        path_match = true;
    }

    bool can_reuse_engine = (engine_exists && thread_active && type_match && path_match);

    // If already analyzing same position, do nothing
    if (can_reuse_engine) {
        if (strcmp(controller->last_analysis_fen, current_fen) == 0 && controller->analysis_running) {
            return true;
        }
    }

    // IMPORTANT: do NOT try to save before snapshots here.
    // Only mark_human_move_begin() captures the parent snapshot reliably.

    if (!can_reuse_engine) {
        if (engine_exists || controller->analysis_running) {
            ai_controller_stop_analysis(controller, true);
        }

        EngineHandle* new_engine = NULL;

        if (use_custom) {
            new_engine = ai_engine_init_external(custom_path);
        } else {
            if (!controller->internal_engine) controller->internal_engine = ai_engine_init_internal();
            new_engine = controller->internal_engine;
        }

        controller->analysis_engine = new_engine;
        if (!controller->analysis_engine) return false;

        ai_engine_send_command(controller->analysis_engine, "uci");

        controller->analysis_is_custom = use_custom;
        if (controller->analysis_custom_path) g_free(controller->analysis_custom_path);
        controller->analysis_custom_path = use_custom ? g_strdup(custom_path) : NULL;

        controller->analysis_running = true;
        controller->analysis_thread = g_thread_new("ai-analysis", ai_analysis_thread, controller);
    } else {
        controller->analysis_running = true;
    }

    g_strlcpy(controller->last_analysis_fen, current_fen, 256);

    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", current_fen);

    ai_engine_send_command(controller->analysis_engine, "stop");
    drain_engine_output(controller->analysis_engine);

    ai_engine_send_command(controller->analysis_engine, pos_cmd);
    ai_engine_send_command(controller->analysis_engine, "go infinite");

    if (debug_mode) printf("[AI Controller] Analysis Started successfully.\n");
    return true;
}

void ai_controller_stop_analysis(AiController* controller, bool free_engine) {
    if (debug_mode) printf("[AI Controller] Stopping Analysis.\n");
    if (!controller) return;

    controller->analysis_running = false;

    if (controller->analysis_engine) {
        ai_engine_send_command(controller->analysis_engine, "stop");
    }

    if (controller->analysis_thread) {
        g_thread_join(controller->analysis_thread);
        controller->analysis_thread = NULL;
    }

    if (free_engine && controller->analysis_engine) {
        bool is_shared = (controller->analysis_engine == controller->internal_engine);

        if (!is_shared) {
            ai_engine_cleanup(controller->analysis_engine);
        } else {
            if (debug_mode) printf("[AI Controller] Skipping cleanup of shared internal engine.\n");
        }

        controller->analysis_engine = NULL;

        controller->last_analysis_fen[0] = '\0';
        controller->analysis_is_custom = false;
        if (controller->analysis_custom_path) {
            g_free(controller->analysis_custom_path);
            controller->analysis_custom_path = NULL;
        }
    }
}

void ai_controller_set_nnue(AiController* controller, bool enabled, const char* path) {
    if (controller->analysis_engine) {
        ai_engine_set_option(controller->analysis_engine, "Use NNUE", enabled ? "true" : "false");
        if (enabled && path) ai_engine_set_option(controller->analysis_engine, "EvalFile", path);
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

// This must be called exactly when the human starts committing a move
// (before the move is applied to gamelogic).
void ai_controller_mark_human_move_begin(AiController* controller) {
    if (!controller) return;

    // Prefer unthrottled snapshot (most recent), else fallback to current snapshot.
    if (controller->latest_unthrottled_snapshot.valid) {
        controller->before_move_snapshot = controller->latest_unthrottled_snapshot;
    } else if (controller->current_snapshot.valid) {
        controller->before_move_snapshot = controller->current_snapshot;
    } else {
        controller->before_move_snapshot.valid = false;
    }

    controller->rating_pending = controller->before_move_snapshot.valid;

    if (debug_mode) {
        if (controller->before_move_snapshot.valid) {
            printf("[AI Controller] MARK MOVE BEGIN. Snapshot saved:\n");
            printf("  FEN=%s\n", controller->before_move_snapshot.fen);
            printf("  Score=%d Mate=%d MateDist=%d\n",
                   controller->before_move_snapshot.score,
                   controller->before_move_snapshot.is_mate ? 1 : 0,
                   controller->before_move_snapshot.mate_dist);
        } else {
            printf("[AI Controller] MARK MOVE BEGIN. No valid snapshot available.\n");
        }
    }
}

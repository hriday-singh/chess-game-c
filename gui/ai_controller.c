#define RATING_MIN_DEPTH 14
#define ANALYSIS_THROTTLE_MS 200
#define ANALYSIS_MULTIPV 3

#include "ai_controller.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* If you have a move object, include it here. Your old code used Move* and move_create/move_free.
   We keep your existing pattern in request_move thread delivery. */
#include "move.h"

static bool debug_mode = true;

/* ---------------------------
   Helpers: safe defaults
---------------------------- */

static int clamp_i(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static double clamp_d(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* Convert White-perspective cp to "side perspective" cp */
static int side_perspective_cp(Player side, int cp_white) {
    return (side == PLAYER_WHITE) ? cp_white : -cp_white;
}

/* Convert eval to mover perspective (mover known by color who just moved) */
static int mover_perspective_eval(bool white_moved, int cp_white) {
    return white_moved ? cp_white : -cp_white;
}

/* ---------------------------
   WDL mapping for eval bar
   Produce win/draw/loss for a given analysis side.
   This is not Stockfish's native WDL, but a practical predictor for UI.
---------------------------- */
static void eval_to_wdl(Player analysis_side, bool is_mate, int mate_dist_white, int score,
                       double* out_w, double* out_d, double* out_l) {
    double w = 0.0, d = 0.0, l = 0.0;

    if (is_mate) {
        /* mate_dist_white > 0 means White mates. <0 means Black mates. */
        bool white_mating = (mate_dist_white > 0);
        bool side_wins = (analysis_side == PLAYER_WHITE) ? white_mating : !white_mating;
        if (side_wins) { w = 1.0; d = 0.0; l = 0.0; }
        else { w = 0.0; d = 0.0; l = 1.0; }
        *out_w = w; *out_d = d; *out_l = l;
        return;
    }

    /* Convert to analysis side perspective */
    int cp = side_perspective_cp(analysis_side, score);

    /* Clamp extreme cp for probability stability */
    cp = clamp_i(cp, -2000, 2000);

    /* Logistic win probability; k chosen to feel reasonable in chess UI */
    /* cp=0 => ~0.5, cp=200 => ~0.69, cp=400 => ~0.83, cp=800 => ~0.96 */
    const double k = 0.004;

    double p_win = 1.0 / (1.0 + exp(-k * (double)cp));

    /* Draw probability heuristic: highest near equal, declines with large advantage */
    /* At cp=0: ~0.30 ; at cp=400: ~0.12 ; at cp=800: ~0.05 */
    double abs_cp = fabs((double)cp);
    double p_draw = 0.30 * exp(-abs_cp / 400.0);

    /* Normalize: allocate win/loss around draw */
    double p_nodraw = 1.0 - p_draw;
    double p_win_adj = p_win * p_nodraw;
    double p_loss_adj = (1.0 - p_win) * p_nodraw;

    /* guard for numeric issues */
    p_win_adj = clamp_d(p_win_adj, 0.0, 1.0);
    p_draw = clamp_d(p_draw, 0.0, 1.0);
    p_loss_adj = clamp_d(p_loss_adj, 0.0, 1.0);

    /* final normalization */
    double s = p_win_adj + p_draw + p_loss_adj;
    if (s <= 1e-9) {
        w = 0.5; d = 0.0; l = 0.5;
    } else {
        w = p_win_adj / s;
        d = p_draw / s;
        l = p_loss_adj / s;
    }

    *out_w = w; *out_d = d; *out_l = l;
}

/* ---------------------------
   MultiPV parsing structs
---------------------------- */
#define MAX_MP 5

typedef struct {
    bool valid;
    int multipv;            /* 1..N */
    int depth;
    bool is_mate;
    int mate_dist_stm;      /* side-to-move perspective as in UCI */
    int score_stm;          /* cp or +/-30000 if mate, side-to-move perspective */
    char pv_first_move[32]; /* first PV move (UCI) */
} ParsedInfo;

/* Snapshot used for rating */
typedef struct {
    char fen[256];

    /* White perspective eval */
    int score;
    bool is_mate;
    int mate_dist_white;

    /* PV best move for multipv1 */
    char best_move_uci[32];

    /* For rating: best and second best in mover perspective */
    int best_mover_eval;        /* mover perspective cp (or mate mapped to +/-30000) */
    int second_mover_eval;      /* mover perspective */
    char second_move_uci[32];

    bool valid;
    int depth;                  /* depth of multipv1 when captured */
} AiSnapshot;

struct _AiController {
    GameLogic* logic;
    AiDialog* ai_dialog;

    EngineHandle* internal_engine;
    EngineHandle* custom_engine;
    EngineHandle* analysis_engine;

    bool ai_thinking;
    bool analysis_running;

    /* last dispatched eval (white perspective) */
    int last_score;
    bool last_is_mate;
    int last_mate_distance;

    /* analysis side for WDL bar */
    Player analysis_side;

    /* Callbacks */
    AiMoveReadyCallback move_cb;
    gpointer move_cb_data;

    AiEvalUpdateCallback eval_cb;
    gpointer eval_cb_data;

    /* snapshots for move rating */
    AiSnapshot before_move_snapshot;
    AiSnapshot current_snapshot;             /* last dispatched */
    AiSnapshot latest_unthrottled_snapshot;  /* updated on every parse, best effort */
    bool rating_pending;
    char pending_played_move_uci[32];

    /* State to avoid unnecessary engine restarts */
    bool analysis_is_custom;
    char* analysis_custom_path;
    /* Mutex for FEN safety */
    GMutex fen_mutex;
    char last_analysis_fen[256];

    /* Thread management & Communication */
    GAsyncQueue* move_queue;
    GThread* internal_listener;
    GThread* custom_listener;
    bool listener_running;

    /* Throttling */
    gint64 last_dispatch_time;
    int last_dispatch_score;
    bool last_dispatch_mate;
    int last_dispatch_mate_dist;

    /* Mate warning stability */
    gint64 mate_expiry_time;
    int last_mate_dist_stable;

    /* Sticky rating */
    gint64 rating_expiry_time;
    const char* last_rating_label;
    const char* last_rating_reason;

    /* Safety */
    bool destroyed;
    guint64 think_gen;

    /* Track MultiPV for current analysis position */
    int multipv_n;
    ParsedInfo mp[MAX_MP]; /* indexed by multipv-1 */
};

typedef struct {
    AiController* controller;
    EngineHandle* engine;
} AiListenerData;

typedef struct {
    AiController* controller;
    char* fen;
    int score;
    bool is_mate;
    int mate_distance;
    int depth;
    char* best_move_uci;
} AiDispatchData;

/* ---------------------------
   Engine output draining
---------------------------- */
static void drain_engine_output(EngineHandle* engine) {
    if (!engine) return;
    char* resp;
    while ((resp = ai_engine_try_get_response(engine)) != NULL) {
        ai_engine_free_response(resp);
    }
}

/* Forward Declarations */
static gpointer ai_engine_listener_thread(gpointer user_data);

/* ---------------------------
   Listener management
---------------------------- */
static void ensure_listener(AiController* controller, EngineHandle* engine, bool is_custom) {
    if (!controller || !engine) return;

    GThread** slot = is_custom ? &controller->custom_listener : &controller->internal_listener;
    if (slot && *slot != NULL) return;

    AiListenerData* data = g_new0(AiListenerData, 1);
    data->controller = controller;
    data->engine = engine;

    char* name = is_custom ? "ai-custom-listener" : "ai-internal-listener";
    *slot = g_thread_new(name, ai_engine_listener_thread, data);
}

/* ---------------------------
   Parse a UCI info line
   - Extract depth
   - Extract multipv index
   - Extract score cp/mate (side-to-move)
   - Extract first PV move
---------------------------- */
static bool parse_uci_info_line(const char* line, ParsedInfo* out) {
    if (!line || !out) return false;
    memset(out, 0, sizeof(*out));

    if (strncmp(line, "info ", 5) != 0) return false;

    /* Default multipv = 1 if not present */
    out->multipv = 1;

    /* depth */
    {
        const char* p = strstr(line, " depth ");
        if (p) {
            int d = 0;
            if (sscanf(p + 7, "%d", &d) == 1) out->depth = d;
        }
    }

    /* multipv */
    {
        const char* p = strstr(line, " multipv ");
        if (p) {
            int mv = 1;
            if (sscanf(p + 9, "%d", &mv) == 1) {
                out->multipv = mv;
            }
        }
    }

    /* score cp / score mate */
    bool found_score = false;
    {
        const char* p = strstr(line, " score cp ");
        if (p) {
            int sc = 0;
            if (sscanf(p + 10, "%d", &sc) == 1) {
                out->score_stm = sc;
                out->is_mate = false;
                out->mate_dist_stm = 0;
                found_score = true;
            }
        } else {
            p = strstr(line, " score mate ");
            if (p) {
                int md = 0;
                if (sscanf(p + 12, "%d", &md) == 1) {
                    out->is_mate = true;
                    out->mate_dist_stm = md;
                    out->score_stm = (md > 0) ? 30000 : -30000;
                    found_score = true;
                }
            }
        }
    }
    if (!found_score) return false;

    /* pv first move */
    {
        const char* p = strstr(line, " pv ");
        if (p) {
            const char* m = p + 4;
            int i = 0;
            while (*m && *m != ' ' && i < 31) {
                out->pv_first_move[i++] = *m++;
            }
            out->pv_first_move[i] = '\0';
        } else {
            out->pv_first_move[0] = '\0';
        }
    }

    out->valid = true;
    return true;
}

/* Convert ParsedInfo (side-to-move perspective) to White perspective using current analysis FEN */
static void to_white_perspective(const AiController* controller, const ParsedInfo* in,
                                int* out_score, bool* out_is_mate, int* out_mate_dist_white) {
    int score = in->score_stm;
    int mate_dist = in->mate_dist_stm;
    bool is_mate = in->is_mate;

    /* UCI score is side-to-move. Convert to White perspective using last_analysis_fen side-to-move. */
    bool black_to_move = (strstr(controller->last_analysis_fen, " b ") != NULL);
    if (black_to_move) {
        score = -score;
        mate_dist = -mate_dist;
    }

    *out_score = score;
    *out_is_mate = is_mate;
    *out_mate_dist_white = mate_dist;
}

/* ---------------------------
   Rating logic helpers
---------------------------- */
typedef enum {
    RATING_NONE = 0,
    RATING_BLUNDER,
    RATING_MISTAKE,
    RATING_INACCURACY,
    RATING_GOOD,
    RATING_EXCELLENT,
    RATING_BEST
} RatingLevel;

static RatingLevel level_for_penalty(int penalty_cp) {
    if (penalty_cp <= 10) return RATING_BEST;
    if (penalty_cp <= 30) return RATING_EXCELLENT;
    if (penalty_cp <= 80) return RATING_GOOD;
    if (penalty_cp <= 150) return RATING_INACCURACY;
    if (penalty_cp <= 300) return RATING_MISTAKE;
    return RATING_BLUNDER;
}

static const char* label_for_level(RatingLevel level) {
    switch (level) {
        case RATING_BEST:       return "Best";
        case RATING_EXCELLENT:  return "Excellent";
        case RATING_GOOD:       return "Good";
        case RATING_INACCURACY: return "Inaccuracy";
        case RATING_MISTAKE:    return "Mistake";
        case RATING_BLUNDER:    return "Blunder";
        default:                return NULL;
    }
}

static const char* reason_for_penalty(int penalty_cp, bool missed_mate, bool allowed_mate) {
    if (allowed_mate) return "Allowed forced mate";
    if (missed_mate) return "Missed forced mate";
    if (penalty_cp <= 30) return "Near-best move";
    if (penalty_cp <= 80) return "Slightly suboptimal";
    if (penalty_cp <= 150) return "Inaccuracy";
    if (penalty_cp <= 300) return "Mistake";
    return "Blunder";
}

/* Determine if white is to move in a FEN */
static bool fen_white_to_move(const char* fen) {
    return (fen && strstr(fen, " w ") != NULL);
}

/* Compare move strings (UCI), allow exact match only */
static bool uci_move_equal(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

/* ---------------------------
   Thread-safe UI update
---------------------------- */
static gboolean dispatch_eval_update(gpointer user_data) {
    AiDispatchData* data = (AiDispatchData*)user_data;
    if (!data) return FALSE;

    AiController* ctrl = data->controller;

    if (!ctrl || ctrl->destroyed) {
        g_free(data->fen);
        g_free(data->best_move_uci);
        g_free(data);
        return FALSE;
    }

    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis) {
        g_free(data->fen);
        g_free(data->best_move_uci);
        g_free(data);
        return FALSE;
    }

    /* FEN matching guard */
    char current_fen[256];
    if (ctrl->logic) {
        gamelogic_generate_fen(ctrl->logic, current_fen, sizeof(current_fen));
        if (data->fen && strcmp(current_fen, data->fen) != 0) {
            if (debug_mode) {
                printf("[AI Controller] Discard stale eval: Data FEN=%s, Current FEN=%s\n", data->fen, current_fen);
            }
            g_free(data->fen);
            g_free(data->best_move_uci);
            g_free(data);
            return FALSE;
        }
    }

    /* Update controller last known */
    ctrl->last_score = data->score;
    ctrl->last_is_mate = data->is_mate;
    ctrl->last_mate_distance = data->mate_distance;

    /* Update snapshots (current dispatched) */
    g_strlcpy(ctrl->current_snapshot.fen, data->fen, sizeof(ctrl->current_snapshot.fen));
    ctrl->current_snapshot.score = data->score;
    ctrl->current_snapshot.is_mate = data->is_mate;
    ctrl->current_snapshot.mate_dist_white = data->mate_distance;
    ctrl->current_snapshot.depth = data->depth;
    if (data->best_move_uci) g_strlcpy(ctrl->current_snapshot.best_move_uci, data->best_move_uci, sizeof(ctrl->current_snapshot.best_move_uci));
    ctrl->current_snapshot.valid = true;

    /* Sticky mate */
    gint64 now = g_get_monotonic_time();
    if (data->is_mate) {
        ctrl->mate_expiry_time = now + 1500000; /* 1.5s */
        ctrl->last_mate_dist_stable = data->mate_distance;
    }

    AiStats stats;
    memset(&stats, 0, sizeof(stats));

    stats.score = data->score;
    stats.is_mate = data->is_mate;
    stats.mate_distance = data->mate_distance;
    stats.fen = g_strdup(data->fen);

    /* sticky mate usage */
    if (!stats.is_mate && now < ctrl->mate_expiry_time) {
        stats.is_mate = true;
        stats.mate_distance = ctrl->last_mate_dist_stable;
    }

    if (data->best_move_uci) {
        stats.best_move = g_strdup(data->best_move_uci);
    }

    /* WDL for eval bar (analysis_side) */
    stats.analysis_side = ctrl->analysis_side;
    eval_to_wdl(ctrl->analysis_side, stats.is_mate, stats.mate_distance, stats.score,
                &stats.win_prob, &stats.draw_prob, &stats.loss_prob);

    /* hanging pieces */
    if (cfg->show_hanging_pieces && ctrl->logic) {
        stats.white_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_WHITE);
        stats.black_hanging = gamelogic_count_hanging_pieces(ctrl->logic, PLAYER_BLACK);
    }

    /* ---------------- Move Rating ----------------
       Uses:
       - before_move_snapshot (captured at human move begin)
       - played move uci string (captured at human move begin)
       - after eval (current data)
       - depth gating for stability
       - best move / second best context from snapshot
    */
    if (ctrl->rating_pending && ctrl->before_move_snapshot.valid) {

        /* Sanity: must not be same position */
        if (strcmp(ctrl->before_move_snapshot.fen, data->fen) == 0) {
            if (debug_mode) printf("[AI Rating] skipped: before_fen == after_fen\n");
            ctrl->rating_pending = false;
        } else {

            /* Depth gating: only rate once after we have enough depth for after position */
            int min_depth = RATING_MIN_DEPTH;
            if (data->depth < min_depth) {
                /* Keep pending until stable enough */
                if (debug_mode) {
                    printf("[AI Rating] waiting for depth >= %d (now %d)\n", min_depth, data->depth);
                }
            } else {
                bool white_moved = fen_white_to_move(ctrl->before_move_snapshot.fen);

                /* Only rate human moves */
                bool should_rate = true;
                if (ctrl->logic) {
                    if (ctrl->logic->gameMode == GAME_MODE_PVC) {
                        Player mover = white_moved ? PLAYER_WHITE : PLAYER_BLACK;
                        if (ctrl->logic->playerSide != mover) should_rate = false;
                    } else if (ctrl->logic->gameMode == GAME_MODE_CVC) {
                        should_rate = false;
                    }
                }

                /* Compute before/after in white perspective */
                int before_white = ctrl->before_move_snapshot.score;
                int after_white  = data->score;

                /* Clamp non-mate cp for stability but preserve mate magnitude */
                if (!ctrl->before_move_snapshot.is_mate) before_white = clamp_i(before_white, -2000, 2000);
                if (!data->is_mate) after_white = clamp_i(after_white, -2000, 2000);

                int before_mover = mover_perspective_eval(white_moved, before_white);
                int after_mover  = mover_perspective_eval(white_moved, after_white);

                /* Mate-aware overrides */
                bool missed_mate = false;
                bool allowed_mate = false;

                if (ctrl->before_move_snapshot.is_mate) {
                    /* If before is mate for mover and after is not mate for mover => missed mate */
                    int before_md_white = ctrl->before_move_snapshot.mate_dist_white;
                    int before_md_mover = white_moved ? before_md_white : -before_md_white;
                    bool mate_for_mover_before = (before_md_mover > 0);

                    if (mate_for_mover_before) {
                        if (!data->is_mate) {
                            missed_mate = true;
                        } else {
                            int after_md_white = data->mate_distance;
                            int after_md_mover = white_moved ? after_md_white : -after_md_white;
                            bool mate_for_mover_after = (after_md_mover > 0);
                            if (!mate_for_mover_after) missed_mate = true;
                        }
                    }
                }

                if (data->is_mate) {
                    /* If after is mate against mover => allowed mate */
                    int after_md_white = data->mate_distance;
                    int after_md_mover = white_moved ? after_md_white : -after_md_white;
                    bool mate_for_mover_after = (after_md_mover > 0);
                    if (!mate_for_mover_after) allowed_mate = true;
                }

                int loss_cp = before_mover - after_mover;
                int penalty_cp = (loss_cp > 0) ? loss_cp : 0;

                /* If mate override triggers, force strong penalty */
                if (allowed_mate) penalty_cp = 99999;
                if (missed_mate && penalty_cp < 300) penalty_cp = 350;

                /* Best-move matching: if played move equals best PV move, treat as Best (unless mate override says otherwise) */
                bool played_is_best = false;
                if (ctrl->pending_played_move_uci[0] && ctrl->before_move_snapshot.best_move_uci[0]) {
                    if (uci_move_equal(ctrl->pending_played_move_uci, ctrl->before_move_snapshot.best_move_uci)) {
                        played_is_best = true;
                    }
                }

                /* Near-best adjustment: if played move equals second PV move and is close in eval, upgrade */
                bool played_is_second = false;
                int best_eval_mover = ctrl->before_move_snapshot.best_mover_eval;
                int second_eval_mover = ctrl->before_move_snapshot.second_mover_eval;
                int delta_best_second = abs(best_eval_mover - second_eval_mover);

                if (ctrl->pending_played_move_uci[0] && ctrl->before_move_snapshot.second_move_uci[0]) {
                    if (uci_move_equal(ctrl->pending_played_move_uci, ctrl->before_move_snapshot.second_move_uci)) {
                        played_is_second = true;
                    }
                }

                if (should_rate) {
                    RatingLevel level = RATING_NONE;

                    if (allowed_mate) {
                        level = RATING_BLUNDER;
                    } else if (missed_mate) {
                        level = (penalty_cp >= 700) ? RATING_BLUNDER : RATING_MISTAKE;
                    } else if (played_is_best) {
                        level = RATING_BEST;
                    } else {
                        level = level_for_penalty(penalty_cp);

                        /* If the move is the engine second choice and close to best, upgrade by one */
                        if (played_is_second && delta_best_second <= 15) {
                            if (level == RATING_GOOD) level = RATING_EXCELLENT;
                            else if (level == RATING_INACCURACY) level = RATING_GOOD;
                            else if (level == RATING_MISTAKE) level = RATING_INACCURACY;
                            else if (level == RATING_BLUNDER) level = RATING_MISTAKE;
                        }

                        /* If best/second spread is tiny, be more generous overall */
                        if (delta_best_second <= 10 && penalty_cp <= 25) {
                            level = RATING_EXCELLENT;
                        }
                    }

                    stats.rating_label = label_for_level(level);
                    stats.rating_reason = reason_for_penalty(penalty_cp, missed_mate, allowed_mate);

                    if (debug_mode) {
                        printf("[AI Rating] mover=%s played=%s best=%s second=%s\n",
                               white_moved ? "White" : "Black",
                               ctrl->pending_played_move_uci[0] ? ctrl->pending_played_move_uci : "(none)",
                               ctrl->before_move_snapshot.best_move_uci[0] ? ctrl->before_move_snapshot.best_move_uci : "(none)",
                               ctrl->before_move_snapshot.second_move_uci[0] ? ctrl->before_move_snapshot.second_move_uci : "(none)");
                        printf("[AI Rating] before_mover=%d after_mover=%d loss=%d penalty=%d label=%s reason=%s\n",
                               before_mover, after_mover, loss_cp, penalty_cp,
                               stats.rating_label ? stats.rating_label : "(null)",
                               stats.rating_reason ? stats.rating_reason : "(null)");
                    }

                    /* Sticky for 1.5s */
                    ctrl->last_rating_label = stats.rating_label;
                    ctrl->last_rating_reason = stats.rating_reason;
                    ctrl->rating_expiry_time = g_get_monotonic_time() + 1500000;
                }

                /* Rating done once */
                ctrl->rating_pending = false;
                ctrl->pending_played_move_uci[0] = '\0';
            }
        }
    }

    /* Sticky rating apply */
    if (!stats.rating_label && g_get_monotonic_time() < ctrl->rating_expiry_time) {
        stats.rating_label = ctrl->last_rating_label;
        stats.rating_reason = ctrl->last_rating_reason;
    }

    /* Move number */
    if (ctrl->logic) {
        stats.move_number = gamelogic_get_move_count(ctrl->logic);
    }

    /* Mate warning toggle + mate-in-5 constraint */
    if (ctrl->eval_cb) {
        if (!cfg->show_mate_warning) {
            stats.is_mate = false;
            stats.mate_distance = 0;
        } else {
            if (stats.is_mate && abs(stats.mate_distance) > 5) {
                stats.is_mate = false;
                stats.mate_distance = 0;
            }
        }
        ctrl->eval_cb(&stats, ctrl->eval_cb_data);
    }

    /* Cleanup */
    if (stats.best_move) g_free((gpointer)stats.best_move);
    g_free(stats.fen);

    g_free(data->fen);
    g_free(data->best_move_uci);
    g_free(data);

    return FALSE;
}

/* ---------------------------
   AI move request thread (unchanged behavior but cleaned)
---------------------------- */
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
            int c1 = move_ptr[0] - 'a';
            int r1 = 8 - (move_ptr[1] - '0');
            int c2 = move_ptr[2] - 'a';
            int r2 = 8 - (move_ptr[3] - '0');

            Move* m = move_create(r1, c1, r2, c2);
            if (strlen(move_ptr) >= 5) {
                switch (move_ptr[4]) {
                    case 'q': m->promotionPiece = PIECE_QUEEN; break;
                    case 'r': m->promotionPiece = PIECE_ROOK; break;
                    case 'b': m->promotionPiece = PIECE_BISHOP; break;
                    case 'n': m->promotionPiece = PIECE_KNIGHT; break;
                    default: break;
                }
            }

            controller->ai_thinking = false;
            if (result->callback) result->callback(m, result->user_data);
            else move_free(m);
        }
    } else {
        controller->ai_thinking = false;
    }

    /* restart analysis once after AI move */
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

    if (data->nnue_enabled && data->nnue_path) {
        ai_engine_set_option(data->engine, "Use NNUE", "true");
        ai_engine_set_option(data->engine, "EvalFile", data->nnue_path);
    }

    ai_engine_send_command(data->engine, "stop");
    
    // Drain queue for THIS engine just in case
    // Note: This is simpler now that listeners are constant
    while (g_async_queue_length(controller->move_queue) > 0) {
        char* old = g_async_queue_try_pop(controller->move_queue);
        if (old) g_free(old);
    }

    ai_engine_send_command(data->engine, pos_cmd);

    char go_cmd[128];
    if (data->params.depth > 0) snprintf(go_cmd, sizeof(go_cmd), "go depth %d", data->params.depth);
    else snprintf(go_cmd, sizeof(go_cmd), "go movetime %d", data->params.move_time_ms);
    
    ai_engine_send_command(data->engine, go_cmd);

    // Wait for bestmove via QUEUE
    char* bestmove_str = NULL;
    gint64 timeout = (data->params.move_time_ms > 0) ? (data->params.move_time_ms + 5000) * 1000 : 30000000;
    
    bestmove_str = g_async_queue_timeout_pop(controller->move_queue, timeout);

    /* stale generation guard */
    if (!controller || controller->destroyed || data->gen != controller->think_gen) {
        if (bestmove_str) g_free(bestmove_str);
        g_free(data->fen);
        g_free(data->nnue_path);
        g_free(data);
        return NULL;
    }

    if (bestmove_str && strncmp(bestmove_str, "bestmove ", 9) == 0) {
        AiResultData* result = g_new0(AiResultData, 1);
        result->controller = controller;
        result->fen = data->fen;
        result->bestmove = g_strdup(bestmove_str + 9);
        result->callback = data->callback;
        result->user_data = data->user_data;
        result->gen = data->gen;

        g_free(bestmove_str);
        g_free(data->nnue_path);
        g_free(data);

        g_timeout_add(g_ai_move_delay_ms, apply_ai_move_idle, result);
    } else {
        if (controller && !controller->destroyed && data->gen == controller->think_gen) {
            controller->ai_thinking = false;
        }
        if (bestmove_str) g_free(bestmove_str);
        g_free(data->fen);
        g_free(data->nnue_path);
        g_free(data);
    }

    return NULL;
}

/* ---------------------------
   Parse + dispatch analysis info
---------------------------- */
static void update_rating_snapshot_from_multipv(AiController* controller) {
    /* Build latest_unthrottled_snapshot from mp[0] (multipv1), and also record best/second for mover perspective.
       Mover perspective depends on side to move in current analysis position (last_analysis_fen). */
    if (!controller) return;

    ParsedInfo* p1 = &controller->mp[0];
    if (!p1->valid) return;

    int score1_white = 0, mate1_white = 0;
    bool is_mate1 = false;
    to_white_perspective(controller, p1, &score1_white, &is_mate1, &mate1_white);

    AiSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    g_strlcpy(snap.fen, controller->last_analysis_fen, sizeof(snap.fen));
    snap.score = score1_white;
    snap.is_mate = is_mate1;
    snap.mate_dist_white = mate1_white;
    snap.depth = p1->depth;
    if (p1->pv_first_move[0]) g_strlcpy(snap.best_move_uci, p1->pv_first_move, sizeof(snap.best_move_uci));

    /* Determine mover for "best_mover_eval" context:
       In rating we compare from mover perspective for the player who is ABOUT TO MOVE in snap.fen.
       That mover is side_to_move in snap.fen. */
    bool white_to_move = fen_white_to_move(snap.fen);

    int best_white = score1_white;
    if (!is_mate1) best_white = clamp_i(best_white, -2000, 2000);
    snap.best_mover_eval = mover_perspective_eval(white_to_move, best_white);

    /* second best */
    snap.second_mover_eval = snap.best_mover_eval;
    snap.second_move_uci[0] = '\0';

    if (controller->multipv_n >= 2 && controller->mp[1].valid) {
        ParsedInfo* p2 = &controller->mp[1];
        int score2_white = 0, mate2_white = 0;
        bool is_mate2 = false;
        to_white_perspective(controller, p2, &score2_white, &is_mate2, &mate2_white);

        int s2 = score2_white;
        if (!is_mate2) s2 = clamp_i(s2, -2000, 2000);

        snap.second_mover_eval = mover_perspective_eval(white_to_move, s2);
        if (p2->pv_first_move[0]) g_strlcpy(snap.second_move_uci, p2->pv_first_move, sizeof(snap.second_move_uci));
    }

    snap.valid = true;

    controller->latest_unthrottled_snapshot = snap;
}

static void parse_info_line(AiController* controller, char* line) {
    if (!controller || !line) return;

    ParsedInfo parsed;
    if (!parse_uci_info_line(line, &parsed)) return;

    int mpv = parsed.multipv;
    if (mpv < 1) mpv = 1;

    int n = controller->multipv_n;
    if (n <= 0) n = 3;
    if (n > MAX_MP) n = MAX_MP;

    if (mpv > n) return;

    controller->mp[mpv - 1] = parsed;

    /* Refresh unthrottled snapshot primarily from multipv1 */
    update_rating_snapshot_from_multipv(controller);

    /* Dispatch only when we have multipv1 (our main eval) */
    if (mpv != 1) return;

    int score_white = 0;
    bool is_mate = false;
    int mate_dist_white = 0;
    to_white_perspective(controller, &parsed, &score_white, &is_mate, &mate_dist_white);

    /* Throttling decisions */
    gint64 now = g_get_monotonic_time();
    bool urgent = false;

    if (is_mate != controller->last_dispatch_mate) urgent = true;
    if (is_mate && mate_dist_white != controller->last_dispatch_mate_dist) urgent = true;
    if (!urgent && abs(score_white - controller->last_dispatch_score) > 15) urgent = true;

    int throttle_ms = ANALYSIS_THROTTLE_MS;
    gint64 throttle_us = (gint64)throttle_ms * 1000;

    if (!urgent && (now - controller->last_dispatch_time) < throttle_us) return;

    controller->last_dispatch_time = now;
    controller->last_dispatch_score = score_white;
    controller->last_dispatch_mate = is_mate;
    controller->last_dispatch_mate_dist = mate_dist_white;

    AiDispatchData* data = g_new0(AiDispatchData, 1);
    data->controller = controller;
    g_mutex_lock(&controller->fen_mutex);
    data->fen = g_strdup(controller->last_analysis_fen);
    g_mutex_unlock(&controller->fen_mutex);
    data->score = score_white;
    data->is_mate = is_mate;
    data->mate_distance = mate_dist_white;
    data->depth = parsed.depth;
    if (parsed.pv_first_move[0]) data->best_move_uci = g_strdup(parsed.pv_first_move);

    g_idle_add(dispatch_eval_update, data);
}

static gpointer ai_engine_listener_thread(gpointer user_data) {
    AiListenerData* data = (AiListenerData*)user_data;
    AiController* controller = data->controller;
    EngineHandle* engine = data->engine;

    if (debug_mode) printf("[AI Listener] Started persistent listener for engine %p\n", engine);

    while (controller && controller->listener_running && !controller->destroyed) {
        char* response = ai_engine_try_get_response(engine);
        if (response) {
            if (strncmp(response, "info ", 5) == 0) {
                // Analysis output
                if (controller->analysis_engine == engine && controller->analysis_running) {
                    parse_info_line(controller, response);
                }
            } else if (strncmp(response, "bestmove ", 9) == 0) {
                // Search result - push to queue for whichever thread is waiting
                g_async_queue_push(controller->move_queue, g_strdup(response));
            }
            ai_engine_free_response(response);
        } else {
            g_usleep(10000); // 10ms poll
        }
    }

    if (debug_mode) printf("[AI Listener] Thread exiting for engine %p\n", engine);
    g_free(data);
    return NULL;
}

/* ---------------------------
   Public API
---------------------------- */
AiController* ai_controller_new(GameLogic* logic, AiDialog* ai_dialog) {
    if (!logic || !ai_dialog) return NULL;

    AiController* controller = g_new0(AiController, 1);
    controller->logic = logic;
    controller->ai_dialog = ai_dialog;

    controller->move_queue = g_async_queue_new_full(g_free);
    controller->listener_running = true;
    controller->last_analysis_fen[0] = '\0';

    controller->analysis_side = PLAYER_WHITE; /* default */

    /* defaults */
    controller->multipv_n = 3;
    memset(controller->mp, 0, sizeof(controller->mp));

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

    controller->listener_running = false;
    controller->destroyed = true;

    if (controller->internal_listener) {
        g_thread_join(controller->internal_listener);
    }
    if (controller->custom_listener) {
        g_thread_join(controller->custom_listener);
    }

    if (controller->move_queue) {
        g_async_queue_unref(controller->move_queue);
    }

    g_mutex_clear(&controller->fen_mutex);
    g_free(controller);
}

void ai_controller_request_move(AiController* controller,
                                bool use_custom,
                                AiDifficultyParams params,
                                const char* custom_path,
                                AiMoveReadyCallback callback,
                                gpointer user_data) {
    if (!controller || controller->ai_thinking) return;

    char* fen = g_malloc0(256);
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
    const char* nn_path = NULL;
    if (controller->ai_dialog) {
        nn_path = ai_dialog_get_nnue_path(controller->ai_dialog, &nnue_enabled);
    }
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

bool ai_controller_start_analysis(AiController* controller, bool use_custom, const char* custom_path) {
    if (!controller) return false;

    AppConfig* cfg = config_get();
    if (!cfg || !cfg->enable_live_analysis || (controller->logic && controller->logic->isGameOver)) {
        if (controller->analysis_running) ai_controller_stop_analysis(controller, false);
        return false;
    }

    /* Protection: Don't interrupt AI if it's currently thinking for a move */
    if (controller->ai_thinking) return false;

    if (use_custom && (!custom_path || strlen(custom_path) == 0)) return false;

    /* compute fen */
    char current_fen[256];
    gamelogic_generate_fen(controller->logic, current_fen, 256);

    /* determine multipv */
    int mpv = ANALYSIS_MULTIPV;
    if (mpv < 1) mpv = 1;
    if (mpv > MAX_MP) mpv = MAX_MP;
    controller->multipv_n = mpv;

    bool engine_exists = (controller->analysis_engine != NULL);
    bool thread_active = use_custom ? (controller->custom_listener != NULL) : (controller->internal_listener != NULL);
    bool type_match = (use_custom == controller->analysis_is_custom);
    bool path_match = true;

    if (use_custom) {
        path_match = (controller->analysis_custom_path && strcmp(custom_path, controller->analysis_custom_path) == 0);
    } else {
        path_match = true;
    }

    bool can_reuse_engine = (engine_exists && thread_active && type_match && path_match);

    /* If already analyzing same position, do nothing */
    if (can_reuse_engine) {
        if (strcmp(controller->last_analysis_fen, current_fen) == 0 && controller->analysis_running) {
            return true;
        }
    }

    if (!can_reuse_engine) {
        if (engine_exists || controller->analysis_running) {
             // Only free engine if types don't match or engine is NULL
             bool must_free = !engine_exists || !type_match || !path_match;
             ai_controller_stop_analysis(controller, must_free);
        }

        EngineHandle* new_engine = NULL;
        if (!controller->analysis_engine) {
            if (use_custom) {
                new_engine = ai_engine_init_external(custom_path);
            } else {
                if (!controller->internal_engine) controller->internal_engine = ai_engine_init_internal();
                new_engine = controller->internal_engine;
            }
            controller->analysis_engine = new_engine;
        }

        if (!controller->analysis_engine) return false;

        /* uci init if new */
        if (!can_reuse_engine) {
            ai_engine_send_command(controller->analysis_engine, "uci");
        }

        controller->analysis_is_custom = use_custom;
        if (controller->analysis_custom_path) g_free(controller->analysis_custom_path);
        controller->analysis_custom_path = use_custom ? g_strdup(custom_path) : NULL;

        controller->analysis_running = true;
        
        ensure_listener(controller, controller->analysis_engine, use_custom);
    } else {
        controller->analysis_running = true;
        ensure_listener(controller, controller->analysis_engine, use_custom);
    }

    g_mutex_lock(&controller->fen_mutex);
    g_strlcpy(controller->last_analysis_fen, current_fen, sizeof(controller->last_analysis_fen));
    g_mutex_unlock(&controller->fen_mutex);

    /* configure MultiPV */
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", controller->multipv_n);
        ai_engine_set_option(controller->analysis_engine, "MultiPV", buf);
    }

    /* reset multipv cache */
    memset(controller->mp, 0, sizeof(controller->mp));

    char pos_cmd[512];
    snprintf(pos_cmd, sizeof(pos_cmd), "position fen %s", current_fen);

    ai_engine_send_command(controller->analysis_engine, "stop");
    drain_engine_output(controller->analysis_engine);

    ai_engine_send_command(controller->analysis_engine, pos_cmd);
    ai_engine_send_command(controller->analysis_engine, "go infinite");

    return true;
}

void ai_controller_stop_analysis(AiController* controller, bool free_engine) {
    if (!controller) return;

    controller->analysis_running = false;

    if (controller->analysis_engine) {
        ai_engine_send_command(controller->analysis_engine, "stop");
    }

    // With persistent listeners, we don't join the thread here. 
    // They exit in ai_controller_free.
    
    if (free_engine && controller->analysis_engine) {
        bool is_shared = (controller->analysis_engine == controller->internal_engine);

        if (!is_shared) {
            ai_engine_cleanup(controller->analysis_engine);
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
    if (!controller) return;
    if (controller->analysis_engine) {
        ai_engine_set_option(controller->analysis_engine, "Use NNUE", enabled ? "true" : "false");
        if (enabled && path) ai_engine_set_option(controller->analysis_engine, "EvalFile", path);
    }
}

bool ai_controller_is_thinking(AiController* controller) {
    if (!controller) return false;
    return controller->ai_thinking;
}

void ai_controller_get_evaluation(AiController* controller, int* score, bool* is_mate) {
    if (!controller) return;
    if (score) *score = controller->last_score;
    if (is_mate) *is_mate = controller->last_is_mate;
}

void ai_controller_set_params(AiController* controller, AiDifficultyParams params) {
    (void)controller;
    (void)params;
}

void ai_controller_set_eval_callback(AiController* controller, AiEvalUpdateCallback callback, gpointer user_data) {
    if (!controller) return;
    controller->eval_cb = callback;
    controller->eval_cb_data = user_data;
}

void ai_controller_set_analysis_side(AiController* controller, Player side) {
    if (!controller) return;
    controller->analysis_side = side;
}

void ai_controller_mark_human_move_begin(AiController* controller, const char* played_move_uci) {
    if (!controller) return;

    /* Prefer unthrottled snapshot; fallback to current snapshot */
    if (controller->latest_unthrottled_snapshot.valid) {
        controller->before_move_snapshot = controller->latest_unthrottled_snapshot;
    } else if (controller->current_snapshot.valid) {
        controller->before_move_snapshot = controller->current_snapshot;
    } else {
        controller->before_move_snapshot.valid = false;
    }

    /* Save played move string */
    controller->pending_played_move_uci[0] = '\0';
    if (played_move_uci && played_move_uci[0]) {
        g_strlcpy(controller->pending_played_move_uci, played_move_uci, sizeof(controller->pending_played_move_uci));
    }

    /* Mark pending only if we have snapshot + a played move */
    controller->rating_pending = (controller->before_move_snapshot.valid && controller->pending_played_move_uci[0]);

    if (debug_mode) {
        printf("[AI Controller] MARK HUMAN MOVE BEGIN\n");
        printf("  played=%s\n", controller->pending_played_move_uci[0] ? controller->pending_played_move_uci : "(none)");
        if (controller->before_move_snapshot.valid) {
            printf("  before_fen=%s\n", controller->before_move_snapshot.fen);
            printf("  before_score_white=%d mate=%d mateDistWhite=%d depth=%d\n",
                   controller->before_move_snapshot.score,
                   controller->before_move_snapshot.is_mate ? 1 : 0,
                   controller->before_move_snapshot.mate_dist_white,
                   controller->before_move_snapshot.depth);
            printf("  best=%s second=%s bestEvalMover=%d secondEvalMover=%d\n",
                   controller->before_move_snapshot.best_move_uci[0] ? controller->before_move_snapshot.best_move_uci : "(none)",
                   controller->before_move_snapshot.second_move_uci[0] ? controller->before_move_snapshot.second_move_uci : "(none)",
                   controller->before_move_snapshot.best_mover_eval,
                   controller->before_move_snapshot.second_mover_eval);
        } else {
            printf("  no valid snapshot\n");
        }
    }
}
void ai_controller_set_rating_pending(AiController* controller, bool pending) {
    if (controller) {
        controller->rating_pending = pending;
    }
}

#include "ai_analysis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/* Standard UCI strings */
#define UCI_CMD "uci\n"
#define ISREADY_CMD "isready\n"
#define READYOK "readyok"
#define BESTMOVE "bestmove"

/* Internal Job Structure */
struct _AiAnalysisJob {
    /* Configuration */
    AnalysisConfig config;
    char* start_fen;
    char** uci_moves; /* Copy of moves */
    int num_moves;
    
    /* Callbacks */
    AnalysisProgressCb progress_cb;
    AnalysisCompleteCb complete_cb;
    void* user_data;
    
    /* Threading */
    GThread* worker_thread;
    GMutex mutex;
    bool cancel_requested;
    bool finished;
    
    /* Engine Process */
    GPid engine_pid;
    gint param_stdin;
    gint param_stdout;
    
    /* Output */
    GameAnalysisResult* result;
};

/* --- Helpers --- */

/* Portable strtok_r implementation for Windows/MinGW */
static char* strtok_r_portable(char *str, const char *delim, char **saveptr) {
    char *token;
    if (str == NULL) str = *saveptr;
    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }
    token = str;
    str = str + strcspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
    } else {
        *str = '\0';
        *saveptr = str + 1;
    }
    return token;
}

static MoveCompact parse_move_compact(const char* uci_str) {
    MoveCompact m = {0};
    if (!uci_str || strlen(uci_str) < 4) return m;
    
    int c1 = uci_str[0] - 'a';
    int r1 = 8 - (uci_str[1] - '0');
    int c2 = uci_str[2] - 'a';
    int r2 = 8 - (uci_str[3] - '0');
    
    m.from_sq = (uint8_t)(r1 * 8 + c1);
    m.to_sq = (uint8_t)(r2 * 8 + c2);
    
    if (strlen(uci_str) > 4) {
        char p = uci_str[4];
        if (p == 'q') m.promo = 1;
        else if (p == 'r') m.promo = 2;
        else if (p == 'b') m.promo = 3;
        else if (p == 'n') m.promo = 4;
    }
    return m;
}

/* --- Engine Communication --- */

static bool spawn_engine(AiAnalysisJob* job) {
    gchar* argv[] = {(gchar*)job->config.engine_path, NULL};
    GError* err = NULL;
    
    if (!g_spawn_async_with_pipes(NULL, argv, NULL, 
                                  G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                  NULL, NULL, 
                                  &job->engine_pid, 
                                  &job->param_stdin, 
                                  &job->param_stdout, 
                                  NULL, &err)) {
        printf("Failed to spawn engine: %s\n", err->message);
        g_error_free(err);
        return false;
    }
    return true;
}

static void send_command(AiAnalysisJob* job, const char* cmd) {
    if (!job || job->param_stdin == -1) return;
    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "%s\n", cmd);
    
#ifdef _WIN32
    _write(job->param_stdin, buf, len);
#else
    write(job->param_stdin, buf, len);
#endif
}

/* Zero-copy style read - reads into a fixed buffer until newline */
static int read_line_fixed(int fd, char* buffer, int max_len) {
    int pos = 0;
    while (pos < max_len - 1) {
        char c;
#ifdef _WIN32
        int n = _read(fd, &c, 1);
#else
        int n = read(fd, &c, 1);
#endif
        if (n <= 0) return pos; /* EOF or Error */
        
        if (c == '\n') break;
        if (c != '\r') buffer[pos++] = c;
    }
    buffer[pos] = '\0';
    return pos;
}

/* --- Parser --- */

/* 
 * info depth 12 seldepth 18 multipv 1 score cp 34 nodes 1234 pv e2e4 e7e5 ...
 */
static void parse_info_line(char* line, PlyAnalysisRecord* record, int max_multipv) {
    if (!line || !record) return;
    if (strncmp(line, "info", 4) != 0) return;
    
    char* saved_ptr = NULL;
    char* token = strtok_r_portable(line, " ", &saved_ptr);
    
    int depth = 0, seldepth = 0, multipv = 1;
    int score_val = 0;
    uint8_t score_type = 0; // 0=cp
    uint8_t bound = 0; // 0=exact
    
    bool parsing_pv = false;
    int pv_idx = 0;
    PVLineCompact* current_line = NULL;
    
    while (token != NULL) {
        if (parsing_pv) {
            /* Remaining tokens are moves for the current MultiPV line */
            if (current_line && pv_idx < 16) {
                current_line->pv_moves[pv_idx++] = parse_move_compact(token);
                current_line->pv_len = pv_idx;
            }
        } else {
            if (strcmp(token, "depth") == 0) {
                token = strtok_r_portable(NULL, " ", &saved_ptr);
                if (token) depth = atoi(token);
            } else if (strcmp(token, "seldepth") == 0) {
                token = strtok_r_portable(NULL, " ", &saved_ptr);
                if (token) seldepth = atoi(token);
            } else if (strcmp(token, "multipv") == 0) {
                token = strtok_r_portable(NULL, " ", &saved_ptr);
                if (token) multipv = atoi(token);
            } else if (strcmp(token, "score") == 0) {
                token = strtok_r_portable(NULL, " ", &saved_ptr); /* cp or mate */
                if (token && strcmp(token, "mate") == 0) {
                    score_type = 1;
                }
                token = strtok_r_portable(NULL, " ", &saved_ptr); /* value */
                if (token) score_val = atoi(token);
                
                char* next = saved_ptr; /* Peak ahead for bound */
                while (*next == ' ') next++;
                if (strncmp(next, "lowerbound", 10) == 0) {
                    bound = 1;
                    strtok_r_portable(NULL, " ", &saved_ptr); // consume
                } else if (strncmp(next, "upperbound", 10) == 0) {
                    bound = 2;
                    strtok_r_portable(NULL, " ", &saved_ptr); // consume
                }
            } else if (strcmp(token, "pv") == 0) {
                parsing_pv = true;
                
                /* Prepare the line struct */
                if (multipv > 0 && multipv <= max_multipv) {
                    current_line = &record->lines[multipv - 1];
                    
                    /* Only update if depth is higher or equal */
                    if (depth >= current_line->depth) {
                        current_line->multipv_index = multipv;
                        current_line->depth = depth;
                        current_line->seldepth = seldepth;
                        current_line->score_type = score_type;
                        current_line->score_value = (int16_t)score_val;
                        current_line->bound = bound;
                        current_line->pv_len = 0; /* Reset moves */
                        pv_idx = 0;
                        
                        /* Update main record depth if this is PV 1 */
                        if (multipv == 1) {
                            record->depth_main = depth;
                            record->eval_white = score_val; /* TODO: Perspective correction later */
                            record->is_mate = (score_type == 1);
                            record->mate_dist_white = (score_type == 1) ? score_val : 0;
                        }
                        if (multipv > record->num_lines) record->num_lines = multipv;
                    } else {
                        /* Ignore this update (lower depth) */
                        current_line = NULL; 
                    }
                }
            }
        }
        token = strtok_r_portable(NULL, " ", &saved_ptr);
    token = strtok_r_portable(NULL, " ", &saved_ptr);
    }
}

/* --- Metrics & Post-Processing --- */

static bool str_equals_move(const char* uci_str, MoveCompact* m) {
    if (!uci_str || !m) return false;
    // Reconstruction uci string from compact
    char buf[6];
    // int c1 = m->from_sq % 8;
    // int r1 = 7 - (m->from_sq / 8); 
    
    // Inverse:
    // rank char = '8' - row_idx
    
    buf[0] = 'a' + (m->from_sq % 8);
    buf[1] = '8' - (m->from_sq / 8);
    buf[2] = 'a' + (m->to_sq % 8);
    buf[3] = '8' - (m->to_sq / 8);
    int len = 4;
    
    if (m->promo > 0) {
        char p = 'q';
        if (m->promo == 2) p = 'r';
        else if (m->promo == 3) p = 'b';
        else if (m->promo == 4) p = 'n';
        buf[4] = p;
        len = 5;
    }
    buf[len] = '\0';
    
    return (strncmp(uci_str, buf, len) == 0);
}

static int score_to_cp(int score, int type) {
    if (type == 1) { // Mate
        // Treat mate as large CP
        return (score > 0) ? 10000 - score : -10000 - score;
    }
    return score;
}

static void calculate_metrics(PlyAnalysisRecord* rec, const char* played_move_uci) {
    if (!rec || !played_move_uci) return;
    
    // 1. Find best move score
    int best_score = -32000;
    if (rec->num_lines > 0) {
        best_score = score_to_cp(rec->lines[0].score_value, rec->lines[0].score_type);
        // Note: PV lines stores score from SIDE TO MOVE perspective usually in raw UCI.
        // But in parse_info_line we stored raw value.
        // And in worker loop, we flipped it to White's perspective AFTER parsing.
        // calculate_metrics is called inside worker loop? 
        // We should call it AFTER the flip if we want consistency, or handle it here.
        // Let's call it AFTER perspective flip in worker_func.
        // So `lines[0].score_value` is White CP.
        
        // HOWEVER, CPL is "loss for the side who moved".
        // If White moved (ply 0, 2..), we want (BestWhite - PlayedWhite).
        // If Black moved (ply 1, 3..), we want (BestBlack - PlayedBlack).
        // Since `eval_white` is white-relative:
        // White move: loss = BestWhite - PlayedWhite
        // Black move: loss = ( -BestWhite ) - ( -PlayedWhite ) = PlayedWhite - BestWhite.
        // Wait. Black wants minimal White score. Best for black is lowest WhiteEval.
        // Loss = PlayedWhite - BestWhite (should be positive).
    }
    
    // 2. Find played move score
    int played_score = -32001; // Marker
    int rank = 0;
    
    for (int i = 0; i < rec->num_lines; i++) {
        // Check if primary move of PV line matches played move
        // PV moves stored in compact.
        // We need to compare specific moves.
        if (rec->lines[i].pv_len > 0) {
            MoveCompact* pv_m = &rec->lines[i].pv_moves[0];
            if (str_equals_move(played_move_uci, pv_m)) {
                played_score = score_to_cp(rec->lines[i].score_value, rec->lines[i].score_type);
                rank = i + 1;
                break;
            }
        }
    }
    
    if (played_score == -32001) {
        // Played move not in MultiPV. Assume it is worse than the worst PV line.
        // Or if we only have 1 PV, we don't know much.
        if (rec->num_lines > 0) {
             // Heuristic: take worst PV score minus significant penalty?
             // Or just 0 eval if we don't know.
             // For strict CPL, we can't calculate accuractely without search.
             // Falback: use previous iteration or 0.
             // Let's use last PV score - 50cp as heuristic for now.
             int last_score = score_to_cp(rec->lines[rec->num_lines-1].score_value, rec->lines[rec->num_lines-1].score_type);
             
             // Perspective dependent direction.
             // If white to move, worse = lower.
             // If black to move, worse = higher.
             if (rec->side_to_move == 0) played_score = last_score - 100;
             else played_score = last_score + 100;
        } else {
             played_score = best_score; // No info
        }
    }
    
    rec->played_move_eval = played_score;
    rec->best_move_rank = rank;
    
    // 3. Calc CPL
    int cpl = 0;
    if (rec->side_to_move == 0) { // White
        cpl = best_score - played_score;
    } else { // Black (Best is Lower)
        cpl = played_score - best_score;
    }
    
    // Clamping (CPL shouldn't be negative unless search depth variation noise)
    if (cpl < 0) cpl = 0;
    // Cap enormous values (mate misses)
    if (cpl > 2000) cpl = 2000;
    
    rec->cpl = (int16_t)cpl;
    
    // 4. Labelling
    // Thresholds:
    // 0-10: Best
    // 11-25: Excellent/Good
    // 26-50: Inaccuracy
    // 51-150: Mistake
    // 150+: Blunder
    // Rank 1 is always Best.
    
    if (rank == 1 || cpl <= 10) rec->label = 1; // Best
    else if (cpl <= 25) rec->label = 2; // Excellent
    else if (cpl <= 50) rec->label = 3; // Good
    else if (cpl <= 150) rec->label = 5; // Mistake
    else rec->label = 6; // Blunder
    
    // Special: Forced mate found vs missed?
    // Move rank 1 is "Best" (Label 1)
    
    // Check for "Only Move"
    // If only 1 legal move? logic needed
    // Or if MultiPV showed huge gap between #1 and #2.
    if (rec->num_lines >= 2) {
        int sc1 = score_to_cp(rec->lines[0].score_value, rec->lines[0].score_type);
        int sc2 = score_to_cp(rec->lines[1].score_value, rec->lines[1].score_type);
        int diff = abs(sc1 - sc2); // Scores are consistent frame
        if (diff > 150) rec->is_only_move = true; // Heuristic
    }
}

static void finalize_game_stats(GameAnalysisResult* res) {
    if (!res) return;
    
    long white_cp_sum = 0;
    int white_moves = 0;
    long black_cp_sum = 0;
    int black_moves = 0;
    
    for (int i = 0; i < res->total_plies; i++) {
        PlyAnalysisRecord* r = &res->plies[i];
        if (r->side_to_move == 0) {
            white_cp_sum += r->cpl;
            white_moves++;
            if (r->label == 5) res->white_mistakes++;
            if (r->label == 6) res->white_blunders++;
        } else {
            black_cp_sum += r->cpl;
            black_moves++;
            if (r->label == 5) res->black_mistakes++;
            if (r->label == 6) res->black_blunders++;
        }
    }
    
    res->white_acpl = (white_moves > 0) ? (float)white_cp_sum / white_moves : 0;
    res->black_acpl = (black_moves > 0) ? (float)black_cp_sum / black_moves : 0;
}

static void* worker_func(void* data) {
    AiAnalysisJob* job = (AiAnalysisJob*)data;
    if (!job) return NULL;
    
    if (!spawn_engine(job)) {
        job->finished = true;
        if (job->complete_cb) job->complete_cb(NULL, job->user_data);
        return NULL;
    }
    
    /* Setup Engine */
    send_command(job, "uci");
    
    char buffer[4096];
    bool ready = false;
    
    /* Wait for uciok/readyok loop */
    while (!ready && !job->cancel_requested) {
        if (read_line_fixed(job->param_stdout, buffer, sizeof(buffer)) > 0) {
            if (strstr(buffer, "uciok")) {
                char opt[128];
                snprintf(opt, sizeof(opt), "setoption name MultiPV value %d", job->config.multipv);
                send_command(job, opt);
                snprintf(opt, sizeof(opt), "setoption name Hash value %d", job->config.hash_size);
                send_command(job, opt);
                snprintf(opt, sizeof(opt), "setoption name Threads value %d", job->config.threads);
                send_command(job, opt);
                send_command(job, "isready");
            }
            if (strstr(buffer, "readyok")) ready = true;
        }
    }
    
    /* Initialize Result */
    job->result = g_new0(GameAnalysisResult, 1);
    job->result->total_plies = job->num_moves;
    job->result->plies = g_new0(PlyAnalysisRecord, job->num_moves);
    job->result->ref_count = 1;
    
    /* Pass 1: Analysis Loop */
    char move_list_str[8192] = ""; /* Accumulator for moves */
    
    for (int i = 0; i < job->num_moves; i++) {
        if (job->cancel_requested) break;
        
        /* Build position command */
        /* TODO: Efficiency - don't obscure logic with giant string cats. 
         * But we need history. */
        
        if (i > 0) {
            snprintf(move_list_str, sizeof(move_list_str), " %s", job->uci_moves[i-1]);
        }
        
        /* Send Position */
        char cmd[8250];
        if (job->start_fen) {
             /* Handle non-standard start if needed */
        } 
        /* Use startpos assumption for now or fen */
        if (i == 0) move_list_str[0] = '\0'; 
        
        /* NOTE: We must send entire history */
        /* Re-building move_list_str buffer logic carefully: */
        /* Currently strict apppend. */
        
        snprintf(cmd, sizeof(cmd), "position startpos moves %s", move_list_str);
        send_command(job, cmd);
        
        /* Go */
        snprintf(cmd, sizeof(cmd), "go movetime %d", job->config.move_time_pass1);
        send_command(job, cmd);
        
        /* Parse output until bestmove */
        bool move_done = false;
        PlyAnalysisRecord* rec = &job->result->plies[i];
        rec->ply_index = i;
        rec->side_to_move = (i % 2); /* 0=White, 1=Black */
        
        while (!move_done && !job->cancel_requested) {
            if (read_line_fixed(job->param_stdout, buffer, sizeof(buffer)) > 0) {
                if (strncmp(buffer, "bestmove", 8) == 0) {
                    move_done = true;
                } else {
                    parse_info_line(buffer, rec, job->config.multipv);
                }
            }
        }
        
        /* Post-move processing: Perspective Flip */
        if (rec->side_to_move == 1) { /* Black to move */
            rec->eval_white = -rec->eval_white;
            if (rec->is_mate) rec->mate_dist_white = -rec->mate_dist_white;
            
            for (int k = 0; k < rec->num_lines; k++) {
                rec->lines[k].score_value = -rec->lines[k].score_value;
            }
        }
        
        /* Calculate Metrics (CPL, Rank, Label) */
        /* Must be done AFTER perspective flip so scores are White-relative, 
           BUT calculate_metrics expects consistent frame to diff them.
           Actually, my calculate_metrics implementation handles side-to-move diffing.
           It expects `rec->lines[0].score_value` to be WHITE eval.
           Which is true now.
        */
        calculate_metrics(rec, job->uci_moves[i]);
        
        /* Notify Progress */
        if (job->progress_cb) {
            job->progress_cb(i + 1, job->num_moves, job->user_data);
        }
    }
    
    /* Cleanup Engine */
    send_command(job, "quit");
    
#ifdef _WIN32
    _close(job->param_stdin);
    _close(job->param_stdout);
    g_spawn_close_pid(job->engine_pid);
#else
    close(job->param_stdin);
    close(job->param_stdout);
#endif
    
    job->finished = true;
    if (job->complete_cb && !job->cancel_requested) {
        finalize_game_stats(job->result);
        job->complete_cb(job->result, job->user_data);
    }
    
    return NULL;
}

/* --- Public API --- */

AiAnalysisJob* ai_analysis_start(const char* start_fen, 
                                 char** uci_moves, 
                                 int num_moves, 
                                 AnalysisConfig config,
                                 AnalysisProgressCb progress_cb,
                                 AnalysisCompleteCb complete_cb,
                                 void* user_data) 
{
    AiAnalysisJob* job = g_new0(AiAnalysisJob, 1);
    job->config = config;
    if (start_fen) job->start_fen = g_strdup(start_fen);
    
    job->uci_moves = g_new0(char*, num_moves);
    for (int i=0; i<num_moves; i++) job->uci_moves[i] = g_strdup(uci_moves[i]);
    job->num_moves = num_moves;
    
    job->progress_cb = progress_cb;
    job->complete_cb = complete_cb;
    job->user_data = user_data;
    
    g_mutex_init(&job->mutex);
    
    job->worker_thread = g_thread_new("AnalysisWorker", worker_func, job);
    
    return job;
}

void ai_analysis_cancel(AiAnalysisJob* job) {
    if (!job) return;
    g_mutex_lock(&job->mutex);
    job->cancel_requested = true;
    g_mutex_unlock(&job->mutex);
}

void ai_analysis_free(AiAnalysisJob* job) {
    if (!job) return;
    /* Normally we join thread, but if it's detached or we rely on flag... */
    /* Implementation detail: assumes caller cancelled and thread finished */
    
    if (job->worker_thread) g_thread_unref(job->worker_thread);
    g_mutex_clear(&job->mutex);
    
    g_free(job->start_fen);
    for (int i=0; i<job->num_moves; i++) g_free(job->uci_moves[i]);
    g_free(job->uci_moves);
    g_free(job);
}

/* Result Ref-Counting */
GameAnalysisResult* ai_analysis_result_ref(GameAnalysisResult* res) {
    if (res) g_atomic_int_inc(&res->ref_count);
    return res;
}

void ai_analysis_result_unref(GameAnalysisResult* res) {
    if (!res) return;
    if (g_atomic_int_dec_and_test(&res->ref_count)) {
        if (res->plies) g_free(res->plies);
        g_free(res);
    }
}

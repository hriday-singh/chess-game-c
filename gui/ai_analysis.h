#ifndef AI_ANALYSIS_H
#define AI_ANALYSIS_H

#include <stdint.h>
#include <stdbool.h>
#include <glib.h>

/* --- Compact Data Structures (Memory-Light) --- */

/* 3-byte move representation */
typedef struct MoveCompact {
    uint8_t from_sq;   /* 0-63 */
    uint8_t to_sq;     /* 0-63 */
    uint8_t promo;     /* 0=None, 1=Q, 2=R, 3=B, 4=N */
} MoveCompact;

/* Single MultiPV Line */
typedef struct PVLineCompact {
    uint8_t multipv_index; /* 1-based usually */
    uint16_t depth;
    uint16_t seldepth;
    
    uint8_t score_type;    /* 0=cp, 1=mate */
    int16_t score_value;   /* cp or mate distance. White perspective. */
    
    uint8_t bound;         /* 0=exact, 1=lower, 2=upper */
    
    uint8_t pv_len;
    MoveCompact pv_moves[16]; /* Fixed limit for PV length to avoid heap churn */
} PVLineCompact;

/* Move Classification Labels */
typedef enum {
    LABEL_NONE = 0,
    LABEL_BEST,        /* Rank 1 or very low CPL */
    LABEL_EXCELLENT,   /* Low CPL */
    LABEL_GOOD,        /* Acceptable CPL */
    LABEL_INACCURACY,  /* noticeable eval drop */
    LABEL_MISTAKE,     /* significant eval drop */
    LABEL_BLUNDER,     /* huge eval drop or missed mate */
    LABEL_BRILLIANT    /* sacrifice with compensation (optional) */
} MoveLabel;

/* Analysis Data for a single Ply (Position BEFORE move i is played? Or Result of move i? 
 * Convention: PlyAnalysisRecord[i] corresponds to the position AFTER move i-1 and BEFORE move i.
 * But metrics (CPL) compare PRE-move i and POST-move i. 
 * We will store it as: Record [i] is analysis of the move PLAYED at ply i.
 * (i.e., Record[0] = analysis of White's first move, from Startpos)
 */
typedef struct PlyAnalysisRecord {
    int ply_index;
    uint8_t side_to_move; /* 0=White, 1=Black */

    /* Position Evaluation (Before move) - White Perspective */
    int16_t eval_white;
    uint8_t is_mate;      /* 1 if mate found */
    int16_t mate_dist_white;

    /* Engine Stats */
    uint16_t depth_main;
    uint8_t num_lines;    /* How many MultiPV lines stored */
    PVLineCompact lines[5]; /* Fixed max MultiPV (e.g. 5) */

    /* Derived Metrics */
    int16_t played_move_eval; /* Eval of the move actually played */
    uint8_t best_move_rank;   /* 1..K, or 0 if not in top K */
    int16_t cpl;              /* Centipawn loss (always positive) */
    
    uint8_t label;            /* MoveLabel */
    bool is_only_move;
    
    /* Flags */
    bool is_critical;         /* Marked for pass-2 refinement */
} PlyAnalysisRecord;

/* Final Immutable Result Blob */
typedef struct GameAnalysisResult {
    int total_plies;
    PlyAnalysisRecord* plies; /* Array of [total_plies] */
    
    /* Summary Stats */
    float white_acpl;
    float black_acpl;
    int white_blunders;
    int black_blunders;
    int white_mistakes;
    int black_mistakes;
    /* Add more as needed */
    
    /* Reference count for safe sharing */
    volatile int ref_count;
} GameAnalysisResult;

/* --- Job Control --- */

typedef struct _AiAnalysisJob AiAnalysisJob;

typedef struct {
    int multipv;          /* e.g. 3 or 5 */
    int threads;          /* e.g. 1 or 2 */
    int hash_size;        /* MB */
    int move_time_pass1;  /* ms per move, baseline */
    int move_time_pass2;  /* ms per move, critical positions */
    bool do_pass2;        /* Enable refinement pass */
    
    const char* engine_path; /* Path to stockfish executable */
} AnalysisConfig;

/* Callbacks */
typedef void (*AnalysisProgressCb)(int ply_done, int total_plies, void* user_data);
typedef void (*AnalysisCompleteCb)(GameAnalysisResult* result, void* user_data);

/* Job API */
AiAnalysisJob* ai_analysis_start(const char* start_fen, 
                                 char** uci_moves, 
                                 int num_moves, 
                                 AnalysisConfig config,
                                 AnalysisProgressCb progress_cb,
                                 AnalysisCompleteCb complete_cb,
                                 void* user_data);

void ai_analysis_cancel(AiAnalysisJob* job);
void ai_analysis_free(AiAnalysisJob* job); /* Frees job handle, not result */

/* Result API */
GameAnalysisResult* ai_analysis_result_ref(GameAnalysisResult* res);
void ai_analysis_result_unref(GameAnalysisResult* res);

#endif /* AI_ANALYSIS_H */

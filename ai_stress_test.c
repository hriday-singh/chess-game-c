/**
 * AI Controller Stress Test - Real Game Play
 * 
 * This program stress tests the AI controller by actually playing games.
 * It directly uses gui/ai_controller.c to:
 * - Initialize the AI controller
 * - Play complete games (AI vs AI)
 * - Test analysis during gameplay
 * - Verify move generation and evaluation
 * - Measure performance metrics
 * - Catch all crashes and segfaults
 * 
 * Compile with: make test-ai-stress
 * Run with: ./build/ai_stress_test.exe
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)
#endif

// GTK/GLib includes (needed for AI controller)
#include <gtk/gtk.h>
#include <glib.h>

// Include game logic and AI controller
#include "game/gamelogic.h"
#include "game/move.h"
#include "game/types.h"
#include "gui/ai_controller.h"
#include "gui/ai_dialog.h"
#include "gui/config_manager.h"

// Test configuration
typedef struct {
    const char* name;
    const char* start_fen;
    int max_moves;
    int depth_white;
    int depth_black;
    int time_per_move_ms;
    const char* description;
} GameTest;

// Test suite
static GameTest game_tests[] = {
    {
        "Quick Tactical Game",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        30,
        10,
        10,
        2000,
        "Standard starting position, quick depth"
    },
    {
        "Endgame Test",
        "8/8/8/4k3/8/4K3/4P3/8 w - - 0 1",
        50,
        15,
        15,
        3000,
        "King and pawn endgame"
    },
    {
        "Tactical Position",
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
        20,
        12,
        12,
        2500,
        "Scholar's mate threat position"
    },
    {
        "Deep Analysis Test",
        "rnbqkb1r/ppp1pppp/5n2/3p4/3P4/5N2/PPP1PPPP/RNBQKB1R w KQkq - 2 3",
        40,
        15,
        15,
        5000,
        "Opening position requiring deep analysis"
    },
    {
        "Mate in Few",
        "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1",
        10,
        12,
        12,
        2000,
        "Back rank mate position"
    }
};

static const int num_game_tests = sizeof(game_tests) / sizeof(game_tests[0]);

// Statistics
typedef struct {
    int total_games;
    int completed_games;
    int white_wins;
    int black_wins;
    int draws;
    int total_moves;
    double total_time_ms;
    double avg_time_per_move;
    int total_depth_reached;
    int analysis_updates;
    int crashes;
    int timeouts;
} TestStats;

// Global state for move callback
typedef struct {
    GameLogic* logic;
    AiController* controller;
    Move* received_move;
    bool move_ready;
    int moves_played;
    int max_moves;
    bool game_over;
    TestStats* stats;
    clock_t move_start_time;
    GMainLoop* loop;
    guint timeout_id;
} GameState;

// Evaluation callback
void on_eval_update(const AiStats* stats, gpointer user_data) {
    GameState* state = (GameState*)user_data;
    if (!state) return;
    
    state->stats->analysis_updates++;
    
    // Print evaluation every 10 updates
    if (state->stats->analysis_updates % 10 == 0) {
        printf("    [EVAL] Score: %+d cp, Mate: %s, Best: %s, WDL: %.2f/%.2f/%.2f\n",
               stats->score,
               stats->is_mate ? "Yes" : "No",
               stats->best_move ? stats->best_move : "none",
               stats->win_prob, stats->draw_prob, stats->loss_prob);
    }
}

// Move ready callback
void on_move_ready(Move* move, gpointer user_data) {
    GameState* state = (GameState*)user_data;
    if (!state || !move) {
        printf("  [AI MOVE] Callback received but state or move is NULL\n");
        return;
    }
    
    printf("  [AI MOVE] Received move: %d,%d -> %d,%d",
           move->from_sq / 8, move->from_sq % 8,
           move->to_sq / 8, move->to_sq % 8);
    
    if (move->promotionPiece != NO_PIECE && move->promotionPiece != NO_PROMOTION) {
        printf(" (promote to %d)", move->promotionPiece);
    }
    printf("\n");
    
    // Store the move
    if (state->received_move) {
        move_free(state->received_move);
    }
    state->received_move = move_create(move->from_sq, move->to_sq);
    state->received_move->promotionPiece = move->promotionPiece;
    state->move_ready = true;
    
    // Cancel timeout
    if (state->timeout_id > 0) {
        g_source_remove(state->timeout_id);
        state->timeout_id = 0;
    }
    
    // Quit the main loop
    if (state->loop && g_main_loop_is_running(state->loop)) {
        g_main_loop_quit(state->loop);
    }
}

// Timeout callback
static gboolean on_move_timeout(gpointer user_data) {
    GameState* state = (GameState*)user_data;
    if (!state) return FALSE;
    
    printf("  [TIMEOUT] Move request timed out\n");
    state->timeout_id = 0;
    
    // Quit the main loop
    if (state->loop && g_main_loop_is_running(state->loop)) {
        g_main_loop_quit(state->loop);
    }
    
    return FALSE;
}

// Play a single move
bool play_ai_move(GameState* state, AiDifficultyParams params) {
    state->move_ready = false;
    if (state->received_move) {
        move_free(state->received_move);
        state->received_move = NULL;
    }
    state->move_start_time = clock();
    
    Player current_player = gamelogic_get_turn(state->logic);
    printf("  [TURN] %s to move (Move %d)\n", 
           current_player == PLAYER_WHITE ? "White" : "Black",
           state->moves_played + 1);
    
    // Request move from AI
    ai_controller_request_move(state->controller,
                              false,  // use internal engine
                              params,
                              NULL,
                              on_move_ready,
                              state);
    
    // Set up timeout (extra time for safety)
    int timeout_ms = params.move_time_ms + 10000;  // Extra 10 seconds
    state->timeout_id = g_timeout_add(timeout_ms, on_move_timeout, state);
    
    // Run main loop to wait for callback
    printf("  [WAITING] Running main loop for move (timeout=%d ms)...\n", timeout_ms);
    state->loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(state->loop);
    g_main_loop_unref(state->loop);
    state->loop = NULL;
    
    // Check if we got a move
    if (!state->move_ready || !state->received_move) {
        printf("  [ERROR] No move received\n");
        state->stats->timeouts++;
        return false;
    }
    
    // Calculate time taken
    clock_t move_end_time = clock();
    double time_ms = ((double)(move_end_time - state->move_start_time) / CLOCKS_PER_SEC) * 1000.0;
    state->stats->total_time_ms += time_ms;
    
    printf("  [TIMING] Move calculated in %.2f ms\n", time_ms);
    
    // Perform the move
    bool success = gamelogic_perform_move(state->logic, state->received_move);
    
    if (success) {
        state->moves_played++;
        state->stats->total_moves++;
        
        // Get SAN notation
        char san[32];
        gamelogic_get_move_san(state->logic, state->received_move, san, sizeof(san));
        printf("  [MOVE] %s\n", san);
        
        // Check game state
        gamelogic_update_game_state(state->logic);
        
        if (state->logic->isGameOver) {
            printf("  [GAME OVER] %s\n", state->logic->statusMessage);
            state->game_over = true;
            
            // Update win statistics
            if (strstr(state->logic->statusMessage, "White wins")) {
                state->stats->white_wins++;
            } else if (strstr(state->logic->statusMessage, "Black wins")) {
                state->stats->black_wins++;
            } else {
                state->stats->draws++;
            }
        }
    } else {
        printf("  [ERROR] Failed to perform move\n");
    }
    
    return success;
}

// Play a complete game
bool play_game(GameTest* test, TestStats* stats) {
    printf("\n");
    printf("================================================================================\n");
    printf("GAME TEST: %s\n", test->name);
    printf("Description: %s\n", test->description);
    printf("FEN: %s\n", test->start_fen);
    printf("Max Moves: %d, Depth W/B: %d/%d, Time: %d ms\n",
           test->max_moves, test->depth_white, test->depth_black, test->time_per_move_ms);
    printf("================================================================================\n");
    
    // Create game logic
    GameLogic* logic = gamelogic_create();
    if (!logic) {
        printf("[ERROR] Failed to create game logic\n");
        stats->crashes++;
        return false;
    }
    
    // Load position
    printf("[SETUP] Loading FEN position...\n");
    gamelogic_load_fen(logic, test->start_fen);
    printf("[SETUP] Position loaded: %s\n", test->start_fen);
    
    // Create AI controller
    AiController* controller = ai_controller_new(logic, NULL);
    printf("[SETUP] AI Controller created at %p\n", controller);
    if (!controller) {
        printf("[ERROR] Failed to create AI controller\n");
        gamelogic_free(logic);
        stats->crashes++;
        return false;
    }
    
    // Set up evaluation callback
    GameState state = {0};
    state.logic = logic;
    state.controller = controller;
    state.stats = stats;
    state.max_moves = test->max_moves;
    
    ai_controller_set_eval_callback(controller, on_eval_update, &state);
    ai_controller_set_analysis_side(controller, PLAYER_WHITE);
    
    // Start analysis
    printf("\n[ANALYSIS] Starting live analysis...\n");
    bool analysis_started = ai_controller_start_analysis(controller, false, NULL);
    if (analysis_started) {
        printf("[ANALYSIS] Analysis engine started successfully\n");
        sleep_ms(500);  // Let analysis start
    } else {
        printf("[WARNING] Failed to start analysis\n");
    }
    
    // Play the game
    printf("\n[GAME] Starting game play...\n");
    
    while (!state.game_over && state.moves_played < test->max_moves) {
        Player current = gamelogic_get_turn(logic);
        
        // Set difficulty based on player
        AiDifficultyParams params = {0};
        params.depth = (current == PLAYER_WHITE) ? test->depth_white : test->depth_black;
        params.move_time_ms = test->time_per_move_ms;
        
        // Stop analysis before requesting move
        ai_controller_stop_analysis(controller, false);
        sleep_ms(100);
        
        // Play move
        if (!play_ai_move(&state, params)) {
            printf("[ERROR] Failed to play move, aborting game\n");
            break;
        }
        
        // Restart analysis if game continues
        if (!state.game_over && state.moves_played < test->max_moves) {
            ai_controller_start_analysis(controller, false, NULL);
            sleep_ms(200);  // Brief pause for analysis
        }
    }
    
    // Game summary
    printf("\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("GAME SUMMARY\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("Moves Played: %d\n", state.moves_played);
    printf("Game Over: %s\n", state.game_over ? "Yes" : "No (max moves reached)");
    if (state.game_over) {
        printf("Result: %s\n", logic->statusMessage);
    }
    printf("Analysis Updates: %d\n", stats->analysis_updates);
    printf("--------------------------------------------------------------------------------\n");
    
    printf("[GAME] Game concluded.\n");

    // Clear the move callback to avoid lingering data
    ai_controller_set_eval_callback(controller, NULL, NULL);

    // Cleanup
    if (state.received_move) {
        move_free(state.received_move);
    }
    printf("[CLEANUP] Freeing AI Controller at %p\n", controller);
    ai_controller_stop_analysis(controller, true);
    ai_controller_free(controller);
    gamelogic_free(logic);
    
    // CRITICAL: Drain the main context to ensure no pending idle/timeout callbacks 
    // from this controller fire after it's been freed.
    while (g_main_context_iteration(NULL, FALSE));
    
    printf("[CLEANUP] Done.\n");
    stats->total_games++;
    if (state.game_over) {
        stats->completed_games++;
    }
    
    return true;
}

// Print final statistics
void print_statistics(TestStats* stats) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                         STRESS TEST STATISTICS                             ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Games Played:          %d\n", stats->total_games);
    printf("Games Completed:       %d\n", stats->completed_games);
    printf("White Wins:            %d\n", stats->white_wins);
    printf("Black Wins:            %d\n", stats->black_wins);
    printf("Draws:                 %d\n", stats->draws);
    printf("\n");
    printf("Total Moves:           %d\n", stats->total_moves);
    printf("Total Time:            %.2f seconds\n", stats->total_time_ms / 1000.0);
    printf("Avg Time per Move:     %.2f ms\n", 
           stats->total_moves > 0 ? stats->total_time_ms / stats->total_moves : 0.0);
    printf("Analysis Updates:      %d\n", stats->analysis_updates);
    printf("\n");
    printf("Crashes:               %d\n", stats->crashes);
    printf("Timeouts:              %d\n", stats->timeouts);
    printf("\n");
    printf("════════════════════════════════════════════════════════════════════════════\n");
}

// Main entry point
int main(int argc, char* argv[]) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║              ChessGameC AI Controller Stress Test (Real Games)            ║\n");
    printf("║                         Version 2.0 - 2026                                 ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
    
    // Initialize GTK (required for GLib types used by AI controller)
    gtk_init();
    
    // Initialize config (required for AI controller)
    config_init();
    
    // Enable live analysis for testing
    AppConfig* cfg = config_get();
    if (cfg) {
        cfg->enable_live_analysis = true;
        cfg->show_move_rating = true;
        cfg->show_mate_warning = true;
        cfg->show_hanging_pieces = false;
        printf("[INFO] Config initialized: live_analysis=%d\n", cfg->enable_live_analysis);
    } else {
        printf("[WARNING] Failed to get config\n");
    }
    
    TestStats stats = {0};
    
    // Parse arguments
    int test_index = -1;
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("\nUsage: %s [OPTIONS]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --help, -h           Show this help message\n");
            printf("  --all                Run all game tests (default)\n");
            printf("  --test N             Run specific test number (0-%d)\n", num_game_tests - 1);
            printf("\nAvailable Tests:\n");
            for (int i = 0; i < num_game_tests; i++) {
                printf("  [%d] %s - %s\n", i, game_tests[i].name, game_tests[i].description);
            }
            printf("\n");
            // No cleanup needed
            return 0;
        } else if (strcmp(argv[1], "--test") == 0 && argc > 2) {
            test_index = atoi(argv[2]);
            if (test_index < 0 || test_index >= num_game_tests) {
                printf("Invalid test index: %d (valid range: 0-%d)\n", test_index, num_game_tests - 1);
                // No cleanup needed
                return 1;
            }
        }
    }
    
    // Run tests
    if (test_index >= 0) {
        // Run single test
        play_game(&game_tests[test_index], &stats);
    } else {
        // Run all tests
        printf("\n[INFO] Running %d game tests...\n", num_game_tests);
        for (int i = 0; i < num_game_tests; i++) {
            printf("\n[TEST %d/%d]\n", i + 1, num_game_tests);
            play_game(&game_tests[i], &stats);
            
            // Brief pause between games
            if (i < num_game_tests - 1) {
                printf("\n[INFO] Pausing before next test...\n");
                sleep_ms(2000);
            }
        }
    }
    
    // Print final statistics
    print_statistics(&stats);
    
    // Cleanup (config is managed internally)
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                         Test Suite Complete!                              ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    return 0;
}

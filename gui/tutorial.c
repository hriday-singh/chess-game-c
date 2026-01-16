#include "tutorial.h"
#include <stdio.h>
#include "board_widget.h"
#include "sound_engine.h"
#include "piece.h"
#include "gamelogic.h"
#include "right_side_panel.h"
#include "clock_widget.h"

static bool debug_mode = false;

// Forward declarations
static void tutorial_setup_pawn(AppState* state);
static void tutorial_setup_rook(AppState* state);
static void tutorial_setup_bishop(AppState* state);
static void tutorial_setup_knight(AppState* state);
static void tutorial_setup_queen(AppState* state);
static void tutorial_setup_check(AppState* state);
static void tutorial_setup_escape(AppState* state);
static void tutorial_setup_castling(AppState* state);
static void tutorial_setup_mate(AppState* state);
static void tutorial_finish(AppState* state);

static gboolean on_tutorial_delay_complete(gpointer user_data) {
    if (!user_data) return FALSE;
    AppState* state = (AppState*)user_data;
    
    // Safety check: if tutorial was disabled during delay (e.g. exit clicked), abort
    if (state->tutorial.step == TUT_OFF) {
        state->tutorial.wait = FALSE;
        return FALSE;
    }

    state->tutorial.step = state->tutorial.next_step;
    state->tutorial.wait = FALSE; // Reset wait flag
    
    // Call setup for the new step
    switch (state->tutorial.step) {
        case TUT_ROOK: tutorial_setup_rook(state); break;
        case TUT_BISHOP: tutorial_setup_bishop(state); break;
        case TUT_KNIGHT: tutorial_setup_knight(state); break;
        case TUT_QUEEN: tutorial_setup_queen(state); break;
        case TUT_CHECK: tutorial_setup_check(state); break;
        case TUT_ESCAPE: tutorial_setup_escape(state); break;
        case TUT_CASTLING: tutorial_setup_castling(state); break;
        case TUT_MATE: tutorial_setup_mate(state); break;
        case TUT_DONE: tutorial_finish(state); break;
    }
    
    return FALSE;
}

static void on_message_dialog_ok(GtkButton* btn, gpointer user_data) {
    (void)btn;
    GtkWidget* window = (GtkWidget*)user_data;
    g_object_set_data(G_OBJECT(window), "user-accepted", GINT_TO_POINTER(1));
    gtk_window_destroy(GTK_WINDOW(window));
}

static void on_message_dialog_destroy(GtkWidget* window, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    
    // Clear the active message reference
    if (state->gui.tutorial_msg == window) {
        state->gui.tutorial_msg = NULL;
    }

    if (!g_object_get_data(G_OBJECT(window), "user-accepted")) {
        // User closed via X -> Exit tutorial
        if (state && state->tutorial.step != TUT_OFF) {
             on_tutorial_exit(NULL, state);
        }
    } else {
        // User accepted (OK)
        // Transition from Intro -> Pawn
        if (state && state->tutorial.step == TUT_INTRO) {
             state->tutorial.step = TUT_PAWN;
             tutorial_setup_pawn(state);
        }
    }
    
    // FIX: Put main app in focus
    if (state && state->gui.window) {
        gtk_window_present(state->gui.window);
    }
}

// Helper to update specific tutorial step UI
void show_message_dialog(GtkWindow* parent, const char* message, AppState* state) {
    if (state->gui.tutorial_msg) {
        // Reuse existing dialog
        GtkWidget* window = state->gui.tutorial_msg;
        GtkLabel* label = GTK_LABEL(g_object_get_data(G_OBJECT(window), "msg-label"));
        if (label) {
            gtk_label_set_text(label, message);
        }
        gtk_window_present(GTK_WINDOW(window));
        return;
    }

    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Tutorial");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
    
    state->gui.tutorial_msg = window; // Track it!
    
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
    g_object_set_data(G_OBJECT(window), "msg-label", label); // Store reference
    gtk_box_append(GTK_BOX(box), label);
    
    GtkWidget* btn = gtk_button_new_with_label("OK");
    gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_message_dialog_ok), window);
    gtk_box_append(GTK_BOX(box), btn);
    
    if (state) {
        g_signal_connect(window, "destroy", G_CALLBACK(on_message_dialog_destroy), state);
    }
    
    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_widget_set_visible(window, TRUE);
}


// Helper to update specific tutorial step UI
static void tutorial_update_view(AppState* state, const char* instruction, const char* learning) {
    if (state && state->gui.info_panel) {
        info_panel_update_tutorial_info(state->gui.info_panel, instruction, learning);
    }
    // Restore popup functionality as requested
    if (state && state->gui.window) {
        // Use the instruction as the message
        show_message_dialog(GTK_WINDOW(state->gui.window), instruction, state);
    }
}

// --- Setup Functions ---

static void tutorial_clear_board(AppState* state) {
    if (!state || !state->logic) return;
    
    // Full reset of board array and piece memory
    for (int r=0; r<8; r++) {
        for (int c=0; c<8; c++) {
            if (state->logic->board[r][c]) {
                piece_free(state->logic->board[r][c]); // Free memory
                state->logic->board[r][c] = NULL; 
            }
        }
    }
    // Clean reset of game logic state
    state->logic->turn = PLAYER_WHITE; 
    state->logic->isGameOver = FALSE;
    // Clearing history to prevent undo issues
    
    board_widget_reset_selection(state->gui.board);
    board_widget_refresh(state->gui.board);
}

// Reset current tutorial step
void tutorial_reset_step(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* state = (AppState*)user_data;
    if (!state || state->tutorial.step == TUT_OFF) return;
    
    sound_engine_play(SOUND_RESET);
    state->tutorial.wait = FALSE;
    
    switch (state->tutorial.step) {
        case TUT_PAWN: tutorial_setup_pawn(state); break;
        case TUT_ROOK: tutorial_setup_rook(state); break;
        case TUT_BISHOP: tutorial_setup_bishop(state); break;
        case TUT_KNIGHT: tutorial_setup_knight(state); break;
        case TUT_QUEEN: tutorial_setup_queen(state); break;
        case TUT_CHECK: tutorial_setup_check(state); break;
        case TUT_ESCAPE: tutorial_setup_escape(state); break;
        case TUT_CASTLING: tutorial_setup_castling(state); break;
        case TUT_MATE: tutorial_setup_mate(state); break;
        case TUT_DONE: tutorial_finish(state); break;
        default: break;
    }
}

static void tutorial_setup_pawn(AppState* state) {
    tutorial_clear_board(state);
    state->tutorial.wait = FALSE; // Force clear
    state->logic->board[6][3] = piece_create(PIECE_PAWN, PLAYER_WHITE);
    // Add Black King to prevent stalemate
    state->logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK);
    board_widget_set_nav_restricted(state->gui.board, true, 6, 3, 4, 3);
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state, 
        "Pawns move forward 1 square, but on their first move they can jump 2 squares.\n\nTask: Move the white pawn from d2 to d4.",
        "The Pawn");
}

static void tutorial_setup_rook(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[4][4] = piece_create(PIECE_ROOK, PLAYER_WHITE); // e4
    state->logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK); // a8
    board_widget_set_nav_restricted(state->gui.board, true, 4, 4, 0, 4); // e4 -> e8
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "Rooks move in straight lines (horizontally or vertically) as far as they want.\n\nTask: Move the Rook from e4 to e8.",
        "The Rook");
}

static void tutorial_setup_bishop(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[7][2] = piece_create(PIECE_BISHOP, PLAYER_WHITE); // c1
    state->logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK); // a8
    board_widget_set_nav_restricted(state->gui.board, true, 7, 2, 2, 7); // c1 -> h6
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "Bishops move diagonally as far as they want.\n\nTask: Move the Bishop from c1 to h6.",
        "The Bishop");
}

static void tutorial_setup_knight(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[7][1] = piece_create(PIECE_KNIGHT, PLAYER_WHITE); // b1
    state->logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK); // a8
    board_widget_set_nav_restricted(state->gui.board, true, 7, 1, 5, 2); // b1 -> c3
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "Knights move in an 'L' shape: 2 squares in one direction, then 1 square perpendicular.\n\nTask: Move the Knight from b1 to c3.",
        "The Knight");
}

static void tutorial_setup_queen(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[7][3] = piece_create(PIECE_QUEEN, PLAYER_WHITE); // d1
    state->logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK); // a8
    board_widget_set_nav_restricted(state->gui.board, true, 7, 3, 3, 7); // d1 -> h5
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "The Queen is powerful! She moves like a Rook AND a Bishop combined.\n\nTask: Move the Queen from d1 to h5.",
        "The Queen");
}

static void tutorial_setup_check(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[7][7] = piece_create(PIECE_ROOK, PLAYER_WHITE); // h1
    state->logic->board[0][4] = piece_create(PIECE_KING, PLAYER_BLACK); // e8
    state->logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE); // e1
    board_widget_set_nav_restricted(state->gui.board, true, 7, 7, 0, 7); // h1 -> h8
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "'Check' means the King is under attack.\n\nTask: Move the Rook to h8 to put the Black King in Check.",
        "Check");
}

static void tutorial_setup_escape(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE); // e1
    state->logic->board[0][4] = piece_create(PIECE_ROOK, PLAYER_BLACK); // e8
    state->logic->turn = PLAYER_WHITE; 
    board_widget_set_nav_restricted(state->gui.board, true, 7, 4, 7, 5); // e1 -> f1
    board_widget_set_nav_restricted(state->gui.board, true, 7, 4, 7, 5); // e1 -> f1
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "The Black Rook is attacking your King!\n\nTask: Move your King from e1 to f1 to escape check.",
        "Escape Check");
}

static void tutorial_setup_castling(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE); // e1
    state->logic->board[7][7] = piece_create(PIECE_ROOK, PLAYER_WHITE); // h1
    state->logic->board[0][4] = piece_create(PIECE_KING, PLAYER_BLACK);
    board_widget_set_nav_restricted(state->gui.board, true, 7, 4, 7, 6); // e1 -> g1
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "This is a special move. Move the King TWO squares towards the Rook.\n\nTask: Move the King from e1 to g1.",
        "Castling");
}

static void tutorial_setup_mate(AppState* state) {
    tutorial_clear_board(state);
    state->logic->board[0][0] = piece_create(PIECE_KING, PLAYER_BLACK); // a8
    state->logic->board[1][0] = piece_create(PIECE_PAWN, PLAYER_BLACK); // a7
    state->logic->board[1][1] = piece_create(PIECE_PAWN, PLAYER_BLACK); // b7
    state->logic->board[2][0] = piece_create(PIECE_PAWN, PLAYER_BLACK); // a6
    state->logic->board[7][3] = piece_create(PIECE_ROOK, PLAYER_WHITE); // d1
    state->logic->board[7][4] = piece_create(PIECE_KING, PLAYER_WHITE); // e1
    state->logic->turn = PLAYER_WHITE;
    board_widget_set_nav_restricted(state->gui.board, true, 7, 3, 0, 3); // d1 -> d8
    board_widget_refresh(state->gui.board);
    tutorial_update_view(state,
        "The Black King is trapped.\n\nTask: Deliver Checkmate by moving the Rook to d8!",
        "Final Step: Checkmate");
}

static gboolean on_tutorial_final_message_timeout(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Tutorial Complete!");
    if (state->gui.window) gtk_window_set_transient_for(GTK_WINDOW(dialog), state->gui.window);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, 200);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);

    GtkWidget* label = gtk_label_new("You have learned the basics of Chess.\n\n"
        "HAL :) suggests to play around and customise the game to your liking. PS: Try out Horsey!\n\n"
        "Use the board theme to modify the board.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 40);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* btn = gtk_button_new_with_label("OK");
    gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(box), btn);

    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_widget_set_visible(dialog, TRUE);
    
    return FALSE;
}

static void tutorial_finish(AppState* state) {
    on_tutorial_exit(NULL, state);
    
    // Redirect to Settings -> Piece Theme
    GApplication* app = g_application_get_default();
    g_action_group_activate_action(G_ACTION_GROUP(app), "open-settings", g_variant_new_string("piece"));

    // Use a small delay to ensure settings window is created if not already
    g_timeout_add(500, on_tutorial_final_message_timeout, state);
}

// --- Public Handlers ---

void on_invalid_tutorial_move(void* user_data) {
    AppState* state = (AppState*)user_data;
    
    // FIX: Don't show multiple dialogs
    if (state->gui.tutorial_msg) {
        return;
    }
    
    sound_engine_play(SOUND_LESSON_FAIL); // Added sound

    // Re-trigger setup to show message again
    switch (state->tutorial.step) {
        case TUT_PAWN: tutorial_setup_pawn(state); break;
        case TUT_ROOK: tutorial_setup_rook(state); break;
        case TUT_BISHOP: tutorial_setup_bishop(state); break;
        case TUT_KNIGHT: tutorial_setup_knight(state); break;
        case TUT_QUEEN: tutorial_setup_queen(state); break;
        case TUT_CHECK: tutorial_setup_check(state); break;
        case TUT_ESCAPE: tutorial_setup_escape(state); break;
        case TUT_CASTLING: tutorial_setup_castling(state); break;
        case TUT_MATE: tutorial_setup_mate(state); break;
    }
}

void on_tutorial_exit(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* state = (AppState*)user_data;
    state->tutorial.step = TUT_OFF;
    
    // Reset to PvC ELO 100
    state->logic->gameMode = GAME_MODE_PVC;
    gamelogic_reset(state->logic);
    
    // Set ELO to 100 on exit
    if (state->gui.ai_dialog) ai_dialog_set_elo(state->gui.ai_dialog, 100, false);
    // Also update InfoPanel slider
    if (state->gui.info_panel) info_panel_set_elo(state->gui.info_panel, 100, true); 
    
    // Disable tutorial restrictions
    board_widget_set_nav_restricted(state->gui.board, false, -1, -1, -1, -1);
    
    // Re-enable info panel
    if (state->gui.info_panel) {
        info_panel_set_tutorial_mode(state->gui.info_panel, false);
        info_panel_set_sensitive(state->gui.info_panel, true);
        info_panel_rebuild_layout(state->gui.info_panel);
    }
    
    // Restore RSP
    if (state->gui.right_side_panel) right_side_panel_set_visible(state->gui.right_side_panel, true);
    
    // Re-enable Clocks
    if (state->gui.top_clock) {
        clock_widget_set_disabled(state->gui.top_clock, false);
        clock_widget_set_visible_state(state->gui.top_clock, true);
    }
    if (state->gui.bottom_clock) {
        clock_widget_set_disabled(state->gui.bottom_clock, false);
        clock_widget_set_visible_state(state->gui.bottom_clock, true);
    }
    
    board_widget_refresh(state->gui.board);
}

void on_tutorial_action(GSimpleAction* action, GVariant* parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AppState* state = (AppState*)user_data;
    
    // Reset to Empty -> Intro
    gamelogic_reset(state->logic); // Clear history/tints
    state->tutorial.step = TUT_INTRO;
    tutorial_clear_board(state);
    
    // Show exit button
    if (state->gui.tutorial_exit_btn) gtk_widget_set_visible(state->gui.tutorial_exit_btn, TRUE);
    gtk_window_set_title(state->gui.window, "Interactive Tutorial");
    
    gtk_window_set_title(state->gui.window, "Interactive Tutorial");
    
    // Hide RSP
    if (state->gui.right_side_panel) right_side_panel_set_visible(state->gui.right_side_panel, false);
    
    // Important: Switch to Tutorial Mode
    state->logic->gameMode = GAME_MODE_TUTORIAL;
    
    // Enable Tutorial Mode in InfoPanel + Show Dialog via helper
    if (state->gui.info_panel) {
        info_panel_set_tutorial_mode(state->gui.info_panel, true);
        tutorial_update_view(state, 
            "Hey I am HAL :) A friendly Chess engine.\n\nI will guide you through the basics of Chess so we can play together!", 
            "Introduction");
    }

    // Hide Clocks
    if (state->gui.top_clock) clock_widget_set_visible_state(state->gui.top_clock, false);
    if (state->gui.bottom_clock) clock_widget_set_visible_state(state->gui.bottom_clock, false);

    // Register invalid move callback
    board_widget_set_invalid_move_callback(state->gui.board, on_invalid_tutorial_move, state);

    // Auto-advance from Intro to Pawn after a short delay (3s) so user reads the message
    state->tutorial.next_step = TUT_PAWN; 
    state->tutorial.wait = TRUE; // Prevent other interactions?
    g_timeout_add(4000, on_tutorial_delay_complete, state); 
}

void tutorial_check_progress(AppState* state) {
    if (state->tutorial.step == TUT_OFF) {
        state->tutorial.wait = FALSE;
        return;
    }

    if (state->tutorial.wait) {
        if (debug_mode) printf("[Tutorial] Tutorial waiting...\n");
        return;
    }

    // Fix: Wait for animation to finish before checking progress
    if (board_widget_is_animating(state->gui.board)) {
        if (debug_mode) printf("[Tutorial] Board animating, skipping tutorial check\n");
        return;
    }
    
    // logic...
    // FIX: Delay 500ms
    int delay = 500;

    if (state->tutorial.step == TUT_PAWN) {
        // d2->d4 (6,3 -> 4,3)
        Piece* p = state->logic->board[4][3];
        if (debug_mode) printf("[Tutorial] Checking PAWN success. Slot [4][3] = %p\n", (void*)p);
        if (p) if (debug_mode) printf("[Tutorial] Piece type=%d owner=%d\n", p->type, p->owner);
        
        if (p && p->type == PIECE_PAWN && p->owner == PLAYER_WHITE) {
            if (debug_mode) printf("[Tutorial] PAWN Success! Locking board.\n");
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_ROOK;
            sound_engine_play(SOUND_LESSON_PASS);
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_ROOK) {
        // e4->e8 (4,4 -> 0,4)
        Piece* p = state->logic->board[0][4];
        if (p && p->type == PIECE_ROOK && p->owner == PLAYER_WHITE) {
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_BISHOP;
            sound_engine_play(SOUND_LESSON_PASS); 
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_BISHOP) {
        Piece* p = state->logic->board[2][7];
        if (p && p->type == PIECE_BISHOP && p->owner == PLAYER_WHITE) {
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_KNIGHT;
            sound_engine_play(SOUND_LESSON_PASS);
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_KNIGHT) {
        Piece* p = state->logic->board[5][2];
        if (p && p->type == PIECE_KNIGHT && p->owner == PLAYER_WHITE) {
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_QUEEN;
            sound_engine_play(SOUND_LESSON_PASS);
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_QUEEN) {
        Piece* p = state->logic->board[3][7];
        if (p && p->type == PIECE_QUEEN && p->owner == PLAYER_WHITE) {
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_CHECK;
            sound_engine_play(SOUND_LESSON_PASS);
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_CHECK) {
        Piece* p = state->logic->board[0][7];
        if (p && p->type == PIECE_ROOK && p->owner == PLAYER_WHITE) {
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_ESCAPE;
            sound_engine_play(SOUND_LESSON_PASS);
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_ESCAPE) {
        Piece* p = state->logic->board[7][5];
        if (p && p->type == PIECE_KING && p->owner == PLAYER_WHITE) {
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_CASTLING;
            sound_engine_play(SOUND_LESSON_PASS);
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_CASTLING) {
        Piece* p = state->logic->board[7][6];
        if (p && p->type == PIECE_KING && p->owner == PLAYER_WHITE) {
            state->tutorial.wait = TRUE;
            board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
            state->tutorial.next_step = TUT_MATE;
            sound_engine_play(SOUND_LESSON_PASS);
            g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_MATE) {
        Piece* p = state->logic->board[0][3]; 
        if (p && p->type == PIECE_ROOK && p->owner == PLAYER_WHITE) {
             state->tutorial.wait = TRUE;
             board_widget_set_nav_restricted(state->gui.board, true, -1, -1, -1, -1);
             state->tutorial.next_step = TUT_DONE;
             sound_engine_play(SOUND_LESSON_PASS); // Added sound
             g_timeout_add(delay, on_tutorial_delay_complete, state);
        }
    } else if (state->tutorial.step == TUT_DONE) {
         state->tutorial.wait = TRUE;
         sound_engine_play(SOUND_LESSON_PASS); // Added sound
         tutorial_finish(state);
         // After finish, we exit, and next loop checks TUT_OFF, resetting wait.
    }
    
}

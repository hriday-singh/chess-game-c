#include "game_import.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "move.h"

// Helper to check if a string looks like a standard UCI move (e.g., "e2e4", "a7a8q")
// static bool is_looks_like_uci(const char* token) - Removed unused

static void skip_whitespace(const char** cursor) {
    while (**cursor && isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
}

static void parse_pgn_tag(const char** cursor, GameImportResult* res) {
    // pattern: [Key "Value"]
    (*cursor)++; // Skip '['
    
    char key[64] = {0};
    char value[128] = {0};
    
    // Read Key
    size_t k = 0;
    while (**cursor && **cursor != ' ' && **cursor != ']') {
        if (k < sizeof(key) - 1) key[k++] = **cursor;
        (*cursor)++;
    }
    
    // Find Value start
    while (**cursor && **cursor != '"' && **cursor != ']') (*cursor)++;
    
    if (**cursor == '"') {
        (*cursor)++;
        size_t v = 0;
        while (**cursor && **cursor != '"') {
             if (v < sizeof(value) - 1) value[v++] = **cursor;
             (*cursor)++;
        }
        if (**cursor == '"') (*cursor)++;
    }
    
    // Skip to end of tag
    while (**cursor && **cursor != ']') (*cursor)++;
    if (**cursor == ']') (*cursor)++;
    
    // Store if relevant
    if (res) {
        if (strcmp(key, "White") == 0) snprintf(res->white, sizeof(res->white), "%s", value);
        else if (strcmp(key, "Black") == 0) snprintf(res->black, sizeof(res->black), "%s", value);
        else if (strcmp(key, "Event") == 0) snprintf(res->event, sizeof(res->event), "%s", value);
        else if (strcmp(key, "Date") == 0) snprintf(res->date, sizeof(res->date), "%s", value);
        else if (strcmp(key, "Result") == 0 && res->result[0] == '\0') snprintf(res->result, sizeof(res->result), "%s", value);
    }
}

static void skip_comment_curly(const char** cursor) {
    // pattern: { comment }
    (*cursor)++;
    while (**cursor) {
        if (**cursor == '}') {
            (*cursor)++;
            break;
        }
        (*cursor)++;
    }
}

static void skip_comment_paren(const char** cursor) {
    // pattern: ( variation ) - recursive? For now simple nested count
    int depth = 1;
    (*cursor)++;
    while (**cursor && depth > 0) {
        if (**cursor == '(') depth++;
        else if (**cursor == ')') depth--;
        (*cursor)++;
    }
}

static void skip_comment_semicolon(const char** cursor) {
    // pattern: ; comment to ends of line
    while (**cursor && **cursor != '\n' && **cursor != '\r') {
        (*cursor)++;
    }
}

// Get next game token (Move or Result)
// Returns true if token found, false if end of stream
static bool get_next_token(const char** cursor, char* buffer, size_t buf_size, GameImportResult* res) {
    if (!cursor || !*cursor) return false;
    
    while (**cursor) {
        skip_whitespace(cursor);
        if (!**cursor) return false;
        
        char c = **cursor;
        
        if (c == '[') {
        if (c == '[') {
            parse_pgn_tag(cursor, res); // Pass res pointer directly
            continue;
        }
        }
        if (c == '{') {
            skip_comment_curly(cursor);
            continue;
        }
        if (c == '(') {
            skip_comment_paren(cursor);
            continue;
        }
        if (c == ';') {
            skip_comment_semicolon(cursor);
            continue;
        }
        
        // Handle move numbers "1.", "1..."
        // If starts with digit and contains '.'
        // Actually simpler: read a word. If it ends with '.' or matches digit+dots, ignore it.
        
        const char* start = *cursor;
        size_t len = 0;
        
        // Read until whitespace or delimiter
        while (**cursor && !isspace((unsigned char)**cursor) && 
               **cursor != '[' && **cursor != '{' && **cursor != '(' && **cursor != ';') {
             (*cursor)++;
             len++;
        }
        
        if (len == 0) continue;
        
        // Extract to buffer
        if (len >= buf_size) len = buf_size - 1;
        snprintf(buffer, buf_size, "%.*s", (int)len, start);
        
        // Check if token starts with a digit but contains letters (e.g. "1.e4")
        if (isdigit((unsigned char)buffer[0])) {
             size_t split_idx = 0;
             bool has_alpha = false;
             for (size_t i = 0; i < len; i++) {
                 if (isalpha((unsigned char)buffer[i])) {
                     split_idx = i;
                     has_alpha = true;
                     break;
                 }
             }
             
             // If merged token (starts with digit, has alpha, and isn't a result 1-0/0-1/1/2-1/2)
             if (has_alpha) {
                  // Special check for O-O (starts with letter) -> already safe
                  // Special check for results? 1-0 has no alpha. 1/2-1/2 has no alpha.
                  // So anything with alpha starting with digit is likely "1.e4" or "23...Nf3"
                  
                  // Truncate current token to just the number part
                  buffer[split_idx] = '\0';
                  
                  // Reset cursor to point to the start of the alpha part
                  *cursor = start + split_idx;
                  
                  // Recalculate len for the number part
                  len = split_idx; 
             }
        }

        // Post-processing check
        // If it looks like "1." or "23..." or just "1", skip it
        bool is_number = true;
        for (size_t i = 0; i < len; i++) {
            if (!isdigit((unsigned char)buffer[i]) && buffer[i] != '.') {
                is_number = false;
                break;
            }
        }
        
        if (is_number) continue; // Skip move numbers
        
        return true; // Found a valid token (Move, Result, or garbage)
    }
    return false;
}

// Helper to get SAN for a candidate move (without simple checks/mates)
static char get_import_piece_char(PieceType type) {
    switch (type) {
        case PIECE_KNIGHT: return 'N';
        case PIECE_BISHOP: return 'B';
        case PIECE_ROOK:   return 'R';
        case PIECE_QUEEN:  return 'Q';
        case PIECE_KING:   return 'K';
        default:           return '\0'; 
    }
}

static void get_candidate_san(Move* move, Move** all_moves, int count, char* buffer, size_t size) {
    char temp[32] = {0};
    int p = 0;
    
    if (move->isCastling) {
        int c2 = move->to_sq % 8;
        int c1 = move->from_sq % 8;
        p = snprintf(temp, sizeof(temp), (c2 > c1) ? "O-O" : "O-O-O");
    } else {
        char pc = get_import_piece_char(move->movedPieceType);
        if (pc != '\0') {
            temp[p++] = pc;
            
            // Disambiguation
            bool ambiguity = false;
            bool same_file = false;
            bool same_rank = false;
            
            for (int i = 0; i < count; i++) {
                Move* other = all_moves[i];
                if (other->to_sq == move->to_sq && 
                    other->movedPieceType == move->movedPieceType && 
                    other->from_sq != move->from_sq) {
                    ambiguity = true;
                    if ((other->from_sq % 8) == (move->from_sq % 8)) same_file = true;
                    if ((other->from_sq / 8) == (move->from_sq / 8)) same_rank = true;
                }
            }
            
            if (ambiguity) {
                if (!same_file) {
                    temp[p++] = (char)('a' + (move->from_sq % 8));
                } else if (!same_rank) {
                    temp[p++] = (char)('8' - (move->from_sq / 8));
                } else {
                    temp[p++] = (char)('a' + (move->from_sq % 8));
                    temp[p++] = (char)('8' - (move->from_sq / 8));
                }
            }
            
            // Caption
            if (move->capturedPieceType != NO_PIECE) {
                temp[p++] = 'x';
            }
        } else {
            // Pawn
            if (move->capturedPieceType != NO_PIECE) {
                temp[p++] = (char)('a' + (move->from_sq % 8));
                temp[p++] = 'x';
            }
        }
        
        // Destination
        temp[p++] = (char)('a' + (move->to_sq % 8));
        temp[p++] = (char)('8' - (move->to_sq / 8));
        
        // Promotion
        if (move->promotionPiece != NO_PROMOTION) {
            temp[p++] = '=';
            temp[p++] = get_import_piece_char(move->promotionPiece);
        }
    }
    temp[p] = '\0';
    snprintf(buffer, size, "%s", temp);
}

GameImportResult game_import_from_string(GameLogic* logic, const char* input) {
    GameImportResult res;
    memset(&res, 0, sizeof(res));
    res.success = true; // Optimistic
    snprintf(res.start_fen, sizeof(res.start_fen), "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    
    if (!logic || !input) {
        res.success = false;
        snprintf(res.error_message, sizeof(res.error_message), "Internal error: Null logic or input.");
        return res;
    }
    
    // 1. Reset Logic
    gamelogic_reset(logic);
    
    // 2. Scan Input
    const char* cursor = input;
    char token[128];
    char uci_accum[4096] = ""; // To reconstruct UCI string
    
    while (get_next_token(&cursor, token, sizeof(token), &res)) {
        // Check for Game End markers
        if (strcmp(token, "1-0") == 0 || strcmp(token, "0-1") == 0 || strcmp(token, "1/2-1/2") == 0 || strcmp(token, "*") == 0) {
            snprintf(res.result, sizeof(res.result), "%s", token);
            break; // Valid end of game
        }
        
        // It should be a move. Try to match it.
        int count = 0;
        Move** legal_moves = gamelogic_get_all_legal_moves(logic, logic->turn, &count);
        
        Move* matched_move = NULL;
        // bool ambig_found = false; // Unused
        
        // Strategy 1: Exact SAN match (with logic's generator)
        // Strategy 2: UCI match
        


        for (int i = 0; i < count; i++) {
            Move* m = legal_moves[i];
            
            // Check SAN
            char san[32];
            // Use local candidate generation instead of gamelogic's history-dependent one
            get_candidate_san(m, legal_moves, count, san, sizeof(san));
            
            // Clean SAN (remove check/mate markers from logic output for comparison if input lacks them, or vice versa)
            // But gamelogic_get_move_san produces "Nf3", "O-O", "e4", "Qxd5+" etc.
            // PGN input might have "e4", "Nf3", "Qxd5", "Qxd5+" etc.
            
            // For robust comparison, we can strip Check signs (+, #) from both for comparison?
            // Or just try direct.
            
            bool match = false;
            
            // 1. Direct String Compare
            if (strcmp(token, san) == 0) match = true;
            
            // 2. Fail-soft: input "Nf3" vs "Nf3+" (logic)
            if (!match) {
                 size_t tok_len = strlen(token);
                 size_t san_len = strlen(san);
                 // Check token subset of san (if sanitize)
                 // Just check if san starts with token AND the rest is just +/#
                 if (strncmp(token, san, tok_len) == 0) {
                     // check remainder
                     bool only_suffix = true;
                     for (size_t k = tok_len; k < san_len; k++) {
                         if (san[k] != '+' && san[k] != '#') only_suffix = false;
                     }
                     if (only_suffix) match = true;
                 }
                 // Check SAN subset of token (input "Nf3??")
                 if (!match && strncmp(san, token, san_len) == 0) {
                      match = true; // Allow loose
                 }
            }
            
            // 3. UCI Match
            char uci[8];
            move_to_uci(m, uci); // e2e4
            if (!match && strcmp(token, uci) == 0) match = true;
            
            if (match) {
                if (matched_move == NULL) {
                    matched_move = move_copy(m);
                } else {
                    // Ambiguity? e.g. two knights can move but input is just "N".
                    // Strict SAN rules say this is invalid PGN, but we simply pick the first valid one if ambiguous 
                    // or treat as error? PGN standard requires disambiguation. 
                    // Let's stick to "First Legal Match" logic for robustness, 
                    // unless we want to be strict.
                }
            }
        }
        
        // Clean up legal moves
        gamelogic_free_moves_array(legal_moves, count);
        
        if (matched_move) {
            // Add to UCI accumulator
            char uci[8];
            move_to_uci(matched_move, uci);
            size_t current_len = strlen(uci_accum);
            if (current_len > 0) {
                snprintf(uci_accum + current_len, sizeof(uci_accum) - current_len, " %s", uci);
            } else {
                snprintf(uci_accum, sizeof(uci_accum), "%s", uci);
            }
            
            if (!gamelogic_perform_move(logic, matched_move)) {
                res.success = false;
                snprintf(res.error_message, sizeof(res.error_message), "Failed to perform move '%s' (System Error)", token);
                move_free(matched_move);
                break;
            }
            
            res.moves_count++;
            move_free(matched_move);
        } else {
            res.success = false;
            snprintf(res.error_message, sizeof(res.error_message), "Unrecognized or illegal move: '%s' at ply %d", token, res.moves_count+1);
            break;
        }
    }
    
    if (res.success) {
        snprintf(res.loaded_uci, sizeof(res.loaded_uci), "%s", uci_accum);
    }
    
    return res;
}

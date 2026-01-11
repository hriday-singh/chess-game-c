#include "sound_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

// Include miniaudio - user needs to download miniaudio.h
// Download from: https://github.com/mackron/miniaudio/releases
// Place miniaudio.h in the gui/ directory
// IMPORTANT: Only ONE file in the entire project should define MINIAUDIO_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// Global sound engine state
static ma_engine g_engine;
static bool g_initialized = false;
static bool g_enabled = true;  // Enabled by default

// Sound file names (without path - we'll search multiple locations)
static const char* SOUND_NAMES[] = {
    "move-self.mp3",      // SOUND_MOVE
    "capture.mp3",        // SOUND_CAPTURE
    "castle.mp3",         // SOUND_CASTLES
    "move-check.mp3",     // SOUND_CHECK
    "game-win.mp3",       // SOUND_WIN
    "game-lose.mp3",      // SOUND_DEFEAT
    "game-draw.mp3",      // SOUND_DRAW
    "illegal.mp3",        // SOUND_ERROR
    "lesson-pass.mp3",    // SOUND_LESSON_PASS
    "lesson-fail.mp3",    // SOUND_LESSON_FAIL
    "game-start.mp3",     // SOUND_GAME_START
    "promote.mp3",        // SOUND_PROMOTION
    "click.mp3",          // SOUND_CLICK
    "move-opponent.mp3",  // SOUND_MOVE_OPPONENT
    "puzzle-correct.mp3", // SOUND_PUZZLE_CORRECT
    "puzzle-correct-2.mp3", // SOUND_PUZZLE_CORRECT_2
    "puzzle-wrong.mp3",     // SOUND_PUZZLE_WRONG
    "game-start.mp3"        // SOUND_RESET
};

// Try to find a sound file in common locations
static const char* find_sound_file(const char* filename) {
    // Try multiple paths (relative to executable location)
    static char path_buffer[512];
    const char* search_paths[] = {
        "assets/audio/%s",           // Current directory
        "../assets/audio/%s",        // One level up (if running from build/)
        "../../assets/audio/%s",      // Two levels up
        NULL
    };
    
    for (int i = 0; search_paths[i] != NULL; i++) {
        snprintf(path_buffer, sizeof(path_buffer), search_paths[i], filename);
        // Check if file exists
        if (access(path_buffer, F_OK) == 0) {
            return path_buffer;
        }
    }
    
    // Return original path as fallback
    snprintf(path_buffer, sizeof(path_buffer), "assets/audio/%s", filename);
    return path_buffer;
}

// Initialize sound engine
int sound_engine_init(void) {
    if (g_initialized) {
        return 0;  // Already initialized
    }
    
    ma_result result = ma_engine_init(NULL, &g_engine);
    if (result != MA_SUCCESS) {
        return -1;
    }
    
    g_initialized = true;
    g_enabled = true;
    return 0;
}

// Cleanup sound engine
void sound_engine_cleanup(void) {
    if (g_initialized) {
        ma_engine_uninit(&g_engine);
        g_initialized = false;
    }
}

// Play a sound (non-blocking, lightweight)
void sound_engine_play(SoundType sound) {
    // Don't play if disabled or not initialized
    if (!g_enabled || !g_initialized) {
        return;
    }
    
    // Validate sound type
    if (sound < 0 || sound >= (int)(sizeof(SOUND_NAMES) / sizeof(SOUND_NAMES[0]))) {
        return;
    }
    
    const char* sound_name = SOUND_NAMES[sound];
    const char* sound_file = find_sound_file(sound_name);
    
    // Play sound asynchronously (non-blocking)
    ma_engine_play_sound(&g_engine, sound_file, NULL);
}

// Enable/disable sound effects
void sound_engine_set_enabled(int enabled) {
    g_enabled = (enabled != 0);
}

// Get current enabled state
int sound_engine_is_enabled(void) {
    return g_enabled ? 1 : 0;
}


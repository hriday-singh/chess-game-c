#ifndef SOUND_ENGINE_H
#define SOUND_ENGINE_H

// Sound types enum
typedef enum {
    SOUND_MOVE,
    SOUND_CAPTURE,
    SOUND_CASTLES,
    SOUND_CHECK,
    SOUND_WIN,
    SOUND_DEFEAT,
    SOUND_DRAW,
    SOUND_ERROR,
    SOUND_LESSON_PASS,
    SOUND_LESSON_FAIL,
    SOUND_GAME_START,
    SOUND_PROMOTION,
    SOUND_CLICK,
    SOUND_MOVE_OPPONENT,
    SOUND_PUZZLE_CORRECT,
    SOUND_PUZZLE_CORRECT_2,
    SOUND_PUZZLE_WRONG
} SoundType;

// Initialize sound engine (call once at startup)
int sound_engine_init(void);

// Cleanup sound engine (call on shutdown)
void sound_engine_cleanup(void);

// Play a sound (non-blocking, lightweight)
void sound_engine_play(SoundType sound);

// Enable/disable sound effects
void sound_engine_set_enabled(int enabled);

// Get current enabled state
int sound_engine_is_enabled(void);

#endif // SOUND_ENGINE_H


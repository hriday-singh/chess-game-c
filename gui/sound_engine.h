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
    SOUND_ERROR
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


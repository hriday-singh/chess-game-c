# Sound Engine Setup

The sound engine uses **miniaudio**, a lightweight single-header audio library.

## Setup Instructions

1. Download `miniaudio.h` from:

   - Official releases: https://github.com/mackron/miniaudio/releases
   - Or directly: https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h

2. Place `miniaudio.h` in the `gui/` directory (same directory as `sound_engine.c`)

3. The sound engine will automatically compile with the rest of the project.

## Audio Files

Audio files should be in `assets/audio/` directory (relative to the executable):

- Move.ogg
- Capture.ogg
- Castles.ogg
- Check.ogg
- Win.ogg
- Defeat.ogg
- Draw.ogg
- Error.ogg

## Features

- **Non-blocking**: All sound playback is asynchronous and won't block gameplay
- **Lightweight**: miniaudio has minimal overhead
- **Toggle**: SFX can be enabled/disabled via the "Enable SFX" checkbox in Visual Settings
- **Automatic**: Sounds play automatically based on move type (move, capture, castling, check, game end)

## Troubleshooting

If sounds don't play:

1. Ensure `miniaudio.h` is in the `gui/` directory
2. Check that audio files exist in `assets/audio/` relative to the executable
3. Verify the SFX checkbox is enabled in the Visual Settings panel
4. Check console for any miniaudio initialization errors (sound engine fails silently by design)

#pragma once
#include <pebble.h>

// Sound effects for Kablooey.
//
// Every effect is rendered to PCM at startup and mixed into a single speaker
// stream that stays open while the game is on screen. That is deliberate: the
// speaker amplifier clicks each time it powers down, so starting and stopping
// playback per effect pops no matter how cleanly the waveform ends. With one
// long-lived stream the amp cycles once, when the game opens and closes.
//
// Between effects nothing is written, so the queue drains and the speaker
// carries no filler. An earlier version fed a 1-LSB dither to keep the amp
// awake; it was audible on the real speaker.
//
// If the stream cannot be opened the code falls back to one-shot note
// playback, which works but pops.

typedef enum {
  SFX_CATCH,
  SFX_MISS,   // the bomb that got through
  SFX_POP,    // one link of the chain reaction
  SFX_GAME_OVER,  // the end-of-game jingle, fanfare and all
  SFX_BEEP,   // sound-on confirmation
} Sfx;

void sound_init(void);     // render the effects, load the saved preference
void sound_deinit(void);   // save the preference, close the stream
void sound_resume(void);   // open the stream (window appeared)
void sound_suspend(void);  // close the stream (window disappeared)
void sound_pump(void);     // feed the stream; call once per frame
bool sound_is_on(void);            // the app's own setting
bool sound_blocked_by_watch(void); // system mute or Quiet Time; not ours to override
bool sound_is_audible(void);       // on, and not blocked
void sound_toggle(void);           // ignored while the watch blocks sound
void sound_play(Sfx sfx);  // no-op when sound is off

// Tell the mixer how fast bombs are arriving this wave. Once catches come
// closer together than the splash is long, the tails pile up and outlast the
// wave, so a shorter splash gets used instead.
void sound_set_tempo(int drop_interval_ms);

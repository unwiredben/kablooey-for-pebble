# Kablooey development diary

A running record of what was built and why, in the order it happened. Kept so
the README can stay short.

## Setup

- Target is the Pebble Time 2, which is the `emery` platform: 200x228, colour,
  touchscreen, speaker, 128KB app RAM. Confirmed from the SDK's
  `pebble_sdk_platform.py` (`PBL_TOUCH`, `PBL_SPEAKER`, `PBL_DISPLAY_WIDTH=200`).
- Project scaffolded by hand: `package.json`, a stock `wscript` copied from a
  sibling project, `src/c`. Fresh UUID, `emery` as the only target platform.
- MIT license added on request: `LICENSE`, a `license` field in `package.json`,
  SPDX headers on every source file.

## First playable

Split into four files so gameplay tuning never means reading drawing code:

- `game.h` — layout and tuning constants, game state
- `game.c` — waves, bomber, bombs, catch/miss
- `render.c` — all drawing, from primitives; no bitmap resources
- `main.c` — window, frame timer, input

Sub-pixel positions are fixed point (1/16px, `FP`) so slow motion stays smooth
at the 30fps frame timer. Wave 1 shipped deliberately gentle so the touch
control could be judged before any difficulty curve existed.

## Touch control

First attempt only accepted drags inside the painted strip at the very bottom
(y >= 190). It appeared to work, then "stopped working after the first wave".
Touch event logging settled it immediately: the real drags were landing at
y 143-176, well above the strip. Wave 1 had been played with the buttons.

- The steer zone became the whole bottom half of the screen (`TOUCH_ZONE_TOP`,
  y >= 114); the painted strip stayed as the affordance.
- A finger that wanders into the zone mid-gesture picks up the pails, so a
  dropped touchdown event cannot leave the player without controls.
- Mapping is absolute: the stack goes where the finger is.
- Later: waves start on **touchdown**, not liftoff. Acting on liftoff meant a
  wave began when the player let go of a drag they were already holding.

Buttons were removed as a steering method at the owner's request. The top
button toggles sound; select and bottom are deliberately unassigned.

## Gameplay changes

- Pails shortened to 10px with a 4px gap between them.
- Collision became the whole stack as one box: top rim, a lower pail the stack
  slid under, or a side hit all douse the bomb. The gaps are inside the box,
  since a falling bomb can only reach them by passing through a pail.
- A miss knocks the player back a wave as well as costing a pail: they replay
  the previous wave, then face the one that beat them again. The ready banner
  says "BACK A WAVE" and names the wave, since the knock-back should not be a
  surprise.
- A miss now runs a chain reaction: the ground blast fires, then every bomb
  still in the air cooks off in turn from the bottom up (`GAME_CHAIN`). The
  single effect slot became a 4-slot pool so the blasts overlap. Step timing
  160ms, raised to 260ms after play-testing.
- Wave ramp: each wave adds bombs, cuts the gap, adds fall speed and pays
  `wave` points per catch. Wave 1's base was raised once (800ms gap,
  1.75px/frame). The ramp was then steepened sharply: the first caps (26 bombs,
  300ms, 4px/frame) were all reached by wave 12 and it was still comfortable,
  so the steps grew (3 bombs, 90ms, 0.375px/frame) and the caps moved out to 40
  bombs, 170ms and 7px/frame — a 0.7s reaction window. The bomber's turn window
  tightens with the wave too, which scatters where bombs come from. Checked
  against `MAX_BOMBS`: peak bombs in flight is about 8, around wave 8, so the
  pool went to 10.
- The bomber doubles back at random every 0.7-2.6s, and wall bounces reschedule
  the turn so he cannot stutter against an edge.

## Art

- Started as a blue sky. Changed to a grey brick wall on request: running bond,
  26x12 bricks with 2px mortar, the courses below `TOUCH_ZONE_TOP` laid in a
  darker pair so the steer zone reads as wall in shadow rather than needing a
  tint.
- The bomber was hovering in front of the wall; he now stands on it. `WALL_TOP`
  is defined as his feet line, with a capstone slab beneath them and sky above.
- The wall was darkened two steps (dark grey bricks / light grey mortar above,
  black / dark grey below) because the bomb fuse was invisible against the pale
  original. Only four greys exist on the platform, so this is the full range.
- Bombs gained a pale halo to stay legible against grey and black brick.
- The no-touch banner split by platform: on hardware that has a touchscreen
  (`PBL_TOUCH`), `touch_service_is_enabled()` returning false means the setting
  is off, not that the watch cannot play, so it says "TOUCH IS OFF / enable it
  to play". Only a build without `PBL_TOUCH` says "no touchscreen".
- Banner type went up two sizes (28 Bold title, 24 Bold second line) and the
  strings were shortened to fit; the small strip caption was deleted as
  unreadable on the real screen.

## Launcher icon

A lit bomb, 25x25. There is no image editor in this toolchain, so
`tools/make_icon.py` draws it in code and writes the PNG directly: 8-bit
palette, index 0 black, 1 white, 2 transparent, matching what the pachinko
icon does and what the launcher expects. Re-run it to tweak the shape; it
prints an ASCII preview.

## Sound, in three rounds

**Round 1 — note sequences.** `speaker_play_notes` with short melodies. The
catch was a two-note blip; replaced with a synthesised PCM splash (filtered
noise whose cutoff closes, plus a droplet tone sliding 480Hz to 150Hz).
Generator arithmetic was checked offline before flashing: peak 123 of 127, zero
clipped samples.

**Round 2 — the clicking.** Every effect already ended at exactly zero, so the
pop was not the waveform. First theory was the note duration landing exactly on
the sample's last byte, where an overrun would replay the attack; that was
fixed (silent tail, note stops inside it) and the click survived. It was the
amplifier powering down after each sound.

The fix was to stop starting and stopping playback at all: all six effects are
rendered to 8kHz PCM at startup and mixed into a single `speaker_stream_open()`
stream that stays open while the game is on screen. Verified by compiling the
real `sound.c` on the host against a stub `pebble.h`: every buffer starts and
ends at 0, peaks 85-109 of 127, no clipped samples, DC within +/-0.8. The
clicking went away.

Two follow-ups:

- The first splash of a wave was being swallowed, so idle frames were given a
  1-LSB dither instead of digital silence, on the theory that the amp idles
  during true silence and eats the start of the next effect. Later removed: the
  dither was audible on the real speaker.
- The wave-clear fanfare was disliked between waves, so clearing a wave is
  silent and the fanfare became the tail of the end-of-game jingle.

**Round 3 — latency drift.** Effects began arriving late, worse the longer a
session ran. Telemetry (a once-a-second `audio:` log) was decisive: `short=0`,
`pend=0` — the firmware accepts every byte instantly and never pushes back, and
`speaker_get_status()` reads `Playing` regardless, so its queue depth cannot be
observed. We were feeding 8000 samples per wall-clock second into a queue the
hardware drained slightly slower, and the backlog grew unbounded.

- Our own lead was cut from 100ms to 40ms (audibly better on its own).
- A `sound_flush()` at each wave start reset the queue: `speaker_stop()` first,
  because `speaker_stream_close()` alone *drains*, playing out the whole
  backlog. This was reverted one build later -- stopping and reopening cycles
  the amplifier, so it brought back exactly one pop, at wave start. There is no
  way to flush the firmware's queue without cycling the amp, so the idle drain
  is the only backlog control.
- Final shape: nothing is written between effects at all. The queue drains to
  empty on its own, so no backlog can build and there is no filler to hear. An
  effect re-anchors the clock and gets a 40ms cushion, and 60ms of true silence
  follows it so its own last samples are never the ones the queue runs out on.
  Confirmed good on hardware: no pops, no swallowed starts, and effects stay in
  sync through a long session. The amp turned out not to need the dither at
  all, so the earlier "amp idles during silence" theory was wrong -- the
  swallowed first splash had been the growing backlog all along.
- Pacing was rewritten to be incremental (each pump generates exactly the time
  since the last pump), which removes drift accumulation by construction and
  made an earlier 32-bit overflow guard unnecessary.

## Sound, a fourth round

On wave 8 the splashes were still finishing after the last bomb was caught:
catches arrive every 170ms there, and the splash takes 200ms to decay, so the
tails piled up and outlasted the wave. A 110ms splash was rendered alongside
the full one, then a 155ms one between them, because a single step from 200ms
to 110ms was an audible jump. `sound_set_tempo()` picks by drop interval: 200ms
for waves 1-3, 155ms for 4-5, 110ms from wave 6. Cutting voices short instead
would have meant a discontinuity, which is a click.

## Menu

Added last, following the pattern from the pachinko project in a sibling
directory: `SimpleMenuLayer` for the menu and the difficulty picker, a
`ScrollLayer` over a `TextLayer` for the how-to-play text. The help text
started at Gothic 18 and was unreadable on the watch; it is Gothic 24 Bold now,
the same size the banners settled on.

- Select opens it. The game window's `disappear` handler already stops the
  frame timer and suspends the audio stream, so the menu pauses the game for
  free.
- `settings.c` holds what outlives a run: high score and difficulty. Sound kept
  its own flag inside `sound.c`.
- Easy, Normal and Hard start at wave 1, 3 and 6. The starting wave doubles as
  the floor for the knock-back, so a miss can never drop you below your chosen
  difficulty.
- Changing difficulty calls `game_init()`: the starting wave is part of a run,
  so switching begins a new one rather than editing the current one.
- Vibration became a setting of its own next to sound, persisted in
  `settings.c`; the miss pulse is the only thing that uses it, and switching it
  on pulses once so the choice is confirmed the way it will feel.
- `speaker_is_muted()` is polled once a second. While the watch blocks sound,
  no stream is held open, effects are skipped, the button and menu toggles do
  nothing, and the menu says "Muted by watch" rather than offering a switch
  that cannot work. The score-bar icon follows what is actually audible, not
  the app's own setting.
- The high score updates live during a run and is committed to storage at game
  over. The banner reads "NEW BEST!" instead of "KABLOOEY!" when the run beat
  it.

## Demo mode and capture tooling

Screenshots and animations needed the game to play itself, so `DEMO_MODE` (off
unless the build defines it) adds an autoplayer: it taps through banners after
`DEMO_BANNER_MS`, chases whichever bomb is closest to landing at a speed that
still reads as a hand, and fumbles one bomb every fourth wave so captures
include the chain reaction and the knock-back.

- The flag comes from the environment (`KABLOOEY_DEMO=1 pebble build`) via
  `demo_defines()` in `wscript`, so nothing has to be edited to record. The
  define has to be appended to each platform's env inside the build loop;
  appending to the top-level env before it is silently discarded, and the build
  looks like it worked while producing an ordinary binary.
- A demo build pins the backlight on with `light_enable(true)`. Without it the
  emulator dims after a few seconds of no input and every frame comes out
  darkened -- the first capture did exactly that.
- `tools/capture.py` follows the capture script in the streamingvillage
  project: re-exec under pebble-tool's own interpreter (it has Pillow and
  libpebble2), subclass `ScreenshotCommand` for the emulator plumbing, pull
  frames with the QEMU monitor's `screendump`, and encode with ffmpeg using a
  generated palette and no dither. Frames are timestamped as they arrive and
  resampled onto an even timeline, so machine load changes smoothness but not
  playback speed.

## Instrumentation

Two debug switches, both off or cheap by default:

- `TOUCH_DEBUG_LOG` in `main.c` — logs every touch event. Turned off after the
  control settled: 30 log lines a second during a drag is exactly the jitter
  that would starve the audio stream.
- `AUDIO_DEBUG_LOG` in `sound.c` — one pacing summary per second. Off now that
  the latency work is confirmed; it is what proved the firmware never pushes
  back and that the lag lived in its queue.

Host-side checking of the sound generators (`gcc` against a stub `pebble.h`)
was worth the setup twice over; it caught a DC-removal highpass that integer
truncation had turned into a constant +35 offset, which would have made the
clicking worse.

## Store icons and banner

The appstore wants icons in colour — 80x80 and 144x144, and 48x48 for
Rebble's small icon — which the 25x25 one-bit launcher icon cannot be upscaled
into. Two ways to get them: crop a gameplay
still, or draw the bomb again bigger. Cropping looked like the better one at
first — the emulator captures at emery's native 200x228, so a 144x144 crop is
1:1 with no resampling at all, and an 80 can come from a 160 crop halved
exactly 2:1. But a crop is a scene, not an icon. 144px cannot hold both the
bomber at y 24 and the pails at y 190, so one or the other clips at an edge,
and at 80 the bombs are six pixels across and the whole thing turns to mush at
the size the store actually shows it. The crop path was written, compared and
then deleted.

So `tools/make_store_icon.py` draws it, like `make_icon.py` does, but in unit
coordinates at whatever size is asked for: the bomb large in the square over a
band of the wall, using `render.c`'s own colours. There is no antialiasing to
be had without an image library, so it rasterises at 4x and box-filters down.
Output is 8-bit truecolour, written by hand as it is in `make_icon.py`.

    python3 tools/make_store_icon.py                     # all three, into store/
    python3 tools/make_store_icon.py store/icon-80.png 80

One thing there is not a pure scale of itself: the brick gets coarser below
96px. At 144 the wall band is 35px and holds three courses, but at 48 it is
11px, and three courses there put a 1px joint every 3px, which greys the band
into mush rather than reading as brick. Small icons get two courses of wider
bricks instead.

The Rebble store also wants a 720x320 banner, which is drawn by
`tools/make_banner.py` for the same reason the icons are: 228 to 320 is a 1.4x
scale, so a screen capture would have to be resampled off the pixel grid and
would go soft exactly where the game is one-pixel detail. Instead the bomber,
the bombs and the pails are `render.c`'s own sprites in game units at 3x, so
the banner cannot drift from what the watch shows.

The title is centred with the top of the wall below it and Scarry off to one
side, which is the shape these banners take. It needed a heavy face, so
`TITLE_FONT` is a condensed 6x11 pixel face in the manner of Impact —
two-unit stems, counters squeezed to two — outlined in black by stamping the
word eight times around itself. The 5x7 face from the first attempt stayed on
for the tagline; a second line in the heavy face fought the title. The first
pass had the title ranged left with a bomb crossing the letters, which read as
a hole in the word rather than as a bomb, so nothing overlaps the lettering
now.

    python3 tools/make_banner.py                     # store/banner-720x320.png

## Reference

Pulled out of the README when that was cut down to what a player needs. These
are the numbers and shapes as they stand.

### Files

- `src/c/game.h` — layout and gameplay tuning constants, the game state, and
  the demo-mode knobs
- `src/c/game.c` — waves, bomber, bombs, catch/miss, the miss chain, and the
  demo autoplayer
- `src/c/render.c` — all drawing, from primitives; the only bitmap in the
  project is the launcher icon
- `src/c/sound.c` — the PCM mixer, the synthesised effects and the mute setting
- `src/c/menu.c` — options menu, difficulty picker, how-to-play text
- `src/c/settings.c` — persisted high score, difficulty and vibration
- `src/c/main.c` — window, frame timer, touch and button input
- `resources/kablooey-icon.png` — 25x25 launcher icon from `tools/make_icon.py`
- `store/icon-48.png`, `store/icon-80.png`, `store/icon-144.png` — appstore
  icons from `tools/make_store_icon.py`
- `store/banner-720x320.png` — Rebble store banner from `tools/make_banner.py`
- `tools/capture.py` — demo build, emulator run, GIF and stills

### Screen layout (emery, 200x228)

| Band | Purpose |
| --- | --- |
| y 0–22 | score (left), sound icon, wave number (right) |
| y 24–60 | sky; the bomber paces the top of the wall |
| y 60+ | the wall: capstone, then brick courses |
| y 114+ | touch zone (wall in shadow); drags here steer the pails |
| y 190–228 | grip strip with the thumb marker |

### Waves

Wave 1 throws 10 bombs 800ms apart at 1.75px/frame. Each wave after it adds
three bombs, shortens the gap by 90ms, speeds the bombs up by 0.375px/frame,
tightens the bomber's about-face window and pays `wave` points per catch. The
caps — 40 bombs, 170ms, 7px/frame, a 0.7s fall — are reached around wave 15.
See `wave_config()` in `src/c/game.c`. Peak bombs in flight is about 8, around
wave 8, against a pool of 10.

Difficulty sets the starting wave (1, 3 or 6), which is also the floor for the
knock-back. The high score and difficulty persist in `settings.c`; sound keeps
its own flag in `sound.c`.

### Sound

Effects are rendered to 8kHz PCM at startup and mixed into a single speaker
stream that stays open while the game is on screen. The amplifier clicks when
it powers down, so per-effect playback pops however cleanly the waveform ends;
one long-lived stream moves that click to app open and close. Nothing is
written between effects, which keeps the firmware's queue empty — it accepts
every byte without backpressure and never reports how much it holds, so a
backlog there turns into audible lag, and flushing it would mean cycling the
amplifier. If the stream cannot be opened, `sound.c` falls back to one-shot
tones.

Clearing a wave is silent; the fanfare is the tail of the end-of-game jingle.
The splash comes in three lengths — 200ms, 155ms and 110ms — picked by how fast
the wave throws (waves 1-3, 4-5, and 6 on), so a catch always finishes before
the next bomb lands.

### Demo build and captures

`KABLOOEY_DEMO=1 pebble build` builds a version that plays itself and pins the
backlight on. The knobs are at the top of `src/c/game.h`: `DEMO_BANNER_MS`,
`DEMO_HAND_SPEED`, `DEMO_MISS_EVERY`, `DEMO_START_WAVE`.

    tools/capture.py                        # 12s GIF into screenshots/
    tools/capture.py --seconds 20 --fps 25
    tools/capture.py --stills 3,8,14        # PNG stills at those times too
    tools/capture.py --no-build             # use the pbw already built

### Debug switches

- `TOUCH_DEBUG_LOG` in `main.c` — every touch event. Off: 30 lines a second
  during a drag is enough jitter to starve the audio stream.
- `AUDIO_DEBUG_LOG` in `sound.c` — one pacing summary per second. Off.

Both are read with `pebble logs --emulator emery` or `--cloudpebble`.

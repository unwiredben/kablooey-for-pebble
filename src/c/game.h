#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Kablooey -- catch the mad bomber's bombs in a stack of water buckets.
//
// Everything worth tuning lives in this header so gameplay can be adjusted
// without hunting through the update code.
// ---------------------------------------------------------------------------

// Sub-pixel fixed point. Positions and speeds are stored in 1/16ths of a
// pixel so slow motion stays smooth at 30fps.
#define FP 16
#define TO_FP(px) ((px) * FP)
#define TO_PX(fp) ((fp) / FP)

#define FRAME_MS 33  // ~30fps

// --- Demo mode -------------------------------------------------------------
// A build that plays itself, for capturing screenshots and animations. Off
// unless the build defines it: `KABLOOEY_DEMO=1 pebble build`, which is what
// tools/capture.py does.
#ifndef DEMO_MODE
#define DEMO_MODE 0
#endif

#if DEMO_MODE
#define DEMO_BANNER_MS 1800  // how long banners sit before the demo taps on
#define DEMO_HAND_SPEED 7    // px per frame the demo's hand can travel
#define DEMO_MISS_EVERY 4    // fumble one bomb every Nth wave (0 never misses)
#define DEMO_START_WAVE 0    // force a starting wave, or 0 for the difficulty
#endif

// --- Screen layout (emery: 200 x 228) --------------------------------------
#define SCREEN_W PBL_DISPLAY_WIDTH
#define SCREEN_H PBL_DISPLAY_HEIGHT

#define SCORE_BAR_H 22

#define BOMBER_W 30
#define BOMBER_H 36
#define BOMBER_TOP (SCORE_BAR_H + 2)          // y of bomber's hat
#define BOMBER_FLOOR (BOMBER_TOP + BOMBER_H)  // y of bomber's feet
#define BOMBER_MARGIN 6                       // how close to the edge he walks

// He doubles back at random so his path cannot be read: a turn is scheduled
// somewhere in the wave's window, then rescheduled. Bouncing off a wall counts
// as a turn, so he never stutters against the edge. The window tightens wave
// by wave, which scatters where the bombs come from.
#define BOMBER_TURN_MIN_MS 700
#define BOMBER_TURN_MAX_MS 2600
#define BOMBER_TURN_FLOOR_MS 220

// The bomber paces along the top of the wall; the wall face starts at his feet
// and everything above it is sky.
#define WALL_TOP BOMBER_FLOOR
#define WALL_CAP_H 6  // the capstone course he stands on

#define TOUCH_STRIP_H 38
#define TOUCH_STRIP_TOP (SCREEN_H - TOUCH_STRIP_H)

// The painted strip is the affordance, but the whole bottom half of the screen
// steers the pails -- fingers land higher than the artwork suggests.
#define TOUCH_ZONE_TOP (SCREEN_H / 2)

#define BOMB_R 5                     // bomb body radius
#define BOMB_SPAWN_Y (BOMBER_TOP + 20)  // level with his throwing hand
#define MISS_Y TOUCH_STRIP_TOP       // bomb bottom past this line is a miss

#define BUCKET_W 38
#define BUCKET_H 10           // one pail
#define BUCKET_GAP 4          // air between stacked pails
#define BUCKET_PITCH (BUCKET_H + BUCKET_GAP)
#define BUCKET_STACK_BOTTOM (TOUCH_STRIP_TOP - 4)
#define BUCKET_START_COUNT 3
#define BUCKET_MIN_X (BUCKET_W / 2 + 2)
#define BUCKET_MAX_X (SCREEN_W - BUCKET_W / 2 - 2)

// --- Gameplay --------------------------------------------------------------
#define MAX_BOMBS 10       // bombs in flight at once (peak is ~8 around wave 8)
#define MAX_FX 4           // overlapping splashes / blasts

// Miss chain: every bomb still on screen goes off in turn, bottom to top.
#define CHAIN_FIRST_MS 320  // beat after the ground blast before the chain starts
#define CHAIN_STEP_MS 260   // between detonations
#define CHAIN_END_MS 320    // after the last one, before the banner

typedef struct {
  int bomb_count;        // bombs the bomber throws this wave
  int drop_interval_ms;  // time between throws
  int bomb_speed;        // fall speed, FP units per frame
  int bomber_speed;      // walk speed, FP units per frame
  int turn_min_ms;       // random about-face window
  int turn_max_ms;
  int points_per_catch;
} WaveConfig;

typedef enum {
  GAME_READY,       // waiting for a tap to start the wave
  GAME_PLAYING,
  GAME_CHAIN,       // a bomb got through: the rest cook off one by one
  GAME_WAVE_CLEAR,  // caught them all; tap for the next wave
  GAME_OVER,
} GameState;

typedef struct {
  bool active;
  int x, y;  // FP, centre of the bomb body
} Bomb;

typedef enum {
  FX_NONE,
  FX_SPLASH,  // bomb safely doused
  FX_BLAST,   // bomb hit the ground
} FxKind;

typedef struct {
  FxKind kind;
  int x, y;      // pixels
  int frames;    // countdown; 0 == finished
} Fx;

typedef struct {
  GameState state;
  WaveConfig wave_cfg;
  int wave;

  int score;
  int high_score;   // live: a run beating it shows as it happens
  bool new_best;
  int buckets;

  // Difficulty: where a game starts, and the lowest wave a miss can send you.
  int start_wave;

  // Bomber
  int bomber_x;      // FP, centre
  int bomber_dir;    // +1 right, -1 left
  int turn_timer_ms; // until his next unprompted about-face
  int walk_phase;    // animation counter

  // Bombs
  Bomb bombs[MAX_BOMBS];
  int bombs_to_throw;   // left to throw this wave
  int bombs_in_flight;
  int throw_timer_ms;

  // Player
  int bucket_x;  // pixels, centre of the stack

#if DEMO_MODE
  int demo_tap_timer_ms;
  int demo_waves_played;
  bool demo_fumble;  // this wave lets one bomb through, for the chain reaction
#endif

  Fx fx[MAX_FX];
  int chain_timer_ms;
  bool chain_done;  // last bomb has gone off; waiting out CHAIN_END_MS
  bool knocked_back;  // the miss cost a wave as well as a pail
  int shake;  // frames of screen shake left
  bool touch_ok;  // false where the touchscreen is unavailable
} Game;

void game_init(Game *g);           // reads difficulty and high score from settings
void game_start_wave(Game *g);
void game_tap(Game *g);            // screen tap: start or restart
void game_set_bucket_x(Game *g, int x);
void game_update(Game *g);         // advance one frame

void game_render(Game *g, Layer *layer, GContext *ctx);

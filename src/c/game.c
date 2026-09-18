// SPDX-FileCopyrightText: 2026 Ben Combee
// SPDX-License-Identifier: MIT

#include "game.h"
#include "settings.h"
#include "sound.h"

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Wave 1 stays gentle; each wave after it throws more bombs, throws them
// sooner, drops them faster and scatters them wider, and pays better. The
// caps are what the late game settles at -- with the old ones everything was
// pinned by wave 12 and it was still comfortable, so they are set well beyond
// where a player is expected to survive.
static WaveConfig wave_config(int wave) {
  const int w = wave - 1;  // 0-based step count
  const WaveConfig c = {
    .bomb_count = clampi(10 + w * 3, 10, 40),
    .drop_interval_ms = clampi(800 - w * 90, 170, 800),
    // 1.75 px/frame (~53 px/sec) up to 7 px/frame (~210 px/sec), which crosses
    // the screen in about 0.7s.
    .bomb_speed = clampi(TO_FP(1) + (FP * 3) / 4 + w * 6, 0, TO_FP(7)),
    .bomber_speed = clampi(TO_FP(1) + FP / 4 + w * 4, 0, TO_FP(4)),
    .turn_min_ms = clampi(BOMBER_TURN_MIN_MS - w * 45, BOMBER_TURN_FLOOR_MS,
                          BOMBER_TURN_MIN_MS),
    .turn_max_ms = clampi(BOMBER_TURN_MAX_MS - w * 170,
                          BOMBER_TURN_FLOOR_MS * 2, BOMBER_TURN_MAX_MS),
    .points_per_catch = wave,
  };
  return c;
}

static void schedule_turn(Game *g) {
  const int lo = g->wave_cfg.turn_min_ms;
  const int span = g->wave_cfg.turn_max_ms - lo;
  g->turn_timer_ms = lo + ((span > 0) ? (rand() % span) : 0);
}

static void clear_bombs(Game *g) {
  for (int i = 0; i < MAX_BOMBS; i++) {
    g->bombs[i].active = false;
  }
  g->bombs_in_flight = 0;
}

// Effects share a small pool: a free slot if there is one, otherwise the one
// closest to finishing.
static void start_fx(Game *g, FxKind kind, int x, int y) {
  Fx *slot = &g->fx[0];
  for (int i = 0; i < MAX_FX; i++) {
    if (g->fx[i].frames <= 0) {
      slot = &g->fx[i];
      break;
    }
    if (g->fx[i].frames < slot->frames) slot = &g->fx[i];
  }
  slot->kind = kind;
  slot->x = x;
  slot->y = y;
  slot->frames = (kind == FX_BLAST) ? 10 : 6;
}

void game_init(Game *g) {
  const bool touch_ok = g->touch_ok;
  memset(g, 0, sizeof(*g));
  g->touch_ok = touch_ok;
  g->state = GAME_READY;
  g->start_wave = difficulty_start_wave(settings_difficulty());
#if DEMO_MODE && DEMO_START_WAVE > 0
  g->start_wave = DEMO_START_WAVE;
#endif
  g->high_score = settings_high_score();
  g->wave = g->start_wave;
  g->wave_cfg = wave_config(g->wave);
  g->score = 0;
  g->buckets = BUCKET_START_COUNT;
  g->bomber_x = TO_FP(SCREEN_W / 2);
  g->bomber_dir = (rand() % 2) ? 1 : -1;
  schedule_turn(g);
  g->bucket_x = SCREEN_W / 2;
  clear_bombs(g);
}

void game_start_wave(Game *g) {
  g->knocked_back = false;
#if DEMO_MODE
  g->demo_waves_played++;
  g->demo_fumble = (DEMO_MISS_EVERY > 0) &&
                   (g->demo_waves_played % DEMO_MISS_EVERY == 0);
#endif
  clear_bombs(g);
  g->wave_cfg = wave_config(g->wave);
  sound_set_tempo(g->wave_cfg.drop_interval_ms);
  g->bombs_to_throw = g->wave_cfg.bomb_count;
  g->throw_timer_ms = 400;  // a beat before the first bomb
  for (int i = 0; i < MAX_FX; i++) {
    g->fx[i].kind = FX_NONE;
    g->fx[i].frames = 0;
  }
  g->chain_done = false;
  g->state = GAME_PLAYING;
}

void game_tap(Game *g) {
  switch (g->state) {
    case GAME_READY:
      game_start_wave(g);
      break;
    case GAME_WAVE_CLEAR:
      g->wave++;
      game_start_wave(g);
      break;
    case GAME_OVER:
      game_init(g);
      game_start_wave(g);
      break;
    case GAME_PLAYING:
    case GAME_CHAIN:
      break;  // taps during play are just bucket moves
  }
}

void game_set_bucket_x(Game *g, int x) {
  if (x < BUCKET_MIN_X) x = BUCKET_MIN_X;
  if (x > BUCKET_MAX_X) x = BUCKET_MAX_X;
  g->bucket_x = x;
}

// y of the rim of the topmost remaining pail -- the top of the catch box. It
// drops as the stack gets shorter, which is what makes losing a pail hurt.
static int catch_line(const Game *g) {
  return BUCKET_STACK_BOTTOM - (g->buckets * BUCKET_PITCH - BUCKET_GAP);
}

static void throw_bomb(Game *g) {
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (g->bombs[i].active) continue;
    g->bombs[i].active = true;
    g->bombs[i].x = g->bomber_x;
    g->bombs[i].y = TO_FP(BOMB_SPAWN_Y);
    g->bombs_in_flight++;
    g->bombs_to_throw--;
    return;
  }
  // No free slot: hold the bomb and try again next frame.
}

// A bomb reached the ground: blow the pail, then let every bomb still in the
// air cook off in turn on the way back up. Getting through a wave also costs
// the wave itself -- you drop back one and have to earn your way up again.
static void begin_chain(Game *g, int x, int y) {
  start_fx(g, FX_BLAST, x, y);
  g->shake = 8;
  g->buckets--;
  g->knocked_back = (g->wave > g->start_wave);
  if (g->knocked_back) {
    g->wave--;
    g->wave_cfg = wave_config(g->wave);  // the banner should show what is next
  }
  g->bombs_to_throw = 0;
  if (settings_vibration()) vibes_short_pulse();
  sound_play(SFX_MISS);
  // With nothing else in the air there is no chain to run, just a beat to let
  // the blast land before the banner.
  g->chain_done = (g->bombs_in_flight == 0);
  g->chain_timer_ms = g->chain_done ? CHAIN_END_MS : CHAIN_FIRST_MS;
  g->state = GAME_CHAIN;
}

// Detonate the lowest bomb still on screen. Returns false when none are left.
static bool pop_lowest_bomb(Game *g) {
  Bomb *lowest = NULL;
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!g->bombs[i].active) continue;
    if (!lowest || g->bombs[i].y > lowest->y) lowest = &g->bombs[i];
  }
  if (!lowest) return false;

  start_fx(g, FX_BLAST, TO_PX(lowest->x), TO_PX(lowest->y));
  lowest->active = false;
  g->bombs_in_flight--;
  g->shake = 4;
  sound_play(SFX_POP);
  return true;
}

static void update_chain(Game *g) {
  g->chain_timer_ms -= FRAME_MS;
  if (g->chain_timer_ms > 0) return;

  if (g->chain_done) {
    if (g->buckets <= 0) {
      g->state = GAME_OVER;
      settings_note_score(g->score);  // the run is over: commit the best
      sound_play(SFX_GAME_OVER);
    } else {
      g->state = GAME_READY;
    }
    return;
  }

  if (pop_lowest_bomb(g)) {
    g->chain_timer_ms = CHAIN_STEP_MS;
  } else {
    g->chain_done = true;
    g->chain_timer_ms = CHAIN_END_MS;
  }
}

static void update_bomber(Game *g) {
  g->bomber_x += g->bomber_dir * g->wave_cfg.bomber_speed;
  const int min_x = TO_FP(BOMBER_MARGIN + BOMBER_W / 2);
  const int max_x = TO_FP(SCREEN_W - BOMBER_MARGIN - BOMBER_W / 2);

  if (g->bomber_x <= min_x) {
    g->bomber_x = min_x;
    g->bomber_dir = 1;
    schedule_turn(g);  // the wall took his turn; give him a fresh run
  } else if (g->bomber_x >= max_x) {
    g->bomber_x = max_x;
    g->bomber_dir = -1;
    schedule_turn(g);
  } else {
    g->turn_timer_ms -= FRAME_MS;
    if (g->turn_timer_ms <= 0) {
      g->bomber_dir = -g->bomber_dir;
      schedule_turn(g);
    }
  }

  g->walk_phase++;
}

static void update_bombs(Game *g) {
  // The whole stack is one catch box: touching any pail -- top rim, a lower
  // one the stack slid under, or a side -- douses the bomb. The gaps between
  // pails are inside the box, since a falling bomb can only reach them by
  // passing through a pail.
  const int box_top = catch_line(g);
  const int box_left = g->bucket_x - BUCKET_W / 2;
  const int box_right = g->bucket_x + BUCKET_W / 2;

  for (int i = 0; i < MAX_BOMBS; i++) {
    Bomb *b = &g->bombs[i];
    if (!b->active) continue;

    b->y += g->wave_cfg.bomb_speed;
    const int px = TO_PX(b->x);
    const int py = TO_PX(b->y);
    const int bottom = py + BOMB_R;

    const bool overlaps = (bottom >= box_top && py - BOMB_R <= BUCKET_STACK_BOTTOM &&
                           px + BOMB_R >= box_left && px - BOMB_R <= box_right);
    if (overlaps) {
      b->active = false;
      g->bombs_in_flight--;
      g->score += g->wave_cfg.points_per_catch;
      if (g->score > g->high_score) {
        g->high_score = g->score;
        g->new_best = true;
      }
      sound_play(SFX_CATCH);
      // Splash where it went in: the rim, or lower down for a side hit.
      start_fx(g, FX_SPLASH, px, (py < box_top) ? box_top : py);
      continue;
    }

    if (bottom >= MISS_Y) {
      b->active = false;
      g->bombs_in_flight--;
      begin_chain(g, px, MISS_Y - 2);
      return;  // the chain owns the bomb list from here
    }
  }
}

#if DEMO_MODE
// The demo plays itself: it taps through the banners and chases whichever bomb
// is closest to landing, at a speed that still reads as a hand moving. Every
// DEMO_MISS_EVERY waves it lets one through, so captures include the chain
// reaction and the knock-back as well as clean catches.
static void demo_update(Game *g) {
  if (g->state != GAME_PLAYING && g->state != GAME_CHAIN) {
    g->demo_tap_timer_ms -= FRAME_MS;
    if (g->demo_tap_timer_ms <= 0) {
      game_tap(g);
      g->demo_tap_timer_ms = DEMO_BANNER_MS;
    }
    return;
  }
  g->demo_tap_timer_ms = DEMO_BANNER_MS;

  if (g->state != GAME_PLAYING) return;

  const Bomb *urgent = NULL;
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!g->bombs[i].active) continue;
    if (!urgent || g->bombs[i].y > urgent->y) urgent = &g->bombs[i];
  }
  if (!urgent) return;

  // On a fumble wave, stop chasing once the bomb is committed: backing away
  // looks like a miss rather than a glitch.
  const int half_way = TO_FP((MISS_Y + BOMB_SPAWN_Y) / 2);
  if (g->demo_fumble && urgent->y > half_way) return;

  const int target = TO_PX(urgent->x);
  int dx = target - g->bucket_x;
  if (dx > DEMO_HAND_SPEED) dx = DEMO_HAND_SPEED;
  if (dx < -DEMO_HAND_SPEED) dx = -DEMO_HAND_SPEED;
  game_set_bucket_x(g, g->bucket_x + dx);
}
#endif

void game_update(Game *g) {
  for (int i = 0; i < MAX_FX; i++) {
    if (g->fx[i].frames > 0 && --g->fx[i].frames == 0) {
      g->fx[i].kind = FX_NONE;
    }
  }
  if (g->shake > 0) g->shake--;

#if DEMO_MODE
  demo_update(g);
#endif

  // He keeps pacing through the chain, but the bombs hang where they are.
  if (g->state == GAME_CHAIN) {
    update_bomber(g);
    update_chain(g);
    return;
  }

  if (g->state != GAME_PLAYING) return;

  update_bomber(g);

  if (g->bombs_to_throw > 0) {
    g->throw_timer_ms -= FRAME_MS;
    if (g->throw_timer_ms <= 0) {
      throw_bomb(g);
      g->throw_timer_ms = g->wave_cfg.drop_interval_ms;
    }
  }

  update_bombs(g);

  if (g->state == GAME_PLAYING && g->bombs_to_throw == 0 && g->bombs_in_flight == 0) {
    g->state = GAME_WAVE_CLEAR;  // clearing a wave is its own reward
  }
}

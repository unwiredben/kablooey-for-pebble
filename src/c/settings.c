// SPDX-FileCopyrightText: 2026 Ben Combee
// SPDX-License-Identifier: MIT

#include "settings.h"
#include "game.h"  // BUCKET_W_WIDE / BUCKET_W_NARROW

#define PERSIST_KEY_HIGH_SCORE 2
#define PERSIST_KEY_DIFFICULTY 3
#define PERSIST_KEY_VIBRATION 4
#define PERSIST_KEY_PAIL_WIDTH 5

static int s_high_score;
static Difficulty s_difficulty = DIFFICULTY_NORMAL;
static bool s_vibration = true;
static PailWidth s_pail_width = PAIL_WIDE;

void settings_load(void) {
  if (persist_exists(PERSIST_KEY_HIGH_SCORE)) {
    s_high_score = persist_read_int(PERSIST_KEY_HIGH_SCORE);
  }
  if (persist_exists(PERSIST_KEY_VIBRATION)) {
    s_vibration = persist_read_bool(PERSIST_KEY_VIBRATION);
  }
  if (persist_exists(PERSIST_KEY_DIFFICULTY)) {
    const int d = persist_read_int(PERSIST_KEY_DIFFICULTY);
    if (d >= 0 && d < DIFFICULTY_COUNT) s_difficulty = (Difficulty)d;
  }
  if (persist_exists(PERSIST_KEY_PAIL_WIDTH)) {
    const int p = persist_read_int(PERSIST_KEY_PAIL_WIDTH);
    if (p >= 0 && p < PAIL_WIDTH_COUNT) s_pail_width = (PailWidth)p;
  }
}

int settings_high_score(void) {
  return s_high_score;
}

bool settings_note_score(int score) {
  if (score <= s_high_score) return false;
  s_high_score = score;
  persist_write_int(PERSIST_KEY_HIGH_SCORE, s_high_score);
  return true;
}

void settings_reset_high_score(void) {
  s_high_score = 0;
  persist_write_int(PERSIST_KEY_HIGH_SCORE, 0);
}

bool settings_vibration(void) {
  return s_vibration;
}

void settings_set_vibration(bool on) {
  s_vibration = on;
  persist_write_bool(PERSIST_KEY_VIBRATION, on);
  if (on) vibes_short_pulse();  // confirm the new setting the way it will feel
}

Difficulty settings_difficulty(void) {
  return s_difficulty;
}

void settings_set_difficulty(Difficulty d) {
  if (d >= DIFFICULTY_COUNT) return;  // the enum is unsigned; no low end to check
  s_difficulty = d;
  persist_write_int(PERSIST_KEY_DIFFICULTY, (int)d);
}

int difficulty_start_wave(Difficulty d) {
  switch (d) {
    case DIFFICULTY_EASY:   return 1;
    case DIFFICULTY_NORMAL: return 3;
    case DIFFICULTY_HARD:   return 6;
    default:                return 1;
  }
}

const char *difficulty_name(Difficulty d) {
  switch (d) {
    case DIFFICULTY_EASY:   return "Easy";
    case DIFFICULTY_NORMAL: return "Normal";
    case DIFFICULTY_HARD:   return "Hard";
    default:                return "Normal";
  }
}

PailWidth settings_pail_width(void) {
  return s_pail_width;
}

void settings_set_pail_width(PailWidth p) {
  if (p >= PAIL_WIDTH_COUNT) return;  // the enum is unsigned; no low end to check
  s_pail_width = p;
  persist_write_int(PERSIST_KEY_PAIL_WIDTH, (int)p);
}

int pail_width_px(PailWidth p) {
  switch (p) {
    case PAIL_NARROW: return BUCKET_W_NARROW;
    case PAIL_WIDE:   return BUCKET_W_WIDE;
    default:          return BUCKET_W_WIDE;
  }
}

const char *pail_width_name(PailWidth p) {
  switch (p) {
    case PAIL_NARROW: return "Narrow";
    case PAIL_WIDE:   return "Wide";
    default:          return "Wide";
  }
}

const char *pail_width_detail(PailWidth p) {
  switch (p) {
    case PAIL_NARROW: return "Harder to catch with";
    case PAIL_WIDE:   return "The standard buckets";
    default:          return "";
  }
}

const char *difficulty_detail(Difficulty d) {
  switch (d) {
    case DIFFICULTY_EASY:   return "Start at wave 1";
    case DIFFICULTY_NORMAL: return "Start at wave 3";
    case DIFFICULTY_HARD:   return "Start at wave 6";
    default:                return "";
  }
}

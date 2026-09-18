#pragma once
#include <pebble.h>

// Everything that outlives a run: high score, difficulty and the vibration
// setting. Sound has its own persisted flag inside sound.c.

typedef enum {
  DIFFICULTY_EASY,
  DIFFICULTY_NORMAL,
  DIFFICULTY_HARD,
  DIFFICULTY_COUNT,
} Difficulty;

// A second, independent difficulty axis: how wide the pails are. Difficulty
// picks where a run starts, this picks how much of the screen catches -- so
// narrow pails can be played from wave 1 as well as from wave 6.
typedef enum {
  PAIL_WIDE,
  PAIL_NARROW,
  PAIL_WIDTH_COUNT,
} PailWidth;

void settings_load(void);

int settings_high_score(void);
// Records a finished game. Returns true if it was a new best.
bool settings_note_score(int score);
void settings_reset_high_score(void);

bool settings_vibration(void);
void settings_set_vibration(bool on);

Difficulty settings_difficulty(void);
void settings_set_difficulty(Difficulty d);

// Difficulty picks where a game starts; that wave is also the floor a miss can
// knock you back to.
int difficulty_start_wave(Difficulty d);
const char *difficulty_name(Difficulty d);
const char *difficulty_detail(Difficulty d);

PailWidth settings_pail_width(void);
void settings_set_pail_width(PailWidth p);

// Pixels across. Read once per run into Game.bucket_w, since the catch box and
// the drag clamps both have to agree with what is drawn.
int pail_width_px(PailWidth p);
const char *pail_width_name(PailWidth p);
const char *pail_width_detail(PailWidth p);

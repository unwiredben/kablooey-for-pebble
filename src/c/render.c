// SPDX-FileCopyrightText: 2026 Ben Combee
// SPDX-License-Identifier: MIT

#include "game.h"
#include "sound.h"

// --- Palette ---------------------------------------------------------------
#define C_SKY       GColorPictonBlue
#define C_BRICK     GColorDarkGray    // upper wall
#define C_MORTAR    GColorLightGray
#define C_BRICK_LOW GColorBlack       // the wall in shadow: the touch zone
#define C_MORTAR_LOW GColorDarkGray
#define C_CAP       GColorLightGray   // capstone stays pale under his feet
#define C_BAR       GColorOxfordBlue
#define C_BAR_TEXT  GColorWhite
#define C_STRIP     GColorLightGray
#define C_STRIP_INK GColorDarkGray
#define C_SKIN      GColorMelon
#define C_SHIRT     GColorRed
#define C_PANTS     GColorDukeBlue
#define C_WATER     GColorVividCerulean
#define C_PAIL      GColorLightGray
#define C_FUSE      GColorWindsorTan
#define C_SPARK     GColorYellow
#define C_BLAST     GColorOrange

#define BRICK_W 26
#define BRICK_H 12
#define MORTAR 2

// Sky above, then the wall the bomber stands on: a capstone course at his feet
// and brick courses down to the touch strip. Courses below TOUCH_ZONE_TOP are
// laid in the darker pair, so the steerable zone reads as the wall in shadow
// without needing a separate tint.
static void draw_wall(GContext *ctx) {
  graphics_context_set_fill_color(ctx, C_SKY);
  graphics_fill_rect(ctx, GRect(0, SCORE_BAR_H, SCREEN_W, WALL_TOP - SCORE_BAR_H),
                     0, GCornerNone);

  graphics_context_set_fill_color(ctx, C_MORTAR);
  graphics_fill_rect(ctx, GRect(0, WALL_TOP, SCREEN_W, TOUCH_ZONE_TOP - WALL_TOP),
                     0, GCornerNone);
  graphics_context_set_fill_color(ctx, C_MORTAR_LOW);
  graphics_fill_rect(ctx, GRect(0, TOUCH_ZONE_TOP, SCREEN_W, SCREEN_H - TOUCH_ZONE_TOP),
                     0, GCornerNone);

  // Capstone: one wide slab, with a lip along the front edge so the top of the
  // wall reads as a surface he is standing on.
  graphics_context_set_fill_color(ctx, C_CAP);
  graphics_fill_rect(ctx, GRect(0, WALL_TOP, SCREEN_W, WALL_CAP_H - 1), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_line(ctx, GPoint(0, WALL_TOP), GPoint(SCREEN_W, WALL_TOP));
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_line(ctx, GPoint(0, WALL_TOP + WALL_CAP_H - 2),
                          GPoint(SCREEN_W, WALL_TOP + WALL_CAP_H - 2));

  int course = 0;
  for (int y = WALL_TOP + WALL_CAP_H; y < TOUCH_STRIP_TOP; y += BRICK_H, course++) {
    const bool shadowed = (y >= TOUCH_ZONE_TOP);
    graphics_context_set_fill_color(ctx, shadowed ? C_BRICK_LOW : C_BRICK);

    // Alternate courses are offset by half a brick for a running bond.
    const int start = (course % 2) ? -BRICK_W / 2 : 0;
    for (int x = start; x < SCREEN_W; x += BRICK_W) {
      graphics_fill_rect(ctx, GRect(x, y, BRICK_W - MORTAR, BRICK_H - MORTAR),
                         0, GCornerNone);
    }
  }
}

static void draw_bomber(GContext *ctx, int cx, int top, int dir, int phase) {
  const int face_y = top + 15;

  // Hat: crown and brim.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(cx - 7, top, 14, 8), 2, GCornersTop);
  graphics_fill_rect(ctx, GRect(cx - 13, top + 7, 26, 3), 0, GCornerNone);

  // Head.
  graphics_context_set_fill_color(ctx, C_SKIN);
  graphics_fill_circle(ctx, GPoint(cx, face_y), 8);

  // Eyes, looking the way he walks.
  const int look = dir;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(cx - 3, face_y - 2), 3);
  graphics_fill_circle(ctx, GPoint(cx + 4, face_y - 2), 3);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx - 3 + look, face_y - 2), 1);
  graphics_fill_circle(ctx, GPoint(cx + 4 + look, face_y - 2), 1);

  // Handlebar mustache.
  graphics_fill_rect(ctx, GRect(cx - 6, face_y + 4, 13, 3), 1, GCornersAll);

  // Body.
  graphics_context_set_fill_color(ctx, C_SHIRT);
  graphics_fill_rect(ctx, GRect(cx - 8, top + 22, 16, 10), 3, GCornersAll);

  // Arms: the leading one is raised, ready to lob.
  graphics_context_set_stroke_color(ctx, C_SKIN);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(cx + 6 * dir, top + 24),
                          GPoint(cx + 12 * dir, top + 19));
  graphics_draw_line(ctx, GPoint(cx - 6 * dir, top + 24),
                          GPoint(cx - 11 * dir, top + 28));
  graphics_context_set_stroke_width(ctx, 1);

  // Legs, mid-stride.
  const int step = ((phase / 4) % 2) ? 1 : -1;
  graphics_context_set_fill_color(ctx, C_PANTS);
  graphics_fill_rect(ctx, GRect(cx - 7, top + 31, 5, 5 - step), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx + 2, top + 31, 5, 5 + step), 0, GCornerNone);
}

static void draw_bomb(GContext *ctx, int cx, int cy, int phase) {
  // Fuse.
  graphics_context_set_stroke_color(ctx, C_FUSE);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(cx + 1, cy - BOMB_R),
                          GPoint(cx + 4, cy - BOMB_R - 4));
  graphics_context_set_stroke_width(ctx, 1);

  // Spark, flickering between two sizes.
  graphics_context_set_fill_color(ctx, ((phase / 3) % 2) ? C_SPARK : C_BLAST);
  graphics_fill_circle(ctx, GPoint(cx + 5, cy - BOMB_R - 5), ((phase / 3) % 2) ? 3 : 2);

  // Body with a pale halo, so it stays legible against the grey brick.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(cx, cy), BOMB_R + 1);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx, cy), BOMB_R);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_circle(ctx, GPoint(cx - 2, cy - 2), 1);
}

static void draw_bucket_stack(GContext *ctx, int cx, int count, int yoff) {
  const int left = cx - BUCKET_W / 2;
  for (int i = 0; i < count; i++) {
    const int top = BUCKET_STACK_BOTTOM - BUCKET_H - i * BUCKET_PITCH + yoff;

    // Pail body, slightly tapered by insetting the lower half.
    graphics_context_set_fill_color(ctx, C_PAIL);
    graphics_fill_rect(ctx, GRect(left, top, BUCKET_W, BUCKET_H), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, C_WATER);
    graphics_fill_rect(ctx, GRect(left + 2, top + 2, BUCKET_W - 4, BUCKET_H - 3),
                       0, GCornerNone);

    // Rim highlight and outline.
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(left, top, BUCKET_W, 2), 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_rect(ctx, GRect(left, top, BUCKET_W, BUCKET_H));

    // Water shimmer on the top pail only.
    if (i == count - 1) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_line(ctx, GPoint(left + 7, top + 5), GPoint(left + 16, top + 5));
      graphics_draw_line(ctx, GPoint(left + 22, top + 7), GPoint(left + 30, top + 7));
    }
  }
}

static void draw_fx(GContext *ctx, const Fx *fx) {
  if (fx->kind == FX_NONE || fx->frames <= 0) return;

  if (fx->kind == FX_SPLASH) {
    const int r = 10 - fx->frames;
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, GPoint(fx->x, fx->y), r + 2);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_fill_color(ctx, C_WATER);
    graphics_fill_circle(ctx, GPoint(fx->x - r, fx->y - r / 2), 2);
    graphics_fill_circle(ctx, GPoint(fx->x + r, fx->y - r / 2), 2);
  } else {
    const int r = (12 - fx->frames) * 2;
    graphics_context_set_fill_color(ctx, (fx->frames % 2) ? C_SPARK : C_BLAST);
    graphics_fill_circle(ctx, GPoint(fx->x, fx->y), r);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_circle(ctx, GPoint(fx->x, fx->y), r);
  }
}

// A little speaker, struck through when muted, sitting in the score bar as the
// only feedback for the top button.
static void draw_sound_icon(GContext *ctx, int cx, int cy) {
  const bool on = sound_is_audible();
  // The body stays light either way, so the red strike-through reads clearly.
  graphics_context_set_fill_color(ctx, on ? C_BAR_TEXT : GColorLightGray);
  graphics_context_set_stroke_color(ctx, on ? C_BAR_TEXT : GColorLightGray);
  graphics_fill_rect(ctx, GRect(cx - 5, cy - 2, 3, 5), 0, GCornerNone);
  // Cone.
  for (int i = 0; i < 5; i++) {
    graphics_draw_line(ctx, GPoint(cx - 2 + i, cy - 2 - i), GPoint(cx - 2 + i, cy + 2 + i));
  }
  if (on) {
    graphics_draw_line(ctx, GPoint(cx + 5, cy - 4), GPoint(cx + 7, cy - 6));
    graphics_draw_line(ctx, GPoint(cx + 5, cy + 4), GPoint(cx + 7, cy + 6));
  } else {
    graphics_context_set_stroke_color(ctx, GColorRed);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(cx - 7, cy + 7), GPoint(cx + 7, cy - 7));
    graphics_context_set_stroke_width(ctx, 1);
  }
}

static void draw_score_bar(GContext *ctx, const Game *g) {
  graphics_context_set_fill_color(ctx, C_BAR);
  graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCORE_BAR_H), 0, GCornerNone);

  static char score_text[16];
  static char wave_text[8];
  snprintf(score_text, sizeof(score_text), "%d", g->score);
  snprintf(wave_text, sizeof(wave_text), "W%d", g->wave);

  graphics_context_set_text_color(ctx, C_BAR_TEXT);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  graphics_draw_text(ctx, score_text, font, GRect(6, 0, SCREEN_W - 12, SCORE_BAR_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, wave_text, font, GRect(6, 0, SCREEN_W - 12, SCORE_BAR_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  draw_sound_icon(ctx, SCREEN_W - 48, SCORE_BAR_H / 2);
}

static void draw_touch_strip(GContext *ctx, const Game *g) {
  graphics_context_set_fill_color(ctx, C_STRIP);
  graphics_fill_rect(ctx, GRect(0, TOUCH_STRIP_TOP, SCREEN_W, TOUCH_STRIP_H),
                     0, GCornerNone);
  graphics_context_set_stroke_color(ctx, C_STRIP_INK);
  graphics_draw_line(ctx, GPoint(0, TOUCH_STRIP_TOP), GPoint(SCREEN_W, TOUCH_STRIP_TOP));

  // Grip ridges, so the strip reads as something you drag along.
  for (int x = 8; x < SCREEN_W - 6; x += 12) {
    graphics_draw_line(ctx, GPoint(x, TOUCH_STRIP_TOP + 26),
                            GPoint(x, TOUCH_STRIP_TOP + 32));
  }

  // A thumb marker tracking the stack.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(g->bucket_x - 14, TOUCH_STRIP_TOP + 4, 28, 9),
                     4, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(g->bucket_x - 1, TOUCH_STRIP_TOP + 6, 2, 5),
                     0, GCornerNone);

}

static void draw_banner(GContext *ctx, const char *line1, const char *line2) {
  // Both lines are set big enough to read at a glance mid-game, which leaves
  // the box nearly full width.
  const int h = line2 ? 72 : 44;
  const GRect box = GRect(4, (SCREEN_H - h) / 2 - 10, SCREEN_W - 8, h);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, box, 6, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_round_rect(ctx, box, 6);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, line1, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(box.origin.x, box.origin.y + 1, box.size.w, 34),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  if (line2) {
    graphics_draw_text(ctx, line2, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(box.origin.x, box.origin.y + 36, box.size.w, 32),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

void game_render(Game *g, Layer *layer, GContext *ctx) {
  (void)layer;
  const int shake = (g->shake > 0) ? ((g->shake % 2) ? 2 : -2) : 0;

  draw_wall(ctx);
  draw_score_bar(ctx, g);
  draw_touch_strip(ctx, g);
  draw_bomber(ctx, TO_PX(g->bomber_x), BOMBER_TOP, g->bomber_dir, g->walk_phase);
  draw_bucket_stack(ctx, g->bucket_x, g->buckets, shake);

  for (int i = 0; i < MAX_BOMBS; i++) {
    if (g->bombs[i].active) {
      draw_bomb(ctx, TO_PX(g->bombs[i].x), TO_PX(g->bombs[i].y), g->walk_phase + i * 2);
    }
  }

  for (int i = 0; i < MAX_FX; i++) {
    draw_fx(ctx, &g->fx[i]);
  }

  static char sub[24];
  switch (g->state) {
    case GAME_READY:
      if (!g->touch_ok) {
#ifdef PBL_TOUCH
        // The hardware has a touchscreen, so this is the system setting being
        // off rather than a watch that cannot play at all.
        draw_banner(ctx, "TOUCH IS OFF", "enable it to play");
#else
        draw_banner(ctx, "NEEDS TOUCH", "no touchscreen");
#endif
        break;
      }
      if (g->knocked_back) {
        snprintf(sub, sizeof(sub), "wave %d, %d pails", g->wave, g->buckets);
        draw_banner(ctx, "BACK A WAVE", sub);
        break;
      }
      snprintf(sub, sizeof(sub), "%d pails, %d pt%s", g->buckets,
               g->wave_cfg.points_per_catch,
               (g->wave_cfg.points_per_catch == 1) ? "" : "s");
      draw_banner(ctx, "TAP TO PLAY", sub);
      break;
    case GAME_WAVE_CLEAR:
      snprintf(sub, sizeof(sub), "tap for wave %d", g->wave + 1);
      draw_banner(ctx, "WAVE CLEAR", sub);
      break;
    case GAME_OVER:
      if (g->new_best) {
        snprintf(sub, sizeof(sub), "%d - a record", g->score);
        draw_banner(ctx, "NEW BEST!", sub);
      } else {
        snprintf(sub, sizeof(sub), "%d, best %d", g->score, g->high_score);
        draw_banner(ctx, "KABLOOEY!", sub);
      }
      break;
    case GAME_PLAYING:
    case GAME_CHAIN:
      break;  // no banner while the fireworks are going off
  }
}

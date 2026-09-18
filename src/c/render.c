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
  // Off for the same reason it is off in draw_bomb(): an antialiased edge is a
  // blend with whatever is behind it, so it cannot be captured into a sprite.
  graphics_context_set_antialiased(ctx, false);

  const int face_y = top + 15;

  // Hat: crown and brim.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(cx - 7, top, 14, 8), 2, GCornersTop);
  graphics_fill_rect(ctx, GRect(cx - 13, top + 7, 26, 3), 0, GCornerNone);

  // Head, outlined in black like the arms and for the same reason: skin
  // against the sky is two mid tones, and his face is the thing you track to
  // read which way he is about to turn.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx, face_y), 9);
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

  // Arms: the leading one is raised, ready to lob. Stroked black at 5px first
  // and skin at 3px over it, which leaves a one-pixel outline: on the real
  // screen a bare skin-coloured arm disappeared into both the sky and the
  // brick, since it is three pixels of mid tone against mid tones.
  const GPoint lead_from = GPoint(cx + 6 * dir, top + 24);
  const GPoint lead_to = GPoint(cx + 12 * dir, top + 19);
  const GPoint trail_from = GPoint(cx - 6 * dir, top + 24);
  const GPoint trail_to = GPoint(cx - 11 * dir, top + 28);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 5);
  graphics_draw_line(ctx, lead_from, lead_to);
  graphics_draw_line(ctx, trail_from, trail_to);

  graphics_context_set_stroke_color(ctx, C_SKIN);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, lead_from, lead_to);
  graphics_draw_line(ctx, trail_from, trail_to);
  graphics_context_set_stroke_width(ctx, 1);

  // Legs, mid-stride.
  const int step = ((phase / 4) % 2) ? 1 : -1;
  graphics_context_set_fill_color(ctx, C_PANTS);
  graphics_fill_rect(ctx, GRect(cx - 7, top + 31, 5, 5 - step), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx + 2, top + 31, 5, 5 + step), 0, GCornerNone);

  graphics_context_set_antialiased(ctx, true);
}

static void draw_bomb(GContext *ctx, int cx, int cy, int phase) {
  // Antialiasing off, and not only for speed. An antialiased edge is a blend
  // with whatever is behind it, so it cannot be captured into a sprite: the
  // same bomb over brick and over sky would need different edge pixels. With
  // it off, every pixel is either bomb or background, which is what makes the
  // cached sprite an exact reproduction of this function rather than a close
  // one. At 13 pixels across the difference is a shade on the fuse.
  graphics_context_set_antialiased(ctx, false);

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

  graphics_context_set_antialiased(ctx, true);
}

static void draw_bucket_stack(GContext *ctx, int cx, int w, int count, int yoff) {
  const int left = cx - w / 2;
  for (int i = 0; i < count; i++) {
    const int top = BUCKET_STACK_BOTTOM - BUCKET_H - i * BUCKET_PITCH + yoff;

    // Pail body, slightly tapered by insetting the lower half.
    graphics_context_set_fill_color(ctx, C_PAIL);
    graphics_fill_rect(ctx, GRect(left, top, w, BUCKET_H), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, C_WATER);
    graphics_fill_rect(ctx, GRect(left + 2, top + 2, w - 4, BUCKET_H - 3),
                       0, GCornerNone);

    // Rim highlight and outline.
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(left, top, w, 2), 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_rect(ctx, GRect(left, top, w, BUCKET_H));

    // Water shimmer on the top pail only, proportioned to the pail: the two
    // strokes were placed by eye on the wide one, and scaling them keeps the
    // narrow pail from wearing them past its own rim.
    if (i == count - 1) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_line(ctx, GPoint(left + w * 7 / BUCKET_W_WIDE, top + 5),
                              GPoint(left + w * 16 / BUCKET_W_WIDE, top + 5));
      graphics_draw_line(ctx, GPoint(left + w * 22 / BUCKET_W_WIDE, top + 7),
                              GPoint(left + w * 30 / BUCKET_W_WIDE, top + 7));
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

// --- Wall cache ------------------------------------------------------------
// The wall never changes: draw_wall() takes no arguments, and the shake only
// offsets the pail stack. So it is drawn once, kept as pixels, and copied into
// the framebuffer every frame after that -- 400us against 1700us for the ~85
// fill_rects it replaces, measured on a Time 2, for 41KB of the ~99KB heap.
//
// The copy deliberately does not go through graphics_draw_bitmap_in_rect(),
// which measured 2200us, slower than drawing the bricks. Nothing about the
// wall needs alpha and GCompOpAssign made no difference, so the cost is that
// function's own per-row work rather than compositing. Writing rows into the
// captured framebuffer skips all of it.
#define WALL_CACHE_TOP SCORE_BAR_H
#define WALL_CACHE_H (SCREEN_H - SCORE_BAR_H)

static GBitmap *s_wall_cache;

// Built from the framebuffer rather than hand-rasterised: draw_wall() paints
// the real thing once and the pixels are copied straight out, so the cache
// cannot drift from the primitives the way a second rasteriser would.
static void wall_cache_build(GContext *ctx) {
  draw_wall(ctx);

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;

  GBitmap *cache = gbitmap_create_blank(GSize(SCREEN_W, WALL_CACHE_H),
                                        gbitmap_get_format(fb));
  if (cache) {
    for (int y = 0; y < WALL_CACHE_H; y++) {
      const GBitmapDataRowInfo src = gbitmap_get_data_row_info(fb, y + WALL_CACHE_TOP);
      const GBitmapDataRowInfo dst = gbitmap_get_data_row_info(cache, y);
      const int from = (src.min_x > dst.min_x) ? src.min_x : dst.min_x;
      const int to = (src.max_x < dst.max_x) ? src.max_x : dst.max_x;
      if (to >= from) memcpy(dst.data + from, src.data + from, to - from + 1);
    }
    s_wall_cache = cache;
  }
  graphics_release_frame_buffer(ctx, fb);
}

// The frame's bottom layer, written straight into the framebuffer: no clip box
// is consulted, which is safe only because the game layer covers the whole
// screen. A smaller layer would need its bounds folded in here.
static void copy_wall_rows(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  for (int y = 0; y < WALL_CACHE_H; y++) {
    const GBitmapDataRowInfo dst = gbitmap_get_data_row_info(fb, y + WALL_CACHE_TOP);
    const GBitmapDataRowInfo src = gbitmap_get_data_row_info(s_wall_cache, y);
    const int from = (src.min_x > dst.min_x) ? src.min_x : dst.min_x;
    const int to = (src.max_x < dst.max_x) ? src.max_x : dst.max_x;
    if (to >= from) memcpy(dst.data + from, src.data + from, to - from + 1);
  }
  graphics_release_frame_buffer(ctx, fb);
}

// --- Bomb sprite -----------------------------------------------------------
// The bombs are the expensive part of a frame: four fill_circles and a stroked
// fuse, eight in flight at the wave-8 peak. They are also all the same
// picture, so one palettised sprite serves every bomb on screen, drawn with
// GCompOpSet so its transparent entries are honoured.
//
// The flicker is done by rewriting the palette rather than by holding a sprite
// per phase. Each palette entry stands for a *pair* of colours -- what the
// pixel is on the small-spark phase and what it is on the big one -- and
// drawing swaps the whole 16-entry palette for the phase being drawn. That
// covers every way the two phases can differ at a pixel: the spark core
// (orange, then yellow), the ring that only exists on the big phase (clear,
// then yellow), and the pixels where the big spark covers part of the fuse
// (tan, then yellow), which a core/ring scheme could not express and which
// the on-watch verification caught.
#define BOMB_SPRITE_W 16
#define BOMB_SPRITE_H 20
#define BOMB_SPRITE_OX 6   // sprite's left edge, in pixels left of the bomb centre
#define BOMB_SPRITE_OY 13  // and its top edge above that centre
#define BOMB_PAL_SIZE 16   // 4bpp

// The two phase values that drive draw_bomb()'s `(phase / 3) % 2`: 0 is the
// small orange spark, 3 the large yellow one.
#define BOMB_PHASE_SMALL 0
#define BOMB_PHASE_BIG 3

// Where a sprite is rendered on screen to be captured. Anywhere fully on the
// display will do; the wall is painted over it in the same frame.
#define STAGE_X 20
#define STAGE_Y 40

static GBitmap *s_bomb_sprite;
static GColor *s_bomb_palette;  // the live one, owned by the bitmap
static GColor s_bomb_pal_small[BOMB_PAL_SIZE];
static GColor s_bomb_pal_big[BOMB_PAL_SIZE];
static bool s_bomb_sprite_failed;

// Copies a staged region back out of the framebuffer.
static void stage_capture(GContext *ctx, int w, int h, uint8_t *out) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  for (int y = 0; y < h; y++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, STAGE_Y + y);
    for (int x = 0; x < w; x++) {
      out[y * w + x] = row.data[STAGE_X + x];
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

static void stage_fill(GContext *ctx, GColor bg, int w, int h) {
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, GRect(STAGE_X, STAGE_Y, w, h), 0, GCornerNone);
}

static void sprite_set_pixel(GBitmap *bmp, int x, int y, uint8_t index) {
  const GBitmapDataRowInfo row = gbitmap_get_data_row_info(bmp, y);
  uint8_t *const b = &row.data[x / 2];
  // 4bpp, two pixels to a byte, the even one in the high nibble.
  if ((x % 2) == 0) {
    *b = (uint8_t)((*b & 0x0f) | (index << 4));
  } else {
    *b = (uint8_t)((*b & 0xf0) | (index & 0x0f));
  }
}

// Paints one variant of the bomb over a known background and copies the
// sprite box back out of the framebuffer.
static void bomb_capture(GContext *ctx, GColor bg, int phase, uint8_t *out) {
  stage_fill(ctx, bg, BOMB_SPRITE_W, BOMB_SPRITE_H);
  draw_bomb(ctx, STAGE_X + BOMB_SPRITE_OX, STAGE_Y + BOMB_SPRITE_OY, phase);
  stage_capture(ctx, BOMB_SPRITE_W, BOMB_SPRITE_H, out);
}

// Builds the sprite from four captures: both spark phases, each over two
// different backgrounds. A pixel that comes out the same over both
// backgrounds belongs to the bomb; one that follows the background is
// transparent. That is what separates a black body from black brick, which no
// colour-key could manage. draw_bomb() stays the only description of the art
// -- nothing here knows what a bomb looks like.
static void bomb_sprite_build(GContext *ctx) {
  s_bomb_sprite_failed = true;  // cleared once everything is in place

  const int n = BOMB_SPRITE_W * BOMB_SPRITE_H;
  uint8_t *buf = malloc((size_t)n * 4);
  if (!buf) return;
  uint8_t *const small_a = buf;
  uint8_t *const small_b = buf + n;
  uint8_t *const big_a = buf + n * 2;
  uint8_t *const big_b = buf + n * 3;

  bomb_capture(ctx, GColorRed, BOMB_PHASE_SMALL, small_a);
  bomb_capture(ctx, GColorGreen, BOMB_PHASE_SMALL, small_b);
  bomb_capture(ctx, GColorRed, BOMB_PHASE_BIG, big_a);
  bomb_capture(ctx, GColorGreen, BOMB_PHASE_BIG, big_b);

  s_bomb_palette = malloc(sizeof(GColor) * BOMB_PAL_SIZE);
  if (!s_bomb_palette) {
    free(buf);
    return;
  }
  GBitmap *sprite = gbitmap_create_blank_with_palette(
      GSize(BOMB_SPRITE_W, BOMB_SPRITE_H), GBitmapFormat4BitPalette,
      s_bomb_palette, true);
  if (!sprite) {
    free(s_bomb_palette);
    s_bomb_palette = NULL;
    free(buf);
    return;
  }

  // Entry 0 is transparent in both phases, so any pixel never written still
  // reads as background.
  s_bomb_pal_small[0] = GColorClear;
  s_bomb_pal_big[0] = GColorClear;
  int count = 1;
  bool overflowed = false;

  for (int y = 0; y < BOMB_SPRITE_H; y++) {
    for (int x = 0; x < BOMB_SPRITE_W; x++) {
      const int i = y * BOMB_SPRITE_W + x;
      // Same over both backgrounds means the bomb painted it; otherwise the
      // background showed through and the pixel is transparent in that phase.
      const GColor cs = (small_a[i] == small_b[i]) ? (GColor){ .argb = small_a[i] }
                                                   : GColorClear;
      const GColor cb = (big_a[i] == big_b[i]) ? (GColor){ .argb = big_a[i] }
                                               : GColorClear;

      int index = -1;
      for (int p = 0; p < count; p++) {
        if (s_bomb_pal_small[p].argb == cs.argb && s_bomb_pal_big[p].argb == cb.argb) {
          index = p;
          break;
        }
      }
      if (index < 0) {
        if (count >= BOMB_PAL_SIZE) {
          overflowed = true;
          index = 0;
        } else {
          s_bomb_pal_small[count] = cs;
          s_bomb_pal_big[count] = cb;
          index = count++;
        }
      }
      sprite_set_pixel(sprite, x, y, (uint8_t)index);
    }
  }

  free(buf);

  if (overflowed) {
    // More colour pairs than the palette holds means the capture saw something
    // unexpected, and a sprite built from a truncated palette would be wrong in
    // a way that is hard to see. Fall back to the primitives instead.
    APP_LOG(APP_LOG_LEVEL_WARNING, "bomb sprite: palette overflow, using primitives");
    gbitmap_destroy(sprite);
    s_bomb_palette = NULL;  // freed with the bitmap
    return;
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "bomb sprite: %d palette entries", count);
  s_bomb_sprite = sprite;
  s_bomb_sprite_failed = false;
}

static void draw_bomb_cached(GContext *ctx, int cx, int cy, int phase) {
  const bool big = ((phase / 3) % 2);
  memcpy(s_bomb_palette, big ? s_bomb_pal_big : s_bomb_pal_small,
         sizeof(GColor) * BOMB_PAL_SIZE);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_bomb_sprite,
                               GRect(cx - BOMB_SPRITE_OX, cy - BOMB_SPRITE_OY,
                                     BOMB_SPRITE_W, BOMB_SPRITE_H));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

// One entry point, so the benchmark and the frame agree about which path runs.
static void render_bomb(GContext *ctx, int cx, int cy, int phase) {
  if (s_bomb_sprite) {
    draw_bomb_cached(ctx, cx, cy, phase);
  } else {
    draw_bomb(ctx, cx, cy, phase);
  }
}

// --- Bomber sprites --------------------------------------------------------
// He has exactly four poses: two walk directions (which mirror his arms and
// move his pupils) times two leg positions. Nothing else about him animates,
// so four sprites cover him completely and no palette trickery is needed --
// unlike the bomb, each pose is one fixed picture.
//
// 36x40 at 4bpp is 720 bytes a pose, 2.8KB for the set. The box is wider than
// BOMBER_W because the hat brim and the raised arm both reach past his body,
// and it is symmetric so one box serves both directions.
#define BOMBER_SPRITE_W 36
#define BOMBER_SPRITE_H 40
#define BOMBER_SPRITE_OX 18  // his centre, in pixels from the sprite's left edge
#define BOMBER_SPRITE_OY 1   // his hat top, below the sprite's top edge
#define BOMBER_POSES 4

static GBitmap *s_bomber_sprites[BOMBER_POSES];
static bool s_bomber_sprites_failed;

// Pose index: bit 0 is the walk direction, bit 1 the leg position. The phase
// values are the ones that make draw_bomber()'s `(phase / 4) % 2` come out 0
// and 1, so the sprites are built from the same expression that animates him.
static int bomber_pose_index(int dir, int phase) {
  return ((dir > 0) ? 1 : 0) | (((phase / 4) % 2) ? 2 : 0);
}

static void bomber_capture(GContext *ctx, GColor bg, int pose, uint8_t *out) {
  const int dir = (pose & 1) ? 1 : -1;
  const int phase = (pose & 2) ? 4 : 0;
  stage_fill(ctx, bg, BOMBER_SPRITE_W, BOMBER_SPRITE_H);
  draw_bomber(ctx, STAGE_X + BOMBER_SPRITE_OX, STAGE_Y + BOMBER_SPRITE_OY, dir, phase);
  stage_capture(ctx, BOMBER_SPRITE_W, BOMBER_SPRITE_H, out);
}

// Same two-background trick as the bomb: a pixel that comes out the same over
// red and over green was painted by the bomber, one that follows the
// background is transparent. His hat and pupils are black, so a colour-key
// against the black lower brick would be just as wrong as it is for the bomb.
static GBitmap *bomber_sprite_build(GContext *ctx, int pose) {
  const int n = BOMBER_SPRITE_W * BOMBER_SPRITE_H;
  uint8_t *buf = malloc((size_t)n * 2);
  if (!buf) return NULL;
  uint8_t *const over_red = buf;
  uint8_t *const over_green = buf + n;

  bomber_capture(ctx, GColorRed, pose, over_red);
  bomber_capture(ctx, GColorGreen, pose, over_green);

  GColor *palette = malloc(sizeof(GColor) * 16);
  if (!palette) {
    free(buf);
    return NULL;
  }
  GBitmap *sprite = gbitmap_create_blank_with_palette(
      GSize(BOMBER_SPRITE_W, BOMBER_SPRITE_H), GBitmapFormat4BitPalette,
      palette, true);
  if (!sprite) {
    free(palette);
    free(buf);
    return NULL;
  }

  palette[0] = GColorClear;  // so an unwritten pixel reads as background
  int count = 1;
  bool overflowed = false;

  for (int y = 0; y < BOMBER_SPRITE_H; y++) {
    for (int x = 0; x < BOMBER_SPRITE_W; x++) {
      const int i = y * BOMBER_SPRITE_W + x;
      int index = 0;
      if (over_red[i] == over_green[i]) {
        const GColor c = (GColor){ .argb = over_red[i] };
        index = -1;
        for (int p = 1; p < count; p++) {
          if (palette[p].argb == c.argb) {
            index = p;
            break;
          }
        }
        if (index < 0) {
          if (count >= 16) {
            overflowed = true;
            index = 0;
          } else {
            palette[count] = c;
            index = count++;
          }
        }
      }
      sprite_set_pixel(sprite, x, y, (uint8_t)index);
    }
  }

  free(buf);

  if (overflowed) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "bomber sprite %d: palette overflow", pose);
    gbitmap_destroy(sprite);
    return NULL;
  }
  return sprite;
}

static void bomber_sprites_build(GContext *ctx) {
  s_bomber_sprites_failed = true;  // cleared once the whole set is in place
  for (int pose = 0; pose < BOMBER_POSES; pose++) {
    s_bomber_sprites[pose] = bomber_sprite_build(ctx, pose);
    if (!s_bomber_sprites[pose]) {
      // A partial set would draw some poses and not others, so drop the lot
      // and let the primitives handle him.
      for (int i = 0; i < BOMBER_POSES; i++) {
        if (s_bomber_sprites[i]) {
          gbitmap_destroy(s_bomber_sprites[i]);
          s_bomber_sprites[i] = NULL;
        }
      }
      return;
    }
  }
  s_bomber_sprites_failed = false;
}

static void draw_bomber_cached(GContext *ctx, int cx, int top, int dir, int phase) {
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_bomber_sprites[bomber_pose_index(dir, phase)],
                               GRect(cx - BOMBER_SPRITE_OX, top - BOMBER_SPRITE_OY,
                                     BOMBER_SPRITE_W, BOMBER_SPRITE_H));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

static void render_bomber(GContext *ctx, int cx, int top, int dir, int phase) {
  if (s_bomber_sprites[0]) {
    draw_bomber_cached(ctx, cx, top, dir, phase);
  } else {
    draw_bomber(ctx, cx, top, dir, phase);
  }
}

// --- Frame benchmark -------------------------------------------------------
// Flip to 1 to measure each element of the frame on the watch. It repeats an
// element BENCH_REPS times and divides, because time_ms() only resolves to a
// millisecond and a single element costs less than that. Read the numbers with
// `pebble logs --cloudpebble`, ideally from a `KABLOOEY_DEMO=1` build so the
// watch plays itself while it is measured. One frame's worth of drawing is
// thrown away while it runs; the next frame repaints over it.
#define RENDER_BENCH 0

#if RENDER_BENCH
// Kept for comparison only: both of these measured slower than
// copy_wall_rows(), and slower than the primitives they replace. A later
// change to the wall art should be able to re-run the comparison rather than
// re-argue it.
static void blit_wall(GContext *ctx) {
  graphics_draw_bitmap_in_rect(ctx, s_wall_cache,
                               GRect(0, WALL_CACHE_TOP, SCREEN_W, WALL_CACHE_H));
}

static void blit_wall_assign(GContext *ctx) {
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  graphics_draw_bitmap_in_rect(ctx, s_wall_cache,
                               GRect(0, WALL_CACHE_TOP, SCREEN_W, WALL_CACHE_H));
}

// Capture and release with nothing in between, so the framebuffer lock's own
// cost can be subtracted from the row copy.
static void fb_lock_only(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (fb) graphics_release_frame_buffer(ctx, fb);
}

// Draws the sprite and the primitives over identical backgrounds and counts
// the pixels that differ. Zero means the sprite, its palette and its mask
// reproduce draw_bomb() exactly -- which a screenshot cannot establish, since
// the bombs are 13 pixels across and always in motion.
static void bomb_sprite_verify(GContext *ctx) {
  if (!s_bomb_sprite) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "verify: no bomb sprite");
    return;
  }
  const int n = BOMB_SPRITE_W * BOMB_SPRITE_H;
  uint8_t *buf = malloc((size_t)n * 2);
  if (!buf) return;

  const GColor bgs[] = { GColorBlack, GColorLightGray, GColorPictonBlue };
  const int phases[] = { BOMB_PHASE_SMALL, BOMB_PHASE_BIG };

  for (unsigned b = 0; b < ARRAY_LENGTH(bgs); b++) {
    for (unsigned p = 0; p < ARRAY_LENGTH(phases); p++) {
      bomb_capture(ctx, bgs[b], phases[p], buf);

      graphics_context_set_fill_color(ctx, bgs[b]);
      graphics_fill_rect(ctx, GRect(STAGE_X, STAGE_Y,
                                    BOMB_SPRITE_W, BOMB_SPRITE_H), 0, GCornerNone);
      draw_bomb_cached(ctx, STAGE_X + BOMB_SPRITE_OX,
                       STAGE_Y + BOMB_SPRITE_OY, phases[p]);

      GBitmap *fb = graphics_capture_frame_buffer(ctx);
      if (!fb) break;
      int diff = 0;
      for (int y = 0; y < BOMB_SPRITE_H; y++) {
        const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, STAGE_Y + y);
        for (int x = 0; x < BOMB_SPRITE_W; x++) {
          if (row.data[STAGE_X + x] != buf[y * BOMB_SPRITE_W + x]) diff++;
        }
      }
      graphics_release_frame_buffer(ctx, fb);

      APP_LOG(APP_LOG_LEVEL_INFO, "verify: bg %u phase %s -> %d of %d pixels differ",
              b, (phases[p] == BOMB_PHASE_BIG) ? "big" : "small", diff, n);
    }
  }
  free(buf);
}

// The same check for each of the bomber's four poses.
static void bomber_sprite_verify(GContext *ctx) {
  if (!s_bomber_sprites[0]) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "verify: no bomber sprites");
    return;
  }
  const int n = BOMBER_SPRITE_W * BOMBER_SPRITE_H;
  uint8_t *buf = malloc((size_t)n);
  if (!buf) return;

  const GColor bgs[] = { GColorBlack, GColorLightGray, GColorPictonBlue };
  for (unsigned b = 0; b < ARRAY_LENGTH(bgs); b++) {
    int worst = 0;
    for (int pose = 0; pose < BOMBER_POSES; pose++) {
      bomber_capture(ctx, bgs[b], pose, buf);

      const int dir = (pose & 1) ? 1 : -1;
      const int phase = (pose & 2) ? 4 : 0;
      stage_fill(ctx, bgs[b], BOMBER_SPRITE_W, BOMBER_SPRITE_H);
      draw_bomber_cached(ctx, STAGE_X + BOMBER_SPRITE_OX, STAGE_Y + BOMBER_SPRITE_OY,
                         dir, phase);

      GBitmap *fb = graphics_capture_frame_buffer(ctx);
      if (!fb) break;
      int diff = 0;
      for (int y = 0; y < BOMBER_SPRITE_H; y++) {
        const GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, STAGE_Y + y);
        for (int x = 0; x < BOMBER_SPRITE_W; x++) {
          if (row.data[STAGE_X + x] != buf[y * BOMBER_SPRITE_W + x]) diff++;
        }
      }
      graphics_release_frame_buffer(ctx, fb);
      if (diff > worst) worst = diff;
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "verify: bomber bg %u -> worst %d of %d pixels differ",
            b, worst, n);
  }
  free(buf);
}

#define BENCH_REPS 30
// Repeats, rather than firing once: a log reader attached after the app
// launched would otherwise have missed the only run.
#define BENCH_EVERY_FRAMES 150  // ~5s apart

static uint32_t bench_now_ms(void) {
  time_t s = 0;
  uint16_t ms = 0;
  time_ms(&s, &ms);
  return (uint32_t)s * 1000u + ms;
}

// `r` is the repetition index, and the statement is welcome to use it: the
// animated elements take a phase, and holding it still would let the firmware
// short-circuit work a moving frame really does.
// Variadic because a braced statement's commas would otherwise be read as
// further macro arguments; parentheses are the only thing that shields them.
#define BENCH(label, ...) do {                                              \
    const uint32_t t0 = bench_now_ms();                                     \
    for (int r = 0; r < BENCH_REPS; r++) { __VA_ARGS__; }                   \
    const uint32_t dt = bench_now_ms() - t0;                                \
    APP_LOG(APP_LOG_LEVEL_INFO, "bench %s: %u us/frame (%u ms / %d reps)",  \
            label, (unsigned)(dt * 1000u / BENCH_REPS), (unsigned)dt,       \
            BENCH_REPS);                                                    \
  } while (0)

static void bench_frame(GContext *ctx, Game *g) {
  // Logged either side of the build so the cache's heap cost is measured and
  // not assumed. Only the bench's first run sees the difference -- after that
  // the cache already exists, so both numbers include it.
  APP_LOG(APP_LOG_LEVEL_INFO, "bench: heap free %u before wall cache",
          (unsigned)heap_bytes_free());
  if (!s_wall_cache) wall_cache_build(ctx);
  APP_LOG(APP_LOG_LEVEL_INFO, "bench: heap free %u after wall cache (%dx%d)",
          (unsigned)heap_bytes_free(), SCREEN_W, WALL_CACHE_H);

  bomb_sprite_verify(ctx);
  bomber_sprite_verify(ctx);

  BENCH("wall_prims", draw_wall(ctx));
  if (s_wall_cache) {
    BENCH("wall_blit", blit_wall(ctx));
    BENCH("wall_blit_assign", blit_wall_assign(ctx));
    BENCH("wall_rowcopy", copy_wall_rows(ctx));
    BENCH("fb_lock_only", fb_lock_only(ctx));
  }
  BENCH("score_bar", draw_score_bar(ctx, g));
  BENCH("touch_strip", draw_touch_strip(ctx, g));
  BENCH("bomber", draw_bomber(ctx, SCREEN_W / 2, BOMBER_TOP, 1, r));
  if (s_bomber_sprites[0]) {
    BENCH("bomber_sprite", draw_bomber_cached(ctx, SCREEN_W / 2, BOMBER_TOP, 1, r));
  }
  BENCH("bombs_x8", for (int i = 0; i < 8; i++) {
          draw_bomb(ctx, 20 + i * 20, 100, r + i * 2);
        });
  if (s_bomb_sprite) BENCH("bombs_x8_sprite", for (int i = 0; i < 8; i++) {
          draw_bomb_cached(ctx, 20 + i * 20, 100, r + i * 2);
        });
  BENCH("pails_x3", draw_bucket_stack(ctx, SCREEN_W / 2, g->bucket_w, 3, 0));
  BENCH("fx_splash", {
          const Fx fx = { .kind = FX_SPLASH, .x = 100, .y = 150,
                          .frames = 1 + (r % 6) };
          draw_fx(ctx, &fx);
        });
}
#endif  // RENDER_BENCH

void render_deinit(void) {
  if (s_wall_cache) {
    gbitmap_destroy(s_wall_cache);
    s_wall_cache = NULL;
  }
  if (s_bomb_sprite) {
    gbitmap_destroy(s_bomb_sprite);  // owns the palette
    s_bomb_sprite = NULL;
    s_bomb_palette = NULL;
  }
  for (int i = 0; i < BOMBER_POSES; i++) {
    if (s_bomber_sprites[i]) {
      gbitmap_destroy(s_bomber_sprites[i]);
      s_bomber_sprites[i] = NULL;
    }
  }
}

void game_render(Game *g, Layer *layer, GContext *ctx) {
  (void)layer;

#if RENDER_BENCH
  static int s_bench_frames;
  if (++s_bench_frames >= BENCH_EVERY_FRAMES) {
    s_bench_frames = 0;
    bench_frame(ctx, g);
  }
#endif

  const int shake = (g->shake > 0) ? ((g->shake % 2) ? 2 : -2) : 0;

  // Built before the wall, because staging the sprite captures paints a small
  // rectangle on screen and drawing the wall covers it in the same frame.
  if (!s_bomb_sprite && !s_bomb_sprite_failed) bomb_sprite_build(ctx);
  if (!s_bomber_sprites[0] && !s_bomber_sprites_failed) bomber_sprites_build(ctx);

  if (s_wall_cache) {
    copy_wall_rows(ctx);
  } else {
    wall_cache_build(ctx);  // paints the wall as a side effect; no frame lost
  }
  draw_score_bar(ctx, g);
  draw_touch_strip(ctx, g);
  render_bomber(ctx, TO_PX(g->bomber_x), BOMBER_TOP, g->bomber_dir, g->walk_phase);
  draw_bucket_stack(ctx, g->bucket_x, g->bucket_w, g->buckets, shake);

  for (int i = 0; i < MAX_BOMBS; i++) {
    if (g->bombs[i].active) {
      render_bomb(ctx, TO_PX(g->bombs[i].x), TO_PX(g->bombs[i].y), g->walk_phase + i * 2);
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
        snprintf(sub, sizeof(sub), "wave %d, %d buckets", g->wave, g->buckets);
        draw_banner(ctx, "BACK A WAVE", sub);
        break;
      }
      snprintf(sub, sizeof(sub), "%d buckets, %d pt%s", g->buckets,
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

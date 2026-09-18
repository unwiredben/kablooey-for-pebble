// SPDX-FileCopyrightText: 2026 Ben Combee
// SPDX-License-Identifier: MIT

#include "menu.h"
#include "settings.h"
#include "sound.h"

static Game *s_game;

static Window *s_menu_window;
static SimpleMenuLayer *s_menu_layer;
static Window *s_difficulty_window;
static SimpleMenuLayer *s_difficulty_layer;
static Window *s_pail_window;
static SimpleMenuLayer *s_pail_layer;
static Window *s_help_window;
static ScrollLayer *s_help_scroll;
static TextLayer *s_help_text;

// Subtitles are rendered from these, so they have to outlive each redraw.
static char s_difficulty_sub[24];
static char s_pail_sub[16];
static char s_high_score_sub[24];
static char s_sound_sub[20];
static char s_vibration_sub[8];

static const char s_help_body[] =
  "SCARRY KABLAM IS AT IT AGAIN!\n"
  "\n"
  "You and your bucket brigade are the only thing standing between his "
  "bombs and your city's destruction. Are you ready for the challenge?\n"
  "\n"
  "CONTROLS\n"
  "\n"
  "Slide a finger anywhere on the lower half of the screen to move the buckets. "
  "They go where your finger is.\n"
  "\n"
  "Tap to start a wave, and to carry on after one ends.\n"
  "\n"
  "UP toggles sound. SELECT opens this menu.\n"
  "\n"
  "Sound and vibration can both be switched off in this menu. If the watch "
  "itself is muted, sound stays off until you unmute it.\n"
  "\n"
  "SCORING\n"
  "\n"
  "Every bomb you douse scores the number of the wave you are on: 1 point each "
  "on wave 1, 7 each on wave 7. Clearing a wave moves you up.\n"
  "\n"
  "LOSING\n"
  "\n"
  "A bomb that reaches the ground blows up a bucket and sets off every other "
  "bomb in the air. It also knocks you back a wave, so you replay the one "
  "before it. Lose all three buckets and the game is over.\n"
  "\n"
  "DIFFICULTY\n"
  "\n"
  "Easy, Normal and Hard start you at wave 1, 3 and 6. Later waves throw more "
  "bombs, faster, and pay more per catch. Your starting wave is also the "
  "lowest a miss can knock you back to.\n"
  "\n"
  "Bucket width is a separate choice. Narrow buckets catch a third less of the "
  "screen and can be played from any starting wave. Changing either one "
  "starts a fresh game.\n"
  " \n";

static void close_menus(void) {
  if (s_difficulty_window && window_stack_contains_window(s_difficulty_window)) {
    window_stack_remove(s_difficulty_window, true);
  }
  if (s_pail_window && window_stack_contains_window(s_pail_window)) {
    window_stack_remove(s_pail_window, true);
  }
  if (s_menu_window && window_stack_contains_window(s_menu_window)) {
    window_stack_remove(s_menu_window, true);
  }
}

static void sync_subtitles(void) {
  snprintf(s_difficulty_sub, sizeof(s_difficulty_sub), "%s",
           difficulty_name(settings_difficulty()));
  snprintf(s_pail_sub, sizeof(s_pail_sub), "%s",
           pail_width_name(settings_pail_width()));
  snprintf(s_high_score_sub, sizeof(s_high_score_sub), "Best %d",
           settings_high_score());
  // A watch-level mute is not ours to override, so say so instead of offering
  // a switch that would do nothing.
  snprintf(s_sound_sub, sizeof(s_sound_sub), "%s",
           sound_blocked_by_watch() ? "Muted by watch"
                                    : (sound_is_on() ? "On" : "Off"));
  snprintf(s_vibration_sub, sizeof(s_vibration_sub), "%s",
           settings_vibration() ? "On" : "Off");
}

// --- Item callbacks --------------------------------------------------------

static void on_difficulty(int index, void *context);
static void on_sound(int index, void *context);
static void on_vibration(int index, void *context);
static void on_reset_high_score(int index, void *context);
static void on_help(int index, void *context);
static void on_pick_difficulty(int index, void *context);
static void on_pail_width(int index, void *context);
static void on_pick_pail_width(int index, void *context);

static SimpleMenuItem s_main_items[] = {
  { .title = "Difficulty", .subtitle = s_difficulty_sub, .callback = on_difficulty },
  { .title = "Bucket width", .subtitle = s_pail_sub, .callback = on_pail_width },
  { .title = "Sound", .subtitle = s_sound_sub, .callback = on_sound },
  { .title = "Vibration", .subtitle = s_vibration_sub, .callback = on_vibration },
  { .title = "How to play", .callback = on_help },
  { .title = "Reset high score", .subtitle = s_high_score_sub,
    .callback = on_reset_high_score },
};

static SimpleMenuSection s_main_section[] = {
  { .title = "Kablooey!", .num_items = ARRAY_LENGTH(s_main_items),
    .items = s_main_items },
};

static SimpleMenuItem s_difficulty_items[] = {
  { .title = "Easy", .subtitle = "Start at wave 1", .callback = on_pick_difficulty },
  { .title = "Normal", .subtitle = "Start at wave 3", .callback = on_pick_difficulty },
  { .title = "Hard", .subtitle = "Start at wave 6", .callback = on_pick_difficulty },
};

static SimpleMenuSection s_difficulty_section[] = {
  { .title = "Difficulty", .num_items = ARRAY_LENGTH(s_difficulty_items),
    .items = s_difficulty_items },
};

// Order matches the PailWidth enum, as the difficulty items match Difficulty.
static SimpleMenuItem s_pail_items[] = {
  { .title = "Wide", .subtitle = "The standard buckets",
    .callback = on_pick_pail_width },
  { .title = "Narrow", .subtitle = "Harder to catch with",
    .callback = on_pick_pail_width },
};

static SimpleMenuSection s_pail_section[] = {
  { .title = "Bucket width", .num_items = ARRAY_LENGTH(s_pail_items),
    .items = s_pail_items },
};

static void redraw_main_menu(void) {
  sync_subtitles();
  if (s_menu_layer) {
    layer_mark_dirty(simple_menu_layer_get_layer(s_menu_layer));
  }
}

static void on_sound(int index, void *context) {
  if (sound_blocked_by_watch()) return;  // nothing to toggle
  sound_toggle();
  redraw_main_menu();
}

static void on_vibration(int index, void *context) {
  settings_set_vibration(!settings_vibration());
  redraw_main_menu();
}

static void on_reset_high_score(int index, void *context) {
  settings_reset_high_score();
  if (s_game) {
    s_game->high_score = 0;
    s_game->new_best = false;
  }
  redraw_main_menu();
}

static void on_pick_difficulty(int index, void *context) {
  if (index < 0 || index >= (int)ARRAY_LENGTH(s_difficulty_items)) return;

  settings_set_difficulty((Difficulty)index);
  // The starting wave is part of the run, so switching starts a fresh one.
  if (s_game) game_init(s_game);
  redraw_main_menu();
  close_menus();
}

static void on_pick_pail_width(int index, void *context) {
  if (index < 0 || index >= (int)ARRAY_LENGTH(s_pail_items)) return;

  settings_set_pail_width((PailWidth)index);
  // The catch box is part of the run, so switching starts a fresh one.
  if (s_game) game_init(s_game);
  redraw_main_menu();
  close_menus();
}

// --- Windows ---------------------------------------------------------------

static void difficulty_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_difficulty_layer = simple_menu_layer_create(
      layer_get_bounds(root), window, s_difficulty_section,
      ARRAY_LENGTH(s_difficulty_section), NULL);
  menu_layer_set_selected_index(
      simple_menu_layer_get_menu_layer(s_difficulty_layer),
      (MenuIndex) { .section = 0, .row = (uint16_t)settings_difficulty() },
      MenuRowAlignCenter, false);
  layer_add_child(root, simple_menu_layer_get_layer(s_difficulty_layer));
}

static void difficulty_window_unload(Window *window) {
  simple_menu_layer_destroy(s_difficulty_layer);
  s_difficulty_layer = NULL;
}

static void on_difficulty(int index, void *context) {
  if (!s_difficulty_window) {
    s_difficulty_window = window_create();
    window_set_window_handlers(s_difficulty_window, (WindowHandlers) {
      .load = difficulty_window_load,
      .unload = difficulty_window_unload,
    });
  }
  window_stack_push(s_difficulty_window, true);
}

static void pail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_pail_layer = simple_menu_layer_create(
      layer_get_bounds(root), window, s_pail_section,
      ARRAY_LENGTH(s_pail_section), NULL);
  menu_layer_set_selected_index(
      simple_menu_layer_get_menu_layer(s_pail_layer),
      (MenuIndex) { .section = 0, .row = (uint16_t)settings_pail_width() },
      MenuRowAlignCenter, false);
  layer_add_child(root, simple_menu_layer_get_layer(s_pail_layer));
}

static void pail_window_unload(Window *window) {
  simple_menu_layer_destroy(s_pail_layer);
  s_pail_layer = NULL;
}

static void on_pail_width(int index, void *context) {
  if (!s_pail_window) {
    s_pail_window = window_create();
    window_set_window_handlers(s_pail_window, (WindowHandlers) {
      .load = pail_window_load,
      .unload = pail_window_unload,
    });
  }
  window_stack_push(s_pail_window, true);
}

static void help_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);
  const GRect text_bounds = GRect(5, 4, bounds.size.w - 10, 6000);

  s_help_scroll = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_help_scroll, window);

  s_help_text = text_layer_create(text_bounds);
  text_layer_set_background_color(s_help_text, GColorClear);
  text_layer_set_text_color(s_help_text, GColorBlack);
  text_layer_set_font(s_help_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_help_text, GTextOverflowModeWordWrap);
  text_layer_set_text(s_help_text, s_help_body);

  const GSize size = text_layer_get_content_size(s_help_text);
  text_layer_set_size(s_help_text, GSize(text_bounds.size.w, size.h));
  scroll_layer_add_child(s_help_scroll, text_layer_get_layer(s_help_text));
  scroll_layer_set_content_size(s_help_scroll, GSize(bounds.size.w, size.h + 8));
  layer_add_child(root, scroll_layer_get_layer(s_help_scroll));
}

static void help_window_unload(Window *window) {
  text_layer_destroy(s_help_text);
  s_help_text = NULL;
  scroll_layer_destroy(s_help_scroll);
  s_help_scroll = NULL;
}

static void on_help(int index, void *context) {
  if (!s_help_window) {
    s_help_window = window_create();
    window_set_window_handlers(s_help_window, (WindowHandlers) {
      .load = help_window_load,
      .unload = help_window_unload,
    });
  }
  window_stack_push(s_help_window, true);
}

static void menu_window_load(Window *window) {
  sync_subtitles();
  Layer *root = window_get_root_layer(window);
  s_menu_layer = simple_menu_layer_create(layer_get_bounds(root), window,
                                          s_main_section,
                                          ARRAY_LENGTH(s_main_section), NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_menu_layer));
}

static void menu_window_unload(Window *window) {
  simple_menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

// --- API -------------------------------------------------------------------

void menu_init(Game *game) {
  s_game = game;
  sync_subtitles();
}

void menu_deinit(void) {
  if (s_help_window) window_destroy(s_help_window);
  if (s_pail_window) window_destroy(s_pail_window);
  if (s_difficulty_window) window_destroy(s_difficulty_window);
  if (s_menu_window) window_destroy(s_menu_window);
  s_help_window = NULL;
  s_pail_window = NULL;
  s_difficulty_window = NULL;
  s_menu_window = NULL;
}

void menu_show(void) {
  sync_subtitles();
  if (!s_menu_window) {
    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) {
      .load = menu_window_load,
      .unload = menu_window_unload,
    });
  }
  window_stack_push(s_menu_window, true);
}

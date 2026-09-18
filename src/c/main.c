// SPDX-FileCopyrightText: 2026 Ben Combee
// SPDX-License-Identifier: MIT

#include <pebble.h>
#include "game.h"
#include "menu.h"
#include "settings.h"
#include "sound.h"

static Window *s_window;
static Layer *s_game_layer;
static AppTimer *s_timer;
static Game s_game;

// Touch tracking. A drag steers the pails whenever it is in the bottom touch
// zone (see TOUCH_ZONE_TOP). Between waves the finger landing is what starts
// the next one -- acting on liftoff instead made a wave begin when the player
// let go of a drag they were already holding.
// Flip to 1 to log every touch event (noisy: ~30 lines a second while dragging).
#define TOUCH_DEBUG_LOG 0

static bool s_dragging;

static void game_layer_update(Layer *layer, GContext *ctx) {
  game_render(&s_game, layer, ctx);
}

static void frame_timer(void *data) {
  sound_pump();
  game_update(&s_game);
  layer_mark_dirty(s_game_layer);
  s_timer = app_timer_register(FRAME_MS, frame_timer, NULL);
}

static void touch_handler(const TouchEvent *event, void *context) {
#if TOUCH_DEBUG_LOG
  APP_LOG(APP_LOG_LEVEL_DEBUG, "touch %d at %d,%d nav=%d", (int)event->type,
          (int)event->x, (int)event->y, (int)event->non_navigational);
#endif
  const bool in_zone = (event->y >= TOUCH_ZONE_TOP);

  switch (event->type) {
    case TouchEvent_Touchdown:
      s_dragging = in_zone;
      if (s_dragging) {
        // Position the pails before the wave starts, so a finger already in
        // place is where the stack appears.
        game_set_bucket_x(&s_game, event->x);
      }
      game_tap(&s_game);  // ignored while a wave is running
      break;

    case TouchEvent_PositionUpdate:
      // A finger that wanders into the zone mid-gesture picks up the pails too,
      // so a missed touchdown never leaves the player without controls.
      if (s_dragging || in_zone) {
        s_dragging = true;
        game_set_bucket_x(&s_game, event->x);
      }
      break;

    case TouchEvent_Liftoff:
      s_dragging = false;
      break;
  }
  layer_mark_dirty(s_game_layer);
}

// Steering is touch-only. The top button mutes and unmutes, select opens the
// options menu; down is deliberately unassigned.
static void up_click(ClickRecognizerRef recognizer, void *context) {
  sound_toggle();
  layer_mark_dirty(s_game_layer);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  menu_show();
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_game_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_game_layer, game_layer_update);
  layer_add_child(root, s_game_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_game_layer);
}

static void window_appear(Window *window) {
#if DEMO_MODE
  // Nothing touches the screen in a demo build, so the backlight would fade
  // and every captured frame would come out dimmed.
  light_enable(true);
#endif
  s_game.touch_ok = touch_service_is_enabled();
  sound_resume();
  s_timer = app_timer_register(FRAME_MS, frame_timer, NULL);
}

static void window_disappear(Window *window) {
  sound_suspend();
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
}

static void init(void) {
  srand(time(NULL));  // the bomber's turns should differ run to run
  settings_load();
  sound_init();
  game_init(&s_game);
  menu_init(&s_game);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
    .appear = window_appear,
    .disappear = window_disappear,
  });

  // Take over the touchscreen: no system swipe-to-navigate while playing.
  window_set_touch_bridge_disabled(s_window, true);
  touch_service_subscribe(touch_handler, NULL);

  window_stack_push(s_window, true);
}

static void deinit(void) {
  menu_deinit();
  sound_deinit();
  touch_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

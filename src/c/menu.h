#pragma once
#include <pebble.h>
#include "game.h"

// The options menu, opened with the select button from the game window.
// Changing difficulty restarts the run, so the menu needs to reach the game.
void menu_init(Game *game);
void menu_deinit(void);
void menu_show(void);

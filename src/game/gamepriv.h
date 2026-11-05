// Calls shared by the game rules files (game.c, tank.c, shells.c). Not for
// the Palm layer.
//
// Ported from WinBolo 2 (src/bolo/), Copyright (c) 1998-2026 John Morrison,
// GPLv3. This version Copyright (c) 2026 the PalmBolo authors, GPLv3.

#ifndef GAMEPRIV_H
#define GAMEPRIV_H

#include "game.h"

// game.c
uint8_t g_is_mine(uint8_t x, uint8_t y);
int8_t g_base_at(const GameState *s, uint8_t x, uint8_t y);
// A pill on the map at the square (alive or dead, not carried), or -1.
int8_t g_pill_at(const GameState *s, uint8_t x, uint8_t y);
// The same, alive only.
int8_t g_live_pill_at(const GameState *s, uint8_t x, uint8_t y);
// A pill was put down at x, y during the tick.
void g_pill_moved(uint8_t x, uint8_t y);
void g_explosion(GameState *s, uint16_t wx, uint16_t wy);
uint8_t g_damage(GameState *s, uint8_t kind, uint8_t x, uint8_t y);
void g_damage_remove(GameState *s, uint8_t kind, uint8_t x, uint8_t y);
void g_flood(GameState *s, uint8_t x, uint8_t y);
void g_mine_exp(GameState *s, uint8_t x, uint8_t y);
// minesExistPos: a mine the rules know of (all mines unless hidden mines
// are on, then those seen by a tank or laid by a builder).
uint8_t g_mine_known(const GameState *s, uint8_t x, uint8_t y);
// tankNearMines: tank t at mx, my facing dir (0-15) sees the mines on its
// square and the three ahead.
void g_mines_near(GameState *s, uint8_t t, uint8_t mx, uint8_t my, uint8_t dir);
// A mine laid by player at x, y: shown to the viewer if allied, and known
// to the rules if known is set.
void g_mine_laid(GameState *s, uint8_t x, uint8_t y, uint8_t player, uint8_t known);
// Notes a sound for game_take_sounds. player is GAME_NO_PLAYER unless the
// kind says otherwise.
void g_sound(uint8_t kind, uint8_t x, uint8_t y, uint8_t player);
void g_tank_explosion(GameState *s, uint16_t wx, uint16_t wy, uint16_t angle,
	uint8_t length, uint8_t big, uint8_t creator);

// tank.c
void SEC_TANK tank_create(GameState *s, uint8_t t, uint8_t player);
void SEC_TANK tank_update(GameState *s, uint8_t t, const GameInput *in);
void SEC_TANK tank_mine_damage(GameState *s, uint8_t t, uint8_t mx, uint8_t my);
// tankIsTankHit for a shell of owner (a player, or GAME_NO_PLAYER for a
// pill) at wx, wy: 1 if it hit the tank.
uint8_t SEC_TANK tank_shell_hit(GameState *s, uint8_t t, uint16_t wx, uint16_t wy, uint16_t angle,
	uint8_t owner);
// mapGetSpeed: the terrain's top speed in world units a tick for a tank of
// player (on a boat or not); 0 where it can't drive.
uint8_t SEC_TANK g_speed_at(const GameState *s, uint8_t x, uint8_t y, uint8_t onBoat, uint8_t player);
// mapIsLand: river and deep sea without a base are not land.
uint8_t g_is_land(const GameState *s, uint8_t x, uint8_t y);

// shells.c
void SEC_SHELLS shells_add(GameState *s, uint16_t x, uint16_t y, uint16_t angle,
	uint8_t life, uint8_t owner, uint8_t onBoat);
void SEC_SHELLS shells_update(GameState *s);

// pills.c: pillboxes and bases
void SEC_PILLS pills_update(GameState *s);
void SEC_PILLS pills_damage(GameState *s, uint8_t p, uint8_t amount);
void SEC_PILLS pills_base_hit(GameState *s, uint8_t b);
void SEC_PILLS pills_pickup(GameState *s, uint8_t t);
void SEC_PILLS pills_drop(GameState *s, uint8_t t);
// pillsDropSpiral: puts pill down dead and owned by owner on the nearest
// free square to cx, cy. Returns 0 if there is none.
uint8_t SEC_PILLS pills_drop_one(GameState *s, uint8_t pill, uint8_t cx, uint8_t cy, uint8_t owner);
void SEC_PILLS bases_update(GameState *s);
uint8_t SEC_PILLS bases_cant_drive(const GameState *s, uint8_t b, uint8_t player);
uint8_t SEC_PILLS bases_can_hit(const GameState *s, uint8_t b, uint8_t hitBy);
void SEC_PILLS bases_damage(GameState *s, uint8_t b);
void SEC_PILLS bases_capture(GameState *s, uint8_t b, uint8_t player);

// lgm.c: the builder
void SEC_BUILDER lgm_create(GameState *s, uint8_t t);
// lgmAddRequest: action is LGM_TREE to LGM_BOAT.
void SEC_BUILDER lgm_request(GameState *s, uint8_t t, uint8_t x, uint8_t y, uint8_t action);
void SEC_BUILDER lgm_update(GameState *s, uint8_t t);
// lgmDeathCheck for every builder against an explosion at wx, wy.
void SEC_BUILDER lgm_blast(GameState *s, uint16_t wx, uint16_t wy);
// lgmIsGoingToBuildAt for any builder.
uint8_t SEC_BUILDER lgm_building_at(const GameState *s, uint8_t x, uint8_t y);

// bot.c: in has the players' keys for every tank, out gets them with the
// bots' tanks filled in.
void SEC_BOTS bots_tick(GameState *s, const GameInput *in, GameInput *out);
// Starts tank t's bot afresh at level (BOT_OFF to stop it).
void SEC_BOTS bot_reset(GameState *s, uint8_t t, uint8_t level);

// treegrow.c
void SEC_BUILDER tree_init(GameState *s);
void SEC_BUILDER tree_update(GameState *s);

#endif

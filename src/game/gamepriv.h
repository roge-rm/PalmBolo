// Calls shared by the game rules files (game.c, tank.c, shells.c). Not for
// the Palm layer.
//
// Ported from WinBolo 2 (src/bolo/), Copyright (c) 1998-2026 John Morrison,
// GPLv3. This version Copyright (c) 2026 the PalmBolo authors, GPLv3.

#ifndef GAMEPRIV_H
#define GAMEPRIV_H

#include "game.h"
#include "mapstore.h"

// The small lookups below are inline: on the 68k a call costs more than
// the lookup, more so across code segments.

// game_terrain inline.
static __inline__ uint8_t g_terrain(uint8_t x, uint8_t y)
{
	if (x > MAP_EDGE_LOW && x < MAP_EDGE_HIGH && y > MAP_EDGE_LOW && y < MAP_EDGE_HIGH)
		return mapstore_get(x, y);
	return TERRAIN_DEEP_SEA;
}
#define game_terrain(x, y) g_terrain(x, y)

// game_allied inline.
static __inline__ uint8_t g_allied(const GameState *s, uint8_t a, uint8_t b)
{
	if (a == b)
		return 1;
	if (a >= GAME_MAX_PLAYERS || b >= GAME_MAX_PLAYERS || !s->players[a].inUse)
		return 0;
	return (uint8_t)((s->players[a].allies >> b) & 1);
}
#define game_allied(s, a, b) g_allied(s, a, b)

// game.c
// Bit i of g_row_pills[y] and of g_col_pills[x] is set if pill i may be on
// row y and column x, the same for bases, so the pills that may be on a
// square are the bits in both. Built at the start of each tick, with pills
// put down during it added, and used only inside game_tick (g_occ_on);
// outside it the lookups search every pill and base. Not part of the state.
extern uint16_t g_row_pills[256], g_col_pills[256], g_row_bases[256], g_col_bases[256];
extern uint8_t g_occ_on;
int8_t g_base_find(const GameState *s, uint8_t x, uint8_t y);
int8_t g_pill_find(const GameState *s, uint8_t x, uint8_t y);

uint8_t g_is_mine(uint8_t x, uint8_t y);
// The lookups are macros that test the bits inline and call only when a
// pill or base may be there. The inline parts must not call anything: an
// inline function's calls are made as if from code segment 1. Arguments
// are read twice, so pass nothing with side effects.
static __inline__ uint8_t g_base_maybe(uint8_t x, uint8_t y)
{
	return !g_occ_on || (g_row_bases[y] & g_col_bases[x]);
}
static __inline__ uint8_t g_pill_maybe(uint8_t x, uint8_t y)
{
	return !g_occ_on || (g_row_pills[y] & g_col_pills[x]);
}
int8_t g_live_pill_find(const GameState *s, uint8_t x, uint8_t y);
// An active base at the square, or -1.
#define g_base_at(s, x, y) (g_base_maybe(x, y) ? g_base_find(s, x, y) : -1)
// A pill on the map at the square (alive or dead, not carried), or -1.
#define g_pill_at(s, x, y) (g_pill_maybe(x, y) ? g_pill_find(s, x, y) : -1)
// The same, alive only.
#define g_live_pill_at(s, x, y) (g_pill_maybe(x, y) ? g_live_pill_find(s, x, y) : -1)
// Pill was put down during the tick.
void g_pill_moved(const GameState *s, uint8_t pill);
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

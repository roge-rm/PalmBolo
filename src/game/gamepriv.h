// Calls shared by the game rules files (game.c, tank.c, shells.c). Not for
// the Palm layer.
//
// Ported from WinBolo 2 (src/bolo/), Copyright (c) 1998-2026 John Morrison,
// GPLv3. This version Copyright (c) 2026 the PalmBolo authors, GPLv3.

#ifndef GAMEPRIV_H
#define GAMEPRIV_H

#include "game.h"

// pills.c is in its own code segment on the Palm, as the first is full.
// Callers in other segments must see this on its declarations.
#ifdef __palmos__
#define SEC_RULES __attribute__((section("rules")))
#else
#define SEC_RULES
#endif

// game.c
uint8_t g_is_mine(uint8_t x, uint8_t y);
int8_t g_base_at(const GameState *s, uint8_t x, uint8_t y);
// A pill on the map at the square (alive or dead, not carried), or -1.
int8_t g_pill_at(const GameState *s, uint8_t x, uint8_t y);
// The same, alive only.
int8_t g_live_pill_at(const GameState *s, uint8_t x, uint8_t y);
void g_explosion(GameState *s, uint16_t wx, uint16_t wy);
uint8_t g_damage(GameState *s, uint8_t kind, uint8_t x, uint8_t y);
void g_damage_remove(GameState *s, uint8_t kind, uint8_t x, uint8_t y);
void g_flood(GameState *s, uint8_t x, uint8_t y);
void g_mine_exp(GameState *s, uint8_t x, uint8_t y);
void g_tank_explosion(GameState *s, uint16_t wx, uint16_t wy, uint16_t angle,
	uint8_t length, uint8_t big, uint8_t creator);

// tank.c
void tank_create(GameState *s, uint8_t t, uint8_t player);
void tank_update(GameState *s, uint8_t t, const GameInput *in);
void tank_mine_damage(GameState *s, uint8_t t, uint8_t mx, uint8_t my);
// tankIsTankHit for a shell at wx, wy: 1 if it hit the tank.
uint8_t tank_shell_hit(GameState *s, uint8_t t, uint16_t wx, uint16_t wy, uint16_t angle);
// mapGetSpeed: the terrain's top speed in world units a tick for a tank of
// player (on a boat or not); 0 where it can't drive.
uint8_t g_speed_at(const GameState *s, uint8_t x, uint8_t y, uint8_t onBoat, uint8_t player);
// mapIsLand: river and deep sea without a base are not land.
uint8_t g_is_land(const GameState *s, uint8_t x, uint8_t y);

// shells.c
void shells_add(GameState *s, uint16_t x, uint16_t y, uint16_t angle,
	uint8_t life, uint8_t owner, uint8_t onBoat);
void shells_update(GameState *s);

// pills.c: pillboxes and bases
void SEC_RULES pills_update(GameState *s);
void SEC_RULES pills_damage(GameState *s, uint8_t p, uint8_t amount);
void SEC_RULES pills_base_hit(GameState *s, uint8_t b);
void SEC_RULES pills_pickup(GameState *s, uint8_t t);
void SEC_RULES pills_drop(GameState *s, uint8_t t);
void SEC_RULES bases_update(GameState *s);
uint8_t SEC_RULES bases_cant_drive(const GameState *s, uint8_t b, uint8_t player);
uint8_t SEC_RULES bases_can_hit(const GameState *s, uint8_t b, uint8_t hitBy);
void SEC_RULES bases_damage(GameState *s, uint8_t b);
void SEC_RULES bases_capture(GameState *s, uint8_t b, uint8_t player);

#endif

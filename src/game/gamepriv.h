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
uint8_t g_is_land(const GameState *s, uint8_t x, uint8_t y);
int8_t g_base_at(const GameState *s, uint8_t x, uint8_t y);
int8_t g_pill_at(const GameState *s, uint8_t x, uint8_t y);
void g_explosion(GameState *s, uint16_t wx, uint16_t wy);
uint8_t g_damage(GameState *s, uint8_t kind, uint8_t x, uint8_t y);
void g_damage_remove(GameState *s, uint8_t kind, uint8_t x, uint8_t y);
void g_flood(GameState *s, uint8_t x, uint8_t y);
void g_mine_exp(GameState *s, uint8_t x, uint8_t y);
void g_tank_explosion(GameState *s, uint16_t wx, uint16_t wy, uint16_t angle,
	uint8_t length, uint8_t big, uint8_t creator);

// tank.c
void tank_new(GameState *s, uint8_t t, uint8_t player);
void tank_update(GameState *s, uint8_t t, const GameInput *in);
void tank_mine_damage(GameState *s, uint8_t t, uint8_t mx, uint8_t my);
// mapGetSpeed: the terrain's top speed in world units a tick for a tank of
// player (on a boat or not); 0 where it can't drive.
uint8_t g_speed_at(const GameState *s, uint8_t x, uint8_t y, uint8_t onBoat);

// shells.c
void shells_add(GameState *s, uint16_t x, uint16_t y, uint16_t angle,
	uint8_t sightLen, uint8_t owner, uint8_t onBoat);
void shells_update(GameState *s);

#endif

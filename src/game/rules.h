// Rule numbers, fixed at WinBolo 2's classic defaults (simRulesClassic in
// src/bolo/sim_rules.c and the constants it reads), Copyright (c) 1998-2026
// John Morrison, GPLv3. This file Copyright (c) 2026 the PalmBolo authors,
// GPLv3.
//
// Units: world units are 1/256 of a map square (16 to a pixel at 16 pixels
// a square). Ticks are game ticks, 50 a second. Speeds are in quarter world
// units a tick (WinBolo keeps them as floats in steps of 0.25). Angles are
// in 1/32 bradians, 256 bradians to a turn (WinBolo's float angles only
// ever move in steps of 1/32).

#ifndef RULES_H
#define RULES_H

// Tank
#define R_TANK_RELOAD_TICKS     13
#define R_TANK_FULL_SHELLS      40
#define R_TANK_FULL_MINES       40
#define R_TANK_FULL_TREES       40
#define R_TANK_FULL_ARMOUR      40
#define R_TANK_DEATH_TICKS      255     // wait before a respawn
#define R_TANK_WATER_TICKS      15      // wading ticks per loss of stock
#define R_WATER_LOSS_SHELLS     1
#define R_WATER_LOSS_MINES      1
#define R_JUST_FIRED_TICKS      101
#define R_GUNSIGHT_MIN          2       // half map squares
#define R_GUNSIGHT_MAX          14
#define R_TANK_ACCEL            1       // 0.25 in quarters
#define R_TANK_DECEL            1       // drag above the terrain's cap
#define R_TANK_BRAKE            1       // the slower key
#define R_TANK_AUTOSLOW         1
#define R_TANK_MIN_MOVE         6       // residual speed needed to move
#define R_TANK_HIT_RADIUS       112     // world units
#define R_TANK_NUDGE_PASSES     5
#define R_TANK_BUMP_DECAY_SHIFT 2
#define R_TANK_BOAT_EXIT_INSET  64
#define R_TANK_SLIDE_STEP       32
#define R_BIG_EXPLOSION         20      // shells + mines over this blow up big
#define R_TANK_TURN_RAMP        6       // ticks of 1/8 turn rate after a press

// Damage
#define R_SHELL_DAMAGE          5
#define R_MINE_DAMAGE           15
#define R_MINE_DAMAGE_RANGE     256     // world units on each axis
#define R_MINE_FATAL_DIVISOR    3

// Terrain caps in whole world units a tick, and turn rates in 1/32
// bradians a tick (1, 0.5 and 0.25 bradians).
#define R_SPEED_ROAD            16
#define R_SPEED_GRASS           12
#define R_SPEED_FOREST          6
#define R_SPEED_RIVER           3
#define R_SPEED_SWAMP           3
#define R_SPEED_CRATER          3
#define R_SPEED_RUBBLE          3
#define R_SPEED_BOAT            16
#define R_SPEED_DEEP_SEA        3
#define R_SPEED_BASE            16
#define R_SPEED_SOLID           0       // building, half building, live pill
#define R_TURN_ROAD             32
#define R_TURN_GRASS            32
#define R_TURN_FOREST           16
#define R_TURN_RIVER            8
#define R_TURN_SWAMP            8
#define R_TURN_CRATER           8
#define R_TURN_RUBBLE           8
#define R_TURN_BOAT             32
#define R_TURN_DEEP_SEA         16
#define R_TURN_BASE             32
#define R_TURN_SOLID            0

// Shells
#define R_SHELL_LIFE            8       // ticks per gunsight unit
#define R_SHELL_SPEED           32      // world units a tick
#define R_SHELL_START_ADD       5       // steps ahead of the tank it starts

// Terrain damage: hits a square takes before it changes
#define R_BUILDING_LIFE         4
#define R_RUBBLE_LIFE           4
#define R_GRASS_LIFE            4
#define R_SWAMP_LIFE            3

// Mines, flooding, explosions
#define R_MINE_FUSE_TICKS       10
#define R_FLOOD_FILL_TICKS      16
#define R_EXPLOSION_START       8       // frames of a small explosion
#define R_EXPLOSION_TICKS       3       // ticks per explosion frame
#define R_TK_EXPLOSION_MOVE     48      // world units a dying tank's wreck moves a step
#define R_TK_EXPLOSION_TICKS    2       // ticks between those steps
#define R_TK_EXPLOSION_WIDTH    24
#define R_TK_EXPLOSION_HEIGHT   32

// Bases (the tank side of refuelling)
#define R_BASE_ARMOUR_GIVE      5
#define R_BASE_SHELLS_GIVE      1
#define R_BASE_MINES_GIVE       1
#define R_BASE_MIN_ARMOUR       10
#define R_BASE_MIN_SHELLS       0
#define R_BASE_MIN_MINES        0
#define R_BASE_REFUEL_ARMOUR    46      // ticks after giving armour
#define R_BASE_REFUEL_HALF      7       // shells and mines: 7.5 ticks, given as 7 then 8
#define R_BASE_CAPTURE_ARMOUR   9

// Starts
#define R_START_PILL_RANGE      9
#define R_START_SCATTER_MAX     1000

// The mined border: squares at or outside these read as deep sea and mined.
#define MAP_EDGE_LOW            20
#define MAP_EDGE_HIGH           236

#endif

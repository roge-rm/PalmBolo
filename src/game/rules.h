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
#define R_TANK_BOAT_EXIT_SPEED  16      // whole world units a tick: a boat this fast runs aground
#define R_TANK_SUBTRACT         128     // look-ahead in the pill's aim, world units
#define R_TANK_SLIDE_STEP       32
#define R_BIG_EXPLOSION         20      // shells + mines over this blow up big
#define R_TANK_TURN_RAMP        6       // ticks of 1/8 turn rate after a press
#define R_TANK_PILL_PICKUP_INSET 16     // reach past the centre for picking up a pill
#define R_TREE_HIDE_DIST        768     // pills see a tank in forest nearer than this
#define R_TANK_EXPLOSION_LENGTH 40      // steps of the wreck of a tank shot dead
#define R_TANK_EXPLOSION_DAMAGE 5       // a big wreck's damage to a pill

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

// Pillboxes. A pill fires every speed ticks; it starts at the map's value
// (100 at most), being hit halves it (6 at least) and it eases back by one
// every cool down.
#define R_PILL_MAX_ARMOUR       15
#define R_PILL_ATTACK_NORMAL    100
#define R_PILL_ATTACK_MIN       6
#define R_PILL_COOLDOWN         32
#define R_PILL_ANGRY_DIVISOR    2
#define R_PILL_RANGE            2048    // world units, 8 squares
#define R_PILL_SHELL_DAMAGE     1
#define R_PILL_FIRE_LIFE        64      // 1 + shell_life * 8.5 - start_add ticks
#define R_PILL_BASE_HIT_RANGE   9       // squares: pills this near a hit base get angry
#define R_PILL_AIM_ITERATIONS   200

// Bases
#define R_BASE_FULL_ARMOUR      90
#define R_BASE_FULL_SHELLS      90
#define R_BASE_FULL_MINES       90
#define R_BASE_REGEN_TICKS      1000    // per player in the game: every base +1 of each
#define R_BASE_HIT_ARMOUR       4       // a shell only hits a base with more armour
#define R_BASE_ARMOUR_GIVE      5
#define R_BASE_SHELLS_GIVE      1
#define R_BASE_MINES_GIVE       1
#define R_BASE_MIN_ARMOUR       10
#define R_BASE_MIN_SHELLS       0
#define R_BASE_MIN_MINES        0
#define R_BASE_REFUEL_ARMOUR    46      // ticks after giving armour
#define R_BASE_REFUEL_HALF      7       // shells and mines: 7.5 ticks, given as 7 then 8
#define R_BASE_CAPTURE_ARMOUR   9

// The builder. Walking speeds are in whole world units a tick by the
// terrain he is on (mapGetManSpeed); 0 is a square he can't walk onto.
#define R_MAN_SPEED_ROAD        16
#define R_MAN_SPEED_GRASS       16
#define R_MAN_SPEED_FOREST      8
#define R_MAN_SPEED_RIVER       0
#define R_MAN_SPEED_SWAMP       4
#define R_MAN_SPEED_CRATER      4
#define R_MAN_SPEED_RUBBLE      4
#define R_MAN_SPEED_BOAT        16
#define R_MAN_SPEED_DEEP_SEA    0
#define R_MAN_SPEED_BASE        16      // a base he may drive on, or near the tank on a boat
#define R_MAN_SPEED_SOLID       0       // building, half building, live pill
#define R_MAN_SPEED_ON_TOP      6       // walking off a wall he landed on
#define R_LGM_BUILD_TICKS       20      // working at the square
#define R_LGM_COST_ROAD         2       // trees
#define R_LGM_COST_BUILDING     2
#define R_LGM_COST_REPAIR_BUILDING 1
#define R_LGM_COST_PILL_REPAIR  1       // per load of 4 armour
#define R_LGM_COST_BOAT         20
#define R_LGM_COST_PILL_NEW     4
#define R_LGM_COST_MINE         1       // mines
#define R_LGM_PILL_REPAIR_LOAD  4       // most loads taken to a repair
#define R_LGM_GATHER_TREES      4
#define R_LGM_HELICOPTER_SPEED  3       // world units a tick of the replacement man
#define R_LGM_ARRIVE            16      // this near his goal, each axis, is there
#define R_LGM_RETURN            128     // the same, back at the tank
#define R_LGM_BOAT_LEAVE        144     // near a tank on a boat he walks anywhere
#define R_LGM_BOAT_RETURN       160
#define R_LGM_START_OFFSET      16      // he steps out this far towards his goal
#define R_LGM_BLAST_RANGE       128     // an explosion this near kills him
#define R_PILL_REPAIR_AMOUNT    4       // armour a tree gives a pill

// Tree growth (treegrow.c): each tick one square is scored by its
// neighbours, the best so far grows a tree after R_TREE_GROW_TICKS unless a
// better one turns up.
#define R_TREE_GROW_TICKS       3000
#define R_TREE_GROW_INITIAL_TICKS 30000
#define R_TREE_GROW_INITIAL_SCORE (-10000)
#define R_TREE_FOREST           100
#define R_TREE_GRASS            25
#define R_TREE_RIVER            2
#define R_TREE_BOAT             1
#define R_TREE_DEEP_SEA         0
#define R_TREE_SWAMP            2
#define R_TREE_RUBBLE           (-2)
#define R_TREE_BUILDING         (-20)
#define R_TREE_HALF_BUILDING    (-15)
#define R_TREE_CRATER           (-2)
#define R_TREE_ROAD             (-100)  // also pills, bases and anything else
#define R_TREE_MINE             (-7)

// Starts
#define R_START_TANK_RANGE      1
#define R_START_PILL_RANGE      9
#define R_START_BASE_RANGE      9
#define R_START_SPAWN_SEPARATION 2
#define R_START_SCATTER_MAX     1000
#define R_START_NEUTRAL_PCT     20      // tournament starts favour neutral bases above this

// The mined border: squares at or outside these read as deep sea and mined.
#define MAP_EDGE_LOW            20
#define MAP_EDGE_HIGH           236

#endif

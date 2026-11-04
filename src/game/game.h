// PalmBolo game rules. Plain C, integers only, no Palm headers.
// The state has no pointers, and the checksum is computed field by field in
// a fixed byte order so the 68k and the host get the same value.

#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define GAME_TICKS_PER_SECOND 50

typedef struct GameState {
	uint32_t rng;       // random number generator state, never 0
	uint32_t tick;      // game ticks run so far
	uint32_t inputSum;  // running sum of all inputs, to prove they are used
	int16_t x, y;       // stub position moved by the inputs
} GameState;

// Sets up a new game. A seed of 0 is replaced so the generator never sticks.
void game_init(GameState *s, uint32_t seed);

// Advances one tick. inputs is a bit mask of the keys held this tick.
void game_tick(GameState *s, uint16_t inputs);

// Next random number from the state's generator.
uint32_t game_random(GameState *s);

// CRC-32 over the fields in big-endian order.
uint32_t game_checksum(const GameState *s);

// Runs the fixed test game (seed 12345, 1000 ticks, fixed inputs) and
// returns its checksum.
uint32_t game_selftest(void);

#endif

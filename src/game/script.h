// Scripted games on Everard Island, run by the host test and by the debug
// Palm build so their checksums can be compared.

#ifndef SCRIPT_H
#define SCRIPT_H

#include "game.h"

#define SCRIPT_COUNT 37

// Runs script n on a fresh game. The map must be freshly loaded into the
// store and s->map before each script, as scripts change the terrain. step, if not NULL, is called after each step with its
// number. Returns the CRC of the state and terrain checksums after every
// step.
uint32_t script_run(GameState *s, uint8_t n,
	void (*step)(const GameState *s, uint8_t n, uint8_t i, void *ctx), void *ctx);

const char *script_name(uint8_t n);

#endif

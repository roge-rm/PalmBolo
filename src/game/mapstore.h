// Map storage: the 256 x 256 terrain bytes. The game rules read and write
// terrain only through these calls. The host keeps a plain array. On the
// Palm the bytes live in a storage database, outside the dynamic heap.
//
// There is one map at a time. Row y holds x = 0..255.

#ifndef MAPSTORE_H
#define MAPSTORE_H

#include <stdint.h>

// Creates the storage with every square set to value. Returns 0, or non-zero
// if the storage couldn't be made.
int mapstore_open(uint8_t value);

// Releases the storage.
void mapstore_close(void);

// Sets every square to value.
void mapstore_fill(uint8_t value);

uint8_t mapstore_get(uint8_t x, uint8_t y);
void mapstore_set(uint8_t x, uint8_t y, uint8_t terrain);

// Writes n squares of row y starting at x. x + n must not pass 256.
void mapstore_set_bytes(uint8_t x, uint8_t y, uint16_t n, const uint8_t *src);

// The 256 bytes of row y, for fast reading. Valid until mapstore_close and
// must not be written through.
const uint8_t *mapstore_row(uint8_t y);

#endif

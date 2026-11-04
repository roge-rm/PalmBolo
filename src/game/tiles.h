// Placeholder tile designs, shared by the Palm drawing code and the host
// map renderer. Each tile is an 8 x 8 pattern with two colours. At 16 pixels
// every pattern pixel is drawn 2 x 2. On 1-bit screens a tile is drawn black
// or white by its background brightness with the pattern in the opposite.

#ifndef TILES_H
#define TILES_H

#include <stdint.h>

#define TILE_DEEP_SEA   16      // tile for terrain 255
#define TILE_PILL       17      // neutral; the other sides follow
#define TILE_BASE       18      // neutral
#define TILE_START      19
#define TILE_PILL_OWN   20      // own, allied, enemy, then dead
#define TILE_PILL_DEAD  23
#define TILE_BASE_OWN   24      // own, allied, enemy
#define TILE_COUNT      27
#define TILE_MINE_ADD   8       // a mine tile is its plain tile plus a mark

typedef struct TileDef {
	uint8_t bg[3], fg[3];
	uint8_t pat[8];             // bit 7 is the left pixel
} TileDef;

// The design of tile i.
const TileDef *tile_def(uint8_t i);

// Tile index for a terrain value (0-15 and 255 only).
uint8_t tile_for_terrain(uint8_t terrain);

// 0 white or 1 black for a colour on a 1-bit screen.
uint8_t tile_mono(const uint8_t rgb[3]);

// The mine mark on mine tiles, over pattern pixel (px, py): the middle 4 x 4
// block is 1 for the white ring and 2 for the dark centre, else 0.
uint8_t tile_mine_mark(uint8_t px, uint8_t py);

#endif

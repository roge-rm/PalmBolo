// Picks the art sheet cell a map square is drawn with. Roads, buildings,
// rivers, deep sea, forest and boats have a cell for each way they join
// their neighbours, chosen as WinBolo 2 does (src/bolo/screencalc.c).
// Drawing only: nothing here is part of the game rules.
//
// Plain C with no Palm headers, so the host map renderer uses it too. The
// cell numbers are the AT_ names in build/art/art.h.

#ifndef TILEPICK_H
#define TILEPICK_H

#include <stdint.h>

#ifdef __palmos__
#define SEC_TILEPICK __attribute__((section("ui")))
#else
#define SEC_TILEPICK
#endif

// Neighbour order, as WinBolo passes them.
enum { TP_AL, TP_A, TP_AR, TP_L, TP_R, TP_BL, TP_B, TP_BR };

// Fills n with the terrain of the eight squares round x on the middle of
// three rows (above, this, below), mines taken off. x wraps at the map edge.
void SEC_TILEPICK tile_neighbours(const uint8_t *above, const uint8_t *row,
	const uint8_t *below, uint8_t x, uint8_t n[8]);

// The cell for plain terrain t (mines taken off) with neighbours n. A base
// next to the square counts as road; the caller sets that in n.
uint8_t SEC_TILEPICK tile_pick(uint8_t t, const uint8_t n[8]);

// True if the cell for terrain t depends on its neighbours.
uint8_t SEC_TILEPICK tile_has_variants(uint8_t t);

// True if a base next to a square of terrain t changes its cell.
uint8_t SEC_TILEPICK tile_bases_matter(uint8_t t);

#endif

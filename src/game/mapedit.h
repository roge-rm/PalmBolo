// Map editor rules: the terrain the editor paints, where it may paint and
// the checks before a map is saved. Plain C, no Palm headers, so the host
// tests link it.

#ifndef MAPEDIT_H
#define MAPEDIT_H

#include "map.h"

// The editor's own code segment on the Palm.
#ifndef SEC_EDITOR
#ifdef __palmos__
#define SEC_EDITOR __attribute__((section("editor")))
#else
#define SEC_EDITOR
#endif
#endif

// The terrain picker's choices, in the order of its list (EditTerrainList
// in palmbolo.rcp): every value the .map format holds, deep sea included.
#define MAP_EDIT_TERRAINS 17
uint8_t SEC_EDITOR map_edit_terrain(uint8_t i);

// True for squares inside the mined border, the only ones the editor
// paints. The border reads as deep sea in a game and x 255 can't hold land.
uint8_t SEC_EDITOR map_playable(uint8_t x, uint8_t y);

// What map_check finds wrong, MAPCHK_OK if nothing.
enum {
	MAPCHK_OK = 0,
	MAPCHK_COUNTS,      // more than 16 pills, bases or starts
	MAPCHK_NO_START,    // no start
	MAPCHK_OUTSIDE,     // a pill, base or start outside the playable area
	MAPCHK_SEA,         // a pill or base on deep sea (starts are at sea)
	MAPCHK_EDGE,        // land at x 255
	MAPCHK_FAR,         // an item would leave the playable area when centred
	MAPCHK_WIDE         // the land would reach the border when centred
};

// Checks info and the map store before saving. Returns the first problem,
// with its square in x, y (255, 255 when it has none).
int SEC_EDITOR map_check(const MapInfo *info, uint8_t *x, uint8_t *y);
const char *SEC_EDITOR map_check_text(int err);

struct MapUndo;
// After map_check: as map_parse centres the land on load, centres the map
// along one axis when it would move, so it loads where it is. The move is
// in dx, dy (wrapping at 256) and drops the undo step. Returns MAPCHK_OK,
// or MAPCHK_FAR (with the item's square in x, y) or MAPCHK_WIDE when it
// can't be centred, the map left as it was.
int SEC_EDITOR map_settle(struct MapUndo *u, MapInfo *info, uint8_t *x, uint8_t *y,
	uint8_t *dx, uint8_t *dy);

// Pills, bases and starts. An item is its kind and its place in that
// kind's list; removing one moves the later ones down.
enum { ITEM_PILL, ITEM_BASE, ITEM_START, ITEM_KINDS };
#define ITEM_NONE 255

// The picker's choices after the terrains: the three kinds, then removing.
#define MAP_EDIT_ITEM0   MAP_EDIT_TERRAINS
#define MAP_EDIT_REMOVE  (MAP_EDIT_TERRAINS + ITEM_KINDS)
#define MAP_EDIT_CHOICES (MAP_EDIT_REMOVE + 1)

// Why an item can't go on a square, MAPITEM_OK if it can.
enum {
	MAPITEM_OK = 0,
	MAPITEM_TAKEN,      // another item is there
	MAPITEM_SEA,        // a pill or base on deep sea
	MAPITEM_OUTSIDE,    // outside the playable area
	MAPITEM_FULL        // 16 of that kind already
};

// The item on a square: its kind in *kind and place in *index. Returns 0
// when the square has none.
int SEC_EDITOR map_item_at(const MapInfo *info, uint8_t x, uint8_t y, uint8_t *kind,
	uint8_t *index);
// Whether an item of kind could stand on x, y, leaving out item kind
// skipKind, skipIndex (ITEM_NONE for none) when looking for others.
int SEC_EDITOR map_item_fits(const MapInfo *info, uint8_t kind, uint8_t x, uint8_t y,
	uint8_t skipKind, uint8_t skipIndex);
const char *SEC_EDITOR map_item_text(int err, uint8_t kind);

// One step of undo: the items as they were and the squares' old terrain.
// A step begins at the first change after map_undo_next, so a tap that
// changes nothing keeps the last step. A step longer than MAP_UNDO_SQUARES
// squares goes on in a whole-map copy (map_snap_*); without room for one
// it can't be undone.
#define MAP_UNDO_SQUARES 256
typedef struct MapUndo {
	uint8_t on;         // a step to undo
	uint8_t over;       // the step ran out of room
	uint8_t next;       // the next change begins a step
	uint8_t snap;       // the whole-map copy holds the step's terrain
	uint16_t n;
	uint8_t sq[MAP_UNDO_SQUARES][3];    // x, y, terrain before
	MapInfo info;
} MapUndo;

// The next change begins a new step, dropping the last.
void SEC_EDITOR map_undo_next(MapUndo *u);
// Notes that info is about to change, for changes made outside these
// functions (an item's settings).
void SEC_EDITOR map_undo_change(MapUndo *u, const MapInfo *info);
// Takes the step back: the squares and then the items. Returns 0 when
// there's nothing to undo. Either way it leaves nothing to undo.
int SEC_EDITOR map_undo(MapUndo *u, MapInfo *info);

// The terrain map_paint would set for t on x, y: a pill or base keeps its
// ground as map_parse leaves it (road under bases; under pills, road for
// water and buildings and no mines).
uint8_t SEC_EDITOR map_paint_value(const MapInfo *info, uint8_t x, uint8_t y, uint8_t t);
// Paints x, y with map_paint_value through game_set_terrain. Returns 1 if
// the square changed.
int SEC_EDITOR map_paint(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y, uint8_t t);
// Sets the ground under an item as map_paint does.
void SEC_EDITOR map_item_ground(MapUndo *u, const MapInfo *info, uint8_t kind, uint8_t index);

// Adds an item of kind at x, y, neutral and fully stocked, a start facing
// the middle of the map. The ground is left for map_item_ground on the
// drop. Returns MAPITEM_OK with its place in *index, or why not.
int SEC_EDITOR map_item_add(MapUndo *u, MapInfo *info, uint8_t kind, uint8_t x, uint8_t y,
	uint8_t *index);
// Moves an item to x, y without touching the ground (map_item_ground on
// the drop). Returns MAPITEM_OK or why not.
int SEC_EDITOR map_item_move(MapUndo *u, MapInfo *info, uint8_t kind, uint8_t index, uint8_t x,
	uint8_t y);
void SEC_EDITOR map_item_remove(MapUndo *u, MapInfo *info, uint8_t kind, uint8_t index);
// Where an item stands.
void SEC_EDITOR map_item_xy(const MapInfo *info, uint8_t kind, uint8_t index, uint8_t *x,
	uint8_t *y);

// The whole-map copy for big steps, made by the platform: a database on
// the Palm, memory on the host. map_snap_alloc makes room for it once and
// returns 0 when there is. map_snap_put stores row y's 256 squares.
int SEC_EDITOR map_snap_alloc(void);
void SEC_EDITOR map_snap_put(uint8_t y, const uint8_t *row);
void SEC_EDITOR map_snap_set(uint8_t x, uint8_t y, uint8_t t);
const uint8_t *SEC_EDITOR map_snap_row(uint8_t y);

// A new map: deep sea with a round grass island in the middle, of size
// MAP_BLANK_SMALL to LARGE, and one start on it. The island sits where
// map_parse leaves it, so a saved map loads in the same place.
enum { MAP_BLANK_SMALL, MAP_BLANK_MEDIUM, MAP_BLANK_LARGE, MAP_BLANK_SIZES };
void SEC_EDITOR map_blank(MapInfo *info, uint8_t size);

// Paints a size by size block (1, 3 or 5) centred on x, y with t, inside
// the playable area, as map_paint does. Returns the squares changed.
uint16_t SEC_EDITOR map_brush(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y,
	uint8_t size, uint8_t t);

// Paints the squares joined edge to edge to x, y with the same terrain, in
// the playable area, with t as map_paint does. Returns the squares filled.
// It takes no recursion and a small fixed stack.
uint16_t SEC_EDITOR map_fill(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y, uint8_t t);

// Paints the box with corners x0, y0 and x1, y1, in any order, cut to the
// playable area, as map_paint does. Returns the squares changed.
uint16_t SEC_EDITOR map_box(MapUndo *u, const MapInfo *info, uint8_t x0, uint8_t y0,
	uint8_t x1, uint8_t y1, uint8_t t);

// map_fill, map_box and an undo from the whole-map copy write whole rows
// without noting them for game_take_changes: draw everything after.

#endif

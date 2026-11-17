// Map editor rules (see mapedit.h).

#include "mapedit.h"
#include "mapstore.h"
#include "rules.h"
#include "game.h"

// File-static with an accessor, as const data any other way is read
// through A5 on the Palm.
static const uint8_t kTerrains[MAP_EDIT_TERRAINS] = {
	TERRAIN_GRASS, TERRAIN_FOREST, TERRAIN_ROAD, TERRAIN_BUILDING,
	TERRAIN_HALFBUILDING, TERRAIN_RUBBLE, TERRAIN_CRATER, TERRAIN_SWAMP,
	TERRAIN_RIVER, TERRAIN_BOAT, TERRAIN_DEEP_SEA,
	TERRAIN_MINE_GRASS, TERRAIN_MINE_FOREST, TERRAIN_MINE_ROAD,
	TERRAIN_MINE_RUBBLE, TERRAIN_MINE_CRATER, TERRAIN_MINE_SWAMP
};

uint8_t SEC_EDITOR map_edit_terrain(uint8_t i)
{
	return i < MAP_EDIT_TERRAINS ? kTerrains[i] : TERRAIN_GRASS;
}

uint8_t SEC_EDITOR map_playable(uint8_t x, uint8_t y)
{
	return x > MAP_EDGE_LOW && x < MAP_EDGE_HIGH && y > MAP_EDGE_LOW && y < MAP_EDGE_HIGH;
}

static int SEC_EDITOR found(int err, uint8_t px, uint8_t py, uint8_t *x, uint8_t *y)
{
	*x = px;
	*y = py;
	return err;
}

int SEC_EDITOR map_check(const MapInfo *info, uint8_t *x, uint8_t *y)
{
	uint16_t i;

	*x = *y = 255;
	if (info->numPills > MAP_MAX_PILLS || info->numBases > MAP_MAX_BASES ||
	    info->numStarts > MAP_MAX_STARTS)
		return MAPCHK_COUNTS;
	if (info->numStarts == 0)
		return MAPCHK_NO_START;
	for (i = 0; i < info->numPills; i++)
		if (!map_playable(info->pills[i].x, info->pills[i].y))
			return found(MAPCHK_OUTSIDE, info->pills[i].x, info->pills[i].y, x, y);
	for (i = 0; i < info->numBases; i++)
		if (!map_playable(info->bases[i].x, info->bases[i].y))
			return found(MAPCHK_OUTSIDE, info->bases[i].x, info->bases[i].y, x, y);
	for (i = 0; i < info->numStarts; i++)
		if (!map_playable(info->starts[i].x, info->starts[i].y))
			return found(MAPCHK_OUTSIDE, info->starts[i].x, info->starts[i].y, x, y);
	for (i = 0; i < info->numPills; i++)
		if (mapstore_get(info->pills[i].x, info->pills[i].y) == TERRAIN_DEEP_SEA)
			return found(MAPCHK_SEA, info->pills[i].x, info->pills[i].y, x, y);
	for (i = 0; i < info->numBases; i++)
		if (mapstore_get(info->bases[i].x, info->bases[i].y) == TERRAIN_DEEP_SEA)
			return found(MAPCHK_SEA, info->bases[i].x, info->bases[i].y, x, y);
	for (i = 0; i < 256; i++)
		if (mapstore_get(255, i) != TERRAIN_DEEP_SEA)
			return found(MAPCHK_EDGE, 255, (uint8_t)i, x, y);
	return MAPCHK_OK;
}

const char *SEC_EDITOR map_check_text(int err)
{
	switch (err) {
	case MAPCHK_OK: return "";
	case MAPCHK_COUNTS: return "Too many pills, bases or starts.";
	case MAPCHK_NO_START: return "The map needs a start.";
	case MAPCHK_OUTSIDE: return "A pill, base or start is outside the playable area.";
	case MAPCHK_SEA: return "A pill or base is on deep sea.";
	case MAPCHK_EDGE: return "Land on the map's right edge.";
	case MAPCHK_FAR: return "Too far from the land, which is centred when the map loads.";
	case MAPCHK_WIDE: return "The land is too near the edges to centre when the map loads.";
	}
	return "Can't save this map.";
}

// Whether every item and the land box, moved by d along y (vert) or x,
// stays in the playable area. On a bad item, its square in x, y.
static int SEC_EDITOR settle_fits(const MapInfo *info, uint8_t vert, uint8_t d,
	uint8_t lo, uint8_t hi, uint8_t *x, uint8_t *y)
{
	uint8_t kind, i, n, ix, iy, a, b;

	for (kind = 0; kind < ITEM_KINDS; kind++) {
		n = kind == ITEM_PILL ? info->numPills : kind == ITEM_BASE ? info->numBases :
			info->numStarts;
		for (i = 0; i < n; i++) {
			map_item_xy(info, kind, i, &ix, &iy);
			a = vert ? ix : (uint8_t)(ix + d);
			b = vert ? (uint8_t)(iy + d) : iy;
			if (!map_playable(a, b))
				return found(MAPCHK_FAR, ix, iy, x, y);
		}
	}
	a = (uint8_t)(lo + d);
	b = (uint8_t)(hi + d);
	if (a > b || b - a != hi - lo || a <= MAP_EDGE_LOW || b >= MAP_EDGE_HIGH)
		return MAPCHK_WIDE;
	return MAPCHK_OK;
}

int SEC_EDITOR map_settle(MapUndo *u, MapInfo *info, uint8_t *x, uint8_t *y, uint8_t *dx,
	uint8_t *dy)
{
	int16_t ax, ay, bx, by;
	uint8_t l = 255, r = 0, t = 255, b = 0, pass, vert, d, ex = 255, ey = 255;
	uint16_t i, j;
	int err = MAPCHK_OK, e;

	*x = *y = 255;
	*dx = *dy = 0;
	if (!map_center_move(info, &ax, &ay))
		return MAPCHK_OK;
	for (j = 0; j < 256; j++) {
		const uint8_t *rw = mapstore_row((uint8_t)j);
		for (i = 0; i < 256; i++) {
			if (rw[i] == TERRAIN_DEEP_SEA)
				continue;
			if (i < l) l = (uint8_t)i;
			if (i > r) r = (uint8_t)i;
			if (j < t) t = (uint8_t)j;
			if (j > b) b = (uint8_t)j;
		}
	}
	// Centring one axis is enough, as the load moves the map only when both
	// are off centre. The smaller move goes first, and its reason is the
	// one given when neither fits.
	vert = (uint8_t)((ay < 0 ? -ay : ay) <= (ax < 0 ? -ax : ax));
	for (pass = 0; pass < 2; pass++, vert = (uint8_t)!vert) {
		d = (uint8_t)(vert ? ay : ax);
		e = vert ? settle_fits(info, 1, d, t, b, x, y) : settle_fits(info, 0, d, l, r, x, y);
		if (e == MAPCHK_OK) {
			map_shift(info, vert ? 0 : d, vert ? d : 0);
			if (!map_center_move(info, &bx, &by)) {
				if (vert)
					*dy = d;
				else
					*dx = d;
				// The undo step's squares are where they were.
				u->on = 0;
				u->next = 1;
				return MAPCHK_OK;
			}
			// An item that wrapped round widens the box: put it back.
			d = (uint8_t)(256 - d);
			map_shift(info, vert ? 0 : d, vert ? d : 0);
			e = MAPCHK_FAR;
			*x = *y = 255;
		}
		if (pass == 0) {
			err = e;
			ex = *x;
			ey = *y;
		}
	}
	*x = ex;
	*y = ey;
	return err;
}

// ---- items ----

// Byte copies, as a struct copy or memset can be a library call on the Palm.
static void SEC_EDITOR copy(void *dst, const void *src, uint16_t n)
{
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	while (n--)
		*d++ = *s++;
}

static void SEC_EDITOR zero(void *dst, uint16_t n)
{
	uint8_t *d = (uint8_t *)dst;

	while (n--)
		*d++ = 0;
}

void SEC_EDITOR map_item_xy(const MapInfo *info, uint8_t kind, uint8_t index, uint8_t *x,
	uint8_t *y)
{
	if (kind == ITEM_PILL) {
		*x = info->pills[index].x;
		*y = info->pills[index].y;
	} else if (kind == ITEM_BASE) {
		*x = info->bases[index].x;
		*y = info->bases[index].y;
	} else {
		*x = info->starts[index].x;
		*y = info->starts[index].y;
	}
}

static uint8_t SEC_EDITOR count(const MapInfo *info, uint8_t kind)
{
	return kind == ITEM_PILL ? info->numPills : kind == ITEM_BASE ? info->numBases :
		info->numStarts;
}

// Like map_item_at, leaving out one item.
static int SEC_EDITOR other_at(const MapInfo *info, uint8_t x, uint8_t y, uint8_t skipKind,
	uint8_t skipIndex, uint8_t *kind, uint8_t *index)
{
	uint8_t k, i, n, ix, iy;

	for (k = 0; k < ITEM_KINDS; k++) {
		n = count(info, k);
		for (i = 0; i < n; i++) {
			if (k == skipKind && i == skipIndex)
				continue;
			map_item_xy(info, k, i, &ix, &iy);
			if (ix == x && iy == y) {
				*kind = k;
				*index = i;
				return 1;
			}
		}
	}
	return 0;
}

int SEC_EDITOR map_item_at(const MapInfo *info, uint8_t x, uint8_t y, uint8_t *kind,
	uint8_t *index)
{
	return other_at(info, x, y, ITEM_NONE, ITEM_NONE, kind, index);
}

int SEC_EDITOR map_item_fits(const MapInfo *info, uint8_t kind, uint8_t x, uint8_t y,
	uint8_t skipKind, uint8_t skipIndex)
{
	uint8_t k, i;

	if (!map_playable(x, y))
		return MAPITEM_OUTSIDE;
	if (other_at(info, x, y, skipKind, skipIndex, &k, &i))
		return MAPITEM_TAKEN;
	if (kind != ITEM_START && mapstore_get(x, y) == TERRAIN_DEEP_SEA)
		return MAPITEM_SEA;
	return MAPITEM_OK;
}

const char *SEC_EDITOR map_item_text(int err, uint8_t kind)
{
	switch (err) {
	case MAPITEM_TAKEN: return "That square has an item on it.";
	case MAPITEM_SEA: return "Pills and bases can't go on deep sea.";
	case MAPITEM_OUTSIDE: return "Items go inside the mined border.";
	case MAPITEM_FULL:
		return kind == ITEM_PILL ? "The map has 16 pills already." :
			kind == ITEM_BASE ? "The map has 16 bases already." :
			"The map has 16 starts already.";
	}
	return "";
}

// ---- undo and painting ----

void SEC_EDITOR map_undo_next(MapUndo *u)
{
	u->next = 1;
}

void SEC_EDITOR map_undo_change(MapUndo *u, const MapInfo *info)
{
	if (!u->next)
		return;
	u->next = 0;
	u->on = 1;
	u->over = 0;
	u->snap = 0;
	u->n = 0;
	copy(&u->info, info, sizeof(MapInfo));
}

// Moves the step from the square list to the whole-map copy: the map as it
// is, with the listed squares as they were. Returns 0 without room.
static int SEC_EDITOR snap_take(MapUndo *u)
{
	uint16_t i;

	if (map_snap_alloc() != 0)
		return 0;
	for (i = 0; i < 256; i++)
		map_snap_put((uint8_t)i, mapstore_row(i));
	for (i = 0; i < u->n; i++)
		map_snap_set(u->sq[i][0], u->sq[i][1], u->sq[i][2]);
	return 1;
}

// Notes the square's terrain before the step first changes it.
static void SEC_EDITOR keep(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y)
{
	uint16_t i;

	map_undo_change(u, info);
	if (!u->on || u->over || u->snap)
		return;
	for (i = 0; i < u->n; i++)
		if (u->sq[i][0] == x && u->sq[i][1] == y)
			return;
	if (u->n == MAP_UNDO_SQUARES) {
		if (snap_take(u))
			u->snap = 1;
		else
			u->over = 1;
		return;
	}
	u->sq[u->n][0] = x;
	u->sq[u->n][1] = y;
	u->sq[u->n][2] = mapstore_get(x, y);
	u->n++;
}

int SEC_EDITOR map_undo(MapUndo *u, MapInfo *info)
{
	uint16_t i;

	u->next = 1;
	if (!u->on || u->over) {
		u->on = 0;
		return 0;
	}
	if (u->snap) {
		uint16_t y, x, e;

		// Rows back from the copy, a run of changed squares at a time.
		for (y = 0; y < 256; y++) {
			const uint8_t *was = map_snap_row((uint8_t)y), *now = mapstore_row(y);

			for (x = 0; x < 256; x = e) {
				if (was[x] == now[x]) {
					e = x + 1;
					continue;
				}
				for (e = x + 1; e < 256 && was[e] != now[e]; e++)
					;
				mapstore_set_bytes((uint8_t)x, (uint8_t)y, (uint16_t)(e - x), was + x);
			}
		}
	} else {
		for (i = u->n; i-- > 0;)
			game_set_terrain(u->sq[i][0], u->sq[i][1], u->sq[i][2]);
	}
	copy(info, &u->info, sizeof(MapInfo));
	u->snap = 0;
	u->on = 0;
	return 1;
}

// The ground map_parse leaves under a pill or base for terrain t.
static uint8_t SEC_EDITOR ground(uint8_t kind, uint8_t t)
{
	if (kind == ITEM_BASE)
		return TERRAIN_ROAD;
	if (kind == ITEM_PILL) {
		if (t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA || t == TERRAIN_BUILDING ||
		    t == TERRAIN_HALFBUILDING)
			return TERRAIN_ROAD;
		if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
			return (uint8_t)(t - TERRAIN_MINE_ADD);
	}
	return t;
}

static int SEC_EDITOR set(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y, uint8_t t)
{
	if (mapstore_get(x, y) == t)
		return 0;
	keep(u, info, x, y);
	game_set_terrain(x, y, t);
	return 1;
}

uint8_t SEC_EDITOR map_paint_value(const MapInfo *info, uint8_t x, uint8_t y, uint8_t t)
{
	uint8_t kind, index;

	return map_item_at(info, x, y, &kind, &index) ? ground(kind, t) : t;
}

int SEC_EDITOR map_paint(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y, uint8_t t)
{
	return set(u, info, x, y, map_paint_value(info, x, y, t));
}

void SEC_EDITOR map_item_ground(MapUndo *u, const MapInfo *info, uint8_t kind, uint8_t index)
{
	uint8_t x, y;

	map_item_xy(info, kind, index, &x, &y);
	set(u, info, x, y, ground(kind, mapstore_get(x, y)));
}

// The start direction (0 east, counting anticlockwise in sixteenths) that
// faces the middle of the map most nearly.
static uint8_t SEC_EDITOR facing(uint8_t x, uint8_t y)
{
	int16_t dx = 128 - (int16_t)x, dy = (int16_t)y - 128;     // dy up
	int16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	uint8_t s;

	// Sixteenths from the x axis: tan 11.25, 33.75, 56.25 and 78.75 degrees
	// are about 1/5, 2/3, 3/2 and 5.
	s = ay * 5 < ax ? 0 : ay * 3 < ax * 2 ? 1 : ay * 2 < ax * 3 ? 2 : ay < ax * 5 ? 3 : 4;
	if (dx >= 0)
		return (uint8_t)(dy >= 0 ? s : (16 - s) & 15);
	return (uint8_t)(dy >= 0 ? 8 - s : 8 + s);
}

int SEC_EDITOR map_item_add(MapUndo *u, MapInfo *info, uint8_t kind, uint8_t x, uint8_t y,
	uint8_t *index)
{
	uint8_t i = count(info, kind);
	int err;

	if (i >= 16)
		return MAPITEM_FULL;
	err = map_item_fits(info, kind, x, y, ITEM_NONE, ITEM_NONE);
	if (err != MAPITEM_OK)
		return err;
	map_undo_change(u, info);
	if (kind == ITEM_PILL) {
		MapPill *p = &info->pills[i];

		zero(p, sizeof(*p));
		p->owner = MAP_NEUTRAL;
		p->armour = R_PILL_MAX_ARMOUR;
		p->speed = R_PILL_ATTACK_NORMAL;
		info->numPills++;
	} else if (kind == ITEM_BASE) {
		MapBase *p = &info->bases[i];

		zero(p, sizeof(*p));
		p->owner = MAP_NEUTRAL;
		p->armour = R_BASE_FULL_ARMOUR;
		p->shells = R_BASE_FULL_SHELLS;
		p->mines = R_BASE_FULL_MINES;
		info->numBases++;
	} else {
		MapStart *p = &info->starts[i];

		zero(p, sizeof(*p));
		p->dir = facing(x, y);
		info->numStarts++;
	}
	*index = i;
	map_item_move(u, info, kind, i, x, y);
	return MAPITEM_OK;
}

int SEC_EDITOR map_item_move(MapUndo *u, MapInfo *info, uint8_t kind, uint8_t index, uint8_t x,
	uint8_t y)
{
	int err = map_item_fits(info, kind, x, y, kind, index);

	if (err != MAPITEM_OK)
		return err;
	map_undo_change(u, info);
	if (kind == ITEM_PILL) {
		info->pills[index].x = x;
		info->pills[index].y = y;
		info->pills[index].active = 1;
	} else if (kind == ITEM_BASE) {
		info->bases[index].x = x;
		info->bases[index].y = y;
		info->bases[index].active = 1;
	} else {
		info->starts[index].x = x;
		info->starts[index].y = y;
		info->starts[index].active = 1;
	}
	return MAPITEM_OK;
}

void SEC_EDITOR map_item_remove(MapUndo *u, MapInfo *info, uint8_t kind, uint8_t index)
{
	uint8_t n = count(info, kind), i;

	if (index >= n)
		return;
	map_undo_change(u, info);
	for (i = index; i + 1 < n; i++) {
		if (kind == ITEM_PILL)
			copy(&info->pills[i], &info->pills[i + 1], sizeof(MapPill));
		else if (kind == ITEM_BASE)
			copy(&info->bases[i], &info->bases[i + 1], sizeof(MapBase));
		else
			copy(&info->starts[i], &info->starts[i + 1], sizeof(MapStart));
	}
	if (kind == ITEM_PILL)
		info->numPills--;
	else if (kind == ITEM_BASE)
		info->numBases--;
	else
		info->numStarts--;
}

// ---- new maps and big tools ----

// Writes t to squares x0..x1 of row y without noting them, keeping the
// old terrain for undo first.
static void SEC_EDITOR span(MapUndo *u, const MapInfo *info, uint8_t y, uint8_t x0, uint8_t x1,
	uint8_t t)
{
	uint8_t buf[32];
	uint16_t x, n;

	for (x = x0; x <= x1; x++)
		if (mapstore_get(x, y) != t)
			keep(u, info, (uint8_t)x, y);
	for (n = 0; n < sizeof(buf); n++)
		buf[n] = t;
	for (x = x0; x <= x1; x += n) {
		n = (uint16_t)(x1 - x + 1);
		if (n > sizeof(buf))
			n = sizeof(buf);
		mapstore_set_bytes((uint8_t)x, y, n, buf);
	}
}

// Sets the ground under the pills and bases inside the box, after its
// squares were written plain.
static void SEC_EDITOR grounds(MapUndo *u, const MapInfo *info, uint8_t x0, uint8_t y0,
	uint8_t x1, uint8_t y1)
{
	uint8_t i;

	for (i = 0; i < info->numPills; i++)
		if (info->pills[i].x >= x0 && info->pills[i].x <= x1 && info->pills[i].y >= y0 &&
		    info->pills[i].y <= y1)
			map_item_ground(u, info, ITEM_PILL, i);
	for (i = 0; i < info->numBases; i++)
		if (info->bases[i].x >= x0 && info->bases[i].x <= x1 && info->bases[i].y >= y0 &&
		    info->bases[i].y <= y1)
			map_item_ground(u, info, ITEM_BASE, i);
}

static uint8_t SEC_EDITOR radius(uint8_t size)
{
	return size == MAP_BLANK_SMALL ? 16 : size == MAP_BLANK_MEDIUM ? 40 : 88;
}

// map_parse centres the land's box on 126 (WinBolo's mapCenter).
#define BLANK_MID 126

void SEC_EDITOR map_blank(MapInfo *info, uint8_t size)
{
	int16_t r = radius(size), dx, dy;
	uint8_t buf[32];
	uint16_t x, n;

	zero(info, sizeof(MapInfo));
	mapstore_fill(TERRAIN_DEEP_SEA);
	for (n = 0; n < sizeof(buf); n++)
		buf[n] = TERRAIN_GRASS;
	for (dy = -r; dy <= r; dy++) {
		for (dx = r; dx * dx + dy * dy > r * r; dx--)
			;
		for (x = BLANK_MID - dx; x <= BLANK_MID + dx; x += n) {
			n = (uint16_t)(BLANK_MID + dx - x + 1);
			if (n > sizeof(buf))
				n = sizeof(buf);
			mapstore_set_bytes((uint8_t)x, (uint8_t)(BLANK_MID + dy), n, buf);
		}
	}
	info->numStarts = 1;
	info->starts[0].x = BLANK_MID;
	info->starts[0].y = BLANK_MID;
	info->starts[0].dir = facing(BLANK_MID, BLANK_MID);
	info->starts[0].active = 1;
}

uint16_t SEC_EDITOR map_brush(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y,
	uint8_t size, uint8_t t)
{
	int16_t r = size / 2, dx, dy, px, py;
	uint16_t n = 0;

	for (dy = -r; dy <= r; dy++)
		for (dx = -r; dx <= r; dx++) {
			px = (int16_t)x + dx;
			py = (int16_t)y + dy;
			if (px >= 0 && py >= 0 && px < 256 && py < 256 &&
			    map_playable((uint8_t)px, (uint8_t)py))
				n = (uint16_t)(n + map_paint(u, info, (uint8_t)px, (uint8_t)py, t));
		}
	return n;
}

uint16_t SEC_EDITOR map_box(MapUndo *u, const MapInfo *info, uint8_t x0, uint8_t y0,
	uint8_t x1, uint8_t y1, uint8_t t)
{
	uint8_t k, y;
	uint16_t n = 0, x;

	if (x0 > x1) {
		k = x0;
		x0 = x1;
		x1 = k;
	}
	if (y0 > y1) {
		k = y0;
		y0 = y1;
		y1 = k;
	}
	if (x0 <= MAP_EDGE_LOW)
		x0 = MAP_EDGE_LOW + 1;
	if (y0 <= MAP_EDGE_LOW)
		y0 = MAP_EDGE_LOW + 1;
	if (x1 >= MAP_EDGE_HIGH)
		x1 = MAP_EDGE_HIGH - 1;
	if (y1 >= MAP_EDGE_HIGH)
		y1 = MAP_EDGE_HIGH - 1;
	if (x0 > x1 || y0 > y1)
		return 0;
	for (y = y0;; y++) {
		for (x = x0; x <= x1; x++)
			if (mapstore_get(x, y) != t)
				n++;
		span(u, info, y, x0, x1, t);
		if (y == y1)
			break;
	}
	grounds(u, info, x0, y0, x1, y1);
	return n;
}

// The fill marks its area with a value no map holds, then paints it. The
// stack holds the first square of each run found beside a marked run; when
// it's full, a sweep over the marked box finds what was dropped.
#define FILL_MARK   254
#define FILL_STACK  64

typedef struct Fill {
	MapUndo *u;
	const MapInfo *info;
	uint8_t src, n, over;
	uint8_t l, r, t, b;         // the marked box
	uint16_t count;
	uint8_t sx[FILL_STACK], sy[FILL_STACK];
} Fill;

static void SEC_EDITOR push(Fill *f, uint8_t x, uint8_t y)
{
	if (f->n == FILL_STACK) {
		f->over = 1;
		return;
	}
	f->sx[f->n] = x;
	f->sy[f->n] = y;
	f->n++;
}

// Pushes the start of each run of the area's terrain in row y, l..r.
static void SEC_EDITOR seeds(Fill *f, uint8_t y, uint8_t l, uint8_t r)
{
	uint16_t x;
	uint8_t in = 0;

	if (y <= MAP_EDGE_LOW || y >= MAP_EDGE_HIGH)
		return;
	for (x = l; x <= r; x++) {
		if (mapstore_get(x, y) == f->src) {
			if (!in)
				push(f, (uint8_t)x, y);
			in = 1;
		} else {
			in = 0;
		}
	}
}

static void SEC_EDITOR drain(Fill *f)
{
	uint8_t x, y, l, r;

	while (f->n) {
		f->n--;
		x = f->sx[f->n];
		y = f->sy[f->n];
		if (mapstore_get(x, y) != f->src)
			continue;
		for (l = x; l > MAP_EDGE_LOW + 1 && mapstore_get(l - 1, y) == f->src; l--)
			;
		for (r = x; r < MAP_EDGE_HIGH - 1 && mapstore_get(r + 1, y) == f->src; r++)
			;
		span(f->u, f->info, y, l, r, FILL_MARK);
		f->count = (uint16_t)(f->count + r - l + 1);
		if (l < f->l) f->l = l;
		if (r > f->r) f->r = r;
		if (y < f->t) f->t = y;
		if (y > f->b) f->b = y;
		seeds(f, (uint8_t)(y - 1), l, r);
		seeds(f, (uint8_t)(y + 1), l, r);
	}
}

// After the stack ran over: pushes squares of the area's terrain beside a
// marked one. Returns 0 when there are none.
static int SEC_EDITOR sweep(Fill *f)
{
	uint8_t x0 = f->l > MAP_EDGE_LOW + 1 ? f->l - 1 : f->l, x1 = f->r < MAP_EDGE_HIGH - 1 ? f->r + 1 : f->r;
	uint8_t y0 = f->t > MAP_EDGE_LOW + 1 ? f->t - 1 : f->t, y1 = f->b < MAP_EDGE_HIGH - 1 ? f->b + 1 : f->b;
	uint16_t x, y;
	int found = 0;

	f->over = 0;
	for (y = y0; y <= y1 && !f->over; y++)
		for (x = x0; x <= x1 && !f->over; x++)
			if (mapstore_get(x, y) == f->src && (mapstore_get(x - 1, y) == FILL_MARK ||
			    mapstore_get(x + 1, y) == FILL_MARK || mapstore_get(x, y - 1) == FILL_MARK ||
			    mapstore_get(x, y + 1) == FILL_MARK)) {
				push(f, (uint8_t)x, (uint8_t)y);
				found = 1;
			}
	return found;
}

uint16_t SEC_EDITOR map_fill(MapUndo *u, const MapInfo *info, uint8_t x, uint8_t y, uint8_t t)
{
	Fill f;
	uint8_t i, rx, ry;
	uint16_t e;

	if (!map_playable(x, y) || mapstore_get(x, y) == t)
		return 0;
	zero(&f, sizeof(f));
	f.u = u;
	f.info = info;
	f.src = mapstore_get(x, y);
	f.l = f.r = x;
	f.t = f.b = y;
	push(&f, x, y);
	for (;;) {
		drain(&f);
		if (!f.over || !sweep(&f))
			break;
	}
	// Pills and bases first, then plain runs.
	for (i = 0; i < info->numPills; i++)
		if (mapstore_get(info->pills[i].x, info->pills[i].y) == FILL_MARK)
			game_set_terrain(info->pills[i].x, info->pills[i].y, ground(ITEM_PILL, t));
	for (i = 0; i < info->numBases; i++)
		if (mapstore_get(info->bases[i].x, info->bases[i].y) == FILL_MARK)
			game_set_terrain(info->bases[i].x, info->bases[i].y, ground(ITEM_BASE, t));
	for (ry = f.t;; ry++) {
		const uint8_t *row = mapstore_row(ry);

		for (rx = f.l;; rx = (uint8_t)e) {
			if (row[rx] == FILL_MARK) {
				for (e = rx + 1; e <= f.r && row[e] == FILL_MARK; e++)
					;
				span(u, info, ry, rx, (uint8_t)(e - 1), t);
			} else {
				e = rx + 1;
			}
			if (e > f.r)
				break;
		}
		if (ry == f.b)
			break;
	}
	return f.count;
}

// Read-only 64 by 64 map overview. No game or map-store state is touched.
#ifndef MAPTHUMB_H
#define MAPTHUMB_H
#include "map.h"
#define MAP_THUMB_SIZE 64
enum { THUMB_SEA, THUMB_WATER, THUMB_LAND, THUMB_FOREST, THUMB_ROAD,
       THUMB_WALL, THUMB_BASE, THUMB_PILL, THUMB_COLOURS };
// A null pixel buffer validates without drawing. Safe without app globals.
int map_thumb(MapSource *source, uint8_t *pixels);
#endif

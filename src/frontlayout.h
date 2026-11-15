// Logical Palm coordinates, shared with the menu geometry checks.
#ifndef FRONTLAYOUT_H
#define FRONTLAYOUT_H
#include "resource.h"

static __inline__ int front_shift(int form, int y, int height)
{
	int extra = height > 160 ? height - 160 : 0;
	if (form == NewGameForm)
		return extra >= 50 ? extra / 2 + (y >= 78 ? 10 : 0) : extra / 4;
	if (form == SettingsForm) return extra / 4;
	return 0;
}

static __inline__ int front_map_size(int height) { return height >= 210 ? 96 : 64; }
static __inline__ int front_map_list_y(int height) { return height >= 210 ? 130 : 96; }
static __inline__ int front_lobby_y(int height) { return height >= 198 ? 72 : 38; }
static __inline__ int front_lobby_row(int height)
{
	return height >= 198 ? (height - 34 - 72) / 8 : 11;
}
static __inline__ int front_results_y(int height) { return height >= 210 ? 63 : 41; }
static __inline__ int front_results_row(int height) { return height >= 210 ? 15 : 11; }
#endif

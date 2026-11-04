#include "game.h"

void game_init(GameState *s, uint32_t seed)
{
	s->rng = seed ? seed : 1;
	s->tick = 0;
	s->inputSum = 0;
	s->x = 0;
	s->y = 0;
}

// xorshift32
uint32_t game_random(GameState *s)
{
	uint32_t r = s->rng;

	r ^= r << 13;
	r ^= r >> 17;
	r ^= r << 5;
	s->rng = r;
	return r;
}

void game_tick(GameState *s, uint16_t inputs)
{
	uint32_t r = game_random(s);

	s->tick++;
	s->inputSum += inputs;
	s->x = (int16_t)(s->x + (int16_t)((r & 7) - 3) + ((inputs & 1) ? 1 : 0));
	s->y = (int16_t)(s->y + (int16_t)(((r >> 8) & 7) - 3) + ((inputs & 2) ? 1 : 0));
}

static uint32_t crc_byte(uint32_t crc, uint8_t b)
{
	int i;

	crc ^= b;
	for (i = 0; i < 8; i++)
		crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
	return crc;
}

static uint32_t crc_u32(uint32_t crc, uint32_t v)
{
	crc = crc_byte(crc, (uint8_t)(v >> 24));
	crc = crc_byte(crc, (uint8_t)(v >> 16));
	crc = crc_byte(crc, (uint8_t)(v >> 8));
	return crc_byte(crc, (uint8_t)v);
}

uint32_t game_checksum(const GameState *s)
{
	uint32_t crc = 0xFFFFFFFFUL;

	crc = crc_u32(crc, s->rng);
	crc = crc_u32(crc, s->tick);
	crc = crc_u32(crc, s->inputSum);
	crc = crc_u32(crc, (uint16_t)s->x);
	crc = crc_u32(crc, (uint16_t)s->y);
	return ~crc;
}

uint32_t game_selftest(void)
{
	GameState s;
	uint32_t i;

	game_init(&s, 12345);
	for (i = 0; i < 1000; i++)
		game_tick(&s, (uint16_t)(((i * 7) ^ (i >> 3)) & 0xFF));
	return game_checksum(&s);
}

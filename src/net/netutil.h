// Byte helpers for the network code, which has no C library on the Palm.
#ifndef NETUTIL_H
#define NETUTIL_H

#include <stdint.h>

static __inline__ void net_zero(void *p, uint16_t n)
{
	uint8_t *b = (uint8_t *)p;

	while (n--)
		*b++ = 0;
}

static __inline__ void net_copy(void *dst, const void *src, uint16_t n)
{
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	while (n--)
		*d++ = *s++;
}

// Big-endian reads and writes.
static __inline__ void net_put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static __inline__ void net_put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static __inline__ uint16_t net_get16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static __inline__ uint32_t net_get32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

#endif

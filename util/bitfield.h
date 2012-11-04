/*
 * vim: ts=8:sw=8:tw=79:noet
 *
 * Copyright (c) 2011-2012, the Locksmith authors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LKSMITH_UTIL_BITFIELD_H
#define LKSMITH_UTIL_BITFIELD_H

#include <stdint.h> /* for uint8_t, etc. */
#include <string.h> /* for memset, etc. */

#define BITFIELD_MEM(size) ((size + 7)/8)

#define BITFIELD_DECL(name, size) uint8_t name[BITFIELD_MEM(size)]

#define BITFIELD_ZERO(name) do { \
	uint8_t *name8 = name; \
	memset(name8, 0, sizeof(name)); \
} while(0);

#define BITFIELD_FILL(name) do { \
	uint8_t *name8 = name; \
	memset(name8, ~0, sizeof(name)); \
} while(0);

#define BITFIELD_SET(name, idx) do { \
	uint8_t *name8 = name; \
	int floor = idx / 8; \
	int rem = idx - (floor * 8); \
	name8[floor] |= (1 << rem); \
} while(0);

#define BITFIELD_COPY(dst, src) do { \
	uint8_t *src8 = src; \
	memcpy(dst, src8, sizeof(src)); \
} while(0);

#define BITFIELD_CLEAR(name, idx) do { \
	uint8_t *name8 = name; \
	int floor = idx / 8; \
	int rem = idx - (floor * 8); \
	name8[floor] &= ~(1 << rem); \
} while(0);

#define BITFIELD_TEST(name, idx) \
	((name[idx / 8] >> (idx % 8)) & 0x1)

#endif

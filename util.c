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

/* get the POSIX strerror_r */
#undef _GNU_SOURCE
#define _XOPEN_SOURCE 600
#include <string.h>
#define _GNU_SOURCE
#undef _XOPEN_SOURCE

#include "config.h"
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void fwdprintf(char *buf, size_t *off, size_t buf_len, const char *fmt, ...)
{
	int res;
	size_t o;
	va_list ap;

	o = *off;
	va_start(ap, fmt);
	res = vsnprintf(buf + o, buf_len - o, fmt, ap);
	va_end(ap);
	if (res < 0)
		return;
	else if (o + res > buf_len)
		*off = buf_len;
	else
		*off = o + res;
}

void simple_spin_lock(int *lock)
{
	struct timespec ts;

	while (1) {
		if (__sync_bool_compare_and_swap(lock, 0, 1)) {
			return;
		}
		ts.tv_sec = 0;
		ts.tv_nsec = 10000;
		nanosleep(&ts, NULL);
	}
}

void simple_spin_unlock(int *lock)
{
	__sync_bool_compare_and_swap(lock, 1, 0);
}

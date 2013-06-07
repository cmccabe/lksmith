/*
 * vim: ts=8:sw=8:tw=79:noet
 *
 * Copyright (c) 2013, the Locksmith authors.
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

#include "error.h"
#include "backtrace.h"

#include <execinfo.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INITIAL_SCRATCH_SIZE 16

#define MAX_SCRATCH_SIZE 8192

void bt_frames_free(char **backtrace)
{
	free(backtrace);
}

static int try_backtrace(void **scratch, int scratch_len)
{
	int num_symbols;
	if (!scratch) {
		return -ENOMEM;
	}
	num_symbols = backtrace(scratch, scratch_len);
	if (num_symbols == scratch_len) {
		return -ENOMEM;
	}
	return num_symbols;
}

int bt_frames_create(void ***scratch, int *scratch_len, char ***out)
{
	int num_symbols;
	void **next;
	char **symbols;

	while (((num_symbols = try_backtrace(*scratch, *scratch_len))) < 0) {
		int next_size = (*scratch_len == 0) ?
			INITIAL_SCRATCH_SIZE : (*scratch_len * 2);
		if (next_size > MAX_SCRATCH_SIZE) {
			return -ENOMEM;
		}
		next = realloc(*scratch, next_size * sizeof(void*));
		if (!next) {
			return -ENOMEM;
		}
		*scratch = next;
		*scratch_len = next_size;
	}
	symbols = backtrace_symbols(*scratch, num_symbols);
	if (!symbols) {
		return -ENOMEM;
	}
	*out = symbols;
	return num_symbols;
}

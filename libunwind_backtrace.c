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

#include <libunwind.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STACK_BUF_SZ 512

static int do_get_proc_fname(unw_cursor_t *cursor, char **out_fname,
		char **heap_buf, size_t *heap_buf_len)
{
	int ret;
	unw_word_t  offset;
	char *next_heap_buf = NULL, *stack_buf = NULL, *fname;
	size_t len = *heap_buf_len;

	while (1) {
		if (!len) {
			stack_buf = alloca(STACK_BUF_SZ);
			ret = unw_get_proc_name(cursor, stack_buf,
				STACK_BUF_SZ, &offset);
		} else {
			ret = unw_get_proc_name(cursor, *heap_buf,
				len, &offset);
		}
		if (ret == 0) {
			break;
		} else if (ret != -UNW_ENOMEM) {
			lksmith_error(ENOMEM, "create_backtrace "
				"failed: unw_get_proc_name failed "
				"with error %d\n", ret);
			return EIO;
		}
		len = len ? (len * 2) : (STACK_BUF_SZ * 2);
		next_heap_buf = realloc(*heap_buf, len);
		if (!next_heap_buf) {
			lksmith_error(ENOMEM, "create_backtrace "
				"failed: failed to allocate buffer of "
					"size %zd to hold proc name\n", len);
			return ENOMEM;
		}
		*heap_buf = next_heap_buf;
		*heap_buf_len = len;
	}
	fname = strdup(len ? *heap_buf : stack_buf);
	if (!fname) {
		lksmith_error(ENOMEM, "create_backtrace failed: failed "
			"to allocate buffer to hold proc name\n");
		return ENOMEM;
	}
	*out_fname = fname;
	return 0;
}

void bt_frames_free(char **backtrace)
{
	char **b;

	if (!backtrace)
		return;
	for (b = backtrace; *b; b++) {
		free(*b);
	}
	free(backtrace);
}

int bt_frames_create(void ***scratch __attribute__((__unused__)),
	int *scratch_len __attribute__((__unused__)), char ***out)
{
	int ret;
	unw_cursor_t cursor;
	unw_context_t context;
	char *heap_buf = NULL;
	size_t heap_buf_len = 0;
	char **backtrace = NULL, **backtrace_new;
	size_t backtrace_len = 0, cap = 0;

	if (unw_getcontext(&context)) {
		lksmith_error(ENOMEM, "bt_frames_create failed: "
			"unw_getcontext failed.\n");
		return -EIO;
	}
	ret = unw_init_local(&cursor, &context);
	if (ret) {
		lksmith_error(ENOMEM, "bt_frames_create failed: "
			"unw_init_local failed with error %d\n", ret);
		return -EIO;
	}
	while (unw_step(&cursor) > 0) {
		if (++backtrace_len > cap) {
			size_t new_cap = cap ? cap * 2 : 32;
			backtrace_new = realloc(backtrace, 
						sizeof(char*) * new_cap);
			if (!backtrace_new) {
				lksmith_error(ENOMEM, "bt_frames_create "
					"failed: failed to allocate char* "
					"array of length %zd\n", new_cap);
				ret = -EIO;
				goto done;
			}
			backtrace = backtrace_new;
			cap = new_cap;
		}
		ret = do_get_proc_fname(&cursor, &backtrace[backtrace_len - 1],
					&heap_buf, &heap_buf_len);
		if (ret != 0) {
			lksmith_error(ENOMEM, "bt_frames_create failed: "
				"do_get_proc_fname failed with error %d\n",
				ret);
			ret = -EIO;
			goto done;
		}
	}
	backtrace[backtrace_len] = NULL;
	ret = backtrace_len;
done:
	free(heap_buf);
	if (ret < 0) {
		bt_frames_free(backtrace);
		return ret;
	}
	*out = backtrace;
	return ret;
}

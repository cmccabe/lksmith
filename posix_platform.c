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

#include "platform.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/syscall.h>

static uint64_t g_tid;

void platform_create_thread_name(char * __restrict out, size_t out_len)
{
	uint64_t new_tid;

	/* pthreads contains no way to get a string representation of a
	 * thread's name.  pthread_self gives you an opaque identifier, but
	 * you can't do much with it.  So let's make up our own identifier
	 * with an atomic variable.
	 */
	new_tid = __sync_add_and_fetch(&g_tid, 1);
	snprintf(out, out_len, "thread_%"PRId64, new_tid);
}

void* get_dlsym_next(const char *fname)
{
	void *v;

	/* We need RTLD_NEXT to get this to work. */
	v = dlsym(RTLD_NEXT, fname);
	if (!v) {
		fprintf(stderr, "locksmith handler error: dlsym error: %s\n",
			dlerror());
		return NULL;
	}
	return v;
}

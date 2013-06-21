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
#include <string.h>
#include <sys/syscall.h>

extern pid_t gettid(void);

void platform_create_thread_name(char * __restrict out, size_t out_len)
{
	/* On Linux, we can call gettid() to get the kernel's ID number for
	 * this thread.  This is preferrable to making up our own number, since
	 * other debugging tools can also examine the kernel thread ID. */
	pid_t tid = (pid_t)syscall(SYS_gettid);
	snprintf(out, out_len, "thread_%"PRId64, (uint64_t)tid);
}

void* get_dlsym_next(const char *fname)
{
	void *v;

	if ((!strcmp(fname, "pthread_cond_init")) ||
			(!strcmp(fname, "pthread_cond_wait")) ||
			(!strcmp(fname, "pthread_cond_timedwait")) ||
			(!strcmp(fname, "pthread_cond_destroy"))) {
		v = dlvsym(RTLD_NEXT, fname, "GLIBC_2.3.2");
	} else {
		v = dlsym(RTLD_NEXT, fname);
	}
	if (!v) {
		/* dlerror is not thread-safe.  However, since there is no
		 * thread-safe interface, we really don't have much choice,
		 * do we?
		 *
		 * Also, technically a NULL return from dlsym doesn't
		 * necessarily indicate an error.  However, NULL is not
		 * a valid address for any of the things we're looking up, so
		 * we're going to assume that an error occurred.
		 */
		fprintf(stderr, "locksmith handler error: dlsym error: %s\n",
			dlerror());
		return NULL;
	}
	/* Another problem with the dlsym interface is that technically, a
	 * void* should never be cast to a function pointer, since the C
	 * standard allows them to have different sizes.
	 * Apparently the POSIX committee didn't read that part of the C
	 * standard.  We'll pretend we didn't either.
	 */
	return v;
}

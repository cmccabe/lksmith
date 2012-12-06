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

#include "error.h"
#include "shim.h"
#include "test.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

void die_on_error(int code, const char *msg)
{
	if (code == 0)
		return;
	fprintf(stderr, "die_on_error: got error %d: %s\n", code, msg);
	abort();
}

static char g_lksmith_log_env[4096];

void set_error_cb(lksmith_error_cb_t cb)
{
	snprintf(g_lksmith_log_env, sizeof(g_lksmith_log_env),
		"LKSMITH_LOG=callback://0x%"PRIxPTR, (uintptr_t)cb);
	putenv(g_lksmith_log_env);
}

struct recorded_error {
	int code;
	struct recorded_error *next;
};

static struct recorded_error *g_recorded_errors;

static pthread_mutex_t g_recorded_errors_lock = PTHREAD_MUTEX_INITIALIZER;

void record_error(int code, const char *msg __attribute__((unused)))
{
	struct recorded_error *rec;

	if (code == 0)
		return;
	rec = calloc(1, sizeof(*rec));
	if (!rec)
		abort();
	rec->code = code;
	r_pthread_mutex_lock(&g_recorded_errors_lock);
	rec->next = g_recorded_errors;
	g_recorded_errors = rec;
	r_pthread_mutex_unlock(&g_recorded_errors_lock);
}

void clear_recorded_errors(void)
{
	struct recorded_error *rec, *next;

	r_pthread_mutex_lock(&g_recorded_errors_lock);
	rec = g_recorded_errors;
	while (1) {
		if (!rec)
			break;
		next = rec->next;
		free(rec);
		rec = next;
	}
	r_pthread_mutex_unlock(&g_recorded_errors_lock);
}

int find_recorded_error(int expect)
{
	int found = 0;
	struct recorded_error **rec, *cur;

	r_pthread_mutex_lock(&g_recorded_errors_lock);
	rec = &g_recorded_errors;
	while (1) {
		cur = *rec;
		if (!cur)
			break;
		if (expect == cur->code) {
			found = 1;
			*rec = cur->next;
			free(cur);
			break;
		}
		rec = &(*rec)->next;
	}
	r_pthread_mutex_unlock(&g_recorded_errors_lock);
	return found;
}

void *xcalloc(size_t s)
{
	void *p;
	
	p = calloc(1, s);
	if (!p) {
		fprintf(stderr, "out of memory allocating %zd bytes.\n", s);
		abort();
	}
	return p;
}

char *xstrdup(const char *str)
{
	char *s = strdup(str);
	if (!s) {
		fprintf(stderr, "failed to duplicate string of size "
			"%zd.\n", strlen(str));
		exit(1);
	}
	return s;
}

int get_current_timespec(struct timespec *ts)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL))
		return errno;
	ts->tv_nsec = tv.tv_usec * 1000;
	ts->tv_sec = tv.tv_sec;
	return 0;
}

void timespec_add_milli(struct timespec *ts, unsigned int ms)
{
	uint64_t nsec = ts->tv_nsec;
	nsec += (ms * 1000);
	if (nsec > 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec = nsec - 1000000000;
	} else {
		ts->tv_nsec = nsec;
	}
}

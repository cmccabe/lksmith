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

#include "lksmith.h"
#include "test.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_multi_mutex_lock(signed int max_locks)
{
	signed int i;
	struct lksmith_mutex *mutex;
	static char name[LKSMITH_LOCK_NAME_MAX];

	mutex = xcalloc(sizeof(struct lksmith_mutex) * max_locks);
	for (i = 0; i < max_locks; i++) {
		snprintf(name, sizeof(name), "test_multi_%04d", i);
		EXPECT_ZERO(lksmith_mutex_init(name, &mutex[i], NULL));
	}
	for (i = 0; i < max_locks; i++) {
		EXPECT_ZERO(lksmith_mutex_lock(&mutex[i]));
	}
	for (i = max_locks - 1; i >= 0; i--) {
		EXPECT_ZERO(lksmith_mutex_unlock(&mutex[i]));
	}
	for (i = 0; i < max_locks; i++) {
		EXPECT_ZERO(lksmith_mutex_destroy(&mutex[i]));
	}
	return 0;
}

struct contention_data {
	struct lksmith_mutex *locks;
	int num_locks;
	int *thread_data;
	pthread_t *threads;
	int num_threads;
};

static struct contention_data cdata;

static int do_test_thread_contention_impl(int idx)
{
	int i;

	while (cdata.thread_data[idx]-- > 0) {
		for (i = 0; i < cdata.num_locks; i++) {
			EXPECT_ZERO(lksmith_mutex_lock(&cdata.locks[i]));
			pthread_yield();
		}
		for (i = 0; i < cdata.num_locks; i++) {
			EXPECT_ZERO(lksmith_mutex_unlock(
				&cdata.locks[cdata.num_locks - i - 1]));
			pthread_yield();
		}
	}
	return 0;
}

static void *do_test_thread_contention(void *v)
{
	return (void*)(intptr_t)
		do_test_thread_contention_impl((int)(intptr_t)v);
}

static int test_thread_contention(int num_locks, int num_threads)
{
	int i;
	char name[LKSMITH_LOCK_NAME_MAX];
	void *rval;

	memset(&cdata, 0, sizeof(cdata));
	cdata.locks = xcalloc(sizeof(struct lksmith_mutex) * num_locks);
	cdata.num_locks = num_locks;
	cdata.thread_data = xcalloc(sizeof(int) * num_threads);
	cdata.threads = xcalloc(sizeof(pthread_t) * num_threads);
	cdata.num_threads = num_threads;

	for (i = 0; i < num_locks; i++) {
		snprintf(name, sizeof(name), "test_cnt_%04d", i);
		EXPECT_ZERO(lksmith_mutex_init(name, &cdata.locks[i], NULL));
		cdata.thread_data[i] = 15;
	}
	for (i = 0; i < num_threads; i++) {
		EXPECT_ZERO(pthread_create(&cdata.threads[i], NULL,
			do_test_thread_contention, (void*)(intptr_t)i));
	}
	for (i = 0; i < num_threads; i++) {
		pthread_join(cdata.threads[i], &rval);
		EXPECT_EQ(rval, NULL);
	}
	for (i = 0; i < num_locks; i++) {
		lksmith_mutex_destroy(&cdata.locks[i]);
	}
	free(cdata.locks);
	free(cdata.thread_data);
	free(cdata.threads);

	return 0;
}

int main(void)
{
	lksmith_set_error_cb(die_on_error);

	EXPECT_ZERO(test_multi_mutex_lock(5));
	EXPECT_ZERO(test_multi_mutex_lock(100));
	EXPECT_ZERO(test_thread_contention(3, 2));
	EXPECT_ZERO(test_thread_contention(2, 3));
	EXPECT_ZERO(test_thread_contention(15, 60));
	return EXIT_SUCCESS;
}

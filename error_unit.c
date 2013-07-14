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
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define THREAD_WRAPPER_VOID(fn) \
static void *fn##_wrap(void *v __attribute__((unused))) { \
	return (void*)(intptr_t)fn(); \
}

#define THREAD_WRAPPER_INT(fn) \
static void *fn##_wrap(void *v) { \
	return (void*)(intptr_t)fn((int)(intptr_t)v); \
}

static pthread_mutex_t g_lock1 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_lock2 = PTHREAD_MUTEX_INITIALIZER;

static sem_t g_inver_sem1;
static sem_t g_inver_sem2;

static int inver_thread_a(void)
{
	EXPECT_ZERO(pthread_mutex_lock(&g_lock1));
	EXPECT_ZERO(pthread_mutex_lock(&g_lock2));
	EXPECT_ZERO(pthread_mutex_unlock(&g_lock2));
	EXPECT_ZERO(sem_post(&g_inver_sem1));
	EXPECT_ZERO(sem_wait(&g_inver_sem2));
	EXPECT_ZERO(pthread_mutex_unlock(&g_lock1));
	return 0;
}

THREAD_WRAPPER_VOID(inver_thread_a);

static int inver_thread_b(void)
{
	EXPECT_ZERO(sem_wait(&g_inver_sem1));
	EXPECT_ZERO(pthread_mutex_lock(&g_lock2));
	EXPECT_EQ(pthread_mutex_trylock(&g_lock1), EBUSY);
	EXPECT_ZERO(sem_post(&g_inver_sem2));
	EXPECT_ZERO(pthread_mutex_unlock(&g_lock2));
	return 0;
}

THREAD_WRAPPER_VOID(inver_thread_b);

static int test_ab_inversion(void)
{
	pthread_t thread_a, thread_b;
	void *rval;

	EXPECT_ZERO(sem_init(&g_inver_sem1, 0, 0));
	EXPECT_ZERO(sem_init(&g_inver_sem2, 0, 0));
	EXPECT_ZERO(pthread_create(&thread_a, NULL, inver_thread_a_wrap, NULL));
	EXPECT_ZERO(pthread_create(&thread_b, NULL, inver_thread_b_wrap, NULL));
	EXPECT_ZERO(pthread_join(thread_a, &rval));
	EXPECT_EQ(rval, NULL);
	EXPECT_ZERO(pthread_join(thread_b, &rval));
	EXPECT_EQ(rval, NULL);
	EXPECT_EQ(find_recorded_error(EDEADLK), 1);
	EXPECT_ZERO(sem_destroy(&g_inver_sem1));
	EXPECT_ZERO(sem_destroy(&g_inver_sem2));
	clear_recorded_errors();

	return 0;
}

static int test_destroy_while_same_thread_has_locked(void)
{
	pthread_mutex_t mutex;
	EXPECT_ZERO(pthread_mutex_init(&mutex, NULL));
	EXPECT_ZERO(pthread_mutex_lock(&mutex));
	EXPECT_EQ(pthread_mutex_destroy(&mutex), EBUSY);
	EXPECT_EQ(find_recorded_error(EBUSY), 1);
	EXPECT_ZERO(pthread_mutex_unlock(&mutex));
	EXPECT_ZERO(pthread_mutex_destroy(&mutex));
	clear_recorded_errors();

	return 0;
}

static sem_t g_test_destroy_sem1;
static sem_t g_test_destroy_sem2;
static pthread_mutex_t g_test_destroy_mutex = PTHREAD_MUTEX_INITIALIZER;

static int test_destroy_helper1(void)
{
	EXPECT_ZERO(pthread_mutex_lock(&g_test_destroy_mutex));
	EXPECT_ZERO(sem_post(&g_test_destroy_sem1));
	EXPECT_ZERO(sem_wait(&g_test_destroy_sem2));
	EXPECT_ZERO(pthread_mutex_unlock(&g_test_destroy_mutex));
	return 0;
}

THREAD_WRAPPER_VOID(test_destroy_helper1);

static int test_destroy_while_other_thread_has_locked(void)
{
	pthread_t thread_c;
	void *rval;

	EXPECT_ZERO(sem_init(&g_test_destroy_sem1, 0, 0));
	EXPECT_ZERO(sem_init(&g_test_destroy_sem2, 0, 0));
	EXPECT_ZERO(pthread_create(&thread_c, NULL,
		test_destroy_helper1_wrap, NULL));
	sem_wait(&g_test_destroy_sem1);
	EXPECT_EQ(pthread_mutex_destroy(&g_test_destroy_mutex), EBUSY);
	EXPECT_EQ(find_recorded_error(EBUSY), 1);
	sem_post(&g_test_destroy_sem2);
	EXPECT_ZERO(pthread_join(thread_c, &rval));
	EXPECT_EQ(rval, NULL);
	EXPECT_ZERO(pthread_mutex_destroy(&g_test_destroy_mutex));
	EXPECT_ZERO(sem_destroy(&g_test_destroy_sem1));
	EXPECT_ZERO(sem_destroy(&g_test_destroy_sem2));
	clear_recorded_errors();
	return 0;
}

static sem_t g_test_bad_unlock_sem1;
static sem_t g_test_bad_unlock_sem2;
static pthread_mutex_t g_test_bad_unlock_mutex;

static int test_bad_unlock_helper1(void)
{
	EXPECT_ZERO(pthread_mutex_lock(&g_test_bad_unlock_mutex));
	EXPECT_ZERO(sem_post(&g_test_bad_unlock_sem1));
	EXPECT_ZERO(sem_wait(&g_test_bad_unlock_sem2));
	EXPECT_ZERO(pthread_mutex_unlock(&g_test_bad_unlock_mutex));
	return 0;
}

THREAD_WRAPPER_VOID(test_bad_unlock_helper1);

static int test_bad_unlock(void)
{
	pthread_t thread_d;
	void *rval;

	EXPECT_ZERO(sem_init(&g_test_bad_unlock_sem1, 0, 0));
	EXPECT_ZERO(sem_init(&g_test_bad_unlock_sem2, 0, 0));
	EXPECT_ZERO(pthread_mutex_init(&g_test_bad_unlock_mutex, NULL));
	EXPECT_ZERO(pthread_create(&thread_d, NULL,
		test_bad_unlock_helper1_wrap, NULL));
	sem_wait(&g_test_bad_unlock_sem1);
	EXPECT_EQ(pthread_mutex_unlock(&g_test_bad_unlock_mutex), EPERM);
	EXPECT_EQ(find_recorded_error(EPERM), 1);
	sem_post(&g_test_bad_unlock_sem2);
	EXPECT_ZERO(pthread_join(thread_d, &rval));
	EXPECT_EQ(rval, NULL);
	EXPECT_ZERO(pthread_mutex_destroy(&g_test_bad_unlock_mutex));
	EXPECT_ZERO(sem_destroy(&g_test_bad_unlock_sem1));
	EXPECT_ZERO(sem_destroy(&g_test_bad_unlock_sem2));
	clear_recorded_errors();
	return 0;
}

#define MAX_BIG_INVERSION_LOCKS 1024 

static pthread_mutex_t g_locks[MAX_BIG_INVERSION_LOCKS];
static sem_t g_inver_sem[3];
static unsigned int g_bigenv_threads;

static int big_inversion_thread(int idx)
{
	unsigned int i;
	int next;

	next = (idx + 1) % g_bigenv_threads;
	EXPECT_ZERO(pthread_mutex_lock(&g_locks[idx]));
	EXPECT_ZERO(sem_post(&g_inver_sem[0]));
	if (idx == 0) {
		for (i = 0; i < g_bigenv_threads - 1; i++) {
			EXPECT_ZERO(sem_wait(&g_inver_sem[2]));
		}
		EXPECT_EQ(pthread_mutex_trylock(&g_locks[next]), EBUSY);
		EXPECT_EQ(find_recorded_error(EDEADLK), 1);
	} else {
		EXPECT_ZERO(sem_wait(&g_inver_sem[1]));
		EXPECT_EQ(pthread_mutex_trylock(&g_locks[next]), EBUSY);
		EXPECT_ZERO(sem_post(&g_inver_sem[2]));
		EXPECT_ZERO(pthread_mutex_lock(&g_locks[next]));
		EXPECT_ZERO(pthread_mutex_unlock(&g_locks[next]));
	}
	EXPECT_ZERO(pthread_mutex_unlock(&g_locks[idx]));
	return 0;
}

THREAD_WRAPPER_INT(big_inversion_thread);

static int test_big_inversion(unsigned int num_threads)
{
	unsigned int i;
	pthread_t thread[MAX_BIG_INVERSION_LOCKS];
	void *rval;

	g_bigenv_threads = num_threads;
	for (i = 0; i < sizeof(g_inver_sem)/sizeof(g_inver_sem[0]); i++) {
		EXPECT_ZERO(sem_init(&g_inver_sem[i], 0, 0));
	}
	for (i = 0; i < num_threads; i++) {
		EXPECT_ZERO(pthread_mutex_init(&g_locks[i], NULL));
	}
	for (i = 0; i < num_threads; i++) {
		EXPECT_ZERO(pthread_create(&thread[i], NULL,
			big_inversion_thread_wrap, (void*)(intptr_t)i));
	}
	for (i = 0; i < num_threads; i++) {
		EXPECT_ZERO(sem_wait(&g_inver_sem[0]));
	}
	for (i = 0; i < num_threads - 1; i++) {
		EXPECT_ZERO(sem_post(&g_inver_sem[1]));
	}
	for (i = 0; i < num_threads; i++) {
		EXPECT_ZERO(pthread_join(thread[i], &rval));
		EXPECT_EQ(rval, NULL);
	}
	for (i = 0; i < num_threads; i++) {
		EXPECT_ZERO(pthread_mutex_destroy(&g_locks[i]));
	}
	for (i = 0; i < sizeof(g_inver_sem)/sizeof(g_inver_sem[0]); i++) {
		EXPECT_ZERO(sem_destroy(&g_inver_sem[i]));
	}
	clear_recorded_errors();

	return 0;
}

static int test_take_sleeping_lock_while_holding_spin(void)
{
	pthread_mutex_t mutex;
	pthread_spinlock_t spin;
	EXPECT_ZERO(pthread_mutex_init(&mutex, NULL));
	EXPECT_ZERO(pthread_spin_init(&spin, 0));

	/* taking spin lock while holding mutex-- this is ok */
	EXPECT_ZERO(pthread_mutex_lock(&mutex));
	EXPECT_ZERO(pthread_spin_lock(&spin));
	EXPECT_ZERO(pthread_spin_unlock(&spin));
	EXPECT_ZERO(pthread_mutex_unlock(&mutex));
	EXPECT_EQ(find_recorded_error(EWOULDBLOCK), 0);

	/* taking mutex while holding spin lock-- this is not ok */
	EXPECT_ZERO(pthread_spin_lock(&spin));
	EXPECT_ZERO(pthread_mutex_lock(&mutex));
	EXPECT_ZERO(pthread_mutex_unlock(&mutex));
	EXPECT_ZERO(pthread_spin_unlock(&spin));
	EXPECT_EQ(find_recorded_error(EWOULDBLOCK), 1);

	/* we don't whine about the same problem more than once-- it would
	 * flood the logs */
	EXPECT_ZERO(pthread_spin_lock(&spin));
	EXPECT_ZERO(pthread_mutex_lock(&mutex));
	EXPECT_ZERO(pthread_mutex_unlock(&mutex));
	EXPECT_ZERO(pthread_spin_unlock(&spin));
	EXPECT_EQ(find_recorded_error(EWOULDBLOCK), 0);

	EXPECT_ZERO(pthread_spin_destroy(&spin));
	EXPECT_ZERO(pthread_mutex_destroy(&mutex));
	clear_recorded_errors();

	return 0;
}

static pthread_cond_t g_cond1;
static pthread_mutex_t g_cslock1;

static int cond_signaller1(void)
{
	EXPECT_ZERO(pthread_mutex_lock(&g_cslock1));
	EXPECT_ZERO(pthread_cond_signal(&g_cond1));
	EXPECT_ZERO(pthread_mutex_unlock(&g_cslock1));
	return 0;
}

THREAD_WRAPPER_VOID(cond_signaller1);

static int do_pthread_cond_wait(pthread_cond_t *__restrict cond,
	pthread_mutex_t *__restrict mutex,
	const struct timespec *__restrict ts)
{
	// The main reason for testing both pthread_cond_wait and
	// pthread_cond_timedwait here is to shake out bugs in our usage of
	// dlsym and friends.
	if (!ts) {
		return pthread_cond_wait(cond, mutex);
	}
	return pthread_cond_timedwait(cond, mutex, ts);
}

static int test_invalid_cond_wait(const struct timespec *ts)
{
	pthread_t thread;

	EXPECT_ZERO(pthread_mutex_init(&g_cslock1, NULL));
	EXPECT_ZERO(pthread_cond_init(&g_cond1, NULL));

	/* We must not call pthread_cond_wait on a mutex we don't actually
	 * hold. */
	EXPECT_EQ(do_pthread_cond_wait(&g_cond1, &g_cslock1, ts), EPERM);
	EXPECT_EQ(find_recorded_error(EPERM), 1);

	/* Here is an example of using the API correctly. */
	EXPECT_ZERO(pthread_mutex_lock(&g_cslock1));
	EXPECT_ZERO(pthread_create(&thread, NULL,
		cond_signaller1_wrap, NULL));
	EXPECT_EQ(do_pthread_cond_wait(&g_cond1, &g_cslock1, ts), 0);
	EXPECT_ZERO(pthread_mutex_unlock(&g_cslock1));
	EXPECT_ZERO(pthread_join(thread, NULL));

	/* test that we can destroy the pthread_cond and re-create it. */
	EXPECT_ZERO(pthread_cond_destroy(&g_cond1));
	EXPECT_ZERO(pthread_cond_init(&g_cond1, NULL));
	EXPECT_ZERO(pthread_cond_destroy(&g_cond1));
	EXPECT_ZERO(pthread_mutex_destroy(&g_cslock1));
	return 0;
}

static int test_recursion_on_nonrecursive(void)
{
	pthread_mutexattr_t attr;
	pthread_mutex_t mutex;
	int ty = PTHREAD_MUTEX_NORMAL;

	EXPECT_ZERO(pthread_mutexattr_init(&attr));
	EXPECT_ZERO(pthread_mutexattr_settype(&attr, ty));
	EXPECT_ZERO(pthread_mutex_init(&mutex, &attr));
	EXPECT_ZERO(pthread_mutex_lock(&mutex));
	EXPECT_EQ(pthread_mutex_trylock(&mutex), EBUSY);
	EXPECT_EQ(find_recorded_error(EDEADLK), 1);
	EXPECT_ZERO(pthread_mutex_unlock(&mutex));
	EXPECT_ZERO(pthread_mutexattr_destroy(&attr));
	EXPECT_ZERO(pthread_mutex_destroy(&mutex));
	return 0;
}

static pthread_cond_t g_tbcw_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_tbcw_lock1 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_tbcw_lock2 = PTHREAD_MUTEX_INITIALIZER;

static int tbcw_thread1(void)
{
	int ret;

	pthread_mutex_lock(&g_tbcw_lock1);
	ret = pthread_cond_wait(&g_tbcw_cond, &g_tbcw_lock1);
	pthread_mutex_unlock(&g_tbcw_lock1);
	if (ret) {
		pthread_mutex_lock(&g_tbcw_lock2);
		pthread_cond_signal(&g_tbcw_cond);
		pthread_mutex_unlock(&g_tbcw_lock2);
	}
	return ret;
}

THREAD_WRAPPER_VOID(tbcw_thread1);

static int tbcw_thread2(void)
{
	int ret;

	pthread_mutex_lock(&g_tbcw_lock2);
	ret = pthread_cond_wait(&g_tbcw_cond, &g_tbcw_lock2);
	pthread_mutex_unlock(&g_tbcw_lock2);
	if (ret) {
		pthread_mutex_lock(&g_tbcw_lock1);
		pthread_cond_signal(&g_tbcw_cond);
		pthread_mutex_unlock(&g_tbcw_lock1);
	}
	return ret;
}

THREAD_WRAPPER_VOID(tbcw_thread2);

static int test_bad_cond_wait(void)
{
	pthread_t thread_1, thread_2;
	void *rval1 = NULL, *rval2 = NULL;

	EXPECT_ZERO(pthread_create(&thread_1, NULL, tbcw_thread1_wrap, NULL));
	EXPECT_ZERO(pthread_create(&thread_2, NULL, tbcw_thread2_wrap, NULL));
	EXPECT_ZERO(pthread_join(thread_1, &rval1));
	EXPECT_ZERO(pthread_join(thread_2, &rval2));
	if (rval1 == NULL) {
		EXPECT_NOT_EQ(rval2, NULL);
	} else if (rval2 == NULL) {
		EXPECT_NOT_EQ(rval1, NULL);
	} else {
		fprintf(stderr, "expected one of the threads to fail.");
		return EINVAL;
	}
	EXPECT_EQ(find_recorded_error(EINVAL), 1);
	clear_recorded_errors();
	return 0;
}

int main(void)
{
	struct timespec ts;

	set_error_cb(record_error);
	EXPECT_ZERO(test_ab_inversion());
	EXPECT_ZERO(test_destroy_while_same_thread_has_locked());
	EXPECT_ZERO(test_destroy_while_other_thread_has_locked());
	EXPECT_ZERO(test_bad_unlock());
	EXPECT_ZERO(test_big_inversion(3));
	EXPECT_ZERO(test_big_inversion(100));
	EXPECT_ZERO(test_take_sleeping_lock_while_holding_spin());
	EXPECT_ZERO(test_invalid_cond_wait(NULL));
	EXPECT_ZERO(test_invalid_cond_wait(NULL));

	get_current_timespec(&ts);
	ts.tv_sec += 600;
	EXPECT_ZERO(test_invalid_cond_wait(&ts));

	EXPECT_ZERO(test_recursion_on_nonrecursive());

	EXPECT_ZERO(test_bad_cond_wait());

	return EXIT_SUCCESS;
}

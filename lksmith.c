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

#include "bitfield.h"
#include "lksmith.h"

#include <pthreads.h>
#include <stdint.h>
#include <stdio.h>

/******************************************************************
 *  Locksmith private constants
 *****************************************************************/
/** Minimum size of the before bitfield */
#define LKSMITH_BEFORE_MIN 16

/******************************************************************
 *  Locksmith private data structures
 *****************************************************************/
struct lksmith_lock_data {
	/** The name of this lock */
	char name[LKSMITH_LOCK_NAME_MAX];
	/** The number of times this mutex has been locked. */
	uint64_t nlock;
	/** lksmith-assigned ID. */
	uint32_t id;
	/** Size of the before bitfield */
	int before_size;
	/** Bitfield of locks that this lock must be taken before this one. */
	uint8_t before[0];
};

/******************************************************************
 *  Locksmith globals
 *****************************************************************/
static pthread_mutex_t g_internal_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Locksmith error callback to use.  Protected by g_internal_lock.
 */
static lksmith_error_cb_t g_error_cb = lksmith_error_cb_to_stderr;

/**
 * Array of locksmith_lock_data structures.
 * Indexed by lock data id.  Protected by g_internal_lock.
 */
static struct lksmith_lock_data * __restrict * __restrict g_locks;

/**
 * Size of the g_locks array.
 * Protected by g_internal_lock.
 */
static int g_locks_size;

/**
 * Bitfield of lock structures in use in g_locks.
 * 0 = unused; 1 = used.
 * Protected by g_internal_lock.
 */
static uint8_t * __restrict g_locks_used;

/******************************************************************
 *  Locksmith functions
 *****************************************************************/
static int lksmith_error_to_errno(int lkerr)
{
	switch (lkerr) {
	case LKSMITH_ERROR_OOM:
		return ENOMEM;
	case LKSMITH_ERROR_DESTROY_WHILE_IN_USE:
	case LKSMITH_ERROR_CREATE_WHILE_IN_USE:
		return EINVAL;
	default:
		return EIO;
	}
}

uint32_t lksmith_get_version(void)
{
	return LKSMITH_API_VERSION;
}

int lksmith_verion_to_str(uint32_t ver, char *str, size_t str_len)
{
	int res;
	
	res = snprintf(str, str_len, "%d.%d",
		((LKSMITH_API_VERSION >> 16) & 0xffff), (LKSMITH_API_VERSION & 0xffff));
	if (res < 0) {
		return -EIO;
	}
	if (res >= str_len) {
		return -ENAMETOOLONG;
	}
	return 0;
}

void lksmith_set_error_cb(lksmith_error_cb_t fn)
{
	pthread_mutex_lock(&g_internal_lock);
	g_error_cb = fn;
	pthread_mutex_unlock(&g_internal_lock);
}

void lksmith_error_cb_to_stderr(int code, const char *__restrict msg)
{
	fprintf(stderr, "LOCKSMITH ERROR %d: %s\n", code, msg);
}

lksmith_lock_data_

static int lksmith_realloc_lock_data(struct lksmith_lock_data **data,
		                                 int before_size)
{
	struct lksmith_lock_data *new_data;
	size_t new_size = sizeof(struct lksmith_lock_data) +
		                 BITFIELD_MEM(before_size);
	size_t old_size;

	if (*data) {
		 old_size = (*data)->before_size;
	} else {
		old_size = 0;
	}
	new_data = realloc(data, new_size);
	if (!new_data) {
		return -LKSMITH_ERROR_OOM;
	}
	if (old_size == 0) {
		/* zero everything */
		memset(new_data, 0, new_size);
	} else {
		size_t old_byte_size = (old_size + 7) / 8;
		size_t new_byte_size = (new_size + 7) / 8;
		if (new_byte_size > old_byte_size) {
		  memset(&new_data->before[old_byte_size], 0, new_byte_size - before_size);
		}
	}
	new_data->before_size = before_size;
	*data = new_data;
	return 0;
}

/**
 * Scan the bitfield for the next available lock ID.
 *
 * Note: you must call this function with the g_internal_lock held.
 *
 * @return          If positive: a new allocated lock ID.
 *                  If negative: a locksmith error code.
 */
static int lksmith_alloc_next_lock_id(void)
{
	int i;
	struct lksmith_lock_data **new_locks;
	uint8_t *new_locks_used;

	for (i = 0; i < g_locks_size; i++) {
		if (!BITFIELD_TEST(g_locks_used, i)) {
		  break;
		}
	}
	// TODO: optimize with "find first zero bit" compiler intrinsics?
	if (i > g_locks_size) {
		if (BITFIELD_MEM(i) > BITFIELD_MEM(g_locks_size)) {
		  new_locks_used = realloc(g_locks_used, BITFIELD_MEM(i));
		  if (!new_locks_used) {
		    return -LKSMITH_ERROR_OOM;
		  }
		  g_locks_used = new_locks_used;
		}
		new_locks = realloc(g_locks, i * sizeof(struct lksmith_lock_data*));
		if (!new_locks) {
		  return -LKSMITH_ERROR_OOM;
		}
		g_locks = new_locks;
		g_locks_size = i;
	}
	BITFIELD_SET(g_locks_used, i);
	return i;
}

int lksmith_mutex_init(const char * __restrict name,
		struct lksmith_mutex_t *__mutex,
		__const pthread_mutexattr_t *__mutexattr)
{
	int ret, next_lock_id;
	struct lksmith_lock_data *data = NULL, *prev;
	lksmith_error_cb_t error_cb;
	char buf[256] = { 0 };

	ret = lksmith_realloc_lock_data(&data, LKSMITH_BEFORE_MIN);
	pthread_mutex_lock(&g_internal_lock);
	if (ret) {
		ret = LKSMITH_ERROR_OOM;
		snprintf(buf, sizeof(buf), "lksmith_pthread_mutex_init(%s) "
		    "out of memory trying to allocate lksmith_lock_data.");
		goto error;
	}
	next_lock_id = lksmith_alloc_next_lock_id();
	if (next_lock_id < 0) {
		ret = LKSMITH_ERROR_OOM;
		snprintf(buf, sizeof(buf), "lksmith_pthread_mutex_init(%s) "
		    "out of memory trying to allocate a new lock id.", name);
		goto error:
	}
	data->id = next_lock_id;
	snprintf(data->name, LKSMITH_LOCK_NAME_MAX, "%s", name);
	prev = __sync_val_compare_and_swap(&mutex->info.data, NULL, data);
	if (prev) {
		error_cb = g_error_cb;
		ret = LKSMITH_ERROR_CREATE_WHILE_IN_USE;
		snprintf(buf, sizeof(buf), "lksmith_pthread_mutex_init(%s) "
		    "this mutex has already been initialized!", name);
		goto error;
	}
	pthread_mutex_unlock(&g_internal_lock);
	return 0;

error:
	error_cb = g_error_cb;
	pthread_mutex_unlock(&g_internal_lock);
	free(data);
	error_cb(ret, buf);
	return lksmith_error_to_errno(ret);
}

int lksmith_pthread_mutex_destroy(pthread_mutex_t *__mutex)
{
	struct lksmith_lock_data *cur;
	int ret;

	__sync_synchronize();
	cur = mutex->info.data;
	prev = __sync_val_compare_and_swap(&mutex->info.data,
			cur, NULL);
	if (prev != cur) {
		if (prev == NULL) {
			ret = LKSMITH_ERROR_MULTIPLE_DESTROY;
		}
	}
	
	// OK, so... in order to re-use the mutex number, we'd have to manually clear
	// the entry in the bitfield for all mutexes-- which sucks.
	// We could have a scavenger thread to do this, plus a condition variable.  I
	// suppose that's the best answer?

	if (prev) {
		error_cb = g_error_cb;
		ret = LKSMITH_ERROR_CREATE_WHILE_IN_USE;
		snprintf(buf, sizeof(buf), "lksmith_pthread_mutex_init(%s) "
		    "this mutex has already been initialized!");
		goto error;
	}
}

int lksmith_pthread_mutex_trylock(pthread_mutex_t *__mutex, int bypass)
{
}

int lksmith_pthread_mutex_lock(pthread_mutex_t *__mutex)
{
}

int lksmith_pthread_mutex_timedlock (pthread_mutex_t *__restrict __mutex,
				    __const struct timespec *__restrict
				    __abstime)
{
}

int lksmith_pthread_mutex_unlock (pthread_mutex_t *__mutex)
{
}

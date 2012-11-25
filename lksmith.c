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

#include "config.h"
#include "error.h"
#include "lksmith.h"
#include "platform.h"
#include "shim.h"
#include "tree.h"
#include "util.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/******************************************************************
 *  Locksmith private data structures
 *****************************************************************/
struct lksmith_lock {
	RB_ENTRY(lksmith_lock) entry;
	/** The lock pointer */
	const void *ptr;
	/** The number of times this mutex has been locked. */
	uint64_t nlock;
	/** The color that this node has been painted (used in traversal) */
	uint64_t color;
	/** Reference count for this lock */
	int refcnt;
	/** Size of the before list. */
	int before_size;
	/** list of locks that have been taken before this lock */
	struct lksmith_lock **before;
};

struct lksmith_tls {
	/** The name of this thread. */
	char name[LKSMITH_THREAD_NAME_MAX];
	/** Size of the held list. */
	unsigned int num_held;
	/** Unsorted list of locks held */
	const void **held;
};

/******************************************************************
 *  Locksmith prototypes
 *****************************************************************/
static int lksmith_lock_compare(const struct lksmith_lock *a, 
		const struct lksmith_lock *b) __attribute__((const));
RB_HEAD(lock_tree, lksmith_lock);
RB_GENERATE(lock_tree, lksmith_lock, entry, lksmith_lock_compare);
static void lksmith_tls_destroy(void *v);
static void tree_print(void) __attribute__((unused));

/******************************************************************
 *  Locksmith globals
 *****************************************************************/
/**
 * 1 if the library has been initialized.
 */
static int g_initialized;

/**
 * Protects the initialization state.
 */
static int g_init_state_lock;

/**
 * The key that allows us to retrieve thread-local data.
 * Protected by g_init_state_lock.
 */
static pthread_key_t g_tls_key;

/**
 * Mutex which protects g_tree
 */
static pthread_mutex_t g_tree_lock;

/**
 * Tree of mutexes sorted by pointer
 */
struct lock_tree g_tree;

/**
 * The latest color that has been used in graph traversal
 */
static uint64_t g_color;

/******************************************************************
 *  Initialization
 *****************************************************************/
/**
 * Initialize the locksmith library.
 */
static void lksmith_init(void)
{
	int ret;

	ret = lksmith_shim_init();
	if (ret) {
		/* can't use lksmith_error before shim_init */
		fprintf(stderr, "lksmith_init: lksmith_shim_init failed.  "
			"Can't find the real pthreads functions.\n");
		abort();
	}
	ret = pthread_key_create(&g_tls_key, lksmith_tls_destroy);
	if (ret) {
		lksmith_error(ret, "lksmith_init: pthread_key_create("
			"g_tls_key) failed: error %d: %s\n", ret, terror(ret));
		abort();
	}
	ret = r_pthread_mutex_init(&g_tree_lock, NULL);
	if (ret) {
		lksmith_error(ret, "lksmith_init: pthread_mutex_init "
			"g_tree_lock) failed: error %d: %s\n", ret, terror(ret));
		abort();
	}
	lksmith_error(0, "Locksmith has been initialized for process %lld\n",
		      (long long)getpid());
	g_initialized = 1;
}

/******************************************************************
 *  Thread-local storage
 *****************************************************************/
/**
 * Callback which destroys thread-local storage.
 *
 * This callback will be invoked whenever a thread exits.
 *
 * @param v		The TLS object.
 */
static void lksmith_tls_destroy(void *v)
{
	struct lksmith_tls *tls = v;
	free(tls->held);
	free(tls);
}

/**
 * Get or create the thread-local storage associated with this thread.
 *
 * The problem with POSIX thread-local storage is that in order to use it,
 * you must first initialize a 'key'.  But how do you initialize this key
 * prior to use?  There's no good way to do it.
 *
 * gcc provides the non-portable __attribute__((constructor)), which seems like
 * it could be useful.  Unfortunately, the order in which these constructor
 * functions are called between shared libraries is not defined.
 * If another shared library defines some constructor functions which invoke
 * pthreads functions, and it gets run before we initialize, it's game over.
 * C++ global constructors have the same issues.
 *
 * We could force Locksmith users to call an init function before calling any
 * other Locksmith functions.  Then, this init function could initialize the
 * key.  But that would be painful for many users.  This is especially true
 * in C++, where global constructors often make use of mutexes long before
 * main() runs.
 *
 * The other approach, which we have taken here, is to protect the key with a
 * mutex.  This is somewhat slow, since it means that we have to take this
 * mutex before every access to thread-local data.  Luckily, on platforms
 * that support the __thread keyword, we can bypass this slowness.
 * Thread-local variables declared using __thread don't need to be manually
 * intialized before use.  They're ready to go before any code has been run.
 *
 * One advantage that POSIX thread-local variables have over __thread
 * variables is that the former can declare "destructors" which are run when
 * the thread is destroyed.  (These have no relation to the C++ concept of
 * the same name.) We make use of that ability here, to clean up our
 * malloc()ed thread-local data when the thread in question exits.
 * By combining the __thread keyword and POSIX thread-local variables, we can
 * get the best of each.
 *
 * @return			NULL on OOM, the TLS otherwise.
 */
static struct lksmith_tls *get_or_create_tls(void)
{
	int ret = 0;
	struct lksmith_tls *tls;

#ifdef HAVE_IMPROVED_TLS
	static __thread struct lksmith_tls *t_improved_tls = NULL;
	if (t_improved_tls) {
		return t_improved_tls;
	}
#endif
	simple_spin_lock(&g_init_state_lock);
	if (!g_initialized) {
		lksmith_init();
	}
	simple_spin_unlock(&g_init_state_lock);
#ifndef HAVE_IMPROVED_TLS
	tls = pthread_getspecific(g_tls_key);
	if (tls) {
		return tls;
	}
#endif
	tls = calloc(1, sizeof(*tls));
	if (!tls) {
		lksmith_error(ENOMEM,
			"get_or_create_tls(): failed to allocate "
			"memory for thread-local storage.\n");
		return NULL;
	}
	platform_create_thread_name(tls->name, LKSMITH_THREAD_NAME_MAX);
	ret = pthread_setspecific(g_tls_key, tls);
	if (ret) {
		free(tls->held);
		free(tls);
		lksmith_error(ENOMEM,
			"get_or_create_tls(): pthread_setspecific "
			"failed with error %d: %s\n", ret, terror(ret));
		return NULL;
	}
#ifdef HAVE_IMPROVED_TLS
	t_improved_tls = tls;
#endif
	return tls;
}

/**
 * Add a lock ID to the end of the list of lock IDs we hold.
 *
 * NOTE: lock IDs can be added more than once to this list!
 * This is so that we can support recursive mutexes.
 *
 * @param tls		The thread-local data.
 * @param ptr		the lock to add to the list.
 *
 * @return		0 on success; ENOMEM if we ran out of memory.
 */
static int tls_append_held(struct lksmith_tls *tls, const void *ptr)
{
	const void **held;

	held = realloc(tls->held, sizeof(uintptr_t) * (tls->num_held + 1));
	if (!held)
		return ENOMEM;
	tls->held = held;
	held[tls->num_held++] = ptr;

	unsigned int i;
	const char *prefix = "";
	printf("%s: tls_append_lid: you are now holding ", tls->name);
	for (i = 0; i < tls->num_held; i++) {
		printf("%s%p", prefix, tls->held[i]);
		prefix = ", ";
	}
	printf("\n");
	return 0;
}

/**
 * Remove a lock ID from the list of lock IDs we hold.
 *
 * @param tls		The thread-local data.
 * @param ptr		the lock ID to add to the list.
 *
 * @return		0 on success; ENOENT if we are not holding the
 *			lock ID.
 */
static int tls_remove_held(struct lksmith_tls *tls, const void *ptr)
{
	signed int i;
	const void **held;

	for (i = tls->num_held - 1; i >= 0; i--) {
		if (tls->held[i] == ptr)
			break;
	}
	if (i < 0)
		return ENOENT;
	memmove(&tls->held[i], &tls->held[i + 1],
		sizeof(struct lksmith_held*) * (tls->num_held - i - 1));
	held = realloc(tls->held, sizeof(uintptr_t) * (--tls->num_held));
	if (held || (tls->num_held == 0)) {
		tls->held = held;
	}
	return 0;
}

/**
 * Determine if we are holding a lock.
 *
 * @param tls		The thread-local data.
 * @param ptr		The lock ID to find.
 *
 * @return		1 if we hold the lock; 0 otherwise.
 */
static int tls_contains_lid(struct lksmith_tls *tls, const void *ptr)
{
	unsigned int i;

	for (i = 0; i < tls->num_held; i++) {
		if (tls->held[i] == ptr)
			return 1;
	}
	return 0;
}

/******************************************************************
 *  Lock functions
 *****************************************************************/
static int lksmith_lock_compare(const struct lksmith_lock *a,
		const struct lksmith_lock *b)
{
	const void *pa = a->ptr;
	const void *pb = b->ptr;
	if (pa < pb)
		return -1;
	else if (pa > pb)
		return 1;
	else
		return 0;
}

/**
 * Add an element to a sorted array, if it's not already there.
 *
 * @param arr		(inout) the array
 * @param num		(inout) the array length
 * @param lid		The lock ID to add.
 *
 * @return		0 on success; ENOMEM if we ran out of memory.
 */
static int lk_add_sorted(struct lksmith_lock ** __restrict * __restrict arr,
			int * __restrict num, struct lksmith_lock *lk)
{
	int i;
	struct lksmith_lock **narr;

	for (i = 0; i < *num; i++) {
		if ((*arr)[i] == lk)
			return 0;
		else if ((*arr)[i] > lk)
			break;
	}
	narr = realloc(*arr, sizeof(struct lksmith_lock*) * (*num + 1));
	if (!narr)
		return ENOMEM;
	*arr = narr;
	memmove(&narr[i + 1], &narr[i], sizeof(uintptr_t) * (*num - i));
	narr[i] = lk;
	*num = *num + 1;
	return 0;
}

/**
 * Remove an element from a sorted array.
 * We assume it appears only once in that array.
 * TODO: be smarter here-- use bsearch
 *
 * @param arr		(inout) the array
 * @param num		(inout) the array length
 * @param lid		The lock ID to remove.
 */
static void lk_remove_sorted(struct lksmith_lock ** __restrict * __restrict arr,
			int * __restrict num, struct lksmith_lock *ak)
{
	int i;
	struct lksmith_lock **narr;

	if (*arr == NULL)
		return;
	for (i = 0; i < *num; i++) {
		if ((*arr)[i] == ak)
			break;
		else if ((*arr)[i] > ak)
			return;
	}
	if (i == *num)
		return;
	memmove(&(*arr)[i - 1], &(*arr)[i],
		sizeof(struct lksmith_lock*) * (*num - i - 1));
	narr = realloc(*arr, sizeof(struct lksmith_lock*) * (--*num));
	if (narr || (*num == 0))
		*arr = narr;
}

/**
 * Add a lock to the 'before' set of this lock data.
 * Note: you must call this function with the info->lock held.
 *
 * @param lk		The lock data.
 * @param lid		The lock ID to add.
 *
 * @return		0 on success; ENOMEM if we ran out of memory.
 */
static int lk_add_before(struct lksmith_lock *lk, struct lksmith_lock *ak)
{
	printf("adding %p to the before set for %p\n", ak, lk);
	return lk_add_sorted(&lk->before, &lk->before_size, ak);
}

/**
 * Remove a lock from the 'after' set of this lock data.
 * Note: you must call this function with the info->lock held.
 *
 * @param lk		The lock data.
 * @param lid		The lock ID to remove.
 */
static void lk_remove_before(struct lksmith_lock *lk, struct lksmith_lock *ak)
{
	lk_remove_sorted(&lk->before, &lk->before_size, ak);
}

/**
 * Dump out the contents of a lock data structure.
 *
 * @param lk		The lock data
 * @param buf		(out param) the buffer to write to
 * @param off		(inout param) current position in the buffer
 * @param buf_len	length of buf
 */
static void lk_dump(const struct lksmith_lock *lk,
		char *buf, size_t *off, size_t buf_len)
{
	int i;
	const char *prefix = "";

	fwdprintf(buf, off, buf_len, "lk{ptr=%p "
		  "nlock=%"PRId64", color=%"PRId64", refcnt=%d, "
		  "before={", (void*)lk->ptr, lk->nlock, lk->color, lk->refcnt);
	for (i = 0; i < lk->before_size; i++) {
		fwdprintf(buf, off, buf_len, "%s%p",
			  prefix, lk->before[i]);
		prefix = " ";
	}
	fwdprintf(buf, off, buf_len, "}}");
}

static void tree_print(void)
{
	char buf[8196];
	struct lksmith_lock *lk;
	size_t off;
	const char *prefix = "";

	fprintf(stderr, "g_lock_tree: {");
	RB_FOREACH(lk, lock_tree, &g_tree) {
		off = 0;
		lk_dump(lk, buf, &off, sizeof(buf));
		fprintf(stderr, "%s%s", prefix, buf);
		prefix = ",\n";
	}
	fprintf(stderr, "\n}\n");
}

static int lksmith_insert(const void *ptr, struct lksmith_lock **lk)
{
	struct lksmith_lock *ak, *bk;
	ak = calloc(1, sizeof(*ak));
	if (!ak) {
		return ENOMEM;
	}
	ak->ptr = ptr;
	bk = RB_INSERT(lock_tree, &g_tree, ak);
	if (bk) {
		free(ak);
		return EEXIST;
	}
	*lk = ak;
	return 0;
}

static struct lksmith_lock *lksmith_find(const void *ptr)
{
	struct lksmith_lock exemplar;
	memset(&exemplar, 0, sizeof(exemplar));
	exemplar.ptr = ptr;
	return RB_FIND(lock_tree, &g_tree, &exemplar);
}

/******************************************************************
 *  API functions
 *****************************************************************/
int lksmith_optional_init(const void *ptr)
{
	struct lksmith_tls *tls;
	struct lksmith_lock *lk;
	int ret;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_optional_init(lock=%p): "
			"failed to allocate thread-local storage.\n", ptr);
		return ENOMEM;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	ret = lksmith_insert(ptr, &lk);
	r_pthread_mutex_unlock(&g_tree_lock);
	if (ret) {
		lksmith_error(ret, "lksmith_optional_init(lock=%p, "
			"thread=%s): failed to allocate lock data: "
			"error %d: %s\n", ptr, tls->name, ret, terror(ret));
		return ret;
	}
	return 0;
}

int lksmith_destroy(const void *ptr)
{
	int ret;
	struct lksmith_lock *lk, *ak;
	struct lksmith_tls *tls;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_destroy(lock=%p): failed to "
			"allocate thread-local storage.\n", ptr);
		ret = ENOMEM;
		goto done;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		/* This might not be an error, if we used
		 * PTHREAD_MUTEX_INITIALIZER and then never did anything else
		 * with the lock prior to destroying it. */
		ret = ENOENT;
		goto done_unlock;
	}
	if (lk->refcnt != 0) {
		if (tls_contains_lid(tls, ptr) == 1) {
			lksmith_error(EBUSY, "lksmith_destroy(lock=%p, "
				"thread=%s): you must unlock this mutex "
				"before destroying it.", ptr, tls->name);
		} else {
			lksmith_error(EBUSY, "lksmith_destroy(lock=%p, "
				"thread=%s): this mutex is currently in use "
				"and so cannot be destroyed.", ptr, tls->name);
		}
		ret = EBUSY;
		goto done_unlock;
	}
	RB_REMOVE(lock_tree, &g_tree, lk);
	/* TODO: could probably avoid traversing the whole tree by using both
	 * before and after pointers inside locks, or some such? */
	RB_FOREACH(ak, lock_tree, &g_tree) {
		lk_remove_before(ak, lk);
	}
	ret = 0;
done_unlock:
	r_pthread_mutex_unlock(&g_tree_lock);
done:
	return ret;
}

static int lksmith_search(struct lksmith_lock *lk, const void *start)
{
	int ret, i;

	if (lk->ptr == start)
		return 1;
	if (lk->color == g_color)
		return 0;
	lk->color = g_color;
	for (i = 0; i < lk->before_size; i++) {
		ret = lksmith_search(lk->before[i], start);
		if (ret)
			return ret;
	}
	return 0;
}

int lksmith_prelock(const void *ptr)
{
	const void *held;
	struct lksmith_lock *lk, *ak;
	struct lksmith_tls *tls;
	int ret;
	unsigned int i;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_prelock(lock=%p): failed to "
			"allocate thread-local storage.\n", ptr);
		ret = ENOMEM;
		goto done;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		ret = lksmith_insert(ptr, &lk);
		if (ret) {
			lksmith_error(ret, "lksmith_prelock(lock=%p, "
				"thread=%s): failed to allocate lock data: "
				"error %d: %s\n", ptr, tls->name, ret, terror(ret));
			goto done_unlock;
		}
	}
	g_color++;
	for (i = 0; i < tls->num_held; i++) {
		held = tls->held[i];
		ak = lksmith_find(held);
		if (!ak) {
			lksmith_error(ENOMEM, "lksmith_prelock(lock=%p, "
				"thread=%s): thread holds unknown lock %p.\n",
				ptr, tls->name, held);
			continue;
		}
		ret = lksmith_search(ak, ptr);
		if (ret) {
			lksmith_error(EDEADLK, "lksmith_prelock(lock=%p, "
				"thread=%s): lock inversion!  This lock "
				"should have been taken before lock %p, which "
				"this thread already holds.\n",
				ptr, tls->name, held);
			continue;
		}
		lk_add_before(lk, ak);
	}
	lk->refcnt++;
	ret = 0;
done_unlock:
	r_pthread_mutex_unlock(&g_tree_lock);
done:
	return ret;
}

void lksmith_postlock(const void *ptr, int error)
{
	struct lksmith_tls *tls;
	struct lksmith_lock *lk;
	int ret;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_postlock(lock=%p): failed "
			"to allocate thread-local storage.\n", ptr);
		goto done;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		lksmith_error(EBADE, "lksmith_postlock(lock=%p, thread=%s): "
			"logic error: prelock didn't create the lock data?\n",
			ptr, tls->name);
		goto done_unlock;
	}
	if (error) {
		/* If the lock operation failed, decrement the refcnt. */
		lk->refcnt--;
		goto done_unlock;
	}
	lk->nlock++;
	ret = tls_append_held(tls, ptr);
	if (ret) {
		lksmith_error(ENOMEM, "lksmith_postlock(lock=%p, "
			"thread=%s): failed to allocate space to store "
			"another thread id.\n", ptr, tls->name);
	}
done_unlock:
	r_pthread_mutex_unlock(&g_tree_lock);
done:
	return;
}

int lksmith_preunlock(const void *ptr)
{
	struct lksmith_tls *tls;
	struct lksmith_lock *lk;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_preunlock(lock=%p): failed "
			"to allocate thread-local storage.\n", ptr);
		return ENOMEM;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		lksmith_error(ENOENT, "lksmith_preunlock(lock=%p, thread=%s): "
			"attempted to unlock an unknown lock.\n", ptr, tls->name);
		r_pthread_mutex_unlock(&g_tree_lock);
		return ENOENT;
	}
	r_pthread_mutex_unlock(&g_tree_lock);
	if (tls_contains_lid(tls, ptr) == 0) {
		lksmith_error(EPERM, "lksmith_preunlock(lock=%p, "
			"thread=%s): attempted to unlock a lock that this "
			"thread does not currently hold.\n", ptr, tls->name);
		return EPERM;
	}
	return 0;
}

void lksmith_postunlock(const void *ptr)
{
	struct lksmith_tls *tls;
	struct lksmith_lock *lk;
	int ret;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_postunlock(lock=%p): failed "
			"to allocate thread-local storage.\n", ptr);
		return;
	}
	ret = tls_remove_held(tls, ptr);
	if (ret) {
		lksmith_error(EBADE, "lksmith_postunlock(lock=%p, "
			"thread=%s): logic error: preunlock check told us "
			"we had the lock, but we don't?\n", ptr, tls->name);
		return;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		lksmith_error(EBADE, "lksmith_preunlock(lock=%p, thread=%s): "
			"logic error: attempted to unlock an unknown lock.\n",
			ptr, tls->name);
		r_pthread_mutex_unlock(&g_tree_lock);
		return;
	}
	lk->refcnt--;
	r_pthread_mutex_unlock(&g_tree_lock);
}

int lksmith_set_thread_name(const char *const name)
{
	struct lksmith_tls *tls = get_or_create_tls();

	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_set_thread_name(name=%s): "
			"failed to allocate thread-local storage.\n", name);
		return ENOMEM;
	}
	snprintf(tls->name, LKSMITH_THREAD_NAME_MAX, "%s", name);
	return 0;
}

const char* lksmith_get_thread_name(void)
{
	struct lksmith_tls *tls = get_or_create_tls();

	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_get_thread_name(): failed "
			"to allocate thread-local storage.\n");
		return NULL;
	}
	return tls->name;
}

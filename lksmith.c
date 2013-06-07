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

#include "backtrace.h"
#include "config.h"
#include "error.h"
#include "handler.h"
#include "lksmith.h"
#include "platform.h"
#include "tree.h"
#include "util.h"

#include <errno.h>
#include <execinfo.h>
#include <fnmatch.h>
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
#define MAX_NLOCK 0x1fffffffffffffffULL

struct lksmith_lock_props {
	/** The number of times this mutex has been locked. */
	uint64_t nlock : 61;
	/** 1 if we should allow recursive locks. */
	uint64_t recursive : 1;
	/** 1 if this mutex is a sleeping lock */
	uint64_t sleeper : 1;
	/** 1 if we have already warned about taking this lock while
	 * a spin lock is held. */
	uint64_t spin_warn : 1;
};

struct lksmith_holder {
	/** Name of the thread holding the lock */
	char name[LKSMITH_THREAD_NAME_MAX];
	/** Stack frames */
	char** bt_frames;
	/** Number of stack frames */
	int bt_len;
	/** Next in singly-linked list */
	struct lksmith_holder *next;
};

struct lksmith_lock {
	RB_ENTRY(lksmith_lock) entry;
	/** The lock pointer */
	const void *ptr;
	struct lksmith_lock_props props;
	/** The color that this node has been painted (used in traversal) */
	uint64_t color;
	/** Lock holders */
	struct lksmith_holder *holders;
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
	/** Number of spin locks currently held. */
	uint64_t num_spins : 63;
	/** 1 if we should intercept pthreads calls; 0 otherwise */
	uint64_t intercept : 1;
	/** scratch area for backtraces */
	void **backtrace_scratch;
	/** length of scratch area for backtraces */
	int backtrace_scratch_len;
};

/******************************************************************
 *  Locksmith prototypes
 *****************************************************************/
static int lksmith_lock_compare(const struct lksmith_lock *a, 
		const struct lksmith_lock *b) __attribute__((const));
RB_HEAD(lock_tree, lksmith_lock);
RB_GENERATE(lock_tree, lksmith_lock, entry, lksmith_lock_compare);
static void lksmith_tls_destroy(void *v);
static void lk_dump_to_stderr(struct lksmith_lock *lk) __attribute__((unused));
static void tree_print(void) __attribute__((unused));
static int compare_strings(const void *a, const void *b)
	__attribute__((const));

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

/**
 * A sorted list of frames to ignore.
 */
static char **g_ignored_frames;

/**
 * The number of ignored frames.
 */
static int g_num_ignored_frames;

/**
 * A list of frame patterns to ignore.
 */
static char **g_ignored_frame_patterns;

/**
 * The number of ignored frame patterns.
 */
static int g_num_ignored_frame_patterns;

/******************************************************************
 *  Initialization
 *****************************************************************/
static int compare_strings(const void *a, const void *b)
{
	const char *sa = *(const char **)a;
	const char *sb = *(const char **)b;
	return strcmp(sa, sb);
}

static int lksmith_init_ignored(const char *env, char ***out, int *out_len)
{
	int ret, num_ignored = 0;
	const char *ignored_env;
	char *ignored = NULL, **ignored_arr = 0, *saveptr = NULL;
	const char *str;

	ignored_env = getenv(env);
	if (!ignored_env) {
		ret = 0;
		goto done;
	}
	ignored = strdup(ignored_env);
	if (!ignored) {
		ret = ENOMEM;
		goto done;
	}
	for (str = strtok_r(ignored, ":", &saveptr);
			str; str = strtok_r(NULL, ":", &saveptr)) {
		num_ignored++;
	}
	strcpy(ignored, ignored_env);
	ignored_arr = calloc(num_ignored, sizeof(char*));
	if (!ignored_arr) {
		ret = ENOMEM;
		goto done;
	}
	num_ignored = 0;
	for (str = strtok_r(ignored, ":", &saveptr);
			str; str = strtok_r(NULL, ":", &saveptr)) {
		ignored_arr[num_ignored] = strdup(str);
		if (!ignored_arr[num_ignored]) {
			ret = ENOMEM;
			goto done;
		}
		num_ignored++;
	}
	qsort(ignored_arr, num_ignored, sizeof(char*), compare_strings);
	ret = 0;

done:
	free(ignored);
	if (ret) {
		if (ignored_arr) {
			char **i;
			for (i = ignored_arr; *i; i++) {
				free(*i);
			}
			free(ignored_arr);
		}
		return ret;
	}
	*out = ignored_arr;
	*out_len = num_ignored;
	return 0;
}

/**
 * Initialize the locksmith library.
 */
static void lksmith_init(void)
{
	int ret;

	ret = lksmith_handler_init();
	if (ret) {
		/* can't use lksmith_error before handler */
		fprintf(stderr, "lksmith_init: lksmith_handler_init failed.  "
			"Can't find the real pthreads functions.\n");
		abort();
	}
	ret = lksmith_init_ignored("LKSMITH_IGNORED_FRAMES",
			&g_ignored_frames, &g_num_ignored_frames);
	if (ret) {
		lksmith_error(ret, "lksmith_init: lksmith_init_ignored_frames("
			"frames) failed: error %d: %s\n", ret, terror(ret));
		abort();
	}
	ret = lksmith_init_ignored("LKSMITH_IGNORED_FRAME_PATTERNS",
			&g_ignored_frame_patterns,
			&g_num_ignored_frame_patterns);
	if (ret) {
		lksmith_error(ret, "lksmith_init: lksmith_init_ignored_frames("
			"patterns) failed: error %d: %s\n", ret, terror(ret));
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
	tls->intercept = 1;
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

int init_tls(void)
{
	struct lksmith_tls *tls;
	
	tls = get_or_create_tls();
	if (!tls)
		return -ENOMEM;
	return 0;
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
 *  Lock holder functions
 *****************************************************************/
/**
 * Dump out the contents of a lock holder structure
 *
 * @param holder        The lock holder
 * @param buf		(out param) the buffer to write to
 * @param off		(inout param) current position in the buffer
 * @param buf_len	length of buf
 */
static void holder_dump(const struct lksmith_holder *holder,
		char *buf, size_t *off, size_t buf_len)
{
	const char *prefix = "";
	int i;

	fwdprintf(buf, off, buf_len, "{name=%s, "
		"bt_frames=[", holder->name);
	for (i = 0; i < holder->bt_len; i++) {
		fwdprintf(buf, off, buf_len, "%s%s", prefix,
			  holder->bt_frames[i]);
		prefix = ", ";
	}
	fwdprintf(buf, off, buf_len, "]}");
}

/**
 * Create a lock holder.
 *
 * @param tls		The thread-local storage for the current thread.
 *
 * @return		The lock holder on success; NULL otherwise.
 */
static struct lksmith_holder* holder_create(struct lksmith_tls *tls)
{
	struct lksmith_holder *holder;
	int intercept, ret;

	holder = calloc(1, sizeof(*holder));
	if (!holder)
		return NULL;
	snprintf(holder->name, sizeof(holder->name), "%s", tls->name); 
	intercept = tls->intercept;
	tls->intercept = 0;
	ret = bt_frames_create(&tls->backtrace_scratch,
		&tls->backtrace_scratch_len, &holder->bt_frames);
	tls->intercept = intercept;
	if (ret < 0) {
		free(holder);
		return NULL;
	}
	holder->bt_len = ret;
	return holder;
}

/**
 * Free a lock holder structure
 *
 * @param holder        The lock holder
 */
static void holder_free(struct lksmith_holder *holder)
{
	bt_frames_free(holder->bt_frames);
	free(holder);
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
 * Add a lock holder to the lock.
 * Note: you must call this function with the info->lock held.
 *
 * @param lk		The lock data.
 * @param holder	The lock holder to add.
 */
static void lk_holder_add(struct lksmith_lock *lk,
			struct lksmith_holder *holder)
{
	holder->next = lk->holders;
	lk->holders = holder;
}

/**
 * Remove a lock holder from the lock.
 * Note: you must call this function with the info->lock held.
 *
 * @param lk		The lock data.
 * @param tls		The thread-local storage for the current thread.
 *
 * @return		0 on success; -ENOENT if the lock holder wasn't found.
 */
static int lk_holder_remove(struct lksmith_lock *lk,
			struct lksmith_tls *tls)
{
	struct lksmith_holder **holder, *next;

	/* By iterating forward through the list, we ensure that holders are
	 * taken out in the reverse order that they were put in (since we also
	 * insert to the head of the list.)  This is important when dealing
	 * with recursive locks, where one thread can take the same lock over
	 * and over. */
	holder = &lk->holders;
	while (*holder) {
		if (!strcmp(tls->name, (*holder)->name))
			break;
		holder = &(*holder)->next;
	}
	if (!holder)
		return -ENOENT;
	next = (*holder)->next;
	holder_free(*holder);
	*holder = next;
	return 0;
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
	struct lksmith_holder *holder;

	fwdprintf(buf, off, buf_len, "lk{ptr=%p, "
		"nlock=%"PRId64", recursive=%d, sleeper=%d,"
		"color=%"PRId64", before={", 
		(void*)lk->ptr, (uint64_t)lk->props.nlock,
		lk->props.recursive, lk->props.sleeper,
		lk->color);
	for (i = 0; i < lk->before_size; i++) {
		fwdprintf(buf, off, buf_len, "%s%p",
			  prefix, lk->before[i]);
		prefix = " ";
	}
	fwdprintf(buf, off, buf_len, "}, holders=[");
	prefix = "";
	holder = lk->holders;
	while (holder) {
		fwdprintf(buf, off, buf_len, "%s", prefix);
		holder_dump(holder, buf, off, buf_len);
		prefix = ", ";
		holder = holder->next;
	}
	fwdprintf(buf, off, buf_len, "]}");
}

static void lk_dump_to_stderr(struct lksmith_lock *lk)
{
	char buf[16384];
	size_t off = 0;

	lk_dump(lk, buf, &off, sizeof(buf));
	fputs(buf, stderr);
	fputs("\n", stderr);
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

static int lksmith_insert(const void *ptr, int recursive,
		int sleeper, struct lksmith_lock **lk)
{
	struct lksmith_lock *ak, *bk;
	ak = calloc(1, sizeof(*ak));
	if (!ak) {
		return ENOMEM;
	}
	ak->ptr = ptr;
	ak->props.recursive = !!recursive;
	ak->props.sleeper = !!sleeper;
	ak->holders = NULL;
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
int lksmith_optional_init(const void *ptr, int recursive, int sleeper)
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
	if (!tls->intercept)
		return 0;
	r_pthread_mutex_lock(&g_tree_lock);
	ret = lksmith_insert(ptr, recursive, sleeper, &lk);
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
	if (!tls->intercept)
		return 0;
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		/* This might not be an error, if we used
		 * PTHREAD_MUTEX_INITIALIZER and then never did anything else
		 * with the lock prior to destroying it. */
		ret = ENOENT;
		goto done_unlock;
	}
	if (lk->holders != NULL) {
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
	free(lk->before);
	free(lk);
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

static void lksmith_prelock_process_depends(struct lksmith_tls *tls,
			struct lksmith_lock *lk, const void *ptr)
{
	unsigned int i;
	const void *held;
	struct lksmith_lock *ak;

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
		if (ak == lk) {
			if (ak->props.recursive)
				continue;
			lksmith_error(EDEADLK, "lksmith_prelock(lock=%p, "
				"thread=%s): this thread already holds "
				"this lock, and it is not a recursive lock.\n",
				ptr, tls->name);
			continue;
		}
		if (lksmith_search(ak, ptr)) {
			lksmith_error(EDEADLK, "lksmith_prelock(lock=%p, "
				"thread=%s): lock inversion!  This lock "
				"should have been taken before lock %p, which "
				"this thread already holds.\n",
				ptr, tls->name, held);
			continue;
		}
		lk_add_before(lk, ak);
	}
}

/**
 * Returns true if lksmith_prelock should skip dependency processing.
 *
 * We search the current backtrace for any element that is in the ignore
 * list.
 */
static int should_skip_dependency_processing(struct lksmith_holder *holder)
{
	int bt_idx, ip_idx;
	char *match;

	for (bt_idx = 0; bt_idx < holder->bt_len; bt_idx++) {
		const char *frame = holder->bt_frames[bt_idx];
		match = bsearch(&frame, g_ignored_frames, g_num_ignored_frames,
				sizeof(char*), compare_strings);
		if (match) {
			return 1;
		}
		for (ip_idx = 0; ip_idx < g_num_ignored_frame_patterns;
			     ip_idx++) {
			if (!fnmatch(g_ignored_frame_patterns[ip_idx],
				     frame, 0)) {
				return 1;
			}
		}
	}
	return 0;
}

int lksmith_prelock(const void *ptr, int sleeper)
{
	struct lksmith_lock *lk;
	struct lksmith_tls *tls;
	int ret;
	struct lksmith_holder *holder = NULL;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_prelock(lock=%p): failed to "
			"allocate thread-local storage.\n", ptr);
		ret = ENOMEM;
		goto done;
	}
	if (!tls->intercept)
		return 0;
	holder = holder_create(tls);
	if (!holder) {
		lksmith_error(ENOMEM, "lksmith_prelock(lock=%p): failed to "
			"allocate lock holder data.\n", ptr);
		ret = ENOMEM;
		goto done;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		/* If the lock hasn't been explicitly initialized using
		 * lksmith_optional_init, we allow it to be recursive.
		 * It might have been statically initialized with
		 * PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP.
		 */
		ret = lksmith_insert(ptr, 1, sleeper, &lk);
		if (ret) {
			lksmith_error(ret, "lksmith_prelock(lock=%p, "
				"thread=%s): failed to allocate lock data: "
				"error %d: %s\n", ptr, tls->name, ret, terror(ret));
			goto done_unlock;
		}
	}
	if (!should_skip_dependency_processing(holder)) {
		lksmith_prelock_process_depends(tls, lk, ptr);
	}
	lk_holder_add(lk, holder);

	holder = NULL;
	ret = 0;
done_unlock:
	r_pthread_mutex_unlock(&g_tree_lock);
done:
	if (holder) {
		holder_free(holder);
	}
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
	if (!tls->intercept)
		return;
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		lksmith_error(EIO, "lksmith_postlock(lock=%p, thread=%s): "
			"logic error: prelock didn't create the lock data?\n",
			ptr, tls->name);
		goto done_unlock;
	}
	if (error) {
		lk_holder_remove(lk, tls);
		goto done_unlock;
	}
	if (lk->props.nlock < MAX_NLOCK) {
		lk->props.nlock++;
	}
	ret = tls_append_held(tls, ptr);
	if (ret) {
		lksmith_error(ENOMEM, "lksmith_postlock(lock=%p, "
			"thread=%s): failed to allocate space to store "
			"another thread id.\n", ptr, tls->name);
		goto done_unlock;
	}
	if (!lk->props.sleeper) {
		tls->num_spins++;
	} else if ((tls->num_spins > 0) && (!lk->props.spin_warn)) {
		lksmith_error(EWOULDBLOCK, "lksmith_postlock(lock=%p, "
			"thread=%s): performance problem: you are taking "
			"a sleeping lock while holding a spin lock.\n",
			ptr, tls->name);
		lk->props.spin_warn = 1;
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
	int sleeper;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_preunlock(lock=%p): failed "
			"to allocate thread-local storage.\n", ptr);
		return ENOMEM;
	}
	if (!tls->intercept)
		return 0;
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		lksmith_error(ENOENT, "lksmith_preunlock(lock=%p, thread=%s): "
			"attempted to unlock an unknown lock.\n", ptr, tls->name);
		r_pthread_mutex_unlock(&g_tree_lock);
		return ENOENT;
	}
	sleeper = lk->props.sleeper;
	r_pthread_mutex_unlock(&g_tree_lock);
	if (tls_contains_lid(tls, ptr) == 0) {
		lksmith_error(EPERM, "lksmith_preunlock(lock=%p, "
			"thread=%s): attempted to unlock a lock that this "
			"thread does not currently hold.\n", ptr, tls->name);
		return EPERM;
	}
	if (!sleeper) {
		tls->num_spins--;
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
	if (!tls->intercept)
		return;
	ret = tls_remove_held(tls, ptr);
	if (ret) {
		lksmith_error(EIO, "lksmith_postunlock(lock=%p, "
			"thread=%s): logic error: preunlock check told us "
			"we had the lock, but we don't?\n", ptr, tls->name);
		return;
	}
	r_pthread_mutex_lock(&g_tree_lock);
	lk = lksmith_find(ptr);
	if (!lk) {
		lksmith_error(EIO, "lksmith_preunlock(lock=%p, thread=%s): "
			"logic error: attempted to unlock an unknown lock.\n",
			ptr, tls->name);
		r_pthread_mutex_unlock(&g_tree_lock);
		return;
	}
	ret = lk_holder_remove(lk, tls);
	if (ret) {
		lksmith_error(EIO, "lksmith_preunlock(lock=%p, thread=%s): "
			"logic error: failed to find backtrace for this "
			"thread in the list of stored backtraces for this "
			"lock (error %d).\n", ptr, tls->name, ret);
		r_pthread_mutex_unlock(&g_tree_lock);
		return;
	}
	r_pthread_mutex_unlock(&g_tree_lock);
}

int lksmith_check_locked(const void *ptr)
{
	struct lksmith_tls *tls;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_check_locked(lock=%p): failed "
			"to allocate thread-local storage.\n", ptr);
		return ENOMEM;
	}
	if (!tls->intercept)
		return 0;
	return tls_contains_lid(tls, ptr) ? 0 : -1;
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

int lksmith_get_ignored_frames(char *** ignored, int *num_ignored)
{
	struct lksmith_tls *tls = get_or_create_tls();

	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_get_ignored_frames(): failed "
			"to allocate thread-local storage.\n");
		return ENOMEM;
	}
	*ignored = g_ignored_frames;
	*num_ignored = g_num_ignored_frames;
	return 0;
}

int lksmith_get_ignored_frame_patterns(char *** ignored, int *num_ignored)
{
	struct lksmith_tls *tls = get_or_create_tls();

	if (!tls) {
		lksmith_error(ENOMEM, "lksmith_get_ignored_frame_patterns(): "
			"failed to allocate thread-local storage.\n");
		return ENOMEM;
	}
	*ignored = g_ignored_frame_patterns;
	*num_ignored = g_num_ignored_frame_patterns;
	return 0;
}

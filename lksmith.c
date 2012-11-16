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
#include "lksmith.h"
#include "platform.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/******************************************************************
 *  Locksmith private data structures
 *****************************************************************/
typedef uint32_t lid_t;

#define INVAL_LOCK_ID	0xffffffffLU
#define MAX_LOCK_ID	0xfffffffeLU

struct lksmith_lock_data {
	/** The name of this lock. */
	char name[LKSMITH_LOCK_NAME_MAX];
	/** The number of times this mutex has been locked. */
	uint64_t nlock;
	/** lksmith-assigned ID. */
	lid_t lid;
	/** Size of the before list. */
	int before_size;
	/** Sorted list of locks that have been taken before this lock. */
	lid_t *before;
	/** Size of the after list. */
	int after_size;
	/** Sorted list of locks that have been taken after this lock. */
	lid_t *after;
};

struct lksmith_tls {
	/** The name of this thread. */
	char name[LKSMITH_THREAD_NAME_MAX];
	/** Size of the held list. */
	unsigned int num_held;
	/** List of locks held */
	lid_t *held;
};

/******************************************************************
 *  Locksmith globals
 *****************************************************************/
/**
 * The key that allows us to retrieve thread-local data.
 */
static pthread_key_t g_tls_key;

/**
 * Nonzero if we have already initialized g_tls_key.
 */
static int g_tls_key_init;

/**
 * Protects g_tls_key.
 */
static pthread_mutex_t g_tls_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Locksmith error callback to use.  Protected by g_internal_lock.
 */
static lksmith_error_cb_t g_error_cb = lksmith_error_cb_to_stderr;

/**
 * Protects g_error_cb.  This is not held while the callback is in progress,
 * though.
 */
static pthread_mutex_t g_error_cb_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Array of locksmith_lock_data structures.
 * Indexed by lock data id.  Protected by g_internal_lock.
 */
static struct lksmith_lock_info **g_lock_info;

/**
 * Length of g_lock_info.  Protected by g_internal_lock.
 */
static lid_t g_lock_info_len;

/**
 * Protects internal Locksmith data structures.
 */
static pthread_mutex_t g_lock_info_lock = PTHREAD_MUTEX_INITIALIZER;

/******************************************************************
 *  Prototypes
 *****************************************************************/
static struct lksmith_lock_info* lksmith_lookup_info_unlocked(lid_t lid);
static struct lksmith_lock_info* lksmith_lookup_info(lid_t lid);

/******************************************************************
 *  Error handling
 *****************************************************************/
/**
 * Log an error message using the global error callback.
 *
 * This function must be called without g_error_cb_lock held.
 *
 * @param err		The error code.
 * @param fmt		printf-style erorr code.
 * @param ...		printf-style arguments.
 */
static void lksmith_print_error(int err, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void lksmith_print_error(int err, const char *fmt, ...)
{
	va_list ap;
	char buf[2048];
	lksmith_error_cb_t error_cb;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	pthread_mutex_lock(&g_error_cb_lock);
	error_cb = g_error_cb;
	pthread_mutex_unlock(&g_error_cb_lock);
	error_cb(err, buf);
}

/**
 * Look up the error message associated with a POSIX error code.
 *
 * This function is thread-safe.
 *
 * @param err		The POSIX error code (should be non-negative)
 *
 * @return		The error message.  This is a statically allocated
 *			string. 
 */
static const char *terror(int err)
{
	if ((err < 0) || (err >= sys_nerr)) {
		return "unknown error";
	}
	return sys_errlist[err];
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
	pthread_mutex_lock(&g_tls_lock);
	if (!g_tls_key_init) {
		ret = pthread_key_create(&g_tls_key, lksmith_tls_destroy);
		if (ret == 0) {
			g_tls_key_init = 1;
		}
	}
	pthread_mutex_unlock(&g_tls_lock);
	if (ret != 0) {
		lksmith_print_error(ENOMEM,
			"get_or_create_tls(): pthread_key_create failed "
			"with error code %d: %s", ret, terror(ret));
		return NULL;
	}
#ifndef HAVE_IMPROVED_TLS
	tls = pthread_getspecific(g_tls_key);
	if (tls) {
		return tls;
	}
#endif
	tls = calloc(1, sizeof(*tls));
	if (!tls) {
		lksmith_print_error(ENOMEM,
			"get_or_create_tls(): failed to allocate "
			"memory for thread-local storage.");
		return NULL;
	}
	platform_create_thread_name(tls->name, LKSMITH_THREAD_NAME_MAX);
	ret = pthread_setspecific(g_tls_key, tls);
	if (ret) {
		free(tls->held);
		free(tls);
		lksmith_print_error(ENOMEM,
			"get_or_create_tls(): pthread_setspecific "
			"failed with error %d: %s", ret, terror(ret));
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
 * @param lid		the lock ID to add to the list.
 *
 * @return		0 on success; ENOMEM if we ran out of memory.
 */
static int tls_append_lid(struct lksmith_tls *tls, lid_t lid)
{
	lid_t *held;

	held = realloc(tls->held, sizeof(lid_t) * (tls->num_held + 1));
	if (!held)
		return ENOMEM;
	tls->held = held;
	held[tls->num_held++] = lid;
	return 0;
}

/**
 * Remove a lock ID from the list of lock IDs we hold.
 *
 * @param tls		The thread-local data.
 * @param lid		the lock ID to add to the list.
 *
 * @return		0 on success; ENOENT if we are not holding the
 *			lock ID.
 */
static int tls_remove_lid(struct lksmith_tls *tls, lid_t lid)
{
	signed int i;
	lid_t *held;

	for (i = tls->num_held - 1; i > 0; i--) {
		if (tls->held[i] == lid)
			break;
	}
	if (i < 0)
		return ENOENT;
	memmove(&tls->held[i - 1], &tls->held[i], 
		sizeof(lid_t) * (tls->num_held - i));
	held = realloc(tls->held, sizeof(lid_t) * (--tls->num_held));
	if (held || (tls->num_held == 0)) {
		tls->held = held;
	}
	return 0;
}

/**
 * Determine if we are holding a lock.
 *
 * @param tls		The thread-local data.
 * @param lid		The lock ID to find.
 *
 * @return		1 if we hold the lock; 0 otherwise.
 */
static int tls_contains_lid(struct lksmith_tls *tls, lid_t lid)
{
	unsigned int i;

	for (i = 0; i < tls->num_held; i++) {
		if (tls->held[i] == lid)
			return 1;
	}
	return 0;
}

/******************************************************************
 *  Lock data functions
 *****************************************************************/
/**
 * Compare one lock ID with another.
 *
 * @param a		Pointer to the first lock ID.
 * @param b		Pointer to the second lock ID.
 *
 * @return		-1 if *a < *b; 0 if *a == *b; 1 if *a > *b.
 */
static int compare_lid(const void * __restrict a, const void * __restrict b)
	__attribute__((const));

static int compare_lid(const void * __restrict va, const void * __restrict vb)
{
	lid_t a = *(lid_t*)va;
	lid_t b = *(lid_t*)vb;
	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

/**
 * Discover if a lock is in the before set of another lock.
 *
 * @param ldata		The lock data.
 * @param aid		The lock ID to check.
 *
 * @return		0 if the lock is not in the set; 1 if it is.
 */
static int ldata_in_before(struct lksmith_lock_data *ldata, lid_t aid)
{
	return !!bsearch(&aid, ldata->before, ldata->before_size, sizeof(lid_t),
		compare_lid);
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
static int ldata_add_sorted(lid_t * __restrict * __restrict arr,
			    int * __restrict num, lid_t lid)
{
	int i;
	lid_t *narr;

	for (i = 0; i < *num; i++) {
		if ((*arr)[i] == lid)
			return 0;
		else if ((*arr)[i] > lid)
			break;
	}
	narr = realloc(*arr, sizeof(lid_t) * (*num + 1));
	if (!narr)
		return ENOMEM;
	*arr = narr;
	memmove(&narr[i + 1], &narr[i], sizeof(lid_t) * (*num - i));
	narr[i] = lid;
	return 0;
}

/**
 * Remove an element from a sorted array.
 * We assume it appears only once in that array.
 *
 * @param arr		(inout) the array
 * @param num		(inout) the array length
 * @param lid		The lock ID to remove.
 */
static void ldata_remove_sorted(lid_t * __restrict * __restrict arr,
			int * __restrict num, lid_t lid)
{
	int i;
	lid_t *narr;

	for (i = 0; i < *num; i++) {
		if ((*arr)[i] == lid)
			break;
		else if ((*arr)[i] > lid)
			return;
	}
	memmove(&(*arr)[i - 1], &(*arr)[i], sizeof(lid_t) * (*num - i - 1));
	narr = realloc(*arr, sizeof(lid_t) * (--*num));
	if (narr || (*num == 0))
		*arr = narr;
}

/**
 * Add a lock to the 'after' set of this lock data.
 *
 * @param ldata		The lock data.
 * @param lid		The lock ID to add.
 *
 * @return		0 on success; ENOMEM if we ran out of memory.
 */
static int ldata_add_after(struct lksmith_lock_data *ldata, lid_t lid)
{
	return ldata_add_sorted(&ldata->after, &ldata->after_size, lid);
}

/**
 * Add a lock to the 'before' set of this lock data.
 *
 * @param ldata		The lock data.
 * @param lid		The lock ID to add.
 *
 * @return		0 on success; ENOMEM if we ran out of memory.
 */
static int ldata_add_before(struct lksmith_lock_data *ldata, lid_t lid)
{
	return ldata_add_sorted(&ldata->before, &ldata->before_size, lid);
}

/**
 * Remove a lock from the 'before' set of this lock data.
 *
 * @param ldata		The lock data.
 * @param lid		The lock ID to remove.
 */
static void ldata_remove_after(struct lksmith_lock_data *ldata, lid_t lid)
{
	ldata_remove_sorted(&ldata->after, &ldata->after_size, lid);
}

/**
 * Remove a lock from the 'after' set of this lock data.
 *
 * @param ldata		The lock data.
 * @param lid		The lock ID to remove.
 */
static void ldata_remove_before(struct lksmith_lock_data *ldata, lid_t lid)
{
	ldata_remove_sorted(&ldata->before, &ldata->before_size, lid);
}

/**
 * Scan the g_lock_info array for the next available lock ID.
 *
 * Note: you must call this function with the g_lock_info_lock held.
 *
 * @return		INVAL_LOCK_ID if a memory allocation failed; the new
 *			lock ID otherwise.
 */
static lid_t lksmith_alloc_lid(void)
{
	lid_t lid;
	struct lksmith_lock_info **new_lock_info;

	for (lid = 0; lid < g_lock_info_len; lid++) {
		if (!g_lock_info[lid])
			return lid;
	}
	if (g_lock_info_len == MAX_LOCK_ID)
		return INVAL_LOCK_ID;
	new_lock_info = realloc(g_lock_info,
		sizeof(struct lksmith_lock_info*) * (g_lock_info_len + 1));
	if (!new_lock_info)
		return INVAL_LOCK_ID;
	g_lock_info = new_lock_info;
	return g_lock_info_len++;
}

/**
 * Release a lock ID from the g_lock_info array.
 *
 * Note: you must call this function with the g_lock_info_lock held.
 */
static void lksmith_release_lid(lid_t lid)
{
	struct lksmith_lock_info **new_lock_info;

	//printf("removing %d; g_lock_info_len = %d\n", lid, g_lock_info_len);

	if (g_lock_info_len <= lid)
		return;
	else if (g_lock_info_len < (lid + 1)) {
		g_lock_info[lid] = NULL;
		return;
	}
	new_lock_info = realloc(g_lock_info,
		sizeof(struct lksmith_lock_info*) * (--g_lock_info_len));
	if (new_lock_info || (g_lock_info_len == 0))
		g_lock_info = new_lock_info;
	//printf("g_lock_info_len = %d\n", g_lock_info_len);
}

/**
 * Initialize a lock info structure.
 *
 * This does not allocate the memory for the info structure, or initialize the
 * mutex.  However, it initializes everything else.
 *
 * @param name		The name of the lock, or NULL to get the default.
 * @param info		The info to initialize.
 */
int linfo_init(const char * __restrict name,
		struct lksmith_lock_info * __restrict info)
{
	struct lksmith_lock_data *ldata = NULL;
	int ret;
	lid_t lid = INVAL_LOCK_ID;

	pthread_mutex_lock(&g_lock_info_lock);
	lid = lksmith_alloc_lid();
	if (lid == INVAL_LOCK_ID) {
		ret = ENOMEM;
		goto error;
	}
	ldata = calloc(1, sizeof(*ldata));
	if (!ldata) {
		ret = ENOMEM;
		goto error;
	}
	ldata->lid = lid;
	if (name) {
		snprintf(ldata->name, LKSMITH_LOCK_NAME_MAX, "%s", name);
	} else {
		snprintf(ldata->name, LKSMITH_LOCK_NAME_MAX, "lock %d",
			ldata->lid);
	}
	info->data = ldata;
	g_lock_info[lid] = info;
	pthread_mutex_unlock(&g_lock_info_lock);
	return 0;

error:
	if (lid != INVAL_LOCK_ID) {
		lksmith_release_lid(lid);
	}
	pthread_mutex_unlock(&g_lock_info_lock);
	free(ldata);
	return ret;
}

/******************************************************************
 *  API functions
 *****************************************************************/
int lksmith_mutex_init(const char * __restrict name,
		struct lksmith_mutex *mutex,
		__const pthread_mutexattr_t *attr)
{
	int ret;

	ret = pthread_mutex_init(&mutex->info.lock, NULL);
	if (ret) {
		lksmith_print_error(ret,
			"lksmith_mutex_init(lock=%s): pthread_mutex_init "
			"of our internal lock failed with error code %d: "
			"%s", (name ? name : "(none)"), ret, terror(ret));
		goto error;
	}
	ret = pthread_mutex_init(&mutex->raw, attr);
	if (ret) {
		lksmith_print_error(ret,
			"lksmith_mutex_init(%s): pthread_mutex_init failed "
			"of the raw lock failed with error code %d: %s",
			(name ? name : "(none)"), ret, terror(ret));
		goto error_destroy_internal_mutex;
	}
	ret = linfo_init(name, &mutex->info);
	if (ret) {
		lksmith_print_error(ret,
			"lksmith_mutex_init(lock=%s): linfo_init failed "
			"with error code %d: %s", (name ? name : "(none)"),
			ret, terror(ret));
		goto error_destroy_mutex;
	}
	return 0;

error_destroy_mutex:
	pthread_mutex_destroy(&mutex->raw);
error_destroy_internal_mutex:
	pthread_mutex_destroy(&mutex->info.lock);
error:
	return ret;
}

int lksmith_spin_init(const char * __restrict name,
		struct lksmith_spin *spin, int pshared)
{
	int ret;

	if (pshared == PTHREAD_PROCESS_SHARED) {
		ret = ENOTSUP;
		lksmith_print_error(ret,
			"lksmith_spin_init(lock=%s): Locksmith doesn't yet "
			"support cross-process spin-locks.", 
			(name ? name : "(none)"));
		goto error;
	}
	ret = pthread_mutex_init(&spin->info.lock, NULL);
	if (ret) {
		lksmith_print_error(ret,
			"lksmith_spin_init(lock=%s): pthread_mutex_init "
			"of our internal lock failed with error code %d: "
			"%s", (name ? name : "(none)"), ret, terror(ret));
		goto error;
	}
	ret = pthread_spin_init(&spin->raw, pshared);
	if (ret) {
		lksmith_print_error(ret,
			"lksmith_spin_init(%s): pthread_mutex_init failed "
			"of the raw lock failed with error code %d: %s",
			(name ? name : "(none)"), ret, terror(ret));
		goto error_destroy_internal_mutex;
	}
	ret = linfo_init(name, &spin->info);
	if (ret) {
		lksmith_print_error(ret,
			"lksmith_spin_init(lock=%s): linfo_init failed "
			"with error code %d: %s", (name ? name : "(none)"),
			ret, terror(ret));
		goto error_destroy_spin;
	}
	return 0;

error_destroy_spin:
	pthread_spin_destroy(&spin->raw);
error_destroy_internal_mutex:
	pthread_mutex_destroy(&spin->info.lock);
error:
	return ret;
}

static int ldata_destroy(struct lksmith_lock_data *ldata,
		struct lksmith_tls *tls)
{
	lid_t lid;
	struct lksmith_lock_info *ainfo;

	if (tls_contains_lid(tls, ldata->lid)) {
		lksmith_print_error(EINVAL,
			"linfo_destroy(thread=%s, lock=%s): tried to "
			"destroy a lock that this thread currently holds.  "
			"Destroying a lock that is currently held causes "
			"undefined behavior.", tls->name, ldata->name);
		return EINVAL;
	}
	// All references to this lock should be gone.
	// The only exception is if a thread is still holding this mutex
	// while it was destroyed.  This will probably lead to us spewing
	// warnings, although that can't be guaranteed-- we do re-use IDs.
	// TODO: investigate using unique 64-bit LIDs.  Probably would want to
	// use the low 32 bits to represent the array position, just as now,
	// and use the top 32 bits as an incrementing counter.  That would
	// allow us to warn in the destroy-while-holding case without fail.

	// Now let's get rid of all references to this lock ID in other locks.
	// TODO: avoid holding g_lock_info_lock for so long?
	// It's tricky since if we didn't hold the lock for this whole time,
	// another thread could copy around the ID we're trying to destroy,
	// and basically undo our work.
	pthread_mutex_lock(&g_lock_info_lock);
	for (lid = 0; lid < g_lock_info_len; lid++) {
		if (lid == ldata->lid)
			continue;
		ainfo = lksmith_lookup_info_unlocked(lid);
		if (!ainfo)
			continue;
		ldata_remove_after(ainfo->data, lid);
		ldata_remove_before(ainfo->data, lid);
	}
	// Remove this lock from the global list.
	lksmith_release_lid(ldata->lid);
	pthread_mutex_unlock(&g_lock_info_lock);

	// Finally, let's destroy the internal data structures.
	free(ldata->before);
	free(ldata->after);
	free(ldata);
	return 0;
}

static void linfo_destroy(struct lksmith_lock_info *__restrict info)
{
	int ret;
	struct lksmith_lock_data *ldata;
	struct lksmith_tls *tls;

	ldata = info->data;
	tls = get_or_create_tls();
	if (!tls) {
		lksmith_print_error(ENOMEM, "linfo_destroy(lock=%s): "
			"failed to allocate thread-local storage.",
			(ldata ? ldata->name : "(none)"));
		return;
	}
	if (ldata) {
		if (ldata_destroy(ldata, tls))
			return;
	}
	ret = pthread_mutex_destroy(&info->lock);
	if (ret) {
		lksmith_print_error(ret, "linfo_destroy(lock=%s): "
			"pthread_mutex_destroy of mutex->info->lock "
			"returned error %d: %s",
			info->data->name, ret, terror(ret));
	}
}

int lksmith_mutex_destroy(struct lksmith_mutex *mutex)
{
	char name[LKSMITH_LOCK_NAME_MAX];
	int ret;

	snprintf(name, sizeof(name), "%s", mutex->info.data->name);
	linfo_destroy(&mutex->info);
	ret = pthread_mutex_destroy(&mutex->raw);
	if (ret) {
		lksmith_print_error(ret, "lksmith_mutex_destroy(lock=%s): "
			"pthread_mutex_destroy of mutex->raw returned error "
			"%d: %s", name, ret, terror(ret));
		return ret;
	}
	return 0;
}

int lksmith_spin_destroy(struct lksmith_spin *spin)
{
	char name[LKSMITH_LOCK_NAME_MAX];
	int ret;

	snprintf(name, sizeof(name), "%s", spin->info.data->name);
	linfo_destroy(&spin->info);
	ret = pthread_spin_destroy(&spin->raw);
	if (ret) {
		lksmith_print_error(ret, "lksmith_spin_destroy(lock=%s): "
			"pthread_spin_destroy of spin->raw returned error "
			"%d: %s", name, ret, terror(ret));
		return ret;
	}
	return 0;
}

static struct lksmith_lock_info* lksmith_lookup_info_unlocked(lid_t lid)
{
	struct lksmith_lock_info *info;

	if (lid >= g_lock_info_len)
		return NULL;
	info = g_lock_info[lid];
	pthread_mutex_lock(&info->lock);
	return info;
}

static struct lksmith_lock_info* lksmith_lookup_info(lid_t lid)
{
	struct lksmith_lock_info *info;

	pthread_mutex_lock(&g_lock_info_lock);
	info = lksmith_lookup_info_unlocked(lid);
	pthread_mutex_unlock(&g_lock_info_lock);
	return info;
}

static void lksmith_unlock_info(struct lksmith_lock_info *info)
{
	pthread_mutex_unlock(&info->lock);
}

/**
 * Given a lock ID, get the name of the lock.
 *
 * @param lid		The lock ID
 * @param name		(out param) The name of the lock.  This buffer must
 *			have length LKSMITH_LOCK_NAME_MAX.  If the lock ID
 *			can't be found, this will be set to "unknown [lock-id]"
 */
static void lid_get_name(lid_t lid, char *lname)
{
	struct lksmith_lock_info *info;

	info = lksmith_lookup_info(lid);
	if (!info) {
		snprintf(lname, LKSMITH_LOCK_NAME_MAX, "unknown %d", lid);
		return;
	}
	snprintf(lname, LKSMITH_LOCK_NAME_MAX, "%s", info->data->name);
	lksmith_unlock_info(info);
}

/**
 * Given a lock ID, add a lock to its 'after' set.
 *
 * @param lid		The lock ID
 * @param aid		The lock ID to add to the after set.
 *
 * @return		0 on success; ENOMEM if we ran out of memory;
 * 			EDEADLK if aid is also in our 'before' set.
 */
static int lid_add_after(lid_t lid, lid_t aid)
{
	struct lksmith_lock_info *info;
	char name[LKSMITH_LOCK_NAME_MAX];
	int ret;

	info = lksmith_lookup_info(lid);
	if (!info) {
		lksmith_print_error(EINVAL,
			"lid_add_after(%d): no lock found for this ID.", lid);
		return EINVAL;
	}
	if (ldata_in_before(info->data, aid)) {
		snprintf(name, sizeof(name), "%s", info->data->name);
		lksmith_unlock_info(info);
		lksmith_print_error(EDEADLK,
			"lid_add_after(%d): lock order inversion!  %s is "
			"supposed to be taken before %s.",
			lid, name, name);
		return EDEADLK;
	}
	ret = ldata_add_after(info->data, aid);
	lksmith_unlock_info(info);
	return ret;
}

uint32_t lksmith_get_version(void)
{
	return LKSMITH_API_VERSION;
}

int lksmith_verion_to_str(uint32_t ver, char *str, size_t str_len)
{
	int res;
	
	res = snprintf(str, str_len, "%d.%d",
		((ver >> 16) & 0xffff), (ver & 0xffff));
	if (res < 0) {
		return EIO;
	}
	if ((size_t)res >= str_len) {
		return ENAMETOOLONG;
	}
	return 0;
}

void lksmith_set_error_cb(lksmith_error_cb_t fn)
{
	pthread_mutex_lock(&g_error_cb_lock);
	g_error_cb = fn;
	pthread_mutex_unlock(&g_error_cb_lock);
}

void lksmith_error_cb_to_stderr(int code, const char *__restrict msg)
{
	fprintf(stderr, "LOCKSMITH ERROR %d: %s\n", code, msg);
}

/**
 * Take a lock, internally.
 *
 * @param ldata		The lock to take.
 * @param tls		The thread-local data for this thread.
 *
 * @return		0 on success; error code otherwise.
 */
int ldata_lock(struct lksmith_lock_info *info, struct lksmith_tls *tls)
{
	lid_t held;
	unsigned int i;
	int ret;
	struct lksmith_lock_data *ldata;
	char name[LKSMITH_LOCK_NAME_MAX];

	if (!info->data) {
		// In this case, the Locksmith mutex was initialized by
		// LKSMITH_MUTEX_INITIALIZER.  This sets up 
		// mutex->info->lock, but not mutex->info.data.
		// So let's set it up here.
		ret = linfo_init(NULL, info);
		if (ret)
			return ret;
	}
	ldata = info->data;
	/* Add this lock ID to the list of locks we're holding. */
	ret = tls_append_lid(tls, ldata->lid);
	if (ret) {
		lksmith_print_error(ENOMEM,
			"ldata_lock(lock=%s, thread=%s): failed to allocate "
			"space to store another thread id.",
			ldata->name, tls->name);
		return ENOMEM;
	}
	pthread_mutex_lock(&info->lock);
	for (i = 0; i < tls->num_held; i++) {
		ret = ldata_add_before(ldata, tls->held[i]);
		if (ret) {
			lksmith_print_error(ret, "ldata_lock("
				"lock=%s, thread=%s): error adding lid "
				"%d to the before set: error %d: %s.",
				ldata->name, tls->name, tls->held[i], 
				ret, terror(ret));
		}
	}
	ldata->nlock++;
	pthread_mutex_unlock(&info->lock);

	/* Add ldata->lid to the 'after' set of all the locks we're
	 * currently holding.  Unlike in the previous loop, we don't hold
	 * ldata->lock while doing this.  This is to avoid deadlocks while
	 * taking the locks for the other lksmith_lock_data objects.
	 */
	for (i = 0; i < tls->num_held; i++) {
		held = tls->held[i];
		if (held == ldata->lid)
			continue;
		ret = lid_add_after(held, ldata->lid);
		if (ret == EDEADLK) {
			lid_get_name(held, name);
			lksmith_print_error(EDEADLK,
				"ldata_lock(lock=%s, thread=%s): lock order "
				"inversion.  This lock is supposed to be "
				"taken before %s.",
				ldata->name, tls->name, name);
		} else if (ret == ENOMEM) {
			lksmith_print_error(ret, "ldata_lock(%s): "
				"out of memory.", ldata->name);
		} else {
			lksmith_print_error(ret, "ldata_lock(%s): "
				"unknown error %d.", ldata->name, ret);
		}
	}
	return 0;
}

int ldata_unlock(struct lksmith_lock_data *ldata, struct lksmith_tls *tls)
{
	int ret;

	ret = tls_remove_lid(tls, ldata->lid);
	if (ret) {
		lksmith_print_error(EINVAL,
			"ldata_ulock(lock=%s, thread=%s): you tried to "
			"unlock a lock you do not hold!",
			ldata->name, tls->name);
		return EINVAL;
	}
	return 0;
}

static int lksmith_mutex_lock_internal(struct lksmith_mutex *mutex,
		int trylock, const struct timespec *ts)
{
	int ret;
	struct lksmith_tls *tls;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_print_error(ENOMEM,
			"lksmith_mutex_lock_internal(lock=%s): "
			"failed to allocate thread-local storage.", 
			(mutex->info.data ? mutex->info.data->name : "(none)"));
		return ENOMEM;
	}
	ret = ldata_lock(&mutex->info, tls);
	if (ret)
		return ret;
	if (trylock) {
		ret = pthread_mutex_trylock(&mutex->raw);
	} else if (ts) {
		ret = pthread_mutex_timedlock(&mutex->raw, ts);
	} else {
		ret = pthread_mutex_lock(&mutex->raw);
	}
	if (ret) {
		ldata_unlock(mutex->info.data, tls);
		return ret;
	}
	return 0;
}

int lksmith_mutex_lock(struct lksmith_mutex *mutex)
{
	return lksmith_mutex_lock_internal(mutex, 0, NULL);
}

int lksmith_mutex_trylock(struct lksmith_mutex *mutex)
{
	return lksmith_mutex_lock_internal(mutex, 1, NULL);
}

int lksmith_mutex_timedlock(struct lksmith_mutex *mutex,
			    const struct timespec *ts)
{
	return lksmith_mutex_lock_internal(mutex, 0, ts);
}

static int lksmith_spin_lock_internal(struct lksmith_spin *spin,
		int trylock)
{
	int ret;
	struct lksmith_tls *tls;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_print_error(ENOMEM,
			"lksmith_spin_lock_internal(%s): failed to allocate "
			"thread-local storage.",
			(spin->info.data ? spin->info.data->name : "(none)"));
		return ENOMEM;
	}
	ret = ldata_lock(&spin->info, tls);
	if (ret)
		return ret;
	if (trylock) {
		ret = pthread_spin_trylock(&spin->raw);
	} else {
		ret = pthread_spin_lock(&spin->raw);
	}
	if (ret) {
		ldata_unlock(spin->info.data, tls);
		return ret;
	}
	return 0;
}

// TODO: warn on taking mutex when holding spin

int lksmith_spin_lock(struct lksmith_spin *spin)
{
	return lksmith_spin_lock_internal(spin, 0);
}

int lksmith_spin_trylock(struct lksmith_spin *spin)
{
	return lksmith_spin_lock_internal(spin, 1);
}

int lksmith_mutex_unlock(struct lksmith_mutex *mutex)
{
	struct lksmith_tls *tls;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_print_error(ENOMEM,
			"lksmith_mutex_unlock(%s): failed to allocate "
			"thread-local storage.", mutex->info.data->name);
		return ENOMEM;
	}
	return ldata_unlock(mutex->info.data, tls);
}

int lksmith_spin_unlock (struct lksmith_spin *spin)
{
	struct lksmith_tls *tls;

	tls = get_or_create_tls();
	if (!tls) {
		lksmith_print_error(ENOMEM,
			"lksmith_spin_unlock(lock=%s): failed to allocate "
			"thread-local storage.", spin->info.data->name);
		return ENOMEM;
	}
	return ldata_unlock(spin->info.data, tls);
}

int lksmith_set_thread_name(const char *const name)
{
	struct lksmith_tls *tls = get_or_create_tls();

	if (!tls) {
		lksmith_print_error(ENOMEM,
			"lksmith_set_thread_name(thread=%s): failed "
			"to allocate thread-local storage.", name);
		return ENOMEM;
	}
	snprintf(tls->name, LKSMITH_THREAD_NAME_MAX, "%s", name);
	return 0;
}

const char* lksmith_get_thread_name(void)
{
	struct lksmith_tls *tls = get_or_create_tls();

	if (!tls) {
		lksmith_print_error(ENOMEM,
			"lksmith_get_thread_name(): failed "
			"to allocate thread-local storage.");
		return NULL;
	}
	return tls->name;
}

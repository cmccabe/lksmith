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

#ifndef LKSMITH_H
#define LKSMITH_H

#include <pthread.h> /* for pthread_mutex_t, etc. */
#include <stdint.h> /* for uint32_t, etc. */
#include <unistd.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
 *  Locksmith version
 *****************************************************************/
/** The current Locksmith version.
 *
 * Format: the first 16 bits are the major version; the second 16 bits are the
 * minor version.  Changes in the major version break the ABI; minor version
 * changes may add to the ABI, but they never break it.
 *
 * Use lksmith_verion_to_str to get a human-readable version.
 */
#define LKSMITH_API_VERSION 0x0001000

/******************************************************************
 *  Locksmith data structures
 *****************************************************************/
struct lksmith_lock_data;

/**
 * Maximum length of a lock name, including the terminating NULL byte.
 */
#define LKSMITH_LOCK_NAME_MAX 16

/**
 * Maximum length of a thread name, including the terminating NULL byte.
 */
#define LKSMITH_THREAD_NAME_MAX 16

struct lksmith_lock_info {
	/** Private lksmith data, or NULL if the lock hasn't been initialized
	 * yet.  This field must always be accessed using atomic
	 * operations. */
	struct lksmith_lock_data *data;
};

struct lksmith_mutex_t {
	/** The raw pthread lock. */
	pthread_mutex_t raw;
	/** lksmith data. */
	struct lksmith_lock_info info;
};

struct lksmith_spinlock_t {
	/** The raw spinlock. */
	pthread_spinlock_t raw;
	/** lksmith data. */
	struct lksmith_lock_info info;
};

/******************************************************************
 *  Locksmith initializers
 *****************************************************************/
/** Static lksmith mutex initializer.
 *
 * Similar to PTHREAD_MUTEX_INITIALIZER, this can be used to initialize a mutex
 * as part of the mutex declaration.
 *
 * Note: the name string will be deep-copied.
 *	The copy will be truncated to LKSMITH_LOCK_NAME_MAX bytes
 *	long, including the terminating null.
 */
#define LKSMITH_MUTEX_INITIALIZER(name) \
	{ PTHREAD_MUTEX_INITIALIZER, { name, 0, 0, 0 } } 

/******************************************************************
 *  Locksmith error codes
 *****************************************************************/
/**
 * There was an out-of-memory error.
 */
#define LKSMITH_ERROR_OOM 1

/**
 * A pthread_lock or pthread_spin_lock operation did not succeed.
 */
#define LKSMITH_ERROR_LOCK_OPER_FAILED 2

/**
 * Bad lock ordering was detected.
 * This may cause deadlocks in the future if it is not corrected.
 */
#define LKSMITH_ERROR_BAD_LOCK_ORDERING_DETECTED 3

/**
 * There was an attempt to destroy a lock while it was still in use.
 */
#define LKSMITH_ERROR_DESTROY_WHILE_IN_USE 4

/**
 * There was an attempt to destroy a lock more than once.
 */
#define LKSMITH_ERROR_MULTIPLE_DESTROY 5

/**
 * There was an attempt to re-initialize a lock while it was still in use.
 */
#define LKSMITH_ERROR_CREATE_WHILE_IN_USE 6

/******************************************************************
 *  Locksmith API
 *****************************************************************/
/**
 * Get the current Locksmith API version
 *
 * @return		The current locksmith API version
 */
uint32_t lksmith_get_version(void);

/**
 * Convert the current Locksmith API version to a human-readable string.
 * This function is thread-safe.
 *
 * @param ver		A locksmith API version
 * @param str		(out parameter) buffer to place the version string
 * @param str_len	Length of the str buffer
 *
 * @return		0 on success; -ENAMETOOLONG if the provided buffer
 *			length was too short; -EIO if another failure happened.
 */
int lksmith_verion_to_str(uint32_t ver, char *str, size_t str_len);

/**
 * The type signature for a Locksmith error reporting callback.
 *
 * @param code		The numeric Locksmith error code (see LKSMITH_ERROR_).
 * @param msg		The human-readable error string.
 */
void (*lksmith_error_cb_t)(int code, const char * __restrict msg);

/**
 * Set the callback that will be called to deliver errors or warnings.
 * This function is thread-safe.
 *
 * @param fn		The callback.  This callback will be invoked with no
 *			internal locksmith locks held.  This callback might be
 *			invoked concurrently from multiple different threads.
 */
void lksmith_set_error_cb(lksmith_error_cb_t fn);

/**
 * A simple error callback which prints a message to stderr.
 * This is the default error callback.
 * This function is thread-safe.
 *
 * @param code		The numeric Locksmith error code to be printed.
 * @param msg		The human-readable error string to be printed.
 */
void lksmith_error_cb_to_stderr(int code, const char *__restrict msg);

/**
 * Initializes a Locksmith-protected mutex.
 *
 * This function is thread-safe.
 *
 * @param name		Human-readable name to use to identify this lock.
 * 			It will be deep-copied.  The copy will be truncated
 * 			to LKSMITH_LOCK_NAME_MAX bytes long, including the
 * 			terminating null.
 * @param __mutex	The Locksmith mutex to initialize.
 * @param __mutexattr	Pthread mutex attributes, or NULL (man
 *			pthread_mutex_init for details.)
 *
 * @return		0 on success; error code otherwise.
 */
int lksmith_mutex_init (const char * __restrict name,
		struct lksmith_mutex_t *__mutex,
		__const pthread_mutexattr_t *__mutexattr)
	__nonnull ((1, 2));

/* Destroy a mutex.  */
int lksmith_mutex_destroy(lksmith_mutex_t *__mutex)
	__nonnull ((1));

/**
 * Lock a mutex.
 *
 * @param mut		Pointer to the lksmith mutex
 *
 * @return		0 on success, or the error code.
 */
int lksmith_mutex_lock(lksmith_mutex_t *__mutex)
	__nonnull ((1));

/**
 * Try locking a mutex.
 *
 * @param mut		Pointer to the lksmith mutex
 *
 * @return
 */
int lksmith_mutex_trylock(lksmith_mutex_t *__mutex)
	__nonnull ((1));

/**
 * Wait until lock becomes available, or specified time passes.
 *
 * @param __mutex	Pointer to the lksmith mutex
 *
 * @return		0 on success, or the error code.
 */
int lksmith_mutex_timedlock(lksmith_mutex_t *__restrict __mutex,
				    __const struct timespec *__restrict
				    __abstime) __nonnull ((1, 2));

/**
 * Unlock a mutex.
 *
 * @param __mutex	Pointer to the lksmith mutex
 *
 * @return		0 on success, or the error code.
 */
int lksmith_mutex_unlock(lksmith_mutex_t *__mutex) __nonnull ((1));

/**
 * Set the thread name.
 *
 * @param name		The name to use for this thread.
 *			This string will be deep-copied.
 *			The copy will be truncated to LKSMITH_THREAD_NAME_MAX
 *			bytes long, including the terminating null.
 */
void lksmith_set_thread_name(const char * const name);

#ifdef __cplusplus
}
#endif

#endif

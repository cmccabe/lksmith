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
	/**
	 * Lock which protects the data.
	 */
	pthread_mutex_t lock;
	/**
	 * Private lock data.
	 */
	struct lksmith_lock_data *data;
};

struct lksmith_mutex {
	/** The raw pthread lock. */
	pthread_mutex_t raw;
	/** lksmith data. */
	struct lksmith_lock_info info;
};

struct lksmith_spin {
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
 */
#define LKSMITH_MUTEX_INITIALIZER \
	{ PTHREAD_MUTEX_INITIALIZER, { PTHREAD_MUTEX_INITIALIZER, 0 } } 

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
 * For obvious reasons, functions used as error reporting callbacks should not
 * take Locksmith-managed mutexes.
 *
 * @param code		The numeric Locksmith error code (see LKSMITH_ERROR_).
 * @param msg		The human-readable error string.
 */
typedef void (*lksmith_error_cb_t)(int code, const char * __restrict msg);

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
 * @param name		If this field is NULL, we will give this mutex a name
 * 			based on its numeric ID.  Otherwise, we will deep-copy
 * 			up to LKSMITH_LOCK_NAME_MAX - 1 bytes from this string
 * 			for the lock name.
 * @param __mutex	The Locksmith mutex to initialize.
 * @param __mutexattr	Pthread mutex attributes, or NULL (man
 *			pthread_mutex_init for details.)
 *
 * @return		0 on success; error code otherwise.
 */
int lksmith_mutex_init (const char * __restrict name,
		struct lksmith_mutex *__mutex,
		__const pthread_mutexattr_t *__mutexattr)
	__nonnull ((1, 2));

/**
 * Destroy a mutex.
 *
 * @param __mutex	The mutex to destroy.
 *
 * @return		0 on success; error code otherwise.
 */
int lksmith_mutex_destroy(struct lksmith_mutex *__mutex)
	__nonnull ((1));

/**
 * Initializes a Locksmith-protected spin lock.
 *
 * This function is thread-safe.
 *
 * @param name		If this field is NULL, we will give this spin lock a
 *			name based on its numeric ID.  Otherwise, we will
 *			deep-copy up to LKSMITH_LOCK_NAME_MAX - 1 bytes from
 *			this string for the lock name.
 * @param spin		The Locksmith spin lock to initialize.
 * @param pshared	Whether the spin lock should be cross-process.
 *			man pthread_spin_init for details.
 *
 * @return		0 on success; error code otherwise.
 */
int lksmith_spin_init(const char * __restrict name,
		struct lksmith_spin *spin, int pshared);

/**
 * Destroy a spin lock.
 *
 * @param __mutex	The spin lock to destroy.
 *
 * @return		0 on success; error code otherwise.
 */
int lksmith_spin_destroy(struct lksmith_spin *spin) __nonnull ((1));

/**
 * Lock a mutex.
 *
 * @param mut		Pointer to the lksmith mutex
 *
 * @return		0 on success, or the error code.
 */
int lksmith_mutex_lock(struct lksmith_mutex *__mutex)
	__nonnull ((1));

/**
 * Try locking a mutex.
 *
 * @param mut		Pointer to the lksmith mutex
 *
 * @return		0 on success; the error code otherwise 
 */
int lksmith_mutex_trylock(struct lksmith_mutex *__mutex)
	__nonnull ((1));

/**
 * Wait until lock becomes available, or specified time passes.
 *
 * @param __mutex	Pointer to the lksmith mutex
 *
 * @return		0 on success, or the error code.
 */
int lksmith_mutex_timedlock(struct lksmith_mutex *__restrict __mutex,
				    __const struct timespec *__restrict
				    __abstime) __nonnull ((1, 2));

/**
 * Lock a spin lock.
 *
 * @param spin		Pointer to the lksmith spin lock
 *
 * @return		0 on success, or the error code.
 */
int lksmith_spin_lock(struct lksmith_spin *spin);

/**
 * Try locking a spin lock.
 *
 * @param spin		Pointer to the lksmith spin lock.
 *
 * @return		0 on success; the error code otherwise 
 */
int lksmith_spin_trylock(struct lksmith_spin *spin);

/**
 * Unlock a mutex.
 *
 * @param __mutex	Pointer to the lksmith mutex
 *
 * @return		0 on success, or the error code.
 */
int lksmith_mutex_unlock(struct lksmith_mutex *__mutex) __nonnull ((1));

/**
 * Set the thread name.
 *
 * @param name		The name to use for this thread.
 *			This string will be deep-copied.
 *			The copy will be truncated to LKSMITH_THREAD_NAME_MAX
 *			bytes long, including the terminating null.
 *
 * @return		0 on success; error code otherwise
 */
int lksmith_set_thread_name(const char * const name);

/**
 * Get the thread name.
 *
 * @return		The thread name.  This is allocated as a thread-local
 *			string.  Returns NULL if we failed to allocate
 *			thread-local data.
 */
const char* lksmith_get_thread_name(void);

#ifdef __cplusplus
}
#endif

#endif

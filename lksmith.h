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

#include <stdint.h> /* for uint32_t, etc. */
#include <unistd.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
 *  Locksmith macros
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

/**
 * Maximum length of a thread name, including the terminating NULL byte.
 */
#define LKSMITH_THREAD_NAME_MAX 16

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
 * Initialize a locksmith lock.  This function is optional.
 *
 * @param ptr		pointer to the lock to initialize
 * @param sleeper	1 if this lock is a sleeper; 0 otherwise
 *
 * @return		0 on success; error code otherwise
 */
int lksmith_optional_init(const void *ptr, int sleeper);

/**
 * Destroy a lock.
 *
 * @param ptr		pointer to the lock to destroy
 *
 * @return		0 if the lock was destroyed;
 *			ENOENT if we're not aware of any such lock
 */
int lksmith_destroy(const void *ptr);

/**
 * Destroy a lock.
 *
 * @param ptr		pointer to the lock to destroy
 */
void lksmith_postdestroy(const void *ptr);

/**
 * Perform some error checking before taking a lock.
 *
 * @param ptr		pointer to the lock
 * @param sleeper	1 if this lock is a sleeper; 0 otherwise
 *
 * @return		0 if we should continue with the lock; error code
 *			otherwise.  We may print an error even if 0 is
 *			returned.
 */
int lksmith_prelock(const void *ptr, int sleeper);

/**
 * Take a lock.
 *
 * @param ptr		pointer to the lock.
 */
void lksmith_postlock(const void *ptr, int error);

/**
 * Determine if it's safe to release a lock.
 *
 * @param ptr		pointer to the lock.
 *
 * @return		0 on success, or the error code.
 */
int lksmith_preunlock(const void *ptr);

/**
 * Release a lock.
 *
 * @param ptr		pointer to the lock.
 */
void lksmith_postunlock(const void *ptr);

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

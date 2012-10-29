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
 */
#define LKSMITH_API_VERSION 0x0001000

/******************************************************************
 *  Locksmith data structures
 *****************************************************************/
struct lksmith_lock_data;

struct lksmith_lock_info {
  /** The lock name. */
  const char *name;
  /** Private lksmith data, or NULL if the lock hasn't been initialized yet.
   * This field must always be accessed using atomic operations. */
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
 * Note: the name string will be shallow-copied.
 */
#define LKSMITH_MUTEX_INITIALIZER(name) \
  { PTHREAD_MUTEX_INITIALIZER, { name, 0, 0, 0 } } 

/******************************************************************
 *  Locksmith error codes
 *****************************************************************/
/**
 * There was an out-of-memory error.
 */
#define LKSMITH_ERROR_OOM -1

/**
 * A pthread_lock or pthread_spin_lock operation did not succeed.
 */
#define LKSMITH_ERROR_LOCK_OPER_FAILED -2

/**
 * Bad lock ordering was detected.
 * This may cause deadlocks in the future if it is not corrected.
 */
#define LKSMITH_ERROR_BAD_LOCK_ORDERING_DETECTED -3

/**
 * There was an attempt to destroy a mutex while it was still in use.
 */
#define LKSMITH_ERROR_DESTROY_WHILE_IN_USE -4

/**
 * There was an attempt to re-initialize a mutex while it was still in use.
 */
#define LKSMITH_ERROR_CREATE_WHILE_IN_USE -5

/******************************************************************
 *  Locksmith API
 *****************************************************************/
/**
 * Get the current Locksmith API version
 *
 * @return         The current locksmith API version
 */
uint32_t lksmith_get_version(void);

/**
 * Convert the current Locksmith API version to a human-readable string.
 * This function is thread-safe.
 *
 * @param ver      A locksmith API version
 * @param str      (out parameter) buffer to place the version string
 * @param str_len  Length of the str buffer
 *
 * @return         0 on success; -ENAMETOOLONG if the provided buffer length
 *                 was too short; -EIO if another failure happened.
 */
int lksmith_verion_to_str(uint32_t ver, char *str, size_t str_len);

/**
 * Type signature for an error reporting callback function.
 * This function is thread-safe.
 *
 * @param code     The numeric Locksmith error code (see LKSMITH_ERROR_*).
 * @param msg      The human-readable error string.
 */
void (*lksmith_error_cb_t)(int code, const char * __restrict msg);

/**
 * Set the callback that will be called to deliver errors or warnings.
 * This function is thread-safe.
 *
 * @param fn       The callback
 */
void lksmith_set_error_cb(lksmith_error_cb_t fn);

/**
 * Simple error callback which prints a message to stderr.
 * This is the default error callback.
 * This function is thread-safe.
 *
 * @param code     The numeric Locksmith error code to be printed.
 * @param msg      The human-readable error string to be printed.
 */
void lksmith_error_cb_to_stderr(int code, const char *__restrict msg);

int lksmith_mutex_init (const char * __restrict name,
        lksmith_mutex_t *__mutex, __const pthread_mutexattr_t *__mutexattr)
     __lksmith_THROW __nonnull ((1));

/* Destroy a mutex.  */
int lksmith_mutex_destroy(lksmith_mutex_t *__mutex)
     __lksmith_THROW __nonnull ((1));

/* Try locking a mutex.
 *
 * @param mut         Pointer to the lksmith mutex
 *
 * @return
 * */
int lksmith_mutex_trylock(lksmith_mutex_t *mut)
     __lksmith_THROW __nonnull ((1));

/* Lock a mutex.  */
int lksmith_mutex_lock(lksmith_mutex_t *__mutex)
     __lksmith_THROW __nonnull ((1));

/* Wait until lock becomes available, or specified time passes. */
int lksmith_mutex_timedlock (lksmith_mutex_t *__restrict __mutex,
				    __const struct timespec *__restrict
				    __abstime) __lksmith_THROW __nonnull ((1, 2));

/* Unlock a mutex.  */
int lksmith_pthread_mutex_unlock (lksmith_mutex_t *__mutex)
     __lksmith_THROW __nonnull ((1));

#ifdef __cplusplus
}
#endif

#endif

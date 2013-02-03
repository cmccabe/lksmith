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

#define LKSMITH_HANDLER_DOT_C

#include "error.h"
#include "lksmith.h"
#include "util.h"
#include "handler.h"
#include "platform.h"

#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * Handler functions used to redirect pthreads calls to Locksmith.
 */

/**
 * A list of mutex types that are compatible with error checking mutexes.
 * Note that recursive mutexes are NOT compatible.
 */
static const int g_compatible_with_errcheck[] = {
#ifdef PTHREAD_MUTEX_TIMED_NP
	PTHREAD_MUTEX_TIMED_NP,
#endif
#ifdef PTHREAD_MUTEX_ADAPTIVE_NP
	PTHREAD_MUTEX_ADAPTIVE_NP,
#endif
#ifdef PTHREAD_MUTEX_FAST_NP
	PTHREAD_MUTEX_FAST_NP,
#endif
	PTHREAD_MUTEX_NORMAL,
	PTHREAD_MUTEX_DEFAULT
};

#define NUM_COMPATIBLE_WITH_ERRCHECK (sizeof(g_compatible_with_errcheck) / \
	sizeof(g_compatible_with_errcheck[0]))

static int is_compatible_with_errcheck(int ty)
{
	unsigned int i;

	for (i = 0; i < NUM_COMPATIBLE_WITH_ERRCHECK; i++) {
		if (ty == g_compatible_with_errcheck[i])
			return 1;
	}
	return 0;
}

static int pthread_mutex_init_errcheck(pthread_mutex_t *mutex)
{
	int ret;
	pthread_mutexattr_t attr;

	ret = pthread_mutexattr_init(&attr);
	if (ret) {
		lksmith_error(ret, "pthread_mutexattr_init failed "
			"with error code %d: %s\n", ret, terror(ret));
		goto done;
	}
	ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	if (ret) {
		lksmith_error(ret, "pthread_mutexattr_settype failed "
			"with error code %d: %s\n", ret, terror(ret));
		goto done_free_mutexattr;
	}
	ret = r_pthread_mutex_init(mutex, &attr);
	if (ret) {
		lksmith_error(ret, "pthread_mutex_init failed "
			"with error code %d: %s\n", ret, terror(ret));
		goto done_free_mutexattr;
	}
done_free_mutexattr:
	pthread_mutexattr_destroy(&attr);
done:
	return ret;
}

static int pthread_mutex_real_init(pthread_mutex_t *mutex,
			pthread_mutexattr_t *attr, int *recursive)
{
	int ret, ty = 0;

	*recursive = 0;
	if (!attr) {
		/* No mutex attributes provided.  Initialize this as an error
		 * checking mutex. */
		return pthread_mutex_init_errcheck(mutex);
	}
	ret = pthread_mutexattr_gettype(attr, &ty);
	if (ret == EINVAL) {
		lksmith_error(ret, "pthread_mutexattr_gettype failed "
			"with error code %d: %s\n", ret, terror(ret));
		return ret;
	}
	if (is_compatible_with_errcheck(ty)) {
		/* If the requested mutex type is compatible with the error
		 * checking type, let's use that type instead, for extra
		 * safety. */
		ret = pthread_mutexattr_settype(attr,
			PTHREAD_MUTEX_ERRORCHECK);
		if (ret) {
			lksmith_error(ret, "pthread_mutexattr_settype failed "
				"with error code %d: %s\n", ret, terror(ret));
			return ret;
		}
	} else {
		/* If we don't know about the requested mutex type, we assume
		 * that it's recursive, to be on the safe side.
		 */
		*recursive = 1;
	}
	ret = r_pthread_mutex_init(mutex, attr);
	if (ret) {
		lksmith_error(ret, "pthread_mutex_init failed "
			"with error code %d: %s\n", ret, terror(ret));
		return ret;
	}
	return ret;
}

int pthread_mutex_init(pthread_mutex_t *mutex,
	const pthread_mutexattr_t *attr)
{
	int ret, recursive = 0;

	ret = init_tls();
	if (ret)
		return ret;

	/*
	 * We have to cast away the const here, because the alternative,
	 * copying the pthread_mutexattr_t, is not feasible.
	 * pthread_mutexattr_t is an opaque type and no "copy" function is
	 * provided by pthreads.  The set of accessor functions may vary by
	 * platform, so we can't use those to perform a copy either.
	 *
	 * So, we're casting away the const here.  This should be safe in
	 * all cases.  C/C++ compilers can't use const as an aide to
	 * optimization (due to the aliasing problem.)
	 * We also know that pthread_mutexattr_t is not stored in read-only
	 * memory, because the only way to initialize it is through  
	 * pthread_mutexattr_init, which modifies it.  There is no static
	 * initializer provided for pthread_mutexattr_t.
	 */
	ret = pthread_mutex_real_init(mutex, (pthread_mutexattr_t*)attr,
					&recursive);
	if (ret)
		return ret;
	ret = lksmith_optional_init((const void*)mutex, recursive, 1);
	if (ret) {
		pthread_mutex_destroy(mutex);
		return ret;
	}
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	int ret;

	ret = lksmith_destroy(mutex);
	if ((ret != 0) && (ret != ENOENT)) {
		/* We ignore ENOENT here because the mutex may have been
		 * initialized with PTHREAD_MUTEX_INITIALIZER and then
		 * destroyed after no interactions with Locksmith.
		 *
		 * In order to catch this case, we'd have to peek inside the
		 * mutex, which we don't want to do for portability reasons.
		 */
		return ret;
	}
	return r_pthread_mutex_destroy(mutex);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	int ret = lksmith_prelock(mutex, 1);
	if (ret)
		return ret;
	ret = r_pthread_mutex_trylock(mutex);
	lksmith_postlock(mutex, ret);
	return ret;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int ret = lksmith_prelock(mutex, 1);
	if (ret)
		return ret;
	ret = r_pthread_mutex_lock(mutex);
	lksmith_postlock(mutex, ret);
	return ret;
}

int pthread_mutex_timedlock(pthread_mutex_t *__restrict mutex,
		__const struct timespec *__restrict ts)
{
	int ret = lksmith_prelock(mutex, 1);
	if (ret)
		return ret;
	ret = r_pthread_mutex_timedlock(mutex, ts);
	lksmith_postlock(mutex, ret);
	return ret;
}

int pthread_mutex_unlock(pthread_mutex_t *__restrict mutex)
{
	int ret = lksmith_preunlock(mutex);
	if (ret)
		return ret;
	ret = r_pthread_mutex_unlock(mutex);
	if (ret)
		return ret;
	lksmith_postunlock(mutex);
	return 0;
}

// TODO: pthread_rwlock stuff
int pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
	int ret;

	ret = lksmith_optional_init((const void*)lock, 0, 0);
	if (ret)
		return ret;
	ret = r_pthread_spin_init(lock, pshared);
	if (ret) {
		lksmith_destroy((const void*)lock);
		return ret;
	}
	return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
	int ret;

	ret = lksmith_destroy((const void*)lock);
	if (ret)
		return ret;
	return r_pthread_spin_destroy(lock);
}

int pthread_spin_lock(pthread_spinlock_t *lock)
{
	int ret = lksmith_prelock((const void*)lock, 0);
	if (ret)
		return ret;
	ret = r_pthread_spin_lock(lock);
	lksmith_postlock((const void*)lock, ret);
	return ret;
}

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
	int ret = lksmith_prelock((const void*)lock, 0);
	if (ret)
		return ret;
	ret = r_pthread_spin_trylock(lock);
	lksmith_postlock((const void*)lock, ret);
	return ret;
}

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
	int ret = lksmith_preunlock((const void*)lock);
	if (ret)
		return ret;
	ret = r_pthread_spin_unlock(lock);
	if (ret)
		return ret;
	lksmith_postunlock((const void*)lock);
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *__restrict cond,
	pthread_mutex_t *__restrict mutex,
	const struct timespec *__restrict abstime)
{
	int ret = lksmith_check_locked((const void*)mutex);
	if (ret > 0) {
		return ret;
	} else if (ret == -1) {
		lksmith_error(EPERM, "pthread_cond_timedwait(cond=%p, "
			"mutex=%p): you called pthread_cond_timedwait on "
			"a mutex that you do not currently hold.  Please "
			"fix this serious error in your program.\n",
			cond, mutex);
		return EPERM;
	}
	return r_pthread_cond_timedwait(cond, mutex, abstime);
}

int pthread_cond_wait(pthread_cond_t *__restrict cond,
	pthread_mutex_t *__restrict mutex)
{
	int ret = lksmith_check_locked((const void*)mutex);
	if (ret > 0) {
		return ret;
	} else if (ret == -1) {
		lksmith_error(EPERM, "pthread_cond_wait(cond=%p, mutex=%p): "
			"you called pthread_cond_wait on a mutex that you "
			"do not currently hold.  Please fix this serious "
			"error in your program.\n", cond, mutex);
		return EPERM;
	}
	return r_pthread_cond_wait(cond, mutex);
}

// TODO: support barriers

#define LOAD_FUNC(fn) do { \
	r_##fn = get_dlsym_next(#fn); \
	if (!r_##fn) { \
		return ELIBACC; \
	} \
} while (0);

int lksmith_handler_init(void)
{
	LOAD_FUNC(pthread_mutex_init);
	LOAD_FUNC(pthread_mutex_destroy);
	LOAD_FUNC(pthread_mutex_trylock);
	LOAD_FUNC(pthread_mutex_lock);
	LOAD_FUNC(pthread_mutex_timedlock);
	LOAD_FUNC(pthread_mutex_unlock);
	LOAD_FUNC(pthread_spin_init);
	LOAD_FUNC(pthread_spin_destroy);
	LOAD_FUNC(pthread_spin_lock);
	LOAD_FUNC(pthread_spin_trylock);
	LOAD_FUNC(pthread_spin_unlock);
	LOAD_FUNC(pthread_cond_wait);
	LOAD_FUNC(pthread_cond_timedwait);

	return 0;
}

// TODO: support thread cancellation?  ugh...

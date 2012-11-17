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

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * Shim functions used to redirect pthreads calls to Locksmith.
 */

static void* dlsym_next_or_die(const char *fname)
{
	void *v;

	v = dlsym(RTLD_NEXT, fname);
	if (!v) {
		/* dlerror is not thread-safe.  However, since there is no
		 * thread-safe interface, we really don't have much choice,
		 * do we?
		 *
		 * Also, technically a NULL return from dlsym doesn't
		 * necessarily indicate an error.  However, NULL is not
		 * a valid address for any of the things we're looking up, so
		 * we're going to assume that an error occurred.
		 */
		fprintf(stderr, "locksmith shim error: dlsym error: %s\n",
			dlerror());
		abort();
	}
	/* Another problem with the dlsym interface is that technically, a
	 * void* should never be cast to a function pointer, since the C
	 * standard allows them to have different sizes.
	 * Apparently the POSIX committee didn't read that part of the C
	 * standard.  We'll pretend we didn't either.
	 */
	return v;
}


static int (*r_pthread_mutex_init)(pthread_mutex_t *mutex,
	__const pthread_mutexattr_t *attr);

int pthread_mutex_init(pthread_mutex_t *mutex,
	__const pthread_mutexattr_t *attr)
{
	return r_pthread_mutex_init(mutex, attr);
}

static int (*r_pthread_mutex_destroy)(pthread_mutex_t *mutex);

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	return r_pthread_mutex_destroy(mutex);
}

static int (*r_pthread_mutex_trylock)(pthread_mutex_t *mutex);

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	return r_pthread_mutex_trylock(mutex);
}

static int (*r_pthread_mutex_lock)(pthread_mutex_t *mutex);

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	return r_pthread_mutex_lock(mutex);
}

static int (*r_pthread_mutex_timedlock)(pthread_mutex_t *__restrict mutex,
	__const struct timespec *__restrict ts);

int pthread_mutex_timedlock(pthread_mutex_t *__restrict mutex,
		__const struct timespec *__restrict ts)
{
	return r_pthread_mutex_timedlock(mutex, ts);
}

static int (*r_pthread_mutex_unlock)(pthread_mutex_t *__restrict mutex);

int pthread_mutex_unlock(pthread_mutex_t *__restrict mutex)
{
	return r_pthread_mutex_unlock(mutex);
}

// TODO: pthread_rwlock stuff

static int (*r_pthread_spin_init)(pthread_spinlock_t *lock, int pshared);

int pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
	return r_pthread_spin_init(lock, pshared);
}

static int (*r_pthread_spin_destroy)(pthread_spinlock_t *lock);

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
	return r_pthread_spin_destroy(lock);
}

static int (*r_pthread_spin_lock)(pthread_spinlock_t *lock);

int pthread_spin_lock(pthread_spinlock_t *lock)
{
	return r_pthread_spin_lock(lock);
}

static int (*r_pthread_spin_trylock)(pthread_spinlock_t *lock);

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
	return r_pthread_spin_trylock(lock);
}

static int (*r_pthread_spin_unlock)(pthread_spinlock_t *lock);

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
	return r_pthread_spin_unlock(lock);
}

// TODO: support barriers

#define LOAD_FUNC(fn) r_##fn = dlsym_next_or_die(#fn)

void lksmith_shim_init(void) __attribute__((constructor));

void lksmith_shim_init(void)
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
}

// TODO: support thread cancellation?  ugh...

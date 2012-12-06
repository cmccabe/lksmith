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

#ifndef LKSMITH_HANDLER_H
#define LKSMITH_HANDLER_H

#include <pthread.h>

/******************************************************************
 * The raw pthreads functions.
 *****************************************************************/
#ifndef LKSMITH_HANDLER_DOT_C
#define EXTERN extern
#else
#define EXTERN
#endif

EXTERN int (*r_pthread_mutex_init)(pthread_mutex_t *mutex,
	const pthread_mutexattr_t *attr);

EXTERN int (*r_pthread_mutex_destroy)(pthread_mutex_t *mutex);

EXTERN int (*r_pthread_mutex_trylock)(pthread_mutex_t *mutex);

EXTERN int (*r_pthread_mutex_lock)(pthread_mutex_t *mutex);

EXTERN int (*r_pthread_mutex_timedlock)(pthread_mutex_t *__restrict mutex,
	__const struct timespec *__restrict ts);

EXTERN int (*r_pthread_mutex_unlock)(pthread_mutex_t *__restrict mutex);

EXTERN int (*r_pthread_spin_init)(pthread_spinlock_t *lock, int pshared);

EXTERN int (*r_pthread_spin_destroy)(pthread_spinlock_t *lock);

EXTERN int (*r_pthread_spin_lock)(pthread_spinlock_t *lock);

EXTERN int (*r_pthread_spin_trylock)(pthread_spinlock_t *lock);

EXTERN int (*r_pthread_spin_unlock)(pthread_spinlock_t *lock);

/******************************************************************
 * Functions
 *****************************************************************/
int lksmith_handler_init(void);

void* get_dlsym_next(const char *fname);

#endif

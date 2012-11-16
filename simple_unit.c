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

#include "lksmith.h"
#include "test.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int test_mutex_init_teardown(void)
{
	struct lksmith_mutex mutex;
	EXPECT_ZERO(lksmith_mutex_init("test_mutex_1", &mutex, NULL));
	EXPECT_ZERO(lksmith_mutex_destroy(&mutex));
	return 0;
}

static int test_mutex_static_init_teardown(void)
{
	struct lksmith_mutex mutex = LKSMITH_MUTEX_INITIALIZER;
	EXPECT_ZERO(lksmith_mutex_destroy(&mutex));
	return 0;
}

static int test_spin_init_teardown(void)
{
	struct lksmith_spin spin;
	EXPECT_ZERO(lksmith_spin_init("test_spin_1", &spin, 0));
	EXPECT_ZERO(lksmith_spin_destroy(&spin));
	return 0;
}

static int test_mutex_lock_simple(void)
{
	struct lksmith_mutex mutex;
	EXPECT_ZERO(lksmith_mutex_init("simple_mutex_1", &mutex, NULL));
	EXPECT_ZERO(lksmith_mutex_lock(&mutex));
	EXPECT_ZERO(lksmith_mutex_unlock(&mutex));
	EXPECT_ZERO(lksmith_mutex_lock(&mutex));
	EXPECT_ZERO(lksmith_mutex_unlock(&mutex));
	EXPECT_ZERO(lksmith_mutex_destroy(&mutex));
	return 0;
}

static int test_mutex_lock_simple_static(void)
{
	struct lksmith_mutex mutex = LKSMITH_MUTEX_INITIALIZER;
	EXPECT_ZERO(lksmith_mutex_lock(&mutex));
	EXPECT_ZERO(lksmith_mutex_unlock(&mutex));
	EXPECT_ZERO(lksmith_mutex_destroy(&mutex));
	return 0;
}

static int test_spin_lock_simple(void)
{
	struct lksmith_mutex mutex;
	EXPECT_ZERO(lksmith_mutex_init("simple_mutex_1", &mutex, NULL));
	EXPECT_ZERO(lksmith_mutex_lock(&mutex));
	EXPECT_ZERO(lksmith_mutex_unlock(&mutex));
	EXPECT_ZERO(lksmith_mutex_lock(&mutex));
	EXPECT_ZERO(lksmith_mutex_unlock(&mutex));
	EXPECT_ZERO(lksmith_mutex_destroy(&mutex));
	return 0;
}


int main(void)
{
	lksmith_set_error_cb(die_on_error);

	EXPECT_EQ(LKSMITH_API_VERSION, lksmith_get_version());
	EXPECT_ZERO(test_mutex_init_teardown());
	EXPECT_ZERO(test_mutex_static_init_teardown());
	EXPECT_ZERO(test_mutex_init_teardown());
	EXPECT_ZERO(test_spin_init_teardown());
	EXPECT_ZERO(test_mutex_lock_simple());
	EXPECT_ZERO(test_mutex_lock_simple_static());
	EXPECT_ZERO(test_spin_lock_simple());

	return EXIT_SUCCESS;
}

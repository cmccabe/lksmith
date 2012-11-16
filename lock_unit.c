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

static int test_multi_mutex_lock(signed int max_locks)
{
	signed int i;
	struct lksmith_mutex *mutex;
	static char name[LKSMITH_LOCK_NAME_MAX];

	mutex = xcalloc(sizeof(struct lksmith_mutex) * max_locks);
	for (i = 0; i < max_locks; i++) {
		snprintf(name, sizeof(name), "test_multi_%04d", i);
		EXPECT_ZERO(lksmith_mutex_init(name, &mutex[i], NULL));
	}
	for (i = 0; i < max_locks; i++) {
		EXPECT_ZERO(lksmith_mutex_lock(&mutex[i]));
	}
	for (i = max_locks - 1; i >= 0; i--) {
		EXPECT_ZERO(lksmith_mutex_unlock(&mutex[i]));
	}
	return 0;
}

int main(void)
{
	lksmith_set_error_cb(die_on_error);

	EXPECT_ZERO(test_multi_mutex_lock(5));
	EXPECT_ZERO(test_multi_mutex_lock(100));

	return EXIT_SUCCESS;
}

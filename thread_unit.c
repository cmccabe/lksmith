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
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_thread_name_set_and_get_impl(void)
{
	const char * const MY_THREAD = "my_thread";
	const char *name;

	EXPECT_ZERO(lksmith_set_thread_name(MY_THREAD));
	name = lksmith_get_thread_name();
	EXPECT_NOT_EQ(name, NULL);
	EXPECT_ZERO(strcmp(MY_THREAD, name));
	return 0;
}

static void* test_thread_name_set_and_get(void *v __attribute__((unused)))
{
	return (void*)(uintptr_t)test_thread_name_set_and_get_impl();
}

int main(void)
{
	pthread_t pthread;
	void *rval;

	putenv("LKSMITH_LOG=callback://die_on_error");
	EXPECT_ZERO(pthread_create(&pthread, NULL,
		test_thread_name_set_and_get, NULL));
	EXPECT_ZERO(pthread_join(pthread, &rval));
	EXPECT_EQ(rval, 0); 

	return EXIT_SUCCESS;
}

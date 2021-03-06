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

#ifndef LKSMITH_UTIL_TEST_H
#define LKSMITH_UTIL_TEST_H

#include "error.h" /* for lksmith_error_cb_t */

#include <stdio.h> /* for fprintf */

struct timespec;

/**
 * Set the Locksmith error handling callback.
 *
 * @param cb		The callback
 */
void set_error_cb(lksmith_error_cb_t cb);

/**
 * Error handling function that just aborts.
 *
 * @param code		The error code
 * @param msg		The error message
 */
void die_on_error(int code, const char *msg);

/**
 * Error handling function that records errors.
 *
 * @param code		The error code
 * @param msg		The error message
 */
void record_error(int code, const char *msg);

/**
 * Clear all errors recorded by record_error.
 */
void clear_recorded_errors(void);

/**
 * Find a recorded error code and clear it.
 *
 * @param expect	The error code to search for.
 *
 * @return		0 if the error code was not found; 1 if it was.
 */
int find_recorded_error(int expect);

/**
 * @return		the number of recorded errors.
 */
int num_recorded_errors(void);

/**
 * Get the current time
 *
 * @param ts		(out param) the current time
 *
 * @return		0 on success; gettimeofday errno otherwise
 */
int get_current_timespec(struct timespec *ts);

/**
 * Increment a timespec by a few milliseconds
 *
 * @param ts		(out param) the timespec
 * @param ms		The number of milliseconds to add
 */
void timespec_add_milli(struct timespec *ts, unsigned int ms);

#define EXPECT_ZERO(x) \
	do { \
		int __my_ret__ = x; \
		if (__my_ret__) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return __my_ret__; \
		} \
	} while (0);

#define EXPECT_NONZERO(x) \
	do { \
		int __my_ret__ = x; \
		if (__my_ret__ == 0) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return -1; \
		} \
	} while (0);

#define EXPECT_POSITIVE(x) \
	do { \
		int __my_ret__ = x; \
		if (__my_ret__ < 0) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return __my_ret__; \
		} \
	} while (0);

#define EXPECT_EQ(x, y) \
	do { \
		if ((x) != (y)) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return 1; \
		} \
	} while (0);

#define EXPECT_NOT_EQ(x, y) \
	do { \
		if ((x) == (y)) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return 1; \
		} \
	} while (0);

#define EXPECT_LT(x, y) \
	do { \
		if ((x) >= (y)) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return 1; \
		} \
	} while (0);

#define EXPECT_GE(x, y) \
	do { \
		if ((x) < (y)) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return 1; \
		} \
	} while (0);

#define EXPECT_GT(x, y) \
	do { \
		if ((x) <= (y)) { \
			fprintf(stderr, "failed on line %d: %s\n",\
				__LINE__, #x); \
			return 1; \
		} \
	} while (0);

#endif

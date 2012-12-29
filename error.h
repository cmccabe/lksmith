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

#ifndef LKSMITH_ERROR_H
#define LKSMITH_ERROR_H

#include <errno.h>

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
 * Log a Locksmith error message.
 *
 * @param err		The error code.
 * @param fmt		printf-style erorr code.
 * @param ...		printf-style arguments.
 */
void lksmith_error(int err, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/**
 * Look up the error message associated with a POSIX error code.
 *
 * This function is thread-safe.
 *
 * @param err		The POSIX error code (should be non-negative)
 *
 * @return		The error message.  This is a statically allocated
 *			string. 
 */
const char *terror(int err);

/*
 * If we don't have ELIBACC, use EIO instead.
 */
#ifndef ELIBACC
#define ELIBACC EIO
#endif

#endif

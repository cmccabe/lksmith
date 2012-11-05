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

#include "util/lkerror.h"

#include <errno.h>
#include <stdarg.h>

static void lksmith_print_error_unlocked(int err, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	lksmith_error_cb_t error_cb;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), ap);
	va_end(ap);
	pthread_mutex_lock(&g_internal_lock);
	error_cb = g_error_cb;
	pthread_mutex_unlock(&g_internal_lock);
	error_cb(err, buf);
}

/**
 * Convert a Locksmith error code to an error string.
 *
 * @param lkerr		The Locksmith error code
 *
 * @return		The statically allocated error string
 */
const char* lksmith_error_to_str(int lkerr)
{
	switch (lkerr) {
	case LKSMITH_ERROR_OOM:
		return "Out of memory.";
	case LKSMITH_ERROR_LOCK_OPER_FAILED:
		return "A pthread lock operation failed.";
	case LKSMITH_ERROR_BAD_LOCK_ORDERING_DETECTED:
		return "Bad lock ordering was detected.";
	case LKSMITH_ERROR_DESTROY_WHILE_IN_USE:
		return "There was an attempt to destroy a lock while it "
			"was in use.";
	case LKSMITH_ERROR_MULTIPLE_DESTROY:
		return "There was an attempt to destroy a lock more than "
			"once.";
	case LKSMITH_ERROR_CREATE_WHILE_IN_USE:
		return "tried to create a lock while the memory was still "
			"in use for a different lock.";
	default:
		return "other error.";
	}
}

int lksmith_error_to_errno(int lkerr)
{
	switch (lkerr) {
	case LKSMITH_ERROR_OOM:
		return ENOMEM;
	case LKSMITH_ERROR_LOCK_OPER_FAILED:
		return EIO;
	case LKSMITH_ERROR_BAD_LOCK_ORDERING_DETECTED:
		return EDEADLK;
	case LKSMITH_ERROR_DESTROY_WHILE_IN_USE:
	case LKSMITH_ERROR_CREATE_WHILE_IN_USE:
		return EINVAL;
	default:
		return EIO;
	}
}

const char *terror(int err)
{
	if ((err < 0) || (err >= sys_nerr)) {
		return "unknown error";
	}
	return sys_errlist[err];
}

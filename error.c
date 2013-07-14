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

#include "error.h"
#include "handler.h"
#include "util.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

enum lksmith_log_type {
	LKSMITH_LOG_UNINIT = 0,
	LKSMITH_LOG_SYSLOG,
	LKSMITH_LOG_FILE,
	LKSMITH_LOG_CALLBACK,
};

#define DEFAULT_LKSMITH_LOG_TYPE "stderr"

#define FILE_PREFIX "file://"
#define CALLBACK_PREFIX "callback://"

static enum lksmith_log_type g_log_type;

/**
 * Locksmith error callback to use.
 */
static lksmith_error_cb_t g_error_cb;

/**
 * Log file
 */
static FILE *g_log_file;

/**
 * Protects g_error_cb.  This is not held while the callback is in progress,
 * though.
 */
static pthread_mutex_t g_error_lock = PTHREAD_MUTEX_INITIALIZER;

static void lksmith_log_init_file(const char *name)
{
	int err;

	g_log_type = LKSMITH_LOG_FILE;
	g_log_file = fopen(name, "w");
	if (!g_log_file) {
		err = errno;
		fprintf(stderr, "Unable to open '%s': error %d: %s\n"
			"redirecting output to stderr.\n",
			name, err, terror(err));
		g_log_file = stderr;
		return;
	}
}

static int lksmith_log_init_cb(const char *addr_str)
{
	int err;
	unsigned long long int addr;

	if ((addr_str[0] != '0') || (addr_str[1] != 'x')) {
		fprintf(stderr, "Invalid callback address '%s'.\n"
			"Callback address must begin with 0x.\n"
			"Redirecting output to stderr.\n", addr_str);
		return EINVAL;
	}
	errno = 0;
	addr = strtoull(addr_str, NULL, 16);
	err = errno;
	if (err) {
		fprintf(stderr, "Unable to parse callback address '%s'.\n"
			"error %d: %s.\n"
			"Redirecting output to stderr.\n",
			addr_str, err, terror(err));
		return err;
	}
	g_error_cb = (lksmith_error_cb_t)addr;
	g_log_type = LKSMITH_LOG_CALLBACK;
	return 0;
}

static void lksmith_log_init(void)
{
	const char *ty;
	ty = getenv("LKSMITH_LOG");
	if (!ty)
		ty = DEFAULT_LKSMITH_LOG_TYPE;
	if (!strcmp(ty, "syslog")) {
		g_log_type = LKSMITH_LOG_SYSLOG;
	} else if (!strcmp(ty, "stderr")) {
		g_log_type = LKSMITH_LOG_FILE;
		g_log_file = stderr;
	} else if (!strcmp(ty, "stdout")) {
		g_log_type = LKSMITH_LOG_FILE;
		g_log_file = stdout;
	} else if (strstr(ty, FILE_PREFIX) == ty) {
		lksmith_log_init_file(ty + strlen(FILE_PREFIX));
	} else if (strstr(ty, CALLBACK_PREFIX) == ty) {
		if (lksmith_log_init_cb(ty + strlen(CALLBACK_PREFIX))) {
			g_log_type = LKSMITH_LOG_FILE;
			g_log_file = stderr;
		}
	} else {
		fprintf(stderr, "Sorry, unable to understand log target '%s'. "
			"redirecting output to stderr.\n", ty);
		g_log_type = LKSMITH_LOG_FILE;
		g_log_file = stderr;
	}
}

static void lksmith_errora_unlocked(int err, const char *fmt, va_list ap)
{
	if (g_log_type == LKSMITH_LOG_UNINIT) {
		lksmith_log_init();
	}
	if (g_log_type == LKSMITH_LOG_SYSLOG) {
		vsyslog(LOG_USER | LOG_INFO, fmt, ap);
	} else if (g_log_type == LKSMITH_LOG_FILE) {
		vfprintf(g_log_file, fmt, ap);
	} else if (g_log_type == LKSMITH_LOG_CALLBACK) {
		char buf[4096];
		vsnprintf(buf, sizeof(buf), fmt, ap);
		g_error_cb(err, buf);
	}
}

static void lksmith_error_unlocked(int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	lksmith_errora_unlocked(err, fmt, ap);
	va_end(ap);
}

void lksmith_error(int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	lksmith_errora(err, fmt, ap);
	va_end(ap);
}

void lksmith_errora(int err, const char *fmt, va_list ap)
{
	r_pthread_mutex_lock(&g_error_lock);
	lksmith_errora_unlocked(err, fmt, ap);
	r_pthread_mutex_unlock(&g_error_lock);
}

void lksmith_errora_with_bt(int err, char **frames, int frames_len,
			const char *fmt, va_list ap)
{
	int i;

	r_pthread_mutex_lock(&g_error_lock);
	lksmith_errora_unlocked(err, fmt, ap);
	for (i = 0; i < frames_len; i++) { 
		lksmith_error_unlocked(0, "%s\n", frames[i]);
	}
	r_pthread_mutex_unlock(&g_error_lock);
}

const char *terror(int err)
{
#ifdef HAVE_IMPROVED_TLS
	static __thread char buf[4096];
	int ret;

	ret = strerror_r(err, buf, sizeof(buf));
	if (ret)
		return "unknown error";
	return buf;
#else
	if ((err < 0) || (err >= sys_nerr)) {
		return "unknown error";
	}
	return sys_errlist[err];
#endif
}


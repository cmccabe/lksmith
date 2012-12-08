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

/**
 * This program is an attempt to automatically create a shim.c file for Linux,
 * based purely on the symbols found in glibc.  It isn't quite complete, and
 * may not completely solve the problem.  One issue is that not all pthreads
 * symbols are actually defined in glibc.
 */

#include "mem.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PARAMS 10

struct version {
	const char *suffix;
	struct version *next;
	int idx;
};

struct func {
	const char *const name;
	struct version *versions;
	struct version *v_max;
	const char *rtype;
	const char *ptypes[MAX_PARAMS];
};

static struct func g_funcs[] = {
{ "pthread_mutex_init", NULL, NULL, "int",
	{ "pthread_mutex_t*", "const pthread_mutexattr_t", NULL },
},
{ "pthread_mutex_destroy", NULL, NULL, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_mutex_trylock", NULL, NULL, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_mutex_lock", NULL, NULL, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_mutex_timedlock", NULL, NULL, "int",
	{ "pthread_mutex_t*", "const struct timespec*", NULL },
},
{ "pthread_mutex_unlock", NULL, NULL, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_spin_init", NULL, NULL, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_destroy", NULL, NULL, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_lock", NULL, NULL, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_trylock", NULL, NULL, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_unlock", NULL, NULL, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_cond_wait", NULL, NULL, "int",
	{ "pthread_cond_t*", "pthread_mutex_t*",
	"const struct timespec *" }
}
};

static int g_verbose = 0;

#define NUM_FUNCS (sizeof(g_funcs)/sizeof(g_funcs[0]))

static void debug(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

static void debug(const char *fmt, ...)
{
	va_list ap;
	if (!g_verbose)
		return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static void func_add_version(struct func *fn, const char *suffix)
{
	struct version *v = fn->versions;

	while (v) {
		if (strcmp(v->suffix, suffix) == 0)
			return;
		v = v->next;
	}
	v = xcalloc(sizeof(*v));
	v->suffix = xstrdup(suffix);
	v->next = fn->versions;
	fn->versions = v;
}

int func_number_all_versions(void)
{
	int idx;
	struct func *fn;
	struct version *v, *v_max;
	size_t i;

	for (i = 0; i < NUM_FUNCS; i++) {
		fn = &g_funcs[i];
		if (!fn->versions) {
			fprintf(stderr, "failed to find the versions "
				"for %s\n", g_funcs[i].name);
			return -ENOENT;
		}
		v_max = fn->versions;
		for (idx = 0, v = fn->versions; v; idx++, v++) {
			v->idx = idx;
			if (strcmp(v_max->suffix, v->suffix) < 0) {
				v_max = v;
			}
		}
		fn->v_max = v_max;
	}
	return 0;
}

/**
 * Find the glibc library that this executable was linked against.
 * The assumption is that this same version will be used to create
 * liblksmith.so.
 *
 * @param buf		(out param) the buffer to fill
 * @param buf_len	length of buf
 *
 * @return		0 on success; negative error code otherwise
 */
static int find_glibc(char *buf, size_t buf_len)
{
	FILE *fp;
	char cmd[4096], *str, *start, *middle, *end;
	static const char * const ARROW = " => ";

	snprintf(cmd, sizeof(cmd), "ldd '/proc/%"PRId64"/exe'",
		 (uint64_t)getpid());
	fp = popen(cmd, "r");
	while (1) {
		str = fgets(cmd, sizeof(cmd), fp);
		if (!str)
			break;
	        /* We are looking for a line like this:
		 *    libc.so.6 => /lib64/libc.so.6 (0x00007f1872f00000)
		 */
		start = strstr(cmd, "libc.so");
		if (!start)
			continue;
		if ((start != str) && (!isspace(start[-1])))
			continue;
		middle = strstr(start, ARROW);
		if (!middle)
			continue;
		middle += strlen(ARROW);
		end = index(middle, ' ');
		if (!end)
			continue;
		*end = '\0';
		fclose(fp);
		debug("found glibc at %s\n", middle);
		if (snprintf(buf, buf_len, "%s", middle) >= (int)buf_len)
			return -ENAMETOOLONG;
		return 0;
	}
	fclose(fp);
	if (feof(fp)) {
		fprintf(stderr, "failed to find the libc line in our "
			"ldd output.\n");
		return -ENOENT;
	}
	fprintf(stderr, "error reading the ldd output.\n"); 
	return -EIO;
}

static int match_func(char *line, int lineno)
{
	size_t i, name_len;
	char *cmp[] = { NULL, NULL, NULL };
	char *rest = NULL, *suffix;
	char *tmp;

	for (tmp = line, i = 0; i < sizeof(cmp)/sizeof(cmp[0]); i++) {
		cmp[i] = strtok_r(tmp, " ", &rest);
		if (!cmp[i])
			break;
		tmp = rest;
	}
	if (!cmp[2]) {
		fprintf(stderr, "failed to parse line %d into three parts.",
			lineno);
		return 0;
	}
	printf("match_func(line=%s, cmp[0]=%s, cmp[1]=%s, cmp[2]=%s)\n",
	       line, cmp[0], cmp[1], cmp[2]);
	for (i = 0; i < NUM_FUNCS; i++) {
		name_len = strcspn(cmp[2], "@");
		if (strncmp(cmp[2], g_funcs[i].name, name_len))
			continue;
		suffix = cmp[2] + name_len;
		func_add_version(&g_funcs[i], suffix);
		return 1;
	}
	return 0;
}

static void chomp(char *str)
{
	size_t len = strlen(str);
	if (str[len - 1] == '\n') {
		str[len - 1] = '\0';
	}
}

static int find_versions(void)
{
	char cmd[8096], glibc[PATH_MAX], *str;
	FILE *fp;
	int ret = 0, lineno = 0;
	size_t i;

	ret = find_glibc(glibc, sizeof(glibc));
	if (ret) {
		fprintf(stderr, "failed to find glibc.\n");
		return ret;
	}
	snprintf(cmd, sizeof(cmd), "nm '%s'", glibc);
	fp = popen(cmd, "r");
	if (!fp) {
		ret = errno;
		fprintf(stderr, "failed to create nm process: error "
			"%d\n", ret);
		return ret;
	}
	while (1) {
		str = fgets(cmd, sizeof(cmd), fp);
		if (!str)
			break;
		chomp(str);
		match_func(str, ++lineno);
	}
	debug("read %d lines in total from nm.\n", lineno);
	fclose(fp);
	for (i = 0; i < NUM_FUNCS; i++) {
		if (g_funcs[i].versions)
			continue;
		fprintf(stderr, "failed to find the versions for %s\n",
			g_funcs[i].name);
		ret = -ENOENT;
	}
	return ret;
}

void write_func_proto(FILE *fp, const struct func *fn)
{
	struct version *v;
	int i;
	const char *prefix;

	for (v = fn->versions; v; v = v->next) {
		if (!v->suffix[0]) {
			fprintf(fp, "%s %s(", fn->rtype, fn->name);
		} else {
			fprintf(fp, "%s %s_%d(",
				fn->rtype, fn->name, v->idx);
		}
		prefix = "";
		for (i = 0; fn->ptypes[i]; i++) {
			fprintf(fp, "%s%s var%d", prefix, fn->ptypes[i], i);
			prefix = ",";
		}
		fprintf(fp, ") {\n");
		fprintf(fp, "    return r_%s(", fn->name);
		prefix = "";
		for (i = 0; fn->ptypes[i]; i++) {
			fprintf(fp, "%svar%d", prefix, i);
			prefix = ",";
		}
		fprintf(fp, ");\n");
		fprintf(fp, "}\n\n");
	}
}

int write_shim(FILE *fp)
{
	struct func *fn;
	size_t i;
	struct version *v;

	fprintf(fp, "/*** THIS IS A GENERATED FILE.  DO NOT EDIT. ***/\n\n");
	for (i = 0; i < NUM_FUNCS; i++) {
		write_func_proto(fp, g_funcs + i);
	}
	for (i = 0; i < NUM_FUNCS; i++) {
		fn = g_funcs + i;
		for (v = fn->versions; v; v = v->next) {
			if (!v->suffix[0])
				continue;
			fprintf(fp, "__asm__(\".symver %s_%d, %s\");\n",
				fn->name, v->idx, v->suffix);
		}
	}
	return 0;
}

void write_vscript(FILE *fp)
{
	size_t i;
	const struct func *fn;
	const struct version *v;

	for (i = 0; i < NUM_FUNCS; i++) {
		fn = g_funcs + i;
		for (v = fn->versions; v; v = v->next) {
			if (!v->suffix[0])
				continue;
			fprintf(fp,
				"%s {\n"
				"    global:\n"
				"        %s;\n",
				v->suffix, fn->name
				);
			if (v != fn->v_max) {
				fprintf(fp,
					"    local:\n"
					"         *;\n");
			}
			fprintf(fp, "};\n");
		}
	}
};

static void usage(int retcode)
{
	fprintf(stderr, 
"make_linux_shim: creates the shim file needed to build Locksmith \n"
"against glibc.\n");
	fprintf(stderr, 
"-h             This help message.\n");
	fprintf(stderr, 
"-o [outfile]   Set the output shim file.\n");
	fprintf(stderr, 
"-s [file]      Set the output version script file.\n");
	exit(retcode);
	fprintf(stderr, 
"-v             Be verbose.\n");
	exit(retcode);
}

/**
 * Create the linux_shim.c source file.
 */
int main(int argc, char **argv)
{
	int ret, c;
	const char *shim_name = NULL;
	const char *vscript_name = NULL;
	FILE *fp;
	opterr = 0;

	while ((c = getopt(argc, argv, "h:o:s:v")) != -1) {
		switch (c) {
		case 'h':
			usage(0);
			break;
		case 'o':
			shim_name = optarg;
			break;
		case 's':
			vscript_name = optarg;
			break;
		case 'v':
			g_verbose = 1;
			break;
		default:
			fprintf(stderr, "getopt error.\n\n");
			usage(1);
			break;
		}
	}
	if (!shim_name) {
		fprintf(stderr, "you must specify an output shim file "
			"name with -o.\n\n");
		usage(1);
	}
	if (!vscript_name) {
		fprintf(stderr, "you must specify an output version script "
			"file name with -s.\n\n");
		usage(1);
	}
	ret = find_versions();
	if (ret) {
		fprintf(stderr, "failed to find versions of all the "
			"functions we need: error %d\n", ret);
		exit(1);
	}
	ret = func_number_all_versions();
	if (ret)
		exit(1);
	fp = fopen(shim_name, "w");
	if (!fp) {
		ret = errno;
		fprintf(stderr, "failed to open shim %s: error %d\n",
			shim_name, ret);
		exit(1);
	}
	ret = write_shim(fp);
	if (ret) {
		fprintf(stderr, "write_shim failed with error %d\n", ret);
		exit(1);
	}
	fclose(fp);
	fp = fopen(vscript_name, "w");
	if (!fp) {
		ret = errno;
		fprintf(stderr, "failed to open vscript %s: error %d\n",
			vscript_name, ret);
		exit(1);
	}
	write_vscript(fp);
	fclose(fp);
	fprintf(stderr, "done.\n");
	return 0;
}

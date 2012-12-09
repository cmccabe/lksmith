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
#include "tree.h"

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
	RB_ENTRY(version) entry;
	char *suffix;
	int idx;
};

/**
 * Compare two version numbers.
 * We always want the version with "@@" to come first, since that is the
 * default version.
 *
 * @param a		The first version
 * @param b		The second version
 *
 * @return		-1, 0, or 1, representing a < b, a == b, or a > b.
 */
static int version_compare(const struct version *a, const struct version *b)
{
	if (strncmp(a->suffix, "@@", 2) == 0) {
		if (strncmp(b->suffix, "@@", 2) == 0) {
			return strcmp(a->suffix, b->suffix);
		} else {
			return -1;
		}
	} else if (strncmp(b->suffix, "@@", 2) == 0) {
		return 1;
	}
	return strcmp(a->suffix, b->suffix);
}

RB_HEAD(version_tree, version);
RB_GENERATE(version_tree, version, entry, version_compare);

struct func {
	const char *const name;
	struct version_tree versions;
	const char *rtype;
	const char *ptypes[MAX_PARAMS];
};

static struct func g_funcs[] = {
{ "pthread_mutex_init", { NULL }, "int",
	{ "pthread_mutex_t*", "const pthread_mutexattr_t", NULL },
},
{ "pthread_mutex_destroy", { NULL }, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_mutex_trylock", { NULL }, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_mutex_lock", { NULL }, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_mutex_timedlock", { NULL }, "int",
	{ "pthread_mutex_t*", "const struct timespec*", NULL },
},
{ "pthread_mutex_unlock", { NULL }, "int",
	{ "pthread_mutex_t*", NULL },
},
{ "pthread_spin_init", { NULL }, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_destroy", { NULL }, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_lock", { NULL }, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_trylock", { NULL }, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_spin_unlock", { NULL }, "int",
	{ "pthread_spinlock_t*", NULL },
},
{ "pthread_cond_wait", { NULL }, "int",
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
	struct version *a, *b;

	a = xcalloc(sizeof(*a));
	a->suffix = xstrdup(suffix);

	b = RB_INSERT(version_tree, &fn->versions, a);
	if (b) {
		free(a->suffix);
		free(a);
		return;
	}
}

static int func_number_all_versions(void)
{
	int idx;
	struct version *v;
	size_t i;

	for (i = 0; i < NUM_FUNCS; i++) {
		idx = 0;
		RB_FOREACH(v, version_tree, &g_funcs[i].versions) {
			v->idx = idx++;
		}
		if (idx == 0) {
			func_add_version(&g_funcs[i], "");
		}
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
		debug("failed to parse line %d into three "
			"parts.\nline:%s\n", lineno, line);
		return 0;
	}
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
		if (!RB_EMPTY(&g_funcs[i].versions))
			continue;
		debug("failed to find a definition of %s; assuming that "
		      "we should use the oldest version.\n", g_funcs[i].name);
		func_add_version(&g_funcs[i], "");
	}
	return ret;
}

/**
 * Write out a function pointer array.
 *
 * @param fp		The file to write to
 * @param fn		The hijacked function to write out
 */
static void write_func_ptr_array(FILE *fp, struct func *fn)
{
	struct version *v;
	int num_versions = 0, i;
	const char *prefix;

	num_versions = 0;
	RB_FOREACH(v, version_tree, &fn->versions) {
		num_versions++;
	}
	fprintf(fp, "%s (*r_%s[%d])(", fn->rtype, fn->name, num_versions);
	prefix = "";
	for (i = 0; fn->ptypes[i]; i++) {
		fprintf(fp, "%s%s", prefix, fn->ptypes[i]);
		prefix = ", ";
	}
	fprintf(fp, ");\n");
}

/**
 * Write out an entry in the section which contains wrappers for handlers.
 *
 * @param fp		The file to write to
 * @param fn		The hijacked function to write out
 */
static void write_func_wrapper(FILE *fp, struct func *fn)
{
	struct version *v;
	int i;
	const char *prefix;

	RB_FOREACH(v, version_tree, &fn->versions) {
		if (!v->suffix[0]) {
			fprintf(fp, "%s %s(", fn->rtype, fn->name);
		} else {
			fprintf(fp, "%s %s_%d(",
				fn->rtype, fn->name, v->idx);
		}
		prefix = "";
		for (i = 0; fn->ptypes[i]; i++) {
			fprintf(fp, "%s%s var%d", prefix, fn->ptypes[i], i);
			prefix = ", ";
		}
		fprintf(fp, ") {\n");
		fprintf(fp, "    return h_%s(%d, ", fn->name, v->idx);
		prefix = "";
		for (i = 0; fn->ptypes[i]; i++) {
			fprintf(fp, "%svar%d", prefix, i);
			prefix = ", ";
		}
		fprintf(fp, ");\n");
		fprintf(fp, "}\n\n");
	}
}

static void write_shim_c(FILE *fp)
{
	struct func *fn;
	size_t i;
	struct version *v;

	fprintf(fp, "/*** THIS IS A GENERATED FILE.  DO NOT EDIT. ***/\n\n");
	fprintf(fp, "#include \"shim.h\"\n\n");
	fprintf(fp, "#include <pthread.h>\n\n");
	for (i = 0; i < NUM_FUNCS; i++) {
		/* Generate function pointer arrays.
		 *
		 * For each hijacked function, we store an array of function
		 * pointers.  The array is always at least one element long,
		 * and the element at the start is always the default
		 * function pointer.
		 */
		write_func_ptr_array(fp, g_funcs + i);
	}
	fprintf(fp, "\n");
	for (i = 0; i < NUM_FUNCS; i++) {
		/* Generate wrappers.
		 *
		 * For each hijacked function, there will be at least one
		 * wrapper which forwards calls to our handler functions.
		 * For hijacked functions that have multiple versions, there
		 * will be several.
		 */
		write_func_wrapper(fp, g_funcs + i);
	}
	for (i = 0; i < NUM_FUNCS; i++) {
		/* Generate symbol versioning directives, if necessary.
		 */
		fn = g_funcs + i;
		RB_FOREACH(v, version_tree, &g_funcs[i].versions) {
			if (!v->suffix[0])
				continue;
			fprintf(fp, "__asm__(\".symver %s_%d, %s\");\n",
				fn->name, v->idx, v->suffix);
		}
	}
}

static void write_shim_h(FILE *fp)
{
	struct func *fn;
	size_t i;
	int j;
	const char *prefix;

	fprintf(fp, "/*** THIS IS A GENERATED FILE.  DO NOT EDIT. ***/\n\n");
	fprintf(fp, "#ifndef LKSMITH_SHIM_DOT_H\n");
	fprintf(fp, "#define LKSMITH_SHIM_DOT_H\n");
	for (i = 0; i < NUM_FUNCS; i++) {
		/* Generate prototypes for the arrays of funtion pointers
		 * declared in shim.c
		 */
		fprintf(fp, "extern ");
		write_func_ptr_array(fp, g_funcs + i);
	}
	fprintf(fp, "\n");
	for (i = 0; i < NUM_FUNCS; i++) {
		fn = g_funcs + i;
		/* Generate prototypes for the handler functions declared in
		 * handler.c
		 */
		fprintf(fp, "extern %s h_%s(int lksmith_shim_ver, ",
			fn->rtype, fn->name);
		prefix = "";
		for (j = 0; fn->ptypes[j]; j++) {
			fprintf(fp, "%s%s", prefix, fn->ptypes[j]);
			prefix = ", ";
		}
		fprintf(fp, ");\n");
	}
	fprintf(fp, "#endif\n");
}

void write_vscript(FILE *fp)
{
	size_t i;
	struct func *fn;
	struct version *v;

	for (i = 0; i < NUM_FUNCS; i++) {
		fn = g_funcs + i;
		RB_FOREACH(v, version_tree, &fn->versions) {
			if (!v->suffix[0])
				continue;
			fprintf(fp,
				"%s {\n"
				"    global:\n"
				"        %s;\n",
				v->suffix, fn->name
				);
			if (v->idx != 0) {
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
"-o [outfile]   Set the output directory (default: current directory.)\n");
	fprintf(stderr, 
"-v             Be verbose.\n");
	exit(retcode);
}

struct generated_file {
	const char *suffix;
	FILE *fp;
};

enum {
	GGF_SHIM_C = 0,
	GGF_SHIM_H = 1,
	GGF_VSCRIPT = 2
};

struct generated_file g_generated_files[] = {
	{ "shim.c", NULL },
	{ "shim.h", NULL },
	{ "shim.ver", NULL },
	{ NULL, NULL },
};

static int open_generated_files(const char *out_path)
{
	int i;
	char path[PATH_MAX];

	for (i = 0; g_generated_files[i].suffix; i++) {
		snprintf(path, sizeof(path), "%s/%s", 
			out_path, g_generated_files[i].suffix);
		g_generated_files[i].fp = fopen(path, "w");
		if (!g_generated_files[i].fp) {
			fprintf(stderr, "failed to open %s\n", path);
			return -EIO;
		}
	}
	return 0;
}

static int close_generated_files(void)
{
	int i, ret = 0;

	for (i = 0; g_generated_files[i].suffix; i++) {
		if (!g_generated_files[i].fp)
			continue;
		if (fclose(g_generated_files[i].fp)) {
			fprintf(stderr, "error closing %s.\n",
				g_generated_files[i].suffix);
			ret = EIO;
		}
		g_generated_files[i].fp = NULL;
	}
	return ret;
}

/**
 * Create the linux_shim.c source file.
 */
int main(int argc, char **argv)
{
	int ret, c;
	const char *out_path = ".";
	opterr = 0;

	while ((c = getopt(argc, argv, "h:o:v")) != -1) {
		switch (c) {
		case 'h':
			usage(0);
			break;
		case 'o':
			out_path = optarg;
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
	ret = find_versions();
	if (ret) {
		fprintf(stderr, "failed to find versions of all the "
			"functions we need: error %d\n", ret);
		exit(1);
	}
	ret = func_number_all_versions();
	if (ret)
		exit(1);
	ret = open_generated_files(out_path);
	if (ret) {
		fprintf(stderr, "open_generated_files failed.\n");
		exit(1);
	}
	write_shim_c(g_generated_files[GGF_SHIM_C].fp);
	write_shim_h(g_generated_files[GGF_SHIM_H].fp);
	write_vscript(g_generated_files[GGF_VSCRIPT].fp);
	ret = close_generated_files();
	if (ret) {
		fprintf(stderr, "error closing generated files.\n");
	}
	debug("done.\n");
	return ret;
}

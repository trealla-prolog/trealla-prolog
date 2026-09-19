// Issue #801: an allocation failure while printing must become a
// catchable resource_error(memory), not an abort and not a silently
// truncated answer.
//
// Injecting the failure by size keeps this deterministic: the first run
// learns how big the printed text is, the second fails the buffer that
// text needs, which nothing else in the goal asks for. An overall memory
// cap would instead fail whatever allocation happened to come first.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "trealla.h"

#define TMPFILE "samples/oom.tmp"
// Every character of this atom escapes to \x1\, so printing it costs
// five times what holding it does and the print buffer stands alone by
// size. Control characters escape in any locale.

#define GOAL "length(L,60000), maplist(=(1),L), atom_codes(A,L), " \
	"catch((writeq(A),nl),_,(write(oom),nl))."

static size_t g_lo, g_hi;

static bool fails(size_t size)
{
	return g_hi && (size >= g_lo) && (size <= g_hi);
}

static void *lim_malloc(void *ctx, size_t size)
{
	(void)ctx;
	return fails(size) ? NULL : malloc(size);
}

static void *lim_realloc(void *ctx, void *ptr, size_t size)
{
	(void)ctx;
	return fails(size) ? NULL : realloc(ptr, size);
}

static void lim_free(void *ctx, void *ptr)
{
	(void)ctx;
	free(ptr);
}

// Copying a large attribute builds it in a scratch buffer of its own. If
// that copy ran out of memory, the outer scratch buffer it had swapped out
// was never put back, so it leaked. The attribute is bound after put_atts/2
// so that copying it is the biggest allocation in the goal, and every cap
// that fails part of that copy must still leave nothing behind.

#define ATTR_SRC ":- use_module(library(atts)).\n" \
	":- attribute big/1.\n" \
	"mk(0, []) :- !.\n" \
	"mk(N, [N|T]) :- N1 is N-1, mk(N1, T).\n" \
	"t :- put_atts(X, +big(V)), mk(100000, V), duplicate_term(f(X), _).\n"

static size_t attr_leak(size_t cap)
{
	pl_allocator_stats before, after;
	pl_get_allocator_stats(&before);
	prolog *pl = pl_create();

	if (!pl || !pl_consult_text(pl, ATTR_SRC, strlen(ATTR_SRC), "attr_leak"))
		return (size_t)-1;

	set_dump_vars(pl, 0);
	int saved = dup(1);		// an uncatchable failure reports on stdout

	if ((saved < 0) || !freopen("/dev/null", "w", stdout))
		return (size_t)-1;

	pl_sub_query *subq = NULL;
	g_lo = cap; g_hi = (size_t)-1;
	pl_query(pl, "catch(t, _, true)", &subq, 0);
	g_lo = g_hi = 0;

	if (subq)
		pl_done(subq);

	fflush(stdout);
	dup2(saved, 1);
	close(saved);
	clearerr(stdout);
	pl_destroy(pl);
	pl_get_allocator_stats(&after);
	return after.current_bytes - before.current_bytes;
}

// Run the goal with stdout on a file, and hand back what it wrote.

static char *run(size_t lo, size_t hi, size_t *len)
{
	prolog *pl = pl_create();
	int saved = dup(1);

	if (!pl || (saved < 0))
		return NULL;

	set_dump_vars(pl, 0);		// the goal writes what we check

	if (!freopen(TMPFILE, "w+", stdout))
		return NULL;

	pl_sub_query *subq = NULL;
	g_lo = lo; g_hi = hi;
	pl_query(pl, GOAL, &subq, 0);
	g_lo = g_hi = 0;

	if (subq)
		pl_done(subq);

	fflush(stdout);
	long size = ftell(stdout);
	rewind(stdout);
	char *buf = size >= 0 ? malloc(size+1) : NULL;
	size_t got = buf ? fread(buf, 1, size, stdout) : 0;

	if (buf)
		buf[got] = '\0';

	dup2(saved, 1);
	close(saved);
	clearerr(stdout);
	pl_destroy(pl);
	*len = got;
	return buf;
}

int main(void)
{
	pl_allocator a = { sizeof(a), NULL, lim_malloc, lim_realloc, lim_free };

	if (!pl_set_allocator(&a))
		return 1;

	size_t len = 0;
	char *out = run(0, 0, &len);

	if (!out || (len < 100*1024) || (out[0] != '\'') || (out[len-1] != '\n')) {
		fprintf(stderr, "oom: expected the quoted atom, got %zu bytes\n", len);
		return 1;
	}

	free(out);
	// Wide enough to catch a growth step, narrow enough to miss the
	// engine's own allocations.

	out = run(len-1500, len+1500, &len);

	// Anything else means it printed part of an answer it could not
	// finish, or the error never reached the catcher.

	if (!out || strcmp(out, "oom\n")) {
		fprintf(stderr, "oom: expected a caught resource_error, got \"%.40s\"\n",
			out ? out : "(null)");
		return 1;
	}

	free(out);
	remove(TMPFILE);

	for (size_t cap = 1000000; cap <= 8000000; cap += 500000) {
		size_t leaked = attr_leak(cap);

		if (leaked) {
			fprintf(stderr, "oom: attribute copy capped at %zu leaked %zu bytes\n",
				cap, leaked);
			return 1;
		}
	}

	return 0;
}

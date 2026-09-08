#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

typedef struct prolog_ prolog;
// Opaque. An empty struct is a GNU extension and not valid ISO C, which
// matters now this header is installed for embedders to include.
typedef struct pl_sub_query_ pl_sub_query;

// Trealla uses one runtime-wide allocator because atoms and other state are
// shared by every engine. Install it before the first pl_create() or any other
// Trealla allocation; later calls fail and leave the active allocator intact.
// The allocator callbacks and their context must remain valid until every
// Trealla allocation has been released.

typedef void *(*pl_malloc_fn)(void *context, size_t size);
typedef void *(*pl_realloc_fn)(void *context, void *ptr, size_t size);
typedef void (*pl_free_fn)(void *context, void *ptr);

typedef struct pl_allocator_ {
	size_t struct_size;
	void *context;
	pl_malloc_fn malloc_fn;
	pl_realloc_fn realloc_fn;
	pl_free_fn free_fn;
} pl_allocator;

typedef struct pl_allocator_stats_ {
	size_t current_bytes;
	size_t peak_bytes;
	size_t allocation_count;
	size_t failure_count;
} pl_allocator_stats;

bool pl_set_allocator(const pl_allocator *allocator);
void pl_get_allocator_stats(pl_allocator_stats *stats);
// Start a new peak measurement at the current live-byte count.
void pl_reset_allocator_peak(void);
void pl_free(void *ptr);

prolog *pl_create(void);
void pl_destroy(prolog*);

bool pl_consult(prolog*, const char *filename);
bool pl_consult_fp(prolog*, FILE *fp, const char *filename);
bool pl_consult_text(prolog*, const char *source, size_t source_len, const char *source_name);
bool pl_eval(prolog*, const char *expr, bool interactive);
bool pl_isatty(prolog*);
FILE *pl_stdin(prolog*);
bool pl_restore(prolog*, const char *filename);
bool pl_logging(prolog*, const char *filename);

bool pl_query(prolog*, const char *expr, pl_sub_query **q, unsigned int yield_time_in_ms);
bool pl_yield_at(pl_sub_query *q, unsigned int time_in_ms);
bool pl_did_yield(pl_sub_query *q);
bool pl_redo(pl_sub_query *q);
bool pl_done(pl_sub_query *q);	// only call if redo still active

// --- Inspecting an answer -------------------------------------------
//
// A pl_term is a view onto part of the current answer. It is owned by
// the query and stays valid until the next pl_redo() or pl_done() on
// that query - copy anything you need to keep. Text returned by
// pl_atom_text/pl_functor points into the engine and has the same
// lifetime; pl_term_text/pl_int_text return an owned string which the caller
// releases with pl_free().

typedef struct pl_term_ pl_term;

#define PL_TYPE_VAR      0
#define PL_TYPE_INTEGER  1
#define PL_TYPE_FLOAT    2
#define PL_TYPE_ATOM     3
#define PL_TYPE_STRING   4
#define PL_TYPE_COMPOUND 5

int         pl_term_type(pl_term*);
const char *pl_atom_text(pl_term*);			// atom or string, NUL-terminated
size_t      pl_atom_len(pl_term*);			// bytes, for embedded NULs
bool        pl_get_int64(pl_term*, int64_t*);	// false if it does not fit
bool        pl_get_float(pl_term*, double*);
char       *pl_term_text(pl_term*);			// canonical text, caller calls pl_free
											// (this is how a bignum is read)
char       *pl_int_text(pl_term*, int radix);	// an integer in base 2..36,
											// caller calls pl_free; NULL if not an
											// integer. Base 16 is how a host
											// with a limit on decimal parsing
											// reads an unbounded integer.
const char *pl_functor(pl_term*);
unsigned    pl_arity(pl_term*);
pl_term    *pl_arg(pl_term*, unsigned n);	// 0-based, NULL if out of range

// --- Reaching the bindings of the current answer ---------------------

unsigned    pl_num_bindings(pl_sub_query*);
const char *pl_binding_name(pl_sub_query*, unsigned i);
pl_term    *pl_binding_value(pl_sub_query*, unsigned i);
pl_term    *pl_binding(pl_sub_query*, const char *name);

int get_halt_code(prolog*);
bool get_error(prolog*);
bool get_halt(prolog*);
bool get_status(prolog*);
bool get_redo(prolog*);
bool did_dump_vars(prolog*);

void set_trace(prolog*);
void set_autofail(prolog*);
void set_quiet(prolog*);

// pl_query prints each answer the way the toplevel does. An embedder
// reading answers with the pl_term calls above almost certainly does not
// want that as well: set_dump_vars(pl, 0) turns it off. On by default,
// so existing hosts are unaffected.

void set_dump_vars(prolog*, int onoff);
void set_opt(prolog*, int onoff);
void set_limit(prolog*, int onoff);

void convert_path(char *filename);

extern int g_tpl_interrupt;
extern int g_ac, g_avc, g_argvc;
extern char **g_av, **g_argv, *g_argv0;
extern char *g_tpl_lib;
extern const char *g_version;
extern const char *g_compiled_at;

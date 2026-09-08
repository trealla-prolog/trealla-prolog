#pragma once

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#ifndef USE_FFI
#define USE_FFI 0
#endif

#ifndef USE_OPENSSL
#define USE_OPENSSL 0
#endif

#ifndef USE_THREADS
#define USE_THREADS 0
#endif

#include "tpl_features.h"

#if USE_THREADS
#include <pthread.h>
#include <unistd.h>
#endif

typedef double pl_flt;
typedef intmax_t pl_int;
typedef uintmax_t pl_uint;
typedef uint32_t pl_idx;
typedef uint32_t pl_ctx;

#define PL_INT_MAX INTMAX_MAX
#define PL_INT_MIN INTMAX_MIN

#if (__STDC_VERSION__ >= 201112L) && USE_THREADS
#include <stdatomic.h>
#define pl_atomic _Atomic

// Make a new structure visible before the pointer that reveals it.
// Writer-side only: the reader's address dependency orders its own
// loads. skiplist.c has its own copy, not including this header.

#define pl_publish_barrier() atomic_thread_fence(memory_order_release)
#else
#define pl_atomic volatile
#define pl_publish_barrier() ((void)0)
#endif

typedef pl_atomic int64_t pl_refcnt;

#include "list.h"
#include "skiplist.h"
#include "threads.h"
#include "trealla.h"
#include "utf8.h"
#include "stringbuf.h"

#include "imath/imath.h"
#include "imath/imrat.h"

#if defined(_WIN32) || defined(__wasi__)
char *realpath(const char *path, char resolved_path[PATH_MAX]);
#endif

// This MinGW target's errno.h has EAGAIN but not EWOULDBLOCK - on every
// other platform here the two are either the same value or both defined,
// so alias rather than special-case every errno==EAGAIN||EWOULDBLOCK check.
#if defined(_WIN32) && !defined(EWOULDBLOCK)
#include <errno.h>
#define EWOULDBLOCK EAGAIN
#endif

// Sentinel Value
#define ERR_IDX (~(pl_idx)0)
#define IDX_MAX (ERR_IDX-1)
#define CTX_NUL (ERR_IDX-1)

#define MAX_SMALL_STRING ((sizeof(void*)*2)-1)
#define MAX_VAR_POOL_SIZE 16000
#define MAX_ARITY UINT32_MAX
#define MAX_PROCEDURE_ARITY UINT8_MAX
#define MAX_IF_DEPTH 256
#define MAX_VARS 1024
#define MAX_QUEUES 256
#define MAX_MODULES 1024
#define MAX_IGNORES (1024*8)
#define MAX_CYCLE_VARS 64		// named cycle entries in one answer, see cycle_vars
// A ceiling now rather than an allocation: get_slot_name() grows its tables
// from nothing as a term turns out to need them. Past this many distinct
// variables in one term, printing falls back to naming them from the slot.
#define MAX_TABS 64000

// 1024 stream structs are 2.3MB, which is nothing in a process with gigabytes
// and was most of the engine in a freestanding image - so that build gets a
// small number instead. Not as small as it looks like it could be: a
// freestanding image has no open/4 and no sockets, but map_create/2 and
// engine_create/4 take a slot each and are perfectly ordinary predicates, so
// the limit is really "how many maps and engines at once", not "how many
// files". Running out throws rather than corrupting anything.
//
// A port that knows its own appetite can say so with -DMAX_STREAMS=n.

#ifndef MAX_STREAMS
#if TPL_FREESTANDING
#define MAX_STREAMS 16
#else
#define MAX_STREAMS 1024
#endif
#endif
// No longer a cap: threads, message queues and mutexes are allocated
// individually and chained off the prolog instance (see bif_threads.c),
// so the only ceiling is what the O/S will give us. Kept as the initial
// hint for anything that still wants a number.

#define MAX_THREADS 2048
#define MAX_ACTUAL_THREADS MAX_THREADS
#define MAX_STREAM_BUFLEN 1024

#define MAX_OF(a,b) (a) > (b) ? (a) : (b)
#define MIN_OF(a,b) (a) < (b) ? (a) : (b)

// Primary type...

#define is_var(c) ((c)->tag == TAG_VAR)
#define is_interned(c) ((c)->tag == TAG_INTERNED)
#define is_cstring(c) ((c)->tag == TAG_CSTR)
#define is_integer(c) ((c)->tag == TAG_INT)
#define is_float(c) ((c)->tag == TAG_FLOAT)
#define is_rational(c) ((c)->tag == TAG_RATIONAL)
#define is_indirect(c) ((c)->tag == TAG_INDIRECT)
#define is_blob(c) ((c)->tag == TAG_BLOB)
#define is_end(c) ((c)->tag == TAG_END)

// Derived type...

#define is_iso_atom(c) ((is_interned(c) || is_cstring(c)) && !get_arity(c))
#define is_iso_list(c) (is_interned(c) && (get_arity(c) == 2) && ((c)->val_off == g_dot_s))
#define is_smallint(c) (is_integer(c) && !((c)->flags & FLAG_INT_BIG))
#define is_bigint(c) (is_integer(c) && ((c)->flags & FLAG_INT_BIG))
#define is_boolean(c) ((is_interned(c) && !get_arity(c) && (((c)->val_off == g_true_s) || ((c)->val_off == g_false_s))))
#define is_atom(c) ((is_interned(c) && !get_arity(c)) || is_cstring(c))
#define is_string(c) (is_cstring(c) && ((c)->flags & FLAG_CSTR_STRING))
#define is_codes(c) (is_string(c) && ((c)->flags & FLAG_CSTR_CODES))
#define is_managed(c) ((c)->flags & FLAG_MANAGED)
#define is_cstr_blob(c) (is_cstring(c) && ((c)->flags & FLAG_CSTR_BLOB))
#define is_slice(c) (is_cstr_blob(c) && ((c)->flags & FLAG_CSTR_SLICE))
#define is_strbuf(c) (is_cstr_blob(c) && !((c)->flags & FLAG_CSTR_SLICE))
#define is_list(c) (is_iso_list(c) || is_string(c))
#define is_nil(c) (is_interned(c) && !get_arity(c) && ((c)->val_off == g_nil_s))
#define is_anon(c) ((c)->flags & FLAG_VAR_ANON)
#define is_builtin(c) (is_interned(c) && (c)->flags & FLAG_INTERNED_BUILTIN)
#define is_evaluable(c) (is_interned(c) && ((c)->flags & FLAG_INTERNED_EVALUABLE))
#define is_tail_call(c) ((c)->flags & FLAG_INTERNED_TAIL_CALL)
#define is_recursive_call(c) ((c)->flags & FLAG_INTERNED_RECURSIVE_CALL)
#define is_next_cut(c) ((c)->flags & FLAG_INTERNED_NEXT_CUT)
#define is_temporary(c) ((c)->flags & FLAG_VAR_TEMPORARY)
#define is_local(c) ((c)->flags & FLAG_VAR_LOCAL)
#define is_void(c) ((c)->flags & FLAG_VAR_VOID)
#define is_global(c) ((c)->flags & FLAG_VAR_GLOBAL)
#define is_ground(c) ((c)->flags & FLAG_INTERNED_GROUND)
#define is_ref(c) (is_var(c) && ((c)->flags & FLAG_VAR_REF))
#define is_op(c) ((c)->flags & 0xE000) ? true : false
#define is_callable(c) (is_interned(c) || (is_cstring(c) && !is_string(c)))
#define is_compound(c) (is_interned(c) && get_arity(c))
#define is_structure(c) (is_compound(c) || is_string(c))
#define is_number(c) (is_integer(c) || is_float(c) || is_rational(c))
#define is_atomic(c) (is_atom(c) || is_number(c))
#define is_iso_atomic(c) (is_iso_atom(c) || is_number(c))
#define is_nonvar(c) !is_var(c)

#define is_gt(c,n) (get_smallint(c) > (n))
#define is_ge(c,n) (get_smallint(c) >= (n))
#define is_eq(c,n) (get_smallint(c) == (n))
#define is_ne(c,n) (get_smallint(c) != (n))
#define is_le(c,n) (get_smallint(c) <= (n))
#define is_lt(c,n) (get_smallint(c) < (n))

#define get_list_head(c) ((c) + 1)
#define get_list_tail(c) (get_list_head(c) + get_list_head(c)->num_cells)

#define get_float(c) (c)->val_float
#define set_float(c,v) (c)->val_float = (v)
#define get_smallint(c) (c)->val_int
#define set_smallint(c,v) (c)->val_int = (v)
#define get_smalluint(c) (c)->val_uint
#define set_smalluint(c,v) (c)->val_uint = (v)
#define get_voidptr(c) (c)->val_voidptr
#define get_arity(c) (is_interned(c) ? (c)->arity : (uint32_t)(c)->small_arity)
#define set_arity(c,v) do { \
	if (is_interned(c)) { \
		(c)->small_arity = 0; \
		(c)->arity = (uint32_t)(v); \
	} else \
		(c)->small_arity = (uint8_t)(v); \
} while (0)

#define neg_bigint(c) (c)->val_bigint->ival.sign = MP_NEG
#define neg_smallint(c) (c)->val_int = -llabs((c)->val_int)
#define neg_float(c) (c)->val_float = -fabs((c)->val_float)

#define is_zero(c) (is_bigint(c) ?							\
	mp_int_compare_zero(&(c)->val_bigint->ival) == 0 :		\
	is_smallint(c) ? get_smallint(c) == 0 :					\
	is_float(c) ? get_float(c) == 0.0 : false)

#define is_negative(c) (is_bigint(c) ?						\
	(c)->val_bigint->ival.sign == MP_NEG :					\
	is_smallint(c) ? get_smallint(c) < 0 :					\
	is_float(c) ? get_float(c) < 0.0 : false)

#define is_positive(c) (is_bigint(c) ?						\
	mp_int_compare_zero(&(c)->val_bigint->ival) > 0 :		\
	is_smallint(c) ? get_smallint(c) > 0 :					\
	is_float(c) ? get_float(c) > 0.0 : false)

#define is_not_less_than_zero(c) (is_bigint(c) ?			\
	mp_int_compare_zero(&(c)->val_bigint->ival) >= 0 :		\
	is_smallint(c) ? get_smallint(c) >= 0 :					\
	is_float(c) ? get_float(c) >= 0.0 : false)


extern char *g_global_atoms;

typedef struct {
	pl_refcnt refcnt;
	size_t len;
	char cstr[];	// 'len+1' bytes
} strbuf;

typedef struct {
	pl_refcnt refcnt;
	union { mpz_t ival; mpq_t irat; };
} bigint;

typedef struct {
	pl_refcnt refcnt;
	char *ptr, *ptr2;
} blob;

#define _CSTRING_STR(c) 										\
	( is_strbuf(c) ? ((c)->val_strb->cstr + (c)->strb_off)		\
	: is_slice(c) ? (c)->val_str								\
	: (char*)(c)->val_chr										\
	)

#define _CSTRING_LEN(c) 										\
	( is_strbuf(c) ? (c)->strb_len								\
	: is_slice(c) ? (c)->str_len								\
	: (c)->chr_len												\
	)

#define _C_STR(pl,c) 											\
	( !is_cstring(c) ? (g_global_atoms + (c)->val_off)			\
	: _CSTRING_STR(c) 											\
	)

#define _C_STRLEN(pl,c) 										\
	( !is_cstring(c) ? strlen(g_global_atoms + (c)->val_off)	\
	: _CSTRING_LEN(c)											\
	)

#define C_STR(x,c) _C_STR((x)->pl, c)
#define C_STRLEN(x,c) _C_STRLEN((x)->pl, c)
#define C_STRLEN_UTF8(c) substrlen_utf8(C_STR(q, c), C_STRLEN(q, c))

#define GET_POOL(x,off) (g_global_atoms + (off))

#define _CMP_SLICE(pl,c,str,len) slicecmp(_C_STR(pl, c), _C_STRLEN(pl, c), str, len)
#define _CMP_SLICE2(pl,c,str) slicecmp2(_C_STR(pl, c), _C_STRLEN(pl, c), str)
#define _CMP_SLICES(pl,c1,c2) slicecmp(_C_STR(pl, c1), _C_STRLEN(pl, c1), _C_STR(pl, c2), _C_STRLEN(pl, c2))
#define _DUP_SLICE(pl,c) slicedup(_C_STR(pl, c), _C_STRLEN(pl, c))

#define CMP_STRING_TO_CSTRN(x,c,str,len) _CMP_SLICE((x)->pl, c, str, len)
#define CMP_STRING_TO_CSTR(x,c,str) _CMP_SLICE2((x)->pl, c, str)
#define CMP_STRING_TO_STRING(x,c1,c2) _CMP_SLICES((x)->pl, c1, c2)
#define DUP_STRING(x,c) _DUP_SLICE((x)->pl, c)

// If changing the order of these: see unify.c dispatch table

enum {
	TAG_EMPTY=0,
	TAG_VAR=1,
	TAG_INTERNED=2,
	TAG_CSTR=3,
	TAG_INT=4,
	TAG_FLOAT=5,
	TAG_RATIONAL=6,
	TAG_INDIRECT=7,
	TAG_BLOB=8,
	TAG_END=9
};

enum {
	FLAG_INT_HANDLE=1<<0,
	FLAG_INT_STREAM=1<<1,
	FLAG_INT_THREAD=1<<2,
	FLAG_INT_MAP=1<<3,
	FLAG_INT_ALIAS=1<<4,
	FLAG_INT_BIG=1<<5,
	FLAG_INT_TABLE=1<<6,

	FLAG_CSTR_BLOB=1<<0,
	FLAG_CSTR_SLICE=1<<1,
	FLAG_CSTR_STRING=1<<2,				// string of chars
	FLAG_CSTR_CODES=1<<3,				// string of codes

	FLAG_VAR_ANON=1<<0,
	FLAG_VAR_REF=1<<1,
	FLAG_VAR_CYCLIC=1<<2,
	FLAG_VAR_GLOBAL=1<<3,
	FLAG_VAR_TEMPORARY=1<<4,
	FLAG_VAR_LOCAL=1<<5,
	FLAG_VAR_VOID=1<<6,

	FLAG_HANDLE_DLL=1<<0,
	FLAG_HANDLE_FUNC=1<<1,

	FLAG_BLOB_SREGEX=1<<0,

	FLAG_INTERNED_TAIL_CALL=1<<0,
	FLAG_INTERNED_RECURSIVE_CALL=1<<1,
	FLAG_INTERNED_BUILTIN=1<<2,
	FLAG_INTERNED_EVALUABLE=1<<3,
	FLAG_INTERNED_GROUND=1<<4,
	FLAG_INTERNED_NEXT_CUT=1<<5,

	FLAG_LIVE=1<<11,					// used by bb_b_put/2
	FLAG_MANAGED=1<<12,					// any ref-counted object
	FLAG_END=1<<13						// DO NOT USE
};

// The OP types are stored in the high 3 bits of the flag (13-15)
// and only used during parsing

#define	OP_FX 1
#define	OP_FY 2
#define	OP_XF 3
#define	OP_YF 4
#define	OP_YFX 5
#define	OP_XFX 6
#define	OP_XFY 7

#define IS_PREFIX(op) (((op) == OP_FX) || ((op) == OP_FY))
#define IS_POSTFIX(op) (((op) == OP_XF) || ((op) == OP_YF))
#define IS_INFIX(op) (((op) == OP_XFX) || ((op) == OP_XFY) || ((op) == OP_YFX))
#define IS_XF(op) ((op) == OP_XF)
#define IS_YF(op) ((op) == OP_YF)

#define is_prefix(c) IS_PREFIX(GET_OP(c))
#define is_postfix(c) IS_POSTFIX(GET_OP(c))
#define is_infix(c) IS_INFIX(GET_OP(c))

#define is_fx(c) (GET_OP(c) == OP_FX)
#define is_fy(c) (GET_OP(c) == OP_FY)
#define is_xf(c) (GET_OP(c) == OP_XF)
#define is_yf(c) (GET_OP(c) == OP_YF)
#define is_yfx(c) (GET_OP(c) == OP_YFX)
#define is_xfx(c) (GET_OP(c) == OP_XFX)
#define is_xfy(c) (GET_OP(c) == OP_XFY)

#define SET_OP(c,op) (CLR_OP(c), (c)->flags |= (((uint16_t)(op)) << 13))
#define CLR_OP(c) ((c)->flags &= ~((uint16_t)(0xF) << 13))
#define GET_OP(c) (((c)->flags >> 13) & 0xF)
#define IS_OP(c) (GET_OP(c) != 0)

typedef struct module_ module;
typedef struct query_ query;
typedef struct predicate_ predicate;
typedef struct rule_ rule;
typedef struct cell_ cell;
typedef struct clause_ clause;
typedef struct trail_ trail;
typedef struct trail_page_ trail_page;
typedef struct choice_page_ choice_page;
typedef struct frame_page_ frame_page;
typedef struct frame_ frame;
typedef struct parser_ parser;
typedef struct page_ page;
typedef struct stream_ stream;
typedef struct capture_ capture;
typedef struct slot_ slot;
typedef struct choice_ choice;
typedef struct run_state_ run_state;
typedef struct prolog_flags_ prolog_flags;

// A term handed to an embedder through src/trealla.h. Owned by the
// query, invalidated by pl_redo/pl_done - see the header.

struct pl_term_ {
	struct query_ *q;
	cell *c;
	pl_ctx ctx;
};
typedef struct {
	cell *queue;
	pl_idx qp, qcnt, q_size;
} qbuf;

typedef struct builtins_ builtins;
typedef struct scheduler_ scheduler;

// Using a fixed-size cell allows having arrays of cells, which is
// basically what a Term is. A compound is a variable length array of
// cells, the length specified by 'num_cells' field in the 1st cell.
// A cell is a tagged union.
// The size should be 24 bytes... (1 + 2) * 8

struct cell_ {

	// 1 * 8 = 8 bytes

	uint8_t tag;
	uint8_t small_arity;				// used by strings/CSTR callables
	uint16_t flags;

	union {
		uint32_t num_cells;				// number of cells
		uint32_t mid;					// used with TAG_EMPTY
	};

	// 2 * 8 = 16 bytes.

	union {

		void *val_voidptr;
		pl_uint val_uint;
		pl_int val_int;
		pl_flt val_float;
		bigint *val_bigint;
		blob *val_blob;
		uint16_t priority;				// used during parsing

		struct {
			uint8_t	chr_len;
			char val_chr[MAX_SMALL_STRING];
		};

		struct {
			strbuf *val_strb;			// ref-counted string
			uint32_t strb_off;			// ... offset
			uint32_t strb_len;			// ... length
		};

		struct {
			char *val_str;				// static string
			uint64_t str_len;			// ... length
		};

		struct {
			union {
				predicate *match;		// used with TAG_INTERNED
				builtins *bif_ptr;		// used with TAG_INTERNED
				cell *tmp_attrs;		// used with TAG_VAR in copy_term
				cell *val_ptr;			// used with TAG_INDIRECT
				cell *val_attrs;		// used with TAG_EMPTY in slot
			};

			union {
				uint32_t var_num;		// used with TAG_VAR
				uint32_t arity;			// used with TAG_INTERNED
			};

			union {
				uint32_t val_off;		// used with TAG_INTERNED / TAG_VAR -FLAG_VAR_REF
				pl_ctx val_ctx;			// used with TAG_INDIRECT / TAG_VAR +FLAG_VAR_REF
			};
		};

		struct {
			cell *ret_instr;			// used with TAG_EMPTY in call
			uint64_t chgen;				// saves choice generation
		};
	};
};

typedef struct {
	uint64_t u1, u2;					// TODO: proper uuid's
} uuid;

struct clause_ {
	cell *alt;							// alternate representation
	pl_idx cidx, num_allocated_cells;
	unsigned num_vars;
	bool is_first_cut:1;
	bool is_unique:1;
	bool is_fact:1;
	bool is_deleted:1;
	cell cells[];						// 'num_allocated_cells'
};

struct rule_ {
	lnode hdr;							// must be first
	predicate *owner;
	rule *prev, *next;
	const char *filename;
	uuid u;
	uint64_t db_id, matched, attempted, tcos;
	uint64_t dbgen_created, dbgen_retracted;
	unsigned line_num_start, line_num_end;
	clause cl;
};

// Note: use head/tail as an entry can't be on two intrusive lists

struct predicate_ {
	lnode hdr;							// must be first
	predicate *alias;
	rule *head, *tail;
	module *m;
	skiplist *idx0, *idx1, *idx2;
	const char *filename;
	cell *meta_args;
	list dirty;
	cell key;
	pl_refcnt refcnt, cnt, db_id;
	unsigned max_vars, idx2_arg;
	bool is_reload:1;
	bool is_builtin:1;
	bool is_public:1;
	bool is_dynamic:1;
	bool is_meta_predicate:1;
	bool is_multifile:1;
	bool is_discontiguous:1;
	bool is_abolished:1;
	bool is_noindex:1;
	bool is_check_directive:1;
	bool is_processed:1;
	bool is_var_in_head:1;
	bool is_var_in_first_arg:1;
	bool is_var_in_idx2_arg:1;
	bool is_iso:1;
	bool is_dirty:1;

	// Incremental tabling (item 3). is_incremental is opt-in via
	// ":- incremental q/1" and is tested in enter_predicate() before
	// any dependency work, so a program that never declares one pays
	// a single already-cached bit test. last_modified is stamped from
	// pl->dbgen on assert/retract; a table compares it against the
	// generation it completed at to decide whether it is still valid.

	bool is_incremental:1;
	uint64_t last_modified;
};

// The tail of a builtins initialiser. Shorter without FFI, because the
// argument-type arrays and libffi handles are not in the struct at all then.

#if USE_FFI
#define BLAH false, false, {0}, {0}, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL
#else
#define BLAH false, false, NULL, NULL, NULL, NULL
#endif

#define MAX_FFI_ARGS 64

struct builtins_ {
	const char *name;
	unsigned arity;
	bool (*fn)(query*);
	const char *help;
	bool iso;
	bool evaluable;
	bool ffi;
	bool via_directive;

	// FFI argument metadata, and only that. At MAX_FFI_ARGS of 64 these two
	// arrays are 576 bytes - 85% of the whole struct - carried by every
	// builtin in every table whether or not the build has any FFI. Leaving
	// them out where USE_FFI is off takes about 290KB off a freestanding
	// image without removing a single predicate. Nothing outside
	// src/bif_ffi.c reads them.

#if USE_FFI
	uint8_t types[MAX_FFI_ARGS];
	const char *names[MAX_FFI_ARGS];
	uint8_t ret_type;
	void *ffi_ret_type;
	const char *ret_name;
#endif

	module *m;
	char *desc;
	char *help2;
	char *help_alt;

#if USE_FFI
	// Pre-compiled libffi call interface, built once at registration.
	// void* so this header doesn't have to drag in ffi.h. NULL means
	// the signature can't be pre-compiled (it returns a struct) and
	// the cif is built per call as before.

	void *cif;
#endif
};

typedef struct {
	const char *name;
	unsigned specifier;
	unsigned priority;
} op_table;

// Where *ctx* is the context of the var
// And *var_num* is the slot within that context

struct trail_ {
	cell *attrs;
	pl_ctx val_ctx;
	uint32_t var_num;
};

// Trail entries are addressed by their absolute trail index: choicepoints
// retain those indexes across backtracking.  Pages keep the entries stable
// without copying them when the trail grows.
struct trail_page_ {
	trail_page *prev, *next;
	trail *entries;
	pl_idx base, page_size;
};

// Where *c* is the (possibly) instantiated cell in the current frame
// Where *vgen* & *vgen2* represent the visit generation to check for cyclic terms

struct slot_ {
	cell c;
	uint32_t vgen, vgen2;
};

// Where *prev* is the previous frame
// Where *initial_slots* is the number allocated
// Where *actual_slots* is the number allocated+created
// Where *base* is the offset to first slot in use
// Where *op* is the offset to first overflow slot in use
// Where *chgen* is the choice generation that created this frame

struct frame_ {
	cell *instr;
	module *m;
	uint64_t dbgen, chgen;
	uint32_t hp_num, initial_slots, actual_slots, max_vars;
	pl_idx base, op, hp;
	pl_ctx prev;
	pl_ctx idx;
	bool no_recov:1;
	bool heap_pinned:1;
};

#define FRAME_PAGE_SHIFT 7
#define FRAME_PAGE_SIZE (1U << FRAME_PAGE_SHIFT)

struct run_state_ {
	predicate *pr;
	cell *instr;
	rule *dbe;
	sliter *iter, *tmp_iter;

	// Which choicepoint owns *iter when it is a multi-hit prefetch.
	// run_state is copied into every choice after find_key(), so several
	// slots alias the same handle; only this one may free it.
	pl_idx iter_owner;

	module *m;

	union {
		struct {
			cell *key;
			pl_ctx key_ctx;
			bool karg1_is_ground:1, karg2_is_ground:1, karg3_is_ground:1,
			karg1_is_atomic:1, karg2_is_atomic:1, karg3_is_atomic:1,
			iter_single:1;
		};
		struct { uint64_t uv1, uv2; };
		struct { int64_t v1, v2; };
		struct { cell *c; pl_ctx c_ctx; };
		int64_t cnt;
	};

	uint64_t cpu_time;
	pl_idx fp, hp, cp, tp, sp, hp_num, qnum;
	pl_ctx cur_ctx;
};

typedef struct {
	lnode hdr;							// must be first
	module *m;
	union {
		char *key;
		cell *c;
		rule *r;
	};

	bool is_bboard:1;
	bool is_cells:1;
	bool is_rule:1;
} undo_item;

struct choice_ {
	run_state st;
	list undo;
	uint64_t gen, chgen, dbgen;
	pl_idx base, op, initial_slots, actual_slots, skip;
	bool catchme_retry:1;
	bool catchme_exception:1;
	bool barrier:1;
	bool register_cleanup:1;
	bool block_catcher:1;
	bool fail_on_retry:1;
	bool succeed_on_retry:1;
	bool reset:1;
};

struct choice_page_ {
	choice_page *prev, *next;
	choice *entries;
	pl_idx base, page_size;
};

enum { eof_action_eof_code, eof_action_error, eof_action_reset };

// with_output_to/2 captures what a goal writes by making the stream
// hold its output in memory. Captures nest, so each one records where
// in that buffer it began: the text it captured is what was appended
// past its mark, and taking it truncates the buffer back so that the
// capture around it never sees it. The marks form a stack because
// setup_call_cleanup/3 makes the pairing last-in-first-out.

struct capture_ {
	capture *prev;
	size_t at;
};

struct stream_ {
	union {
		FILE *fp;
		FILE *fp_in;
		skiplist *keyval;
		query *engine;
		void *handle;
	};

	FILE *fp_out;
	stringbuf sb_buf;
	char *mode, *filename, *data, *src, *addr;
	skiplist *alias;
	void *sslptr;
	parser *p;
	capture *captures;					// innermost first, NULL when not capturing

	union {
		char srcbuf[MAX_STREAM_BUFLEN];
		struct {
			cell *pattern, *cur_yield;
		};
	};

	char *wbuf;							// a task's parked write, see write_all()
	const query *wbuf_owner;			// ... and whose it is
	const query *data_owner;			// whose partial read str->data is
	unsigned timeout_ms;

	// open/4's mmap(Ls) option maps the whole file. Nothing used to
	// release it - munmap appeared nowhere in the tree - so every such
	// open retained the file for the life of the process. The stream
	// owns the mapping and unmaps it at close.

	void *mmap_addr;
	size_t mmap_len;

	size_t data_len, alloc_nbytes, wbuf_len, wbuf_pos;
	int ungetch, srclen, chan, idx, port;
	unsigned rows, cols;
	uint8_t level, eof_action;
	bool is_active:1;
	bool at_end_of_file:1;
	bool bom:1;
	bool repo:1;
	bool binary:1;
	bool did_getc:1;
	bool invalid_pending:1;				// a peeked ill-formed sequence, still unread
	bool nodelay:1;
	bool udp:1;
	bool ssl:1;
	bool first_time:1;
	bool is_pipe:1;
	bool is_popen:1;
	bool is_socket:1;
	bool is_map:1;
	bool is_memory:1;
	bool is_engine:1;
	bool is_alias:1;
};

// Timeouts are a polled monotonic deadline kept per thread object, not
// a signal - see has_expired_alarm(). This used to be the fallback for
// hosts with no usable POSIX per-thread timer (Windows and WASI have no
// such API; on OpenBSD and NetBSD timer_create() is there but a limit
// armed through it never fires), and is now the only path.
//
// It is also the only design that survives a thread becoming a task: a
// SIGALRM handler can only ask which *pthread* it is on, which under a
// worker pool is the worker rather than the thread object that armed
// the timer. The deadline is keyed to the object throughout.

typedef struct alarm_entry_ alarm_entry;
typedef struct thread_ thread;

struct thread_ {
	const char *filename;
	char *alias;
	prolog *pl;
	query *q;
	cell *goal, *exit_code, *at_exit_goal, *ball;
	list signals, queue;
#if USE_THREADS
    pthread_t id;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
#endif
	lock guard;

	// Tabling state (tries, tables, worklists, SCC stack). Per THREAD,
	// not per prolog and not in statics: a table is only ever read and
	// written by the thread that built it, so tabling needs no locking
	// and every thread may table. threads[0] is the main thread, so
	// two prolog instances still do not share tables either.
	// Opaque: owned and shaped by src/tabling.c.

	void *tabling_state;

	alarm_entry *alarms;					// polled timers, see has_expired_alarm()

	// Intrusive links. live_* chain every live entry in increasing chan
	// order, which is what lets the table be walked without allocating
	// or locking - the SIGALRM handler does exactly that, and a skiplist
	// iterator would do both. free_next chains retired structs awaiting
	// reuse; a struct is on one list or the other, never both.

	thread *live_next, *live_prev, *free_next;

	// Tasks are scheduled per thread object, not per query and not per
	// instance. Per query was phase 0's problem: a scheduler that died
	// with whoever spawned into it. Per instance was phase 0's fix and
	// went too far - two real threads each draining their own tasks
	// then drove one set of queues with no lock, which crashed six runs
	// in ten. A thread object is the thing that actually owns a run
	// queue: it outlives any one query, and only ever has one thread in
	// it.

	scheduler *sched;

	// Tasks parked on this queue waiting for a message. A send walks
	// these and promotes them, which is what makes a receive wake on
	// delivery rather than on its next poll.

	query *msg_waiters;

	unsigned num_vars, at_exit_goal_num_vars, num_locks;
	int chan, locked_by;
	pl_atomic bool is_active;
	volatile int timedout;   // set by SIGALRM handler s_sigfn(); thread-lived
	bool is_init:1;
	bool is_finished:1;
	bool is_detached:1;
	bool is_exception:1;
	bool is_failed:1;
	bool is_queue_only:1;
	bool is_mutex_only:1;
};

struct page_ {
	page *next;
	cell *cells;
	pl_idx idx, page_size;
	unsigned num;
};

enum q_retry { QUERY_OK=0, QUERY_NOOP=1, QUERY_RETRY=2, QUERY_EXCEPTION=3, QUERY_ABORT=4 };
enum unknowns { UNK_FAIL=0, UNK_ERROR=1, UNK_WARNING=2, UNK_CHANGEABLE=3 };
enum occurs { OCCURS_CHECK_FALSE=0, OCCURS_CHECK_TRUE=1, OCCURS_CHECK_ERROR = 2 };

struct prolog_flags_ {
	enum occurs occurs_check;
	enum unknowns unknown, syntax_error;
	bool double_quote_codes:1;
	bool double_quote_chars:1;
	bool double_quote_atom:1;
	bool character_escapes:1;
	bool char_conversion:1;
	bool strict_iso:1;
	bool debug:1;
	bool json:1;
	bool var_prefix:1;

	// Read f() as '()'(f) instead of rejecting it. Off by default, so
	// nothing changes unless a file asks for it. Deliberately NOT behind
	// #ifdef USE_JANUS: only src/library.o is compiled with that define,
	// so an #ifdef here would give that one object a different
	// prolog_flags layout from the rest of the engine.
	bool empty_args:1;
};

typedef struct {
	pl_ctx ctx;
	pl_idx val_off;
	unsigned var_num, cnt;
	bool is_anon;
} var_item;

// Ephemeral compound-pair memo for one unify() call (keyed by q->vgen).
// Avoids re-walking shared DAG nodes (e.g. issue #855 blam/1).
//
// It GROWS. A fixed table stops memoizing once full, and the walk it
// exists to prevent is exponential - so a capacity is not a slowdown
// threshold, it is the bug returning at the next size up. Held at load
// factor <= 1/2 (which is also what lets the linear probe terminate),
// doubling, and allocated on first use so a query that never unifies a
// compound pair pays nothing.
#define UNIFY_SEEN_SIZE 256		// initial capacity, power of two

typedef struct {
	cell *c1, *c2;
	pl_ctx ctx1, ctx2;
	uint32_t gen;
} unify_seen_pair;

struct query_ {
	lnode hdr;							// must be first
	query *prev, *next, *parent;
	module *current_m;
	prolog *pl;
	// `top` is the parser that read the query text, usually borrowed.
	// `p` is a scratch parser for tokenizing text mid-query, built on
	// first use by query_parser() and belonging to parser_m - see there
	// for why it is not built up front.
	parser *top, *p;
	module *parser_m;
	bool owns_top;						// destroy top with the query
	struct pl_term_ **terms;			// arena for the embedding API
	unsigned terms_used, terms_cap;
	slot *slots;
	cell *tmp_heap, *last_arg, *variable_names, *ball, *cont, *suspect;
	void *oom_reserve;					// emergency headroom for constructing a memory error
	cell *clone_root;					// the term copy_term/2 is copying, for cycles back to it
	bool cycle_dropped;					// a clone hit a cycle it could not represent and truncated it
	skiplist *clone_defs;				// close_cycles only: original slot -> tmp offset where its value starts
	bool close_cycles;					// opt-in (copy_term/2, copy_term_nat/2 only): bind back-edges to
										// nested cyclic slots instead of leaving them dangling - see clone_defs
	// One nesting depth of findall/bagof: the solution buffer, how far
	// into it we are, how many solutions it holds and how big it is.
	// These were six MAX_QUEUES arrays side by side - 8KB in every query,
	// for a nesting depth that is nearly always one - of which tmpq and
	// tmpq_size were never read anywhere at all. Grown on demand now, one
	// depth at a time, and freed with the query.

	qbuf *queues;
	unsigned queues_alloc;
	page *heap_pages;
	trail_page *trail_pages, *trail_current;
	trail *trail_next;
	choice_page *choice_pages, *choice_current;
	choice *choice_next;
	frame **frame_pages;
	slot *save_e;
	query *tasks;						// tasks we spawned, our registry of them
	unsigned num_subtasks;				// ... and how many live below us, any depth

	// Task scheduling, see bif_tasks.c. The scheduler itself now hangs
	// off the prolog instance rather than off whoever spawned a task -
	// the fields here are what a task needs to sit in its queues.

	query *sched_next;					// link in the ready FIFO or the io list
	unsigned heap_idx;					// our slot in the timer heap
	int wait_fd;						// descriptor we parked on, if waiting_io
	uint64_t msg_deadline;				// absolute ms for a receive we parked in
	query *wait_next;					// link in a queue's msg_waiters
	thread *waiting_on;					// ... and which queue that is
	short wait_events;					// ... and what we are waiting for
	uint8_t sched_where;				// which of the three we are queued on

	skiplist *vars;
	thread *thread_ptr;
	var_item *tabs;
	size_t tabs_size;
	list dirty, undo;
	cell accum;
	mpz_t tmp_ival;
	mpq_t tmp_irat;
	run_state st;
	stringbuf sb_buf;
	// Name numbers already spoken for by variables the user wrote as _A,
	// _B1 and so on, so a generated name never collides with a source
	// one - see get_slot_name(). Was a flat bool[MAX_IGNORES]: 8KB per
	// query, and an 8KB memset every time the write options were
	// cleared, for a set that is usually a few dozen entries. Grown on
	// demand through ignore_name(), and freed with the query.

	bool *ignores;
	unsigned ignores_alloc;

	// Cycle-entry variables met while dumping an answer (issue #1138).
	// A cyclic term whose cycle starts below the reported variable needs
	// a name of its own for the loop to close on: S = "zabcdef"||_S1 with
	// _S1 = "abcdef"||_S1, not S = "zabcdef"||S, which says something
	// else. Shared so two answers over one cycle name it once.

	struct { uint32_t var_num; pl_ctx ctx; } cycle_vars[MAX_CYCLE_VARS];
	unsigned num_cycle_vars;

	// The term being dumped. A spine leading back to it closes the loop
	// there, so the walk stops rather than going round once more.
	const cell *dump_var_cell;
	pl_ctx dump_var_cell_ctx;

	uint64_t total_goals, total_backtracks, total_retries, total_matches, total_inferences;
	uint64_t total_tcos, total_recovs, total_matched, total_no_recovs;
	uint64_t step, qid, tmo_msecs, chgen, cycle_error;
	uint64_t get_started, yield_at;
	uint64_t cpu_time;					// time/1 baseline, kept out of st so it survives backtracking
	uint64_t time_cpu_last_started, future;
	unsigned max_depth, max_eval_depth, print_idx, tab_idx, dump_var_num;
	unsigned name_idx;		// next free generated-name number, see get_slot_name()

	// Generated variable names, indexed by print_idx above. These used to
	// be two fixed MAX_TABS arrays on the prolog instance - half a
	// megabyte, allocated before a clause was consulted, for naming that
	// most programs never do. The cursor into them was always per-query,
	// so that was also the wrong owner: two queries printing at once wrote
	// over each other's entries. Grown on demand now, and freed with the
	// query.

	pl_idx *tab1, *tab2;
	unsigned tabs_alloc;
	unsigned varno, tab0_varno, cur_engine, cur_chan, my_chan;

	// A task's own mailbox: list of task_msg nodes (bif_tasks.c),
	// scanned in place by recv/1 so a skipped message keeps its
	// position rather than rotating to the back. Guarded by the
	// owning thread's scheduler->guard, since send/2 can enqueue into
	// this from any thread. cur_task_qid is the sender of the last
	// message recv/1 matched - stored for a reply-to, not yet exposed
	// to Prolog. Separate from cur_chan (a thread chan, unsigned):
	// qid is process-wide and uint64_t, would truncate if shared.

	list mailbox;
	uint64_t cur_task_qid;

	// task_cancel/1's cross-thread signal. Deliberately not one of the
	// bool:1 flags below (error, yielded, no_recov, ...): those are
	// packed into a handful of shared bytes, read-modify-written
	// together, and only ever safe to touch from the task's own owning
	// thread while it runs start() on them. A foreign thread calling
	// task_cancel/1 writes only this one, real, standalone atomic -
	// sched_run() is what turns a pending request into `error = true`,
	// from inside the owning thread, at its own dispatch point, which
	// is what actually cancels the task. See the internal.h bitfield
	// note above pl->did_dump_vars for the class of bug this avoids.

	pl_atomic bool cancel_requested;
	unsigned s_cnt, retries, rand_seed;
	int autofail_n;
	pl_ctx latest_ctx, variable_names_ctx, dump_var_ctx, ball_ctx, cont_ctx;
	pl_ctx clone_root_ctx;				// context of clone_root, which alone does not identify a term
	pl_idx tmphp;
	pl_idx frame_pages_size, slots_size;
	pl_idx before_hook_tp;
	pl_idx heap_size, tmph_size;
	pl_idx undo_lo_tp, undo_hi_tp;
	prolog_flags flags;
	enum q_retry retry;
	int is_cyclic1, is_cyclic2;
	uint32_t vgen;
	unify_seen_pair *unify_seen;
	unsigned unify_seen_size, unify_seen_used;
	int8_t halt_code;
	int8_t quoted;
	enum { WAS_OTHER, WAS_SPACE, WAS_COMMA, WAS_SYMBOL } last_thing;
	bool oom:1;
	bool in_throw:1;
	bool done:1;
	bool noskip:1;
	bool parens:1;
	bool in_attvar_print:1;
	bool lists_ok:1;
	bool fail_on_retry:1;
	bool noretry:1;
	bool is_redo:1;
	bool run_hook:1;
	bool do_dump_vars:1;
	bool is_dump_vars:1;
	bool portray_vars:1;
	bool status:1;
	bool no_recov:1;
	bool has_vars:1;
	bool error:1;
	bool did_throw:1;
	bool trace:1;
	bool creep:1;
	bool eval:1;
	bool yield_after:1;
	bool yielded:1;
	bool is_task:1;
	bool is_thread:1;
	bool is_registered:1;			// lazily added to pl->tasks - see bif_task_self_1
	bool json:1;
	bool nl:1;
	bool fullstop:1;
	bool portrayed:1;
	bool ignore_ops:1;
	bool numbervars:1;
	bool halt:1;
	bool abort:1;
	bool spawned:1;
	bool run_init:1;
	bool varnames:1;
	bool listing:1;
	bool did_quote:1;
	bool is_input:1;
	bool is_engine:1;
	bool ops_dirty:1;
	bool noderef:1;
	bool double_quotes:1;
	bool end_wait:1;
	bool waiting_io:1;
	bool did_unhandled_exception:1;
	bool access_private:1;
	bool in_retract:1;
};

// What the parser records about one variable it has seen. These were six
// parallel arrays fixed at MAX_VARS, beside a MAX_VAR_POOL_SIZE name pool -
// 37508 bytes in every parser, for clauses that nearly always have a handful
// of variables. One of the six, vars[], was never read anywhere.
//
// Both the entries and the pool grow on demand now, still capped at MAX_VARS
// and MAX_VAR_POOL_SIZE so the same errors fire at the same limits.

typedef struct {
	unsigned used, depth, in_body, in_head;
	pl_idx off;
} var_entry;

struct parser_ {
	struct {
		// Names, NUL-separated and parallel to v[]: entry i is the i'th
		// name in the pool. Reading past num_vars is normal - a variable
		// invented after the parse has no entry - so go through
		// vartab_off() rather than indexing v[] directly.
		char *pool;
		var_entry *v;
		unsigned pool_size, alloc, num_vars;
	} vartab;

	prolog *pl;
	FILE *fp;
	module *m;
	clause *cl;
	cell v;
	stringbuf token_buf;
	prolog_flags flags;
	query *q;
	char *save_line, *srcptr, *error_desc;
	const char *error_type;				// NULL means syntax_error
	size_t token_size, n_line, pos_start;
	unsigned line_num, line_num_start;
	unsigned depth, read_term_slots, num_vars;
	unsigned nesting_parens, nesting_braces, nesting_brackets;
	int quote_char, entered;
	bool error, if_depth[MAX_IF_DEPTH];
	bool was_consing:1;
	bool was_string:1;
	bool was_partial:1;
	bool did_getline:1;
	bool already_loaded_error:1;
	bool do_read_term:1;
	bool internal:1;
	bool one_shot:1;
	bool start_term:1;
	bool end_of_term:1;
	bool end_of_file:1;
	bool is_directive:1;
	bool is_command:1;
	bool is_comment:1;
	bool is_consulting:1;
	bool expand_dcg:1;			// re-parse of term_expansion/2 output: translate a --> result
	bool saw_initialization:1;		// this load recorded a goal of its own
	bool is_symbol:1;
	bool is_string:1;
	bool is_quoted:1;
	bool is_var:1;
	bool is_op:1;
	bool skip:1;
	bool last_close:1;
	bool last_empty_arglist:1;		// the arg list just closed was f()
	bool last_neg:1;
	bool no_fp:1;
	bool reuse:1;
	bool interactive:1;
	bool in_body:1;
	bool is_number_chars:1;
	bool double_bar:1;
	bool pending_bar:1;			// single '|' seen after a closing quote, deferred to the next token (issue #1134)
	bool is_socket:1;			// fp is a blocking-mode socket; see tpl_wait_fd_readable()
};

// The name offset recorded for a variable, or 0 if it has none. Callers used
// to index a fixed array and get a zeroed slot for anything the parser never
// saw; the bounds check is what keeps that true now that the array fits.

static inline pl_idx vartab_off(const parser *p, unsigned i)
{
	return (i < p->vartab.alloc) ? p->vartab.v[i].off : 0;
}

typedef struct loaded_file_ loaded_file;

// Predicate indicator...

typedef struct pi_ {
	lnode hdr;							// must be first
	struct pi_ *prev, *next;			// ???
	cell key;
} pi;

struct module_ {
	lnode hdr;							// must be first
	// Modules this one uses, appended as `use_module` finds them. Was a
	// flat MAX_MODULES array - 8KB in every module for a list that is
	// nearly always a handful - and the append had no bound check at all.
	module **used;
	unsigned used_alloc;
	module *orig;
	prolog *pl;
	lock guard;							// serializes this module's own predicate mutation; see prolog_lock_mod()
	pi *gex_head, *gex_tail;			// goal expansion ??? (see pi_ above, why not use list?)
	parser *p;
	FILE *fp;
	const char *filename, *name, *actual_filename;
	skiplist *index, *ops, *defops, *keyval;
	loaded_file *loaded_files;
	list predicates;
	prolog_flags flags;
	unsigned id, idx_used, arity;
	int if_depth;
	bool ifs_blocked[MAX_IF_DEPTH];
	bool ifs_done[MAX_IF_DEPTH];
	cell *quad_query;					// pending '?- Query' awaiting its answer description
	cell *quad_name;					// its identifier, for 'Name ?- Query', else NULL
	unsigned quad_num_vars;				// number of vars in quad_query
	unsigned quad_line_num;				// line the pending quad query started on
	bool in_quad:1;						// consuming answer-description terms after '?- Query'
	bool quad_recorded:1;				// at least one answer description seen for quad_query
	bool user_ops:1;
	bool prebuilt:1;
	bool make_public:1;
	bool loaded_properties:1;
	bool loading:1;
	bool error:1;
	bool ignore_vars:1;
	bool wild_goal_expansion:1;
	bool make:1;
	bool run_init:1;
};

struct prolog_ {
	stream streams[MAX_STREAMS];
	// Threads, message queues and mutexes. The skiplist answers "which
	// entry has this id" in O(log n); the intrusive list answers "walk
	// them all" without allocating. Structs come from free_head and go
	// back to it when retired, so memory is bounded by peak concurrent
	// entries rather than by how many have ever existed - and ids, being
	// monotonic, are never reused even though the memory is.

	skiplist *threads;
	thread *live_head, *live_tail, *free_head, *free_tail, *main_thread;
	unsigned next_thread_id;

	// qid -> query* for addressing any query by id (send/2, recv/1).
	// Lazily created, unlike pl->threads: most programs never touch
	// send/recv, and this only needs to exist for those that do.
	// Entries are added lazily too, by task_self/1 (bif_tasks.c) rather
	// than at query construction - the only way anything ever learns a
	// qid is that query calling task_self/1 and telling someone, so a
	// query that never does is unreachable and not worth a skiplist
	// entry. No need to distinguish a task, a thread's root query, or a
	// plain directive's query by type here: the countless transient
	// queries that never call task_self/1 (format's ~@, with_output_to,
	// engines, goal expansion) simply never register themselves.

	skiplist *tasks;
	// Module id -> module, grown as ids are handed out. Ids are monotonic
	// and never reused, so this only ever grows.
	module **modmap;
	unsigned modmap_alloc;
	list modules;
	module *system_m, *user_m, *m, *dcgs;
	parser *p;
	skiplist *biftab, *help, *fortab, *alias;
	FILE *logfp;
	lock guard;
	uint64_t s_last, s_cnt, seed, thr_cnt;
	pl_refcnt q_cnt, dbgen;

	// Set once, when the first thread is created, and never cleared.
	// The database locking in enter_predicate()/leave_predicate() is
	// only needed against another thread, and costs about 7% on dynamic
	// calls - so a program that never creates one does not pay it.
	// Conservative by construction: it is set before the new thread is
	// started, so it is already true by the time anything can race.

	pl_atomic bool is_multithreaded;
	unsigned next_mod_id, def_max_depth, my_chan;

	// Tabling restraints (SWI's flag names). 0 = infinite = unset.

	unsigned tbl_max_answer_size, tbl_max_subgoal_size, tbl_max_answers_for_subgoal;
	unsigned current_input, current_output, current_error;
	pl_atomic int goal_expansions;		// every thread bumps it; plain ++/-- wrapped below zero
	int8_t halt_code, opt, limit;
	pl_refcnt rnd_first_time;
	bool def_quoted:1;
	bool def_double_quotes:1;
	bool is_redo:1;
	bool is_query:1;
	bool no_dump_vars:1;				// set_dump_vars(pl, 0)
	bool halt:1;
	bool status:1;
	bool error:1;
	bool did_dump_vars:1;
	bool autofail:1;
	bool quiet:1;
	bool noindex:1;
	bool iso_only:1;
	bool trace:1;
	bool in_goal_expansion:1;
	bool global_bb:1;
	bool tabling:1;			// tabling flag: enabled by default

	// Shared completed tables (item 4). Lazily created; opaque here
	// because the table/trie types are private to bif_tabling.c.
	// Guarded by its own lock - publication and lookup are short and
	// contain no user code, which is what makes a mutex sound there
	// and unsound around completion/0.

	void *tbl_shared;

	// Set the first time any ":- table ... as ..." / mode spec is
	// declared. The tabling driver consults it once per FRESH table, so
	// a program that declares none skips the lookup entirely - which is
	// most of them, and the cost showed up as ~1 KB of Prolog frames
	// per nesting level in deeply recursive tabling.

	bool tbl_any_specs;

};

// A module by id, or NULL for one that was never registered - which is what
// indexing the old fixed array gave, since it was zeroed.

static inline module *module_by_id(const prolog *pl, unsigned id)
{
	return (id < pl->modmap_alloc) ? pl->modmap[id] : NULL;
}

extern pl_idx g_empty_s, g_pair_s, g_dot_s, g_cut_s, g_nil_s, g_true_s, g_fail_s;
extern pl_idx g_anon_s, g_neck_s, g_eof_s, g_lt_s, g_false_s, g_once_s;
extern pl_idx g_gt_s, g_eq_s, g_sys_elapsed_s, g_sys_queue_s, g_braces_s;
extern pl_idx g_sys_stream_property_s, g_unify_s, g_on_s, g_off_s, g_sys_var_s;
extern pl_idx g_call_s, g_braces_s, g_plus_s, g_minus_s, g_post_unify_hook_s;
extern pl_idx g_quad_s, g_sys_quad_s;
extern bool do_erase(module *m, const char *str);
extern void tabling_destroy(prolog *pl);
extern void tabling_destroy_thread(thread *t);
extern void tbl_note_predicate_dep(query *q, predicate *pr);

extern unsigned g_cpu_count;
extern unsigned g_max_os_threads;

#define share_cell(c) if (is_managed(c)) share_cell_(c)

inline static void share_cell_(const cell *c)
{
	if (is_strbuf(c))
		c->val_strb->refcnt++;
	else if (is_bigint(c))
		c->val_bigint->refcnt++;
	else if (is_rational(c))
		c->val_bigint->refcnt++;
	else if (is_blob(c))
		c->val_blob->refcnt++;
}

#define unshare_cell(c) if (is_managed(c)) unshare_cell_(c)

inline static void unshare_cell_(cell *c)
{
	if (is_strbuf(c)) {
		if (--c->val_strb->refcnt == 0) {
			TPL_free(c->val_strb);
			c->tag = TAG_EMPTY;
		}
	} else if (is_bigint(c)) {
		if (--c->val_bigint->refcnt == 0)	{
			mp_int_clear(&c->val_bigint->ival);
			TPL_free(c->val_bigint);
			c->tag = TAG_EMPTY;
		}
	} else if (is_rational(c)) {
		if (--c->val_bigint->refcnt == 0)	{
			mp_rat_clear(&c->val_bigint->irat);
			TPL_free(c->val_bigint);
			c->flags = 0;
		}
	} else if (is_blob(c)) {
		if (--c->val_blob->refcnt == 0) {
			TPL_free(c->val_blob->ptr2);
			TPL_free(c->val_blob->ptr);
			TPL_free(c->val_blob);
			c->tag = TAG_EMPTY;
		}
	}
}

inline static pl_idx move_cells(cell *dst, const cell *src, pl_idx num_cells)
{
	memmove(dst, src, sizeof(cell)*(num_cells));
	return num_cells;
}

inline static pl_idx copy_cells(cell *dst, const cell *src, pl_idx num_cells)
{
	memcpy(dst, src, sizeof(cell)*(num_cells));
	return num_cells;
}

inline static pl_idx copy_cells_by_ref(cell *dst, const cell *src, pl_ctx src_ctx, pl_idx num_cells)
{
	for (pl_idx i = 0; i < num_cells; i++, src++, dst++) {
		*dst = *src;

		if (is_var(dst) && !is_ref(dst)) {
			dst->flags |= FLAG_VAR_REF;
			dst->val_ctx = src_ctx;
		}
	}

	return num_cells;
}

inline static pl_idx dup_cells(cell *dst, const cell *src, pl_idx num_cells)
{
	for (pl_idx i = 0; i < num_cells; i++, src++, dst++) {
		*dst = *src;
		share_cell(src);
	}

	return num_cells;
}

inline static pl_idx dup_cells_by_ref(cell *dst, const cell *src, pl_ctx src_ctx, pl_idx num_cells)
{
	for (pl_idx i = 0; i < num_cells; i++, src++, dst++) {
		*dst = *src;
		share_cell(src);

		if (is_var(dst) && !is_ref(dst)) {
			dst->flags |= FLAG_VAR_REF;
			dst->val_ctx = src_ctx;
		}
	}

	return num_cells;
}

#define PROLOG_LIST_HANDLER(l) cell l##_h_tmp, l##_t_tmp
#define PROLOG_LIST_HEAD(l) list_head(l, &l##_h_tmp)
#define PROLOG_LIST_TAIL(l) list_tail(l, &l##_t_tmp)

cell *list_head(cell *l, cell *tmp);
cell *list_tail(cell *l, cell *tmp);

enum clause_type { DO_CLAUSE, DO_RETRACT, DO_RETRACTALL };

char *formatted(const char *src, int srclen, bool dq, bool json);
char *slicedup(const char *s, size_t n);
int slicecmp(const char *s1, size_t len1, const char *s2, size_t len2);
uint64_t wall_time_in_usec(void);
uint64_t cpu_time_in_usec(void);
uint64_t monotonic_time_in_usec(void);
char *relative_to(const char *basefile, const char *relfile);
size_t sprint_int(char *dst, size_t size, pl_int n, int base);
int format_integer(char *dst, cell *c, int grouping, int sep, int decimals, int radix);
const char *dump_key(const void *k, const void *v, const void *p);

extern unsigned g_max_depth;

#define slicecmp2(s1,l1,s2) slicecmp(s1,l1,s2,strlen(s2))

inline static int fake_strcmp(const void *ptr1, const void *ptr2, const void *param, void *l) {
	return strcmp(ptr1, ptr2);
}

inline static void predicate_delink(predicate *pr, rule *r)
{
	if (r->prev) r->prev->next = r->next;
	if (r->next) r->next->prev = r->prev;
	if (pr->head == r) pr->head = r->next;
	if (pr->tail == r) pr->tail = r->prev;
}

#define ENSURE(cond, ...) if (!(cond)) { printf("Error: no memory %s %d\n", __FILE__, __LINE__); abort(); }

inline static bool is_empty(const cell *c) {
	return c->tag == TAG_EMPTY;
}

#define CHECK_SENTINEL(expr, err_sentinel, ...) CHECK_SENTINEL_((expr), err_sentinel, ## __VA_ARGS__, error=true)
#define CHECK_SENTINEL_(expr, err_sentinel, on_error, ...) do { if((expr) == err_sentinel){on_error;}} while (0)

#define check_error(expr, ...) CHECK_SENTINEL(expr, 0, __VA_ARGS__; return 0)

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if USE_FFI
#include <dlfcn.h>
#include <ffi.h>
#endif

#include "module.h"
#include "query.h"

#define MAX_FFI_STRUCT_FIELDS UINT8_MAX

#define MAX_FFI 1000

// These are pseudo tags just used here...

enum {
	FFI_TAG_VOID=TAG_END+1,
	FFI_TAG_SINT8,
	FFI_TAG_SINT16,
	FFI_TAG_SINT32,
	FFI_TAG_SINT64,
	FFI_TAG_UINT8,
	FFI_TAG_UINT16,
	FFI_TAG_UINT32,
	FFI_TAG_UINT64,
	FFI_TAG_USHORT,
	FFI_TAG_SHORT,
	FFI_TAG_UINT,
	FFI_TAG_SINT,
	FFI_TAG_ULONG,
	FFI_TAG_LONG,
	FFI_TAG_FP32,
	FFI_TAG_FP64,
	FFI_TAG_PTR,
	FFI_TAG_C_STR,
	FFI_TAG_C_CSTR,
	FFI_TAG_VAR,
	FFI_TAG_STRUCT
};

#define MARK_OUT(t) (((unsigned)(t) << 2) | 1)

// Two byte budgets, deliberately separate from MAX_FFI_ARGS. That one is
// a count of arguments; these are sizes in bytes, and the two have
// nothing to do with each other. They used to share it: the struct
// arguments of a call were packed end to end into a uint8_t
// bytes[MAX_FFI_ARGS], so all of them together had to fit in 64 bytes,
// and the packing loop had no bounds check - going over wrote past the
// array rather than failing. 64 bytes is less than one raylib Model (136)
// or Mesh (120), so the whole 3D model API was unreachable.
//
// Both buffers are stack-allocated per call, and both are now checked.

#define MAX_FFI_STRUCT_BYTES 4096	// all struct args of one call, together
#define MAX_FFI_RET_BYTES 4096		// one returned struct

typedef union result_ {
	float val_ffi_float;
	double val_ffi_double;
	uint8_t val_ffi_uint8;
	uint16_t val_ffi_uint16;
	uint32_t val_ffi_uint32;
	uint64_t val_ffi_uint64;
	int8_t val_ffi_sint8;
	int16_t val_ffi_sint16;
	int32_t val_ffi_sint32;
	int64_t val_ffi_sint64;
	unsigned short val_ffi_ushort;
	signed short val_ffi_sshort;
	unsigned int val_ffi_uint;
	signed int val_ffi_sint;
	unsigned long val_ffi_ulong;
	signed long val_ffi_slong;
	void *val_ffi_pointer;
} result;

// Where ffi_call() writes the return value. Separate from result so that
// the per-argument cells[] array stays one word per entry: only the
// single return needs room for a whole struct. Union alignment comes
// from result's members (8), which covers every struct we can describe.

typedef union ffi_ret_ {
	result val;
	uint8_t bytes[MAX_FFI_RET_BYTES];
} ffi_ret;

typedef struct foreign_struct_ {
	const char *name;
	unsigned arity;
	uint8_t types[MAX_FFI_STRUCT_FIELDS];
	const char *names[MAX_FFI_STRUCT_FIELDS];
} foreign_struct;

static foreign_struct g_ffi_structs[MAX_FFI] = {{0}};

#if USE_FFI
typedef struct nested_elements {
	ffi_type *elements[MAX_FFI_ARGS];
} nested_elements;
#endif

#if USE_FFI
// Prolog code names a shared library the Unix way - 'libfoo.so' - and
// every library/*.pl that uses the FFI is written that way. Two of the
// three platforms we build for want a different suffix, so map it here
// rather than making each of those files carry a platform switch.
//
// Windows was missing until now: dlopen there comes from the dlfcn-win32
// shim the CI installs, and it resolves through LoadLibrary, which will
// not find a .so under any circumstances. So library(gsl), library(curl),
// library(sqlite3) and library(raylib) could not load on Windows at all.

#if defined(__APPLE__)
#define SO_SUFFIX ".dylib"
#elif defined(_WIN32)
#define SO_SUFFIX ".dll"
#endif

#ifdef SO_SUFFIX
// The ".so" that starts the *extension*, not merely the first one in the
// string: a path like /opt/my.software/plugin.so has an earlier match,
// and rewriting that one produced a nonsense filename. Accepts a version
// suffix too, so libfoo.so.1 is still recognised.

static const char *find_so_ext(const char *filename)
{
	const char *found = NULL;

	for (const char *p = filename; (p = strstr(p, ".so")); p += 3) {
		const char *after = p + 3;

		if (!*after || (*after == '.'))
			found = p;
	}

	return found;
}

static char *swap_so_suffix(const char *filename, const char *ptr)
{
	size_t head = ptr - filename;
	char *out = TPL_malloc(strlen(filename) - strlen(".so") + strlen(SO_SUFFIX) + 1);

	if (!out)
		return NULL;

	memcpy(out, filename, head);
	strcpy(out + head, SO_SUFFIX);
	strcat(out + head, ptr + strlen(".so"));
	return out;
}
#endif

void *do_dlopen(const char *filename, int flag)
{
	int mode = !flag ? RTLD_LAZY | RTLD_GLOBAL : flag;

#ifdef SO_SUFFIX
	const char *ptr = find_so_ext(filename);

	if (ptr) {
		char *filename2 = swap_so_suffix(filename, ptr);

		if (filename2) {
			void *handle = dlopen(filename2, mode);
			TPL_free(filename2);

			// Fall through to the name as written if that missed: a
			// platform may genuinely carry a .so, and reporting the
			// error against the name the user gave is friendlier.

			if (handle)
				return handle;
		}
	}
#endif

	return dlopen(filename, mode);
}

static bool bif_sys_dlopen_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,var);
	void *handle = do_dlopen(C_STR(q, p1), get_smallint(p2));
	if (!handle) return false;
	cell tmp;
	make_uint(&tmp, (pl_int)(size_t)handle);
	tmp.flags |= FLAG_INT_HANDLE | FLAG_HANDLE_DLL;
	return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_sys_dlsym_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,var);
	size_t handle = get_smalluint(p1);
	const char *symbol = C_STR(q, p2);

	if (!(p1->flags & FLAG_INT_HANDLE) && !(p1->flags & FLAG_HANDLE_DLL))
		return throw_error(q, p1, p1_ctx, "existence_error", "handle");

	void *ptr = dlsym((void*)handle, symbol);
	if (!ptr) return false;
	cell tmp;
	make_uint(&tmp, (pl_int)(size_t)ptr);
	tmp.flags |= FLAG_INT_HANDLE;
	return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
}

int do_dlclose(void *handle)
{
	return dlclose(handle);
}

static bool bif_sys_dlclose_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	size_t handle = get_smalluint(p1);

	if (!(p1->flags & FLAG_INT_HANDLE) && !(p1->flags & FLAG_HANDLE_DLL))
		return throw_error(q, p1, p1_ctx, "existence_error", "handle");

	return do_dlclose((void*)handle) ? false : true;
}

static int max_struct_idx = 0, max_ffi_idx = 9;

static void register_struct(prolog *pl, const char *name, unsigned arity, void *fn, uint8_t *types, const char **names)
{
	foreign_struct *ptr = &g_ffi_structs[max_struct_idx++];
	ptr->name = name;
	ptr->arity = arity;

	for (unsigned i = 0; i < arity; i++) {
		ptr->types[i] = types[i];
		ptr->names[i] = names[i];
	}

	sl_app(pl->fortab, ptr->name, ptr);
}

// An out-param tag is MARK_OUT(t) = 4t+1, which is always larger than
// the largest plain tag, so this test is exact rather than a bit trick.

#define IS_OUT(t) ((t) > FFI_TAG_STRUCT)

// One row per FFI type. Everything the rest of this file needs to know
// about a type lives here: the name it is written as in a signature,
// its libffi type, and the culprit reported by a type_error. Aliases
// (uchar/bool for uint8, schar for sint8) are extra rows sharing a tag;
// the first row for a tag is its canonical one.
//
// This replaces twelve hand-written if-else chains over the same 22
// values. Two of those chains had drifted: sint64 arguments were being
// described to libffi as sint32, and a -sint8 out-param was read back
// through a 32-bit union member. Both were single wrong tokens in the
// middle of forty near-identical lines.

typedef struct {
	const char *name;
	uint8_t tag;
	ffi_type *type;
	const char *err;
} ffi_typeinfo;

static const ffi_typeinfo g_ffi_typeinfo[] = {
	{"void",   FFI_TAG_VOID,   &ffi_type_void,    "void"},
	{"uint8",  FFI_TAG_UINT8,  &ffi_type_uint8,   "integer"},
	{"uchar",  FFI_TAG_UINT8,  &ffi_type_uint8,   "integer"},
	{"bool",   FFI_TAG_UINT8,  &ffi_type_uint8,   "integer"},
	{"uint16", FFI_TAG_UINT16, &ffi_type_uint16,  "integer"},
	{"uint32", FFI_TAG_UINT32, &ffi_type_uint32,  "integer"},
	{"uint64", FFI_TAG_UINT64, &ffi_type_uint64,  "integer"},
	{"uint",   FFI_TAG_UINT,   &ffi_type_uint,    "integer"},
	{"ushort", FFI_TAG_USHORT, &ffi_type_ushort,  "integer"},
	{"ulong",  FFI_TAG_ULONG,  &ffi_type_ulong,   "integer"},
	{"sint8",  FFI_TAG_SINT8,  &ffi_type_sint8,   "integer"},
	{"schar",  FFI_TAG_SINT8,  &ffi_type_sint8,   "integer"},
	{"sint16", FFI_TAG_SINT16, &ffi_type_sint16,  "integer"},
	{"sint32", FFI_TAG_SINT32, &ffi_type_sint32,  "integer"},
	{"sint64", FFI_TAG_SINT64, &ffi_type_sint64,  "integer"},
	{"sint",   FFI_TAG_SINT,   &ffi_type_sint,    "integer"},
	{"sshort", FFI_TAG_SHORT,  &ffi_type_sshort,  "integer"},
	{"slong",  FFI_TAG_LONG,   &ffi_type_slong,   "integer"},
	{"float",  FFI_TAG_FP32,   &ffi_type_float,   "float"},
	{"double", FFI_TAG_FP64,   &ffi_type_double,  "float"},
	{"ptr",    FFI_TAG_PTR,    &ffi_type_pointer, "stream"},
	{"cstr",   FFI_TAG_C_STR,  &ffi_type_pointer, "atom"},
	{"ccstr",  FFI_TAG_C_CSTR, &ffi_type_pointer, "atom"},
	{NULL,     0,              NULL,              NULL}
};

// The canonical row for a tag, or NULL for a tag with no fixed libffi
// type (struct) or no C type at all (var).

static const ffi_typeinfo *ffi_info(uint8_t tag)
{
	for (const ffi_typeinfo *p = g_ffi_typeinfo; p->name; p++) {
		if (p->tag == tag)
			return p;
	}

	return NULL;
}

// Name as written in a signature -> tag. False if it isn't a known
// type name, which the callers treat as naming a struct.

static bool parse_ffi_type(const char *name, uint8_t *tag)
{
	for (const ffi_typeinfo *p = g_ffi_typeinfo; p->name; p++) {
		if (!strcmp(p->name, name)) {
			*tag = p->tag;
			return true;
		}
	}

	return false;
}

// The type_error culprit for a tag.

static const char *ffi_err_of(uint8_t tag)
{
	const ffi_typeinfo *p = ffi_info(IS_OUT(tag) ? (uint8_t)(tag >> 2) : tag);
	return p ? p->err : (tag == FFI_TAG_VAR ? "var" : "invalid");
}

// The libffi type for a tag. NULL for anything whose ffi_type can only
// be built at call time - that is, structs - and for tags that aren't
// real C types.

static ffi_type *ffi_type_of(uint8_t tag)
{
	if (IS_OUT(tag))
		return &ffi_type_pointer;	// out-params are passed by pointer

	const ffi_typeinfo *p = ffi_info(tag);
	return p ? p->type : NULL;
}

// Storage for the pre-compiled cifs. Registration is one-way and lasts
// for the life of the process (there is no dlclose path that retires a
// builtin), so these are static rather than malloc'd: no teardown to
// get wrong and nothing for leak checkers to find. A signature that
// doesn't fit simply isn't cached.

static ffi_cif g_ffi_cifs[MAX_FFI];
static ffi_type *g_ffi_cif_args[MAX_FFI * 4];
static unsigned g_ffi_cifs_used = 0, g_ffi_cif_args_used = 0;

// Build the cif once, at registration. Everything it needs - the arg
// types and the return type - is fixed by the signature, so the only
// reason to defer it is a struct return, whose ffi_type is assembled
// per call from the foreign_struct table.

static void precompile_cif(builtins *ptr)
{
	unsigned nargs = (ptr->ret_type == FFI_TAG_VOID) ? ptr->arity : ptr->arity - 1;
	ffi_type *ret = ffi_type_of(ptr->ret_type);

	if (!ret || (nargs > MAX_FFI_ARGS))
		return;

	if ((g_ffi_cifs_used >= MAX_FFI)
		|| ((g_ffi_cif_args_used + nargs) > (MAX_FFI * 4)))
		return;

	ffi_type **at = &g_ffi_cif_args[g_ffi_cif_args_used];

	for (unsigned i = 0; i < nargs; i++) {
		ffi_type *t = ffi_type_of(ptr->types[i]);

		if (!t)
			return;				// a struct arg: leave it per-call

		at[i] = t;
	}

	ffi_cif *cif = &g_ffi_cifs[g_ffi_cifs_used];

	if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ret, at) != FFI_OK)
		return;

	g_ffi_cif_args_used += nargs;
	g_ffi_cifs_used++;
	ptr->cif = cif;
}


static void register_ffi(prolog *pl, const char *name, unsigned arity, void *fn, uint8_t *types, uint8_t ret_type, const char *ret_name, bool evaluable)
{
	builtins *ptr = &g_ffi_bifs[max_ffi_idx++];
	ptr->name = name;
	ptr->arity = arity;
	ptr->fn = fn;
	ptr->help = NULL;
	ptr->evaluable = evaluable;
	ptr->ffi = true;

	if (ret_type == FFI_TAG_VOID)
		ptr->arity--;

	for (unsigned i = 0; i < arity; i++)
		ptr->types[i] = types[i];

	ptr->ret_type = ret_type;
	ptr->ret_name = ret_name;
	ptr->cif = NULL;
	precompile_cif(ptr);
	sl_app(pl->biftab, ptr->name, ptr);
}

 bool bif_sys_register_function_4(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,iso_list);
	GET_NEXT_ARG(p4,atom);

	if (!(p1->flags & FLAG_INT_HANDLE) && !(p1->flags & FLAG_HANDLE_DLL))
		return throw_error(q, p1, p1_ctx, "existence_error", "handle");

	size_t handle = get_smalluint(p1);
	const char *symbol = C_STR(q, p2);
	void *func = dlsym((void*)handle, symbol);
	if (!func) return false;

	uint8_t arg_types[MAX_FFI_ARGS], ret_type = 0;
	PROLOG_LIST_HANDLER(l);
	cell *l = p3;
	pl_ctx l_ctx = p3_ctx;
	int idx = 0;

	while (is_iso_list(l) && (idx < MAX_FFI_ARGS)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);

		if (is_interned(h)) {
			const char *src = C_STR(q, h);

			uint8_t t;

			if (parse_ffi_type(src, &t))
				arg_types[idx++] = t;
		}

		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	const char *src = C_STR(q, p4);

	uint8_t t;

	if (parse_ffi_type(src, &t))
		ret_type = t;
	else
		printf("invalid ret_type: %s\n", src);

	register_ffi(q->pl, symbol, idx, (void*)func, arg_types, ret_type, NULL, true);
	return true;
}

// A struct's field count has nothing to do with MAX_FFI_ARGS either. The
// storage in foreign_struct is 8-bit limited, but this loop used to stop
// at MAX_FFI_ARGS (64) and silently drop every field past it - so an
// over-long foreign_struct produced a quietly wrong layout rather than an
// error. Structs get big when they are declared flattened, which is the
// only way to declare a nested one at all.

bool do_register_struct(module *m, query *q, void *handle, const char *symbol, cell *l, pl_ctx l_ctx, const char *ret)
{
	uint8_t arg_types[MAX_FFI_STRUCT_FIELDS];
	const char *arg_names[MAX_FFI_STRUCT_FIELDS];
	PROLOG_LIST_HANDLER(l);
	int idx = 0;

	while (is_iso_list(l)) {
		if (idx >= MAX_FFI_STRUCT_FIELDS) {
			printf("Error: foreign_struct %s: over %u fields\n", symbol, (unsigned)MAX_FFI_STRUCT_FIELDS);
			return false;
		}

		cell *h = PROLOG_LIST_HEAD(l);
		h = q ? deref(q, h, l_ctx) : h;

		if (is_interned(h)) {
			const char *src = C_STR(m, h);
			arg_names[idx] = src;

			uint8_t t;

			if (!strcmp(src, "-") && parse_ffi_type(C_STR(m, h+1), &t))
				arg_types[idx++] = MARK_OUT(t);
			else if (parse_ffi_type(src, &t))
				arg_types[idx++] = t;
			else
				arg_types[idx++] = FFI_TAG_STRUCT;
		} else {
			printf("Warning: register struct\n");
			return false;
		}

		l = PROLOG_LIST_TAIL(l);
		l = q ? deref(q, l, l_ctx) : l;
		l_ctx = q ? q->latest_ctx : 0;
	}

	register_struct(m->pl, symbol, idx, NULL, arg_types, arg_names);
	return true;
}

bool do_register_predicate(module *m, query *q, void *handle, const char *symbol, cell *l, pl_ctx l_ctx, const char *ret)
{
	void *func = dlsym(handle, symbol);
	if (!func) return false;

	uint8_t arg_types[MAX_FFI_ARGS], ret_type = 0;
	PROLOG_LIST_HANDLER(l);
	int idx = 0;

	while (is_iso_list(l) && (idx < MAX_FFI_ARGS)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = q ? deref(q, h, l_ctx) : h;

		if (is_interned(h)) {
			const char *src = C_STR(m, h);

			uint8_t t;

			if (!strcmp(src, "-") && parse_ffi_type(C_STR(m, h+1), &t))
				arg_types[idx++] = MARK_OUT(t);
			else if (parse_ffi_type(src, &t))
				arg_types[idx++] = t;
			else
				arg_types[idx++] = FFI_TAG_STRUCT;
		}

		l = PROLOG_LIST_TAIL(l);
		l = q ? deref(q, l, l_ctx) : l;
		l_ctx = q ? q->latest_ctx : 0;
	}

	const char *src = ret;

	// An unrecognised name is the name of a registered struct.

	uint8_t t;

	if (!parse_ffi_type(src, &t))
		t = FFI_TAG_STRUCT;

	arg_types[idx++] = MARK_OUT(t);
	ret_type = t;

	register_ffi(m->pl, symbol, idx, (void*)func, arg_types, ret_type, src, false);
	return true;
}

bool bif_sys_register_predicate_4(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);

	// [] is an atom, not an iso_list, so demanding iso_list here made a
	// zero-argument function impossible to register - Py_Finalize and
	// Py_GetVersion among them. use_foreign_module/2 has always accepted
	// it; see the 'CloseWindow'([], void) entries in library/raylib.pl.

	GET_NEXT_ARG(p3,iso_list_or_nil);
	GET_NEXT_ARG(p4,atom);

	if (!(p1->flags & FLAG_INT_HANDLE) && !(p1->flags & FLAG_HANDLE_DLL))
		return throw_error(q, p1, p1_ctx, "existence_error", "handle");

	return do_register_predicate(q->st.m, q, (void*)(size_t)get_smallint(p1), C_STR(q, p2), p3, p3_ctx, C_STR(q, p4));
}

bool wrap_ffi_function(query *q, builtins *ptr)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1, any);
	cell *c = p1;
	pl_ctx c_ctx = p1_ctx;

	ffi_cif cif = {0};
	ffi_type *arg_types[MAX_FFI_ARGS];
	void *arg_values[MAX_FFI_ARGS];
	void *s_args[MAX_FFI_ARGS];
	result cells[MAX_FFI_ARGS];
	unsigned arity = ptr->arity - 1;

	for (unsigned i = 0; i < arity; i++) {
		if ((ptr->types[i] == FFI_TAG_UINT8) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT16) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT32) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT64) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_USHORT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_ULONG) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT8) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT16) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT32) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT64) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SHORT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_LONG) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_PTR) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_FP32) && is_float(c))
			;
		else if ((ptr->types[i] == FFI_TAG_FP64) && is_float(c))
			;
		else if ((ptr->types[i] == FFI_TAG_C_STR) && is_atom(c))
			;
		else if ((ptr->types[i] == FFI_TAG_C_CSTR) && is_atom(c))
			;
		else if ((ptr->types[i] != c->tag) && !is_var(c))
			return throw_error(q, c, c_ctx, "type_error", ffi_err_of(ptr->types[i]));

		if (ptr->types[i] != FFI_TAG_STRUCT)
			arg_types[i] = ffi_type_of(ptr->types[i]);

		if (ptr->types[i] == FFI_TAG_UINT8) {
			cells[i].val_ffi_uint8 = c->val_uint;
			arg_values[i] = &cells[i].val_ffi_uint;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT8)) {
			s_args[i] = &cells[i].val_ffi_uint8;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_UINT16) {
			cells[i].val_ffi_uint16 = c->val_uint;
			arg_values[i] = &cells[i].val_ffi_uint16;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT16)) {
			s_args[i] = &cells[i].val_ffi_uint16;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_UINT32) {
			cells[i].val_ffi_uint32 = c->val_uint;
			arg_values[i] = &cells[i].val_ffi_uint32;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT32)) {
			s_args[i] = &cells[i].val_ffi_uint32;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_UINT64) {
			cells[i].val_ffi_uint64 = c->val_uint;
			arg_values[i] = &cells[i].val_ffi_uint64;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT64)) {
			s_args[i] = &cells[i].val_ffi_uint64;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_UINT) {
			cells[i].val_ffi_uint = c->val_uint;
			arg_values[i] = &cells[i].val_ffi_uint;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT)) {
			s_args[i] = &cells[i].val_ffi_uint;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_USHORT) {
			cells[i].val_ffi_ushort = c->val_uint;
			arg_values[i] = &cells[i].val_ffi_ushort;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_USHORT)) {
			s_args[i] = &cells[i].val_ffi_ushort;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_ULONG) {
			cells[i].val_ffi_ulong = c->val_uint;
			arg_values[i] = &cells[i].val_ffi_ulong;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_ULONG)) {
			s_args[i] = &cells[i].val_ffi_ulong;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_SINT8) {
			cells[i].val_ffi_sint8 = c->val_int;
			arg_values[i] = &cells[i].val_ffi_sint;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT8)) {
			s_args[i] = &cells[i].val_ffi_sint;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_SINT16) {
			cells[i].val_ffi_sint16 = c->val_int;
			arg_values[i] = &cells[i].val_ffi_sint16;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT16)) {
			s_args[i] = &cells[i].val_ffi_sint16;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_SINT32) {
			cells[i].val_ffi_sint32 = c->val_int;
			arg_values[i] = &cells[i].val_ffi_sint32;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT32)) {
			s_args[i] = &cells[i].val_ffi_sint32;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_SINT64) {
			cells[i].val_ffi_sint64 = c->val_int;
			arg_values[i] = &cells[i].val_ffi_sint64;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT64)) {
			s_args[i] = &cells[i].val_ffi_sint64;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_SINT) {
			cells[i].val_ffi_sint = c->val_int;
			arg_values[i] = &cells[i].val_ffi_sint;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT)) {
			s_args[i] = &cells[i].val_ffi_sint;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_SHORT) {
			cells[i].val_ffi_sshort = c->val_int;
			arg_values[i] = &cells[i].val_ffi_sshort;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SHORT)) {
			s_args[i] = &cells[i].val_ffi_sshort;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_LONG) {
			cells[i].val_ffi_slong = c->val_int;
			arg_values[i] = &cells[i].val_ffi_slong;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_LONG)) {
			s_args[i] = &cells[i].val_ffi_slong;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_FP32) {
			cells[i].val_ffi_float = c->val_float;
			arg_values[i] = &cells[i].val_ffi_float;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_FP32)) {
			s_args[i] = &cells[i].val_ffi_float;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_FP64) {
			cells[i].val_ffi_double = c->val_float;
			arg_values[i] = &cells[i].val_ffi_double;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_FP64)) {
			s_args[i] = &cells[i].val_ffi_float;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_PTR) {
			cells[i].val_ffi_pointer = c->val_ptr;
			arg_values[i] = &cells[i].val_ffi_pointer;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_PTR)) {
			s_args[i] = &cells[i].val_ffi_pointer;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_C_STR) {
			cells[i].val_ffi_pointer = C_STR(q, c);
			arg_values[i] = &cells[i].val_ffi_pointer;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_C_STR)) {
			cells[i].val_ffi_pointer = C_STR(q, c);
			s_args[i] = &cells[i].val_ffi_pointer;
			arg_values[i] = &s_args[i];
		} else if (ptr->types[i] == FFI_TAG_C_CSTR) {
			cells[i].val_ffi_pointer = C_STR(q, c);
			arg_values[i] = &cells[i].val_ffi_pointer;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_C_CSTR)) {
			cells[i].val_ffi_pointer = C_STR(q, c);
			s_args[i] = &cells[i].val_ffi_pointer;
			arg_values[i] = &s_args[i];
		}

		GET_NEXT_ARG(p2, any);
		c = p2;
		c_ctx = p2_ctx;
	}

	ffi_type *ffi_ret_type = NULL;
	ffi_status ok;

	if ((ok = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, ptr->arity, ffi_ret_type, arg_types)) != FFI_OK) {
		printf("Error: ffi_prep_cif status=%d\n", ok);
		return false;
	}

	ffi_ret r;
	ffi_call(&cif, FFI_FN(ptr->fn), &r, arg_values);

	cell tmp;

	if (ptr->ret_type == FFI_TAG_UINT8)
		make_int(&tmp, r.val.val_ffi_uint8);
	else if (ptr->ret_type == FFI_TAG_UINT16)
		make_int(&tmp, r.val.val_ffi_uint16);
	else if (ptr->ret_type == FFI_TAG_UINT32)
		make_int(&tmp, r.val.val_ffi_uint32);
	else if (ptr->ret_type == FFI_TAG_UINT64)
		make_int(&tmp, r.val.val_ffi_uint64);
	else if (ptr->ret_type == FFI_TAG_UINT)
		make_int(&tmp, r.val.val_ffi_uint);
	else if (ptr->ret_type == FFI_TAG_USHORT)
		make_int(&tmp, r.val.val_ffi_ushort);
	else if (ptr->ret_type == FFI_TAG_ULONG)
		make_int(&tmp, r.val.val_ffi_ulong);
	else if (ptr->ret_type == FFI_TAG_SINT8)
		make_int(&tmp, r.val.val_ffi_sint8);
	else if (ptr->ret_type == FFI_TAG_SINT16)
		make_int(&tmp, r.val.val_ffi_sint16);
	else if (ptr->ret_type == FFI_TAG_SINT32)
		make_int(&tmp, r.val.val_ffi_sint32);
	else if (ptr->ret_type == FFI_TAG_SINT64)
		make_int(&tmp, r.val.val_ffi_sint64);
	else if (ptr->ret_type == FFI_TAG_SINT)
		make_int(&tmp, r.val.val_ffi_sint);
	else if (ptr->ret_type == FFI_TAG_SHORT)
		make_int(&tmp, r.val.val_ffi_sshort);
	else if (ptr->ret_type == FFI_TAG_LONG)
		make_int(&tmp, r.val.val_ffi_slong);
	else if (ptr->ret_type == FFI_TAG_FP32)
		make_float(&tmp, r.val.val_ffi_float);
	else if (ptr->ret_type == FFI_TAG_FP64)
		make_float(&tmp, r.val.val_ffi_double);
	else if (ptr->ret_type == FFI_TAG_PTR)
		make_cstring(&tmp, r.val.val_ffi_pointer);
	else if (ptr->ret_type == FFI_TAG_C_STR)
		make_cstring(&tmp, r.val.val_ffi_pointer);
	else if (ptr->ret_type == FFI_TAG_C_CSTR)
		make_cstring(&tmp, r.val.val_ffi_pointer);
	else
		return false;

	q->accum = tmp;
	return true;
}

static bool handle_struct1(query *q, foreign_struct *sptr, nested_elements *nested, ffi_type *types, unsigned *pdepth)
{
	unsigned sarity = sptr->arity, depth = *pdepth + 1;
	*pdepth = depth;
	types[depth].size = types[depth].alignment = 0;
	types[depth].type = FFI_TYPE_STRUCT;
	types[depth].elements = nested[depth].elements;

	for (unsigned cnt = 0; cnt < sarity; cnt++) {
		//printf("*** [%u] %u\n", cnt, sptr->types[cnt]);

		if (sptr->types[cnt] == FFI_TAG_UINT8)
			nested[depth].elements[cnt] = &ffi_type_uint8;
		else if (sptr->types[cnt] == FFI_TAG_UINT16)
			nested[depth].elements[cnt] = &ffi_type_uint16;
		else if (sptr->types[cnt] == FFI_TAG_UINT32)
			nested[depth].elements[cnt] = &ffi_type_uint32;
		else if (sptr->types[cnt] == FFI_TAG_UINT64)
			nested[depth].elements[cnt] = &ffi_type_uint64;
		else if (sptr->types[cnt] == FFI_TAG_UINT)
			nested[depth].elements[cnt] = &ffi_type_uint;
		else if (sptr->types[cnt] == FFI_TAG_USHORT)
			nested[depth].elements[cnt] = &ffi_type_ushort;
		else if (sptr->types[cnt] == FFI_TAG_ULONG)
			nested[depth].elements[cnt] = &ffi_type_ulong;
		else if (sptr->types[cnt] == FFI_TAG_SINT8)
			nested[depth].elements[cnt] = &ffi_type_sint8;
		else if (sptr->types[cnt] == FFI_TAG_SINT16)
			nested[depth].elements[cnt] = &ffi_type_sint16;
		else if (sptr->types[cnt] == FFI_TAG_SINT32)
			nested[depth].elements[cnt] = &ffi_type_sint32;
		else if (sptr->types[cnt] == FFI_TAG_SINT64)
			nested[depth].elements[cnt] = &ffi_type_sint64;
		else if (sptr->types[cnt] == FFI_TAG_SINT)
			nested[depth].elements[cnt] = &ffi_type_sint;
		else if (sptr->types[cnt] == FFI_TAG_SHORT)
			nested[depth].elements[cnt] = &ffi_type_sshort;
		else if (sptr->types[cnt] == FFI_TAG_LONG)
			nested[depth].elements[cnt] = &ffi_type_slong;
		else if (sptr->types[cnt] == FFI_TAG_FP32)
			nested[depth].elements[cnt] = &ffi_type_float;
		else if (sptr->types[cnt] == FFI_TAG_FP64)
			nested[depth].elements[cnt] = &ffi_type_double;
		else if (sptr->types[cnt] == FFI_TAG_C_STR)
			nested[depth].elements[cnt] = &ffi_type_pointer;
		else if (sptr->types[cnt] == FFI_TAG_C_CSTR)
			nested[depth].elements[cnt] = &ffi_type_pointer;
		else if (sptr->types[cnt] == FFI_TAG_PTR)
			nested[depth].elements[cnt] = &ffi_type_pointer;
		else if (sptr->types[cnt] == FFI_TAG_STRUCT) {
			const char *name = sptr->names[cnt];
			foreign_struct *sptr = NULL;

			if (!sl_get(q->pl->fortab, name, (void*)&sptr)) {
				printf("wrapper: not found struct: %s\n", name);
				return false;
			}

			//printf("wrapper: found struct: %s, arity=%u\n", name, sptr->arity);

			if (!handle_struct1(q, sptr, nested, types, pdepth))
				return false;

			nested[depth].elements[cnt] = &types[depth+1];
		}
	}

	nested[depth].elements[sarity] = NULL;
	nested[depth].elements[sarity] = NULL;
	return true;
}

// Packs one scalar of a struct argument into bytes[], or recurses for a
// nested struct. Returns false if the value would not fit; the caller
// turns that into a resource_error rather than letting the write happen.

static bool handle_struct2(query *q, nested_elements *nested, unsigned *pdepth, unsigned cnt, uint8_t *bytes, size_t *boff, cell *h, pl_ctx h_ctx, void **arg_values, unsigned *p_pos)
{
	size_t bytes_offset = *boff, depth = *pdepth++;
	unsigned pos = *p_pos;
	result r;

	// Widest scalar we might be about to write. Checking once here beats
	// repeating a check in each of the fourteen branches below.

	if ((bytes_offset + sizeof(uint64_t)) > MAX_FFI_STRUCT_BYTES)
		return false;

	if (nested[depth].elements[cnt-1] == &ffi_type_uint8) {
		r.val_ffi_uint8 = h->val_uint;
		memcpy(bytes+bytes_offset, &r.val_ffi_uint8, 1);
		bytes_offset += 1;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_uint16) {
		r.val_ffi_uint16 = h->val_uint;
		memcpy(bytes+bytes_offset, &r.val_ffi_uint16, 2);
		bytes_offset += 2;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_uint32) {
		r.val_ffi_uint32 = h->val_uint;
		memcpy(bytes+bytes_offset, &r.val_ffi_uint32, 4);
		bytes_offset += 4;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_uint64) {
		r.val_ffi_uint64 = h->val_uint;
		memcpy(bytes+bytes_offset, &r.val_ffi_uint64, 8);
		bytes_offset += 8;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_uint) {
		r.val_ffi_uint = h->val_uint;
		memcpy(bytes+bytes_offset, &r.val_ffi_uint, sizeof(unsigned));
		bytes_offset += sizeof(unsigned);
	} else if (nested[depth].elements[cnt-1] == &ffi_type_sint8) {
		r.val_ffi_sint8 = h->val_int;
		memcpy(bytes+bytes_offset, &r.val_ffi_sint8, 1);
		bytes_offset += 1;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_sint16) {
		r.val_ffi_sint16 = h->val_int;
		memcpy(bytes+bytes_offset, &r.val_ffi_sint16, 2);
		bytes_offset += 2;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_sint32) {
		r.val_ffi_sint32 = h->val_int;
		memcpy(bytes+bytes_offset, &r.val_ffi_sint32, 4);
		bytes_offset += 4;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_sint64) {
		r.val_ffi_sint64 = h->val_int;
		memcpy(bytes+bytes_offset, &r.val_ffi_sint64, 8);
		bytes_offset += 8;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_sint) {
		r.val_ffi_sint = h->val_int;
		memcpy(bytes+bytes_offset, &r.val_ffi_sint, sizeof(int));
		bytes_offset += sizeof(int);
	} else if (nested[depth].elements[cnt-1] == &ffi_type_float) {
		r.val_ffi_float = h->val_float;
		memcpy(bytes+bytes_offset, &r.val_ffi_float, 4);
		bytes_offset += 4;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_double) {
		r.val_ffi_double = h->val_float;
		memcpy(bytes+bytes_offset, &r.val_ffi_double, 8);
		bytes_offset += 8;
	} else if (nested[depth].elements[cnt-1] == &ffi_type_pointer) {
		r.val_ffi_pointer = h->val_ptr;
		memcpy(bytes+bytes_offset, &r.val_ffi_pointer, sizeof(void*));
		bytes_offset += sizeof(void*);
	} else {
		cell *l = h;
		pl_ctx l_ctx = h_ctx;
		int cnt = 0;
		PROLOG_LIST_HANDLER(l);
		size_t bytes_offset_start = bytes_offset;

		while (is_iso_list(l)) {
			cell *h = PROLOG_LIST_HEAD(l);
			h = deref(q, h, l_ctx);
			pl_ctx h_ctx = q->latest_ctx;

			if (cnt > 0) {
				if (!handle_struct2(q, nested, pdepth, cnt, bytes, &bytes_offset, h, h_ctx, arg_values, &pos))
					return false;
			}

			l = PROLOG_LIST_TAIL(l);
			l = deref(q, l, l_ctx);
			l_ctx = q->latest_ctx;
			cnt++;
		}

		arg_values[pos] = &bytes[bytes_offset_start];
		*p_pos = ++pos;
	}

	*boff = bytes_offset;
	return true;
}

bool wrap_ffi_predicate(query *q, builtins *ptr)
{
	GET_FIRST_ARG(p1, any);
	cell *c = p1;
	pl_ctx c_ctx = p1_ctx;

	// nested[] is 64 x 64 pointers and types[] is 64 ffi_types: 34 KB
	// between them. Zero-initialising both on every call cost more
	// than the foreign call itself - 390ns/call became 210ns/call on
	// a three-integer function just by not doing it.
	//
	// Both are only ever touched from a FFI_TAG_STRUCT branch (the
	// two handle_struct1 calls, handle_struct2, and the two &types[]
	// takes), so a signature with no struct in it never reads them.
	// Zero them only when there is one.

	nested_elements nested[MAX_FFI_ARGS];
	ffi_type types[MAX_FFI_ARGS];

	ffi_type *arg_types[MAX_FFI_ARGS] = {0};
	void *arg_values[MAX_FFI_ARGS] = {0};
	void *s_args[MAX_FFI_ARGS] = {0};
	result cells[MAX_FFI_ARGS] = {0};
	uint8_t bytes[MAX_FFI_STRUCT_BYTES] = {0};

	ffi_type *ffi_ret_type = NULL;
	unsigned arity = ptr->arity - 1, pdepth = 0, depth = 0, pos = 0;
	size_t bytes_offset = 0;

	if (ptr->ret_type == FFI_TAG_VOID)
		arity++;

	bool uses_struct = ptr->ret_type == FFI_TAG_STRUCT;

	for (unsigned i = 0; !uses_struct && (i < arity); i++)
		uses_struct = ptr->types[i] == FFI_TAG_STRUCT;

	if (uses_struct) {
		memset(nested, 0, sizeof(nested));
		memset(types, 0, sizeof(types));
	}

	for (unsigned i = 0; i < arity; i++) {
		if ((ptr->types[i] == FFI_TAG_UINT8) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT16) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT32) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT64) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_UINT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_USHORT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_ULONG) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT8) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT16) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT32) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT64) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SINT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_SHORT) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_LONG) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_PTR) && is_smallint(c))
			;
		else if ((ptr->types[i] == FFI_TAG_PTR) && is_atom(c))
			;
		else if ((ptr->types[i] == FFI_TAG_FP32) && is_float(c))
			;
		else if ((ptr->types[i] == FFI_TAG_FP64) && is_float(c))
			;
		else if ((ptr->types[i] == FFI_TAG_C_STR) && is_atom(c))
			;
		else if ((ptr->types[i] == FFI_TAG_C_CSTR) && is_atom(c))
			;
		else if ((ptr->types[i] == FFI_TAG_STRUCT) && is_iso_list(c))
			;
		else if ((ptr->types[i] != c->tag) && !is_var(c))
			return throw_error(q, c, c_ctx, "type_error", ffi_err_of(ptr->types[i]));

		if (ptr->types[i] == FFI_TAG_STRUCT) {
			cell *l = c;
			pl_ctx l_ctx = c_ctx;
			const char *name = "invalid";
			PROLOG_LIST_HANDLER(l);

			while (is_iso_list(l)) {
				cell *h = PROLOG_LIST_HEAD(l);
				h = deref(q, h, l_ctx);
				name = C_STR(q, h);
				l = PROLOG_LIST_TAIL(l);
				break;
			}

			foreign_struct *sptr = NULL;

			if (!sl_get(q->pl->fortab, name, (void*)&sptr)) {
				printf("wrapper: not found struct: %s\n", name);
				return false;
			}

			//printf("wrapper: [%d] found struct: %s, arity=%u, depth=%u, pdepth=%u\n", i, name, sptr->arity, depth, pdepth);

			if (!handle_struct1(q, sptr, nested, types, &pdepth))
				return false;

			depth = pdepth;
			arg_types[i] = &types[depth];
		} else if (!(arg_types[i] = ffi_type_of(ptr->types[i]))) {
			printf("Warning: struct ptr->type=%u\n", ptr->types[i]);
			return false;
		}

		if (ptr->types[i] == FFI_TAG_UINT8) {
			cells[pos].val_ffi_uint8 = c->val_uint;
			arg_values[pos] = &cells[pos].val_ffi_uint8;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT8)) {
			s_args[pos] = &cells[pos].val_ffi_uint8;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_UINT16) {
			cells[pos].val_ffi_uint16 = c->val_uint;
			arg_values[pos] = &cells[pos].val_ffi_uint16;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT16)) {
			s_args[pos] = &cells[pos].val_ffi_uint16;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_UINT32) {
			cells[pos].val_ffi_uint32 = c->val_uint;
			arg_values[pos] = &cells[pos].val_ffi_uint32;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT32)) {
			s_args[pos] = &cells[pos].val_ffi_uint32;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_UINT64) {
			cells[pos].val_ffi_uint64 = c->val_uint;
			arg_values[pos] = &cells[pos].val_ffi_uint64;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT64)) {
			s_args[pos] = &cells[pos].val_ffi_uint64;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_UINT) {
			cells[pos].val_ffi_uint = c->val_uint;
			arg_values[pos] = &cells[pos].val_ffi_uint;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT)) {
			s_args[pos] = &cells[pos].val_ffi_uint;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_USHORT) {
			cells[pos].val_ffi_ushort = c->val_uint;
			arg_values[pos] = &cells[pos].val_ffi_ushort;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_USHORT)) {
			s_args[pos] = &cells[pos].val_ffi_ushort;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_ULONG) {
			cells[pos].val_ffi_ulong = c->val_uint;
			arg_values[pos] = &cells[pos].val_ffi_ulong;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_ULONG)) {
			s_args[pos] = &cells[pos].val_ffi_ulong;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_SINT8) {
			cells[pos].val_ffi_sint8 = c->val_int;
			arg_values[pos] = &cells[pos].val_ffi_sint8;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT8)) {
			s_args[pos] = &cells[pos].val_ffi_sint8;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_SINT16) {
			cells[pos].val_ffi_sint16 = c->val_int;
			arg_values[pos] = &cells[pos].val_ffi_sint16;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT16)) {
			s_args[pos] = &cells[pos].val_ffi_sint16;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_SINT32) {
			cells[pos].val_ffi_sint32 = c->val_int;
			arg_values[pos] = &cells[pos].val_ffi_sint32;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT32)) {
			s_args[pos] = &cells[pos].val_ffi_sint32;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_SINT64) {
			cells[pos].val_ffi_sint64 = c->val_int;
			arg_values[pos] = &cells[pos].val_ffi_sint64;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT64)) {
			s_args[pos] = &cells[pos].val_ffi_sint64;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_SINT) {
			cells[pos].val_ffi_sint = c->val_int;
			arg_values[pos] = &cells[pos].val_ffi_sint;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT)) {
			s_args[pos] = &cells[pos].val_ffi_sint;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_SHORT) {
			cells[pos].val_ffi_sshort = c->val_int;
			arg_values[pos] = &cells[pos].val_ffi_sshort;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SHORT)) {
			s_args[pos] = &cells[pos].val_ffi_sshort;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_LONG) {
			cells[pos].val_ffi_slong = c->val_int;
			arg_values[pos] = &cells[pos].val_ffi_slong;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_LONG)) {
			s_args[pos] = &cells[pos].val_ffi_slong;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_FP32) {
			cells[pos].val_ffi_float = c->val_float;
			arg_values[pos] = &cells[pos].val_ffi_float;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_FP32)) {
			s_args[pos] = &cells[pos].val_ffi_float;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_FP64) {
			cells[pos].val_ffi_double = c->val_float;
			arg_values[pos] = &cells[pos].val_ffi_double;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_FP64)) {
			s_args[pos] = &cells[pos].val_ffi_float;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_PTR) {
			cells[pos].val_ffi_pointer = is_atom(c) ? (void*)C_STR(q, c) : (void*)c->val_ptr;
			arg_values[pos] = &cells[pos].val_ffi_pointer;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_PTR)) {
			s_args[pos] = &cells[pos].val_ffi_pointer;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_C_STR) {
			cells[pos].val_ffi_pointer = is_atom(c) ? (void*)C_STR(q, c) : (void*)c->val_ptr;
			arg_values[pos] = &cells[pos].val_ffi_pointer;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_C_STR)) {
			cells[pos].val_ffi_pointer = C_STR(q, c);
			s_args[pos] = &cells[pos].val_ffi_pointer;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_C_CSTR) {
			cells[pos].val_ffi_pointer = C_STR(q, c);
			arg_values[pos] = &cells[pos].val_ffi_pointer;
			pos++;
		} else if (ptr->types[i] == MARK_OUT(FFI_TAG_C_CSTR)) {
			cells[pos].val_ffi_pointer = C_STR(q, c);
			s_args[pos] = &cells[pos].val_ffi_pointer;
			arg_values[pos] = &s_args[pos];
			pos++;
		} else if (ptr->types[i] == FFI_TAG_STRUCT) {
			cell *l = c;
			pl_ctx l_ctx = c_ctx;
			int cnt = 0;
			PROLOG_LIST_HANDLER(l);
			size_t bytes_offset_start = bytes_offset;

			while (is_iso_list(l)) {
				cell *h = PROLOG_LIST_HEAD(l);
				h = deref(q, h, l_ctx);
				pl_ctx h_ctx = q->latest_ctx;

				if (cnt > 0) {
					if (!handle_struct2(q, nested, &pdepth, cnt, bytes, &bytes_offset, h, h_ctx, arg_values, &pos))
						return throw_error(q, c, c_ctx, "resource_error", "ffi_struct_bytes");
				}

				l = PROLOG_LIST_TAIL(l);
				l = deref(q, l, l_ctx);
				l_ctx = q->latest_ctx;
				cnt++;
			}

			arg_values[pos] = &bytes[bytes_offset_start];
			pos++;
		} else {
			printf("Warning: struct ptr->type=%u\n", ptr->types[i]);
			return false;
		}

		GET_NEXT_ARG(p2, any);
		c = p2;
		c_ctx = p2_ctx;
	}

	//printf("*** args=%u\n", pos);

#if 0
	ffi_type **t = arg_types;
	int i = 0, jpos = 0;
	printf("*** ");

	while (t[i]) {
		pos = jpos;
		printf(" [%d]", i);
		if (t[i] == &ffi_type_uint8) printf("   uint8=%u", *(uint8_t*)arg_values[pos]);
		else if (t[i] == &ffi_type_sint) printf("   sint=%d", *(int*)arg_values[pos]);
		else if (t[i] == &ffi_type_float) printf("   float=%f", *(float*)arg_values[pos]);
		else if (t[i]->type == FFI_TYPE_STRUCT) {
			printf("   struct ==>");

			ffi_type **t2 = t[i]->elements;

			int j = 0;

			while (t2[j]) {
				printf(" [%d]", j);
				if (t2[j]->type == FFI_TYPE_UINT8) printf("   uint8"/*, *(uint8_t*)arg_values[pos] */);
				else if (t2[j]->type == FFI_TYPE_INT) printf("   sint"/*, *(int*)arg_values[pos] */);
				else if (t2[j]->type == FFI_TYPE_FLOAT) printf("   float"/*, *(float*)arg_values[pos] */);
				pos++;
				j++;
			}
		}

		jpos++;
		i++;
	}

	printf("\n");
#endif

	// The return type. A struct return is the only one whose ffi_type
	// has to be assembled here; everything else is a table lookup.

	if (ptr->ret_type == FFI_TAG_STRUCT) {
		const char *name = ptr->ret_name;
		foreign_struct *sptr = NULL;

		if (!sl_get(q->pl->fortab, name, (void*)&sptr)) {
			printf("wrapper: not found struct: %s\n", name);
			return false;
		}

		//printf("wrapper: arity=%u, found struct return type: %s, arity=%u, depth=%u, pdepth=%u\n", arity, name, sptr->arity, depth, pdepth);
		//unsigned save_depth = ++pdepth;

		if (!handle_struct1(q, sptr, nested, types, &pdepth))
			return false;

		ffi_ret_type = &types[pdepth];
	} else if (!(ffi_ret_type = ffi_type_of(ptr->ret_type))) {
		printf("Warning: struct ptr->ret_type=%u\n", ptr->ret_type);
		return false;
	}

	//printf("*** fn values = %u, ret-type=%u\n", pos, (unsigned)ffi_ret_type->type);

	// Pre-compiled at registration unless the signature returns a
	// struct, in which case ffi_ret_type was just built above and the
	// cif has to be prepared here as before.

	ffi_cif cif = {0};
	ffi_cif *cifp = (ffi_cif*)ptr->cif;

	if (!cifp) {
		ffi_status ok;

		if ((ok = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, arity, ffi_ret_type, arg_types)) != FFI_OK) {
			printf("Error: ffi_prep_cif status=%d\n", ok);
			return false;
		}

		cifp = &cif;
	}

	ffi_ret r;
	ffi_call(cifp, FFI_FN(ptr->fn), &r, arg_values);

	GET_FIRST_ARG(p11, any);
	c = p11;
	c_ctx = p11_ctx;

	for (unsigned i = 0; i < arity; i++) {
		if (is_var(c)) {
			cell tmp;

			if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT8)) {
				make_int(&tmp, cells[i].val_ffi_uint8);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT16)) {
				make_int(&tmp, cells[i].val_ffi_uint16);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT32)) {
				make_int(&tmp, cells[i].val_ffi_uint32);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT64)) {
				make_int(&tmp, cells[i].val_ffi_uint64);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_UINT)) {
				make_int(&tmp, cells[i].val_ffi_uint);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_USHORT)) {
				make_int(&tmp, cells[i].val_ffi_ushort);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_ULONG)) {
				make_int(&tmp, cells[i].val_ffi_ulong);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT8)) {
				make_int(&tmp, cells[i].val_ffi_sint8);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT16)) {
				make_int(&tmp, cells[i].val_ffi_sint16);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT32)) {
				make_int(&tmp, cells[i].val_ffi_sint32);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT64)) {
				make_int(&tmp, cells[i].val_ffi_sint64);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SINT)) {
				make_int(&tmp, cells[i].val_ffi_sint);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_SHORT)) {
				make_int(&tmp, cells[i].val_ffi_sshort);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_LONG)) {
				make_int(&tmp, cells[i].val_ffi_slong);
				bool ok = unify (q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_FP32)) {
				make_float(&tmp, cells[i].val_ffi_float);
				bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_FP64)) {
				make_float(&tmp, cells[i].val_ffi_double);
				bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_PTR)) {
				make_ptr(&tmp, cells[i].val_ffi_pointer);
				bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_C_STR)) {
				CHECKED(make_cstring(&tmp, cells[i].val_ffi_pointer));
				bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
				unshare_cell(&tmp);
				if (ok != true) return ok;
			} else if (ptr->types[i] == MARK_OUT(FFI_TAG_C_CSTR)) {
				CHECKED(make_cstring(&tmp, cells[i].val_ffi_pointer));
				bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
				unshare_cell(&tmp);
				if (ok != true) return ok;
			}
		}

		GET_NEXT_ARG(p2, any);
		c = p2;
		c_ctx = p2_ctx;
	}

	cell tmp;

	if (ptr->ret_type == FFI_TAG_UINT8) {
		make_int(&tmp, r.val.val_ffi_uint8);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_UINT16) {
		make_int(&tmp, r.val.val_ffi_uint16);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_UINT32) {
		make_int(&tmp, r.val.val_ffi_uint32);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_UINT64) {
		make_int(&tmp, r.val.val_ffi_uint64);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_UINT) {
		make_int(&tmp, r.val.val_ffi_uint);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_USHORT) {
		make_int(&tmp, r.val.val_ffi_ushort);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_ULONG) {
		make_int(&tmp, r.val.val_ffi_ulong);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_SINT8) {
		make_int(&tmp, r.val.val_ffi_sint8);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_SINT16) {
		make_int(&tmp, r.val.val_ffi_sint16);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_SINT32) {
		make_int(&tmp, r.val.val_ffi_sint32);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_SINT64) {
		make_int(&tmp, r.val.val_ffi_sint64);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_SINT) {
		make_int(&tmp, r.val.val_ffi_sint);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_SHORT) {
		make_int(&tmp, r.val.val_ffi_sshort);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_LONG) {
		make_int(&tmp, r.val.val_ffi_slong);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_FP32) {
		make_float(&tmp, r.val.val_ffi_float);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_FP64) {
		make_float(&tmp, r.val.val_ffi_double);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_PTR) {
		make_ptr(&tmp, r.val.val_ffi_pointer);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_C_STR) {
		CHECKED(make_cstring(&tmp, r.val.val_ffi_pointer));
		TPL_free(r.val.val_ffi_pointer);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_C_CSTR) {
		CHECKED(make_cstring(&tmp, r.val.val_ffi_pointer));
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_STRUCT) {
		ffi_type *p = ffi_ret_type;
		//printf("*** struct ffi_type=%u\n", p->type);
		int i = 0, cnt = 0;
		ffi_type *e = p->elements[i++];
		const uint8_t *bytes = r.bytes;

		while (e) {
			//printf("*** ffi_type=%u\n", e->type);
			cell tmp;

			if (e == &ffi_type_uint8) {
				make_uint(&tmp, *((uint8_t*)bytes));
				bytes += 1;
			} else if (e == &ffi_type_uint16) {
				make_uint(&tmp, *((uint16_t*)bytes));
				bytes += 2;
			} else if (e == &ffi_type_uint32) {
				make_uint(&tmp, *((uint32_t*)bytes));
				bytes += 4;
			} else if (e == &ffi_type_uint64) {
				make_uint(&tmp, *((uint64_t*)bytes));
				bytes += 8;
			} else if (e == &ffi_type_sint8) {
				make_int(&tmp, *((int8_t*)bytes));
				bytes += 1;
			} else if (e == &ffi_type_sint16) {
				make_int(&tmp, *((int16_t*)bytes));
				bytes += 2;
			} else if (e == &ffi_type_sint32) {
				make_int(&tmp, *((int32_t*)bytes));
				bytes += 4;
			} else if (e == &ffi_type_sint64) {
				make_int(&tmp, *((int64_t*)bytes));
				bytes += 8;
			} else if (e == &ffi_type_uint) {
				make_uint(&tmp, *((unsigned*)bytes));
				bytes += sizeof(unsigned int);
			} else if (e == &ffi_type_sint) {
				make_int(&tmp, *((signed*)bytes));
				bytes += sizeof(signed int);
			} else if (e == &ffi_type_float) {
				make_float(&tmp, *((float*)bytes));
				bytes += sizeof(float);
			} else if (e == &ffi_type_double) {
				make_float(&tmp, *((double*)bytes));
				bytes += sizeof(double);
			} else if (e == &ffi_type_pointer) {
				make_uint(&tmp, *((size_t*)bytes));
				bytes += sizeof(void*);
			} else
				return false;

			if (cnt == 0) {
				cell tmp2;
				make_cstring(&tmp2, ptr->ret_name);
				allocate_list(q, &tmp2);
			}

			append_list(q, &tmp);
			e = p->elements[i++];
			cnt++;
		}

		if (!unify(q, c, c_ctx, end_list(q), q->st.cur_ctx))
			return false;
	}

	return true;
}

static bool bif_foreign_struct_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,list_or_nil);

	return do_foreign_struct(q->st.m, q->st.instr);
}

static bool bif_use_foreign_module_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,list_or_nil);

	return do_use_foreign_module(q->st.m, q->st.instr);
}

static bool bif_sys_struct_to_pointer_2(query *q)
{
	GET_FIRST_ARG(p1,list);
	GET_NEXT_ARG(p2,var);
	PROLOG_LIST_HANDLER(p1);
	cell *c = PROLOG_LIST_HEAD(p1);
	const char *name = C_STR(q, c);
	foreign_struct *sptr = NULL;

	if (!sl_get(q->pl->fortab, name, (void*)&sptr)) {
		printf("wrapper: not found struct: %s\n", name);
		return false;
	}

	p1 = PROLOG_LIST_TAIL(p1);
	char tmpbuf[MAX_FFI_STRUCT_BYTES];
	char *dst = tmpbuf;
	unsigned i = 0;

	while (is_iso_list(p1)) {
		cell *h = PROLOG_LIST_HEAD(p1);
		uint8_t type = sptr->types[i];
		result rs;

		// Widest scalar any branch below writes.

		if (((size_t)(dst - tmpbuf) + sizeof(uint64_t)) > sizeof(tmpbuf))
			return throw_error(q, p1, p1_ctx, "resource_error", "ffi_struct_bytes");

		if (type == FFI_TAG_ULONG) {
			rs.val_ffi_uint64 = h->val_uint;
			memcpy(dst, &rs.val_ffi_uint64, sizeof(rs.val_ffi_uint64));
			dst += sizeof(rs.val_ffi_uint64);
		} else if (type == FFI_TAG_LONG) {
			rs.val_ffi_sint64 = h->val_int;
			memcpy(dst, &rs.val_ffi_sint64, sizeof(rs.val_ffi_sint64));
			dst += sizeof(rs.val_ffi_sint64);
		} else if (type == FFI_TAG_USHORT) {
			rs.val_ffi_ushort = h->val_uint;
			memcpy(dst, &rs.val_ffi_ushort, sizeof(rs.val_ffi_ushort));
			dst += sizeof(rs.val_ffi_ushort);
		} else if (type == FFI_TAG_SHORT) {
			rs.val_ffi_sshort = h->val_int;
			memcpy(dst, &rs.val_ffi_sshort, sizeof(rs.val_ffi_sshort));
			dst += sizeof(rs.val_ffi_sshort);
		} else if (type == FFI_TAG_SINT) {
			rs.val_ffi_sint = h->val_int;
			memcpy(dst, &rs.val_ffi_sint, sizeof(rs.val_ffi_sint));
			dst += sizeof(rs.val_ffi_sint);
		} else if (type == FFI_TAG_UINT) {
			rs.val_ffi_uint = h->val_uint;
			memcpy(dst, &rs.val_ffi_uint, sizeof(rs.val_ffi_uint));
			dst += sizeof(rs.val_ffi_uint);
		} else if (type == FFI_TAG_SINT8) {
			rs.val_ffi_sint8 = h->val_int;
			memcpy(dst, &rs.val_ffi_sint8, sizeof(rs.val_ffi_sint8));
			dst += sizeof(rs.val_ffi_sint8);
		} else if (type == FFI_TAG_UINT8) {
			rs.val_ffi_uint8 = h->val_uint;
			memcpy(dst, &rs.val_ffi_uint8, sizeof(rs.val_ffi_uint8));
			dst += sizeof(rs.val_ffi_uint8);
		} else if (type == FFI_TAG_SINT16) {
			rs.val_ffi_sint16 = h->val_int;
			memcpy(dst, &rs.val_ffi_sint16, sizeof(rs.val_ffi_sint16));
			dst += sizeof(rs.val_ffi_sint16);
		} else if (type == FFI_TAG_UINT16) {
			rs.val_ffi_uint16 = h->val_uint;
			memcpy(dst, &rs.val_ffi_uint16, sizeof(rs.val_ffi_uint16));
			dst += sizeof(rs.val_ffi_uint16);
		} else if (type == FFI_TAG_SINT32) {
			rs.val_ffi_sint32 = h->val_int;
			memcpy(dst, &rs.val_ffi_sint32, sizeof(rs.val_ffi_sint32));
			dst += sizeof(rs.val_ffi_sint32);
		} else if (type == FFI_TAG_UINT32) {
			rs.val_ffi_uint32 = h->val_uint;
			memcpy(dst, &rs.val_ffi_uint32, sizeof(rs.val_ffi_uint32));
			dst += sizeof(rs.val_ffi_uint32);
		} else if (type == FFI_TAG_SINT64) {
			rs.val_ffi_sint64 = h->val_int;
			memcpy(dst, &rs.val_ffi_sint64, sizeof(rs.val_ffi_sint64));
			dst += sizeof(rs.val_ffi_sint64);
		} else if (type == FFI_TAG_UINT64) {
			rs.val_ffi_uint64 = h->val_uint;
			memcpy(dst, &rs.val_ffi_uint64, sizeof(rs.val_ffi_uint64));
			dst += sizeof(rs.val_ffi_uint64);
		} else if (type == FFI_TAG_FP32) {
			rs.val_ffi_float = h->val_float;
			memcpy(dst, &rs.val_ffi_float, sizeof(rs.val_ffi_float));
			dst += sizeof(rs.val_ffi_float);
		} else if (type == FFI_TAG_FP64) {
			rs.val_ffi_double = h->val_float;
			memcpy(dst, &rs.val_ffi_double, sizeof(rs.val_ffi_double));
			dst += sizeof(rs.val_ffi_double);
		} else if (type == FFI_TAG_PTR) {
			rs.val_ffi_pointer = (void*)(size_t)h->val_uint;
			memcpy(dst, &rs.val_ffi_pointer, sizeof(rs.val_ffi_pointer));
			dst += sizeof(rs.val_ffi_pointer);
		} else
			printf("*** struct to ptr %u\n", i);

		p1 = PROLOG_LIST_TAIL(p1);
		i++;
	}

	size_t len = dst - tmpbuf;
	char *ptr = TPL_malloc(len);
	memcpy(ptr, tmpbuf, len);

	cell tmp;
	make_uint(&tmp, (size_t)(void*)ptr);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

// Only for what $struct_to_pointer returned: that carries an allocator header, so libc free() can't take it.

static bool bif_sys_free_struct_pointer_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	TPL_free((void*)(size_t)get_smalluint(p1));
	return true;
}
#endif

builtins g_ffi_bifs[MAX_FFI] =
{
#if USE_FFI
	{"$dlopen", 3, bif_sys_dlopen_3, "+atom,+atom,-term", false, false, BLAH},
	{"$dlsym", 3, bif_sys_dlsym_3, "+atom,+symbol,-term", false, false, BLAH},
	{"$dlclose", 1, bif_sys_dlclose_1, "+term", false, false, BLAH},
	{"$register_function", 4, bif_sys_register_function_4, "+term,+atom,+list,+atom", false, false, BLAH},
	{"$register_predicate", 4, bif_sys_register_predicate_4, "+term,+atom,+list,+atom", false, false, BLAH},
	{"$struct_to_pointer", 2, bif_sys_struct_to_pointer_2, "+list,-integer", false, false, BLAH},
	{"$free_struct_pointer", 1, bif_sys_free_struct_pointer_1, "+integer", false, false, BLAH},

	{"foreign_struct", 2, bif_foreign_struct_2, "+atom,+list", false, false, BLAH},
	{"use_foreign_module", 2, bif_use_foreign_module_2, "+atom,+list", false, false, BLAH},
#endif

	// 9 builtins: see 'max_ffi_idx'

	{0}
};

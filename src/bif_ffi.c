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

// Owns its name and field names, copied in after it: C_STR() pointers into the atom pool went stale when it grew.

typedef struct foreign_struct_ {
	const char *name;
	unsigned arity;
	uint8_t types[MAX_FFI_STRUCT_FIELDS];
	const char *names[MAX_FFI_STRUCT_FIELDS];
} foreign_struct;

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

// dyld's default search misses Apple Silicon Homebrew and MacPorts, so a bare name also tries their lib dirs.

static void *dlopen_search(const char *filename, int mode)
{
	void *handle = dlopen(filename, mode);

#ifdef __APPLE__
	static const char *dirs[] = {"/opt/homebrew/lib", "/opt/local/lib"};

	for (unsigned i = 0; !handle && !strchr(filename, '/') && (i < sizeof(dirs)/sizeof(*dirs)); i++) {
		char path[1024];

		if (snprintf(path, sizeof(path), "%s/%s", dirs[i], filename) < (int)sizeof(path))
			handle = dlopen(path, mode);
	}
#endif

	return handle;
}

void *do_dlopen(const char *filename, int flag)
{
	int mode = !flag ? RTLD_LAZY | RTLD_GLOBAL : flag;

#ifdef SO_SUFFIX
	const char *ptr = find_so_ext(filename);

	if (ptr) {
		char *filename2 = swap_so_suffix(filename, ptr);

		if (filename2) {
			void *handle = dlopen_search(filename2, mode);
			TPL_free(filename2);

			// Fall through to the name as written if that missed: a
			// platform may genuinely carry a .so, and reporting the
			// error against the name the user gave is friendlier.

			if (handle)
				return handle;
		}
	}
#endif

	return dlopen_search(filename, mode);
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

// The pointer held in a pointer-typed global, which $dlsym only gives the address
// of. GSL hands out its interpolation types this way, as do other libraries their
// singletons, and a library with no accessor for one is otherwise out of reach.

static bool bif_sys_dlsym_deref_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,var);
	size_t handle = get_smalluint(p1);
	const char *symbol = C_STR(q, p2);

	if (!(p1->flags & FLAG_INT_HANDLE) && !(p1->flags & FLAG_HANDLE_DLL))
		return throw_error(q, p1, p1_ctx, "existence_error", "handle");

	void **ptr = dlsym((void*)handle, symbol);
	if (!ptr || !*ptr) return false;
	cell tmp;
	make_uint(&tmp, (pl_int)(size_t)*ptr);
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

// Only fortab holds a struct, everything else looking it up by name, so declaring one again swaps in a new one.

static bool register_struct(prolog *pl, const char *name, unsigned arity, const uint8_t *types, const char **names)
{
	size_t len = strlen(name) + 1;

	for (unsigned i = 0; i < arity; i++)
		len += strlen(names[i]) + 1;

	foreign_struct *ptr = TPL_calloc(1, sizeof(foreign_struct) + len);

	if (!ptr)
		return false;

	char *dst = (char*)(ptr + 1);
	size_t n = strlen(name) + 1;
	memcpy(dst, name, n);
	ptr->name = dst;
	dst += n;
	ptr->arity = arity;
	memcpy(ptr->types, types, arity);

	for (unsigned i = 0; i < arity; i++) {
		n = strlen(names[i]) + 1;
		memcpy(dst, names[i], n);
		ptr->names[i] = dst;
		dst += n;
	}

	foreign_struct *old;

	if (!sl_get(pl->fortab, name, (void*)&old))
		sl_app(pl->fortab, ptr->name, ptr);
	else if (sl_replace(pl->fortab, name, old, ptr->name, ptr))
		TPL_free(old);
	else {
		TPL_free(ptr);
		return false;
	}

	return true;
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

// The base type of an output argument's tag: MARK_OUT() puts every output tag above every input one.

#define OUT_BASE(t) ((uint8_t)((t) >> 2))

// A type passed as one value: a scalar, a pointer or a string, the run of the enum from sint8 to ccstr.

static inline bool ffi_scalar(uint8_t tag)
{
	return (tag >= FFI_TAG_SINT8) && (tag <= FFI_TAG_C_CSTR);
}

// Puts an argument where libffi reads it, once it has checked c can be passed as tag: an input's value in its slot
// at the C type's width, an output as a pointer to its slot for the callee to write through. result is a union, so
// every member is at the slot's own address. Checking and marshalling in one switch reads the tag once.
//
// An unbound variable always passes the check: an output takes one, and the chain this replaced ended by comparing
// an FFI tag with a cell tag, which never match, so it let any variable through. Only a predicate takes a pointer
// as an atom. FFI_ARG_TYPE is a c that does not fit, FFI_ARG_NONE a type with nothing to marshal here: void, var,
// or a struct, which the caller checks and packs itself.
//
// It and ffi_to_term() are kept inline, being on every call: out of line they cost about sixty instructions a
// foreign call more than the chains they replaced.

enum { FFI_ARG_OK, FFI_ARG_TYPE, FFI_ARG_NONE };

#define FFI_FITS(test) if (!(test) && !is_var(c)) return FFI_ARG_TYPE

__attribute__((always_inline))
inline static int ffi_marshal(query *q, uint8_t tag, cell *c, bool predicate, result *slot, void **s_arg, void **arg_value)
{
	switch (tag) {
	case FFI_TAG_UINT8:  FFI_FITS(is_smallint(c)); slot->val_ffi_uint8 = c->val_uint; break;
	case FFI_TAG_UINT16: FFI_FITS(is_smallint(c)); slot->val_ffi_uint16 = c->val_uint; break;
	case FFI_TAG_UINT32: FFI_FITS(is_smallint(c)); slot->val_ffi_uint32 = c->val_uint; break;
	case FFI_TAG_UINT64: FFI_FITS(is_smallint(c)); slot->val_ffi_uint64 = c->val_uint; break;
	case FFI_TAG_UINT:   FFI_FITS(is_smallint(c)); slot->val_ffi_uint = c->val_uint; break;
	case FFI_TAG_USHORT: FFI_FITS(is_smallint(c)); slot->val_ffi_ushort = c->val_uint; break;
	case FFI_TAG_ULONG:  FFI_FITS(is_smallint(c)); slot->val_ffi_ulong = c->val_uint; break;
	case FFI_TAG_SINT8:  FFI_FITS(is_smallint(c)); slot->val_ffi_sint8 = c->val_int; break;
	case FFI_TAG_SINT16: FFI_FITS(is_smallint(c)); slot->val_ffi_sint16 = c->val_int; break;
	case FFI_TAG_SINT32: FFI_FITS(is_smallint(c)); slot->val_ffi_sint32 = c->val_int; break;
	case FFI_TAG_SINT64: FFI_FITS(is_smallint(c)); slot->val_ffi_sint64 = c->val_int; break;
	case FFI_TAG_SINT:   FFI_FITS(is_smallint(c)); slot->val_ffi_sint = c->val_int; break;
	case FFI_TAG_SHORT:  FFI_FITS(is_smallint(c)); slot->val_ffi_sshort = c->val_int; break;
	case FFI_TAG_LONG:   FFI_FITS(is_smallint(c)); slot->val_ffi_slong = c->val_int; break;
	case FFI_TAG_FP32:   FFI_FITS(is_float(c)); slot->val_ffi_float = c->val_float; break;
	case FFI_TAG_FP64:   FFI_FITS(is_float(c)); slot->val_ffi_double = c->val_float; break;

	case FFI_TAG_PTR:
		FFI_FITS(is_smallint(c) || (predicate && is_atom(c)));
		slot->val_ffi_pointer = is_atom(c) ? (void*)C_STR(q, c) : (void*)c->val_ptr;
		break;

	case FFI_TAG_C_STR:
		FFI_FITS(is_atom(c));
		slot->val_ffi_pointer = is_atom(c) ? (void*)C_STR(q, c) : (void*)c->val_ptr;
		break;

	case FFI_TAG_C_CSTR:
		FFI_FITS(is_atom(c));
		slot->val_ffi_pointer = (void*)C_STR(q, c);
		break;

	default:
		// An output, or void, var or struct: only an unbound variable fits any of them.

		if (!is_var(c))
			return FFI_ARG_TYPE;

		if (!IS_OUT(tag) || !ffi_scalar(OUT_BASE(tag)))
			return FFI_ARG_NONE;

		// A string output's slot is set from c even though c is unbound, as it always was.

		if ((OUT_BASE(tag) == FFI_TAG_C_STR) || (OUT_BASE(tag) == FFI_TAG_C_CSTR))
			slot->val_ffi_pointer = (void*)C_STR(q, c);

		*s_arg = slot;
		*arg_value = s_arg;
		return FFI_ARG_OK;
	}

	*arg_value = slot;
	return FFI_ARG_OK;
}

#undef FFI_FITS

// The term for a value of C type tag, read at the C type's width: 1, or 0 for a NULL string, which is how a
// C function says "none" and is no atom, or -1 for a string that could not be made.
//
// A return is widened: libffi hands back an integer narrower than ffi_arg as a whole ffi_arg, which on a
// big-endian machine leaves it at the far end of the slot, so it is read whole and cut down. An output is not.

#define FFI_NARROW(type, member) (widened ? (type)w : v->member)

__attribute__((always_inline))
inline static int ffi_to_term(uint8_t tag, const result *v, bool widened, cell *tmp)
{
	ffi_arg w;
	memcpy(&w, v, sizeof(w));

	switch (tag) {
	case FFI_TAG_UINT8:  make_int(tmp, FFI_NARROW(uint8_t, val_ffi_uint8)); break;
	case FFI_TAG_UINT16: make_int(tmp, FFI_NARROW(uint16_t, val_ffi_uint16)); break;
	case FFI_TAG_UINT32: make_int(tmp, FFI_NARROW(uint32_t, val_ffi_uint32)); break;
	case FFI_TAG_UINT64: make_int(tmp, v->val_ffi_uint64); break;
	case FFI_TAG_UINT:   make_int(tmp, FFI_NARROW(unsigned int, val_ffi_uint)); break;
	case FFI_TAG_USHORT: make_int(tmp, FFI_NARROW(unsigned short, val_ffi_ushort)); break;
	case FFI_TAG_ULONG:  make_int(tmp, FFI_NARROW(unsigned long, val_ffi_ulong)); break;
	case FFI_TAG_SINT8:  make_int(tmp, FFI_NARROW(int8_t, val_ffi_sint8)); break;
	case FFI_TAG_SINT16: make_int(tmp, FFI_NARROW(int16_t, val_ffi_sint16)); break;
	case FFI_TAG_SINT32: make_int(tmp, FFI_NARROW(int32_t, val_ffi_sint32)); break;
	case FFI_TAG_SINT64: make_int(tmp, v->val_ffi_sint64); break;
	case FFI_TAG_SINT:   make_int(tmp, FFI_NARROW(signed int, val_ffi_sint)); break;
	case FFI_TAG_SHORT:  make_int(tmp, FFI_NARROW(signed short, val_ffi_sshort)); break;
	case FFI_TAG_LONG:   make_int(tmp, FFI_NARROW(signed long, val_ffi_slong)); break;
	case FFI_TAG_FP32:   make_float(tmp, v->val_ffi_float); break;
	case FFI_TAG_FP64:   make_float(tmp, v->val_ffi_double); break;
	case FFI_TAG_PTR:    make_ptr(tmp, v->val_ffi_pointer); break;
	case FFI_TAG_C_STR:
	case FFI_TAG_C_CSTR:
		if (!v->val_ffi_pointer)
			return 0;

		return make_cstring(tmp, v->val_ffi_pointer) ? 1 : -1;
	default:
		return 0;
	}

	return 1;
}

#undef FFI_NARROW

// One scalar field of a struct, put at dst at the width libffi describes it with: its size, or 0 for a field
// that is itself a struct. ffi_type_uint, _ushort, _ulong and the rest are aliases of the fixed widths.

static size_t ffi_pack_field(const ffi_type *t, cell *h, uint8_t *dst)
{
	result r;

	switch (t->type) {
	case FFI_TYPE_UINT8:   r.val_ffi_uint8 = h->val_uint; break;
	case FFI_TYPE_UINT16:  r.val_ffi_uint16 = h->val_uint; break;
	case FFI_TYPE_UINT32:  r.val_ffi_uint32 = h->val_uint; break;
	case FFI_TYPE_UINT64:  r.val_ffi_uint64 = h->val_uint; break;
	case FFI_TYPE_SINT8:   r.val_ffi_sint8 = h->val_int; break;
	case FFI_TYPE_SINT16:  r.val_ffi_sint16 = h->val_int; break;
	case FFI_TYPE_SINT32:  r.val_ffi_sint32 = h->val_int; break;
	case FFI_TYPE_SINT64:  r.val_ffi_sint64 = h->val_int; break;
	case FFI_TYPE_INT:     r.val_ffi_sint = h->val_int; break;
	case FFI_TYPE_FLOAT:   r.val_ffi_float = h->val_float; break;
	case FFI_TYPE_DOUBLE:  r.val_ffi_double = h->val_float; break;
	case FFI_TYPE_POINTER: r.val_ffi_pointer = (void*)(size_t)h->val_uint; break;
	default: return 0;
	}

	memcpy(dst, &r, t->size);
	return t->size;
}

// The term for one scalar field of a returned struct, read at src: its size, or 0 for a field it can't read.

static size_t ffi_unpack_field(const ffi_type *t, const uint8_t *src, cell *tmp)
{
	switch (t->type) {
	case FFI_TYPE_UINT8:   make_uint(tmp, *(const uint8_t*)src); break;
	case FFI_TYPE_UINT16:  make_uint(tmp, *(const uint16_t*)src); break;
	case FFI_TYPE_UINT32:  make_uint(tmp, *(const uint32_t*)src); break;
	case FFI_TYPE_UINT64:  make_uint(tmp, *(const uint64_t*)src); break;
	case FFI_TYPE_SINT8:   make_int(tmp, *(const int8_t*)src); break;
	case FFI_TYPE_SINT16:  make_int(tmp, *(const int16_t*)src); break;
	case FFI_TYPE_SINT32:  make_int(tmp, *(const int32_t*)src); break;
	case FFI_TYPE_SINT64:  make_int(tmp, *(const int64_t*)src); break;
	case FFI_TYPE_INT:     make_int(tmp, *(const int*)src); break;
	case FFI_TYPE_FLOAT:   make_float(tmp, *(const float*)src); break;
	case FFI_TYPE_DOUBLE:  make_float(tmp, *(const double*)src); break;
	case FFI_TYPE_POINTER: make_uint(tmp, *(const size_t*)src); break;
	default: return 0;
	}

	return t->size;
}

// A registered predicate or evaluable, never moved or freed as biftab and compiled calls hold it, so registering
// it again updates it in place. Owns its name and its struct return's name, as foreign_struct does.

typedef struct ffi_entry_ {
	builtins b;							// first, so an entry is its builtins
	struct ffi_entry_ *next;
	ffi_cif cif;
	ffi_type **args;
	unsigned args_cap;
	unsigned nfixed;					// C arguments before a '...', or MAX_FFI_ARGS for none
	char name[];
} ffi_entry;

// A variadic function's cif says where its fixed arguments end, as Apple's arm64 ABI passes the rest on the stack.

static ffi_status ffi_prep(ffi_cif *cif, const ffi_entry *e, unsigned nargs, ffi_type *ret, ffi_type **args)
{
	if (e->nfixed < nargs)
		return ffi_prep_cif_var(cif, FFI_DEFAULT_ABI, e->nfixed, nargs, ret, args);

	return ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ret, args);
}

// A variadic argument takes C's default promotions, which libffi insists on: float to double, narrower than int to int.

static uint8_t ffi_promote(uint8_t tag)
{
	switch (tag) {
	case FFI_TAG_FP32:
		return FFI_TAG_FP64;
	case FFI_TAG_SINT8: case FFI_TAG_SINT16: case FFI_TAG_SHORT:
	case FFI_TAG_UINT8: case FFI_TAG_UINT16: case FFI_TAG_USHORT:
		return FFI_TAG_SINT;
	default:
		return tag;
	}
}

static ffi_entry *g_ffi_entries = NULL;

// Build the cif once, at registration. Everything it needs - the arg
// types and the return type - is fixed by the signature, so the only
// reason to defer it is a struct, whose ffi_type is assembled per call
// from the foreign_struct table.

static void precompile_cif(ffi_entry *e)
{
	builtins *ptr = &e->b;
	ptr->cif = NULL;

	// A predicate carries the return as a trailing argument, so its own arity is one
	// more than the C function's; an evaluable returns through q->accum and does not.

	unsigned nargs = (ptr->evaluable || (ptr->ret_type == FFI_TAG_VOID)) ? ptr->arity : ptr->arity - 1;
	ffi_type *ret = ffi_type_of(ptr->ret_type);

	if (!ret || (nargs > MAX_FFI_ARGS))
		return;

	if (nargs > e->args_cap) {
		ffi_type **at = TPL_realloc(e->args, nargs * sizeof(*at));

		if (!at)
			return;

		e->args = at;
		e->args_cap = nargs;
	}

	for (unsigned i = 0; i < nargs; i++) {
		ffi_type *t = ffi_type_of(ptr->types[i]);

		if (!t)
			return;				// a struct arg: leave it per-call

		e->args[i] = t;
	}

	if (ffi_prep(&e->cif, e, nargs, ret, e->args) != FFI_OK)
		return;

	ptr->cif = &e->cif;
}

// The entry registered as name/arity, predicate or evaluable, in this instance, if any.

static ffi_entry *find_ffi_entry(prolog *pl, pl_idx key, unsigned arity, bool evaluable)
{
	sliter *iter = sl_find_key(pl->biftab, (void*)(size_t)key);
	builtins *ptr;

	while (sl_next_key(iter, (void**)&ptr)) {
		if (ptr->ffi && (ptr->arity == arity) && (ptr->evaluable == evaluable)) {
			sl_done(iter);
			return (ffi_entry*)ptr;
		}
	}

	sl_done(iter);
	return NULL;
}

// types holds arity tags; a predicate's last is its return slot, which a void function drops from its own arity.
// nfixed is how many arguments come before a '...' in the signature, or negative for none.

static bool register_ffi(prolog *pl, const char *name, unsigned arity, void *fn, uint8_t *types, uint8_t ret_type, const char *ret_name, bool evaluable, int nfixed)
{
	unsigned own_arity = (!evaluable && (ret_type == FFI_TAG_VOID)) ? arity - 1 : arity;
	pl_idx key = new_atom(pl, name);

	if (key == ERR_IDX)
		return false;

	ffi_entry *e = find_ffi_entry(pl, key, own_arity, evaluable);
	const bool fresh = !e;
	char *ret_copy = NULL;

	if ((ret_type == FFI_TAG_STRUCT) && ret_name && !(ret_copy = TPL_strdup(ret_name)))
		return false;

	if (fresh) {
		size_t len = strlen(name) + 1;

		if (!(e = TPL_calloc(1, sizeof(ffi_entry) + len))) {
			TPL_free(ret_copy);
			return false;
		}

		memcpy(e->name, name, len);
		e->b.name = e->name;
		e->b.arity = own_arity;
		e->b.evaluable = evaluable;
		e->b.ffi = true;
	}

	builtins *ptr = &e->b;
	ptr->fn = fn;
	memcpy(ptr->types, types, arity);
	e->nfixed = nfixed < 0 ? MAX_FFI_ARGS : (unsigned)nfixed;
	unsigned nargs = (evaluable || (ret_type == FFI_TAG_VOID)) ? own_arity : own_arity - 1;

	for (unsigned i = e->nfixed; i < nargs; i++)
		ptr->types[i] = ffi_promote(ptr->types[i]);

	ptr->ret_type = ret_type;
	TPL_free((void*)ptr->ret_name);
	ptr->ret_name = ret_copy;
	precompile_cif(e);

	if (fresh) {
		e->next = g_ffi_entries;
		g_ffi_entries = e;
		sl_app(pl->biftab, (void*)(size_t)key, ptr);
	}

	return true;
}

// Registered names for completion: the first entry, then each entry's name, stepping *cursor on to the next.

const void *ffi_registered_first(void)
{
	return g_ffi_entries;
}

const char *ffi_registered_next(const void **cursor)
{
	const ffi_entry *e = *cursor;

	if (!e)
		return NULL;

	*cursor = e->next;
	return e->b.name;
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
	int idx = 0, nfixed = -1;

	while (is_iso_list(l) && (idx < MAX_FFI_ARGS)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);

		if (is_interned(h)) {
			const char *src = C_STR(q, h);

			uint8_t t;

			if (!strcmp(src, "...") && (nfixed < 0))
				nfixed = idx;
			else if (parse_ffi_type(src, &t))
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

	return register_ffi(q->pl, symbol, idx, (void*)func, arg_types, ret_type, NULL, true, nfixed);
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

	return register_struct(m->pl, symbol, idx, arg_types, arg_names);
}

bool do_register_predicate(module *m, query *q, void *handle, const char *symbol, cell *l, pl_ctx l_ctx, const char *ret)
{
	void *func = dlsym(handle, symbol);
	if (!func) return false;

	uint8_t arg_types[MAX_FFI_ARGS], ret_type = 0;
	PROLOG_LIST_HANDLER(l);
	int idx = 0, nfixed = -1;

	// The return takes the last slot, which this loop stopping at MAX_FFI_ARGS used to put one past the end.

	while (is_iso_list(l)) {
		if (idx >= (MAX_FFI_ARGS - 1)) {
			printf("Error: foreign predicate %s: over %u arguments\n", symbol, (unsigned)(MAX_FFI_ARGS - 1));
			return false;
		}

		cell *h = PROLOG_LIST_HEAD(l);
		h = q ? deref(q, h, l_ctx) : h;

		if (is_interned(h)) {
			const char *src = C_STR(m, h);

			uint8_t t;

			if (!strcmp(src, "...")) {
				if (nfixed >= 0) {
					printf("Error: foreign predicate %s: more than one ...\n", symbol);
					return false;
				}

				nfixed = idx;
			} else if (!strcmp(src, "-") && parse_ffi_type(C_STR(m, h+1), &t))
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

	return register_ffi(m->pl, symbol, idx, (void*)func, arg_types, ret_type, src, false, nfixed);
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

	ffi_type *arg_types[MAX_FFI_ARGS];
	void *arg_values[MAX_FFI_ARGS];
	void *s_args[MAX_FFI_ARGS];
	result cells[MAX_FFI_ARGS];

	// Every argument is the C function's: an evaluable's result goes to q->accum.

	unsigned arity = ptr->arity;
	const bool prepped = ptr->cif != NULL;

	memset(arg_values, 0, arity * sizeof(*arg_values));
	memset(s_args, 0, arity * sizeof(*s_args));
	memset(cells, 0, arity * sizeof(*cells));

	for (unsigned i = 0; i < arity; i++) {
		int st = ffi_marshal(q, ptr->types[i], c, false, &cells[i], &s_args[i], &arg_values[i]);

		if (st == FFI_ARG_TYPE)
			return throw_error(q, c, c_ctx, "type_error", ffi_err_of(ptr->types[i]));

		// Nothing to marshal is a void, var or struct type, which no evaluable takes.

		if (st == FFI_ARG_NONE)
			return false;

		if (!prepped && (ptr->types[i] != FFI_TAG_STRUCT))
			arg_types[i] = ffi_type_of(ptr->types[i]);

		GET_NEXT_ARG(p2, any);
		c = p2;
		c_ctx = p2_ctx;
	}

	ffi_cif cif = {0};
	ffi_cif *cifp = (ffi_cif*)ptr->cif;

	if (!cifp) {
		ffi_type *ffi_ret_type = ffi_type_of(ptr->ret_type);
		ffi_status ok;

		// Not NULL, which ffi_prep_cif dereferences: a signature it cannot describe
		// has to be refused here instead.

		if (!ffi_ret_type)
			return throw_error(q, q->st.instr, q->st.cur_ctx, "type_error", ffi_err_of(ptr->ret_type));

		if ((ok = ffi_prep(&cif, (const ffi_entry*)ptr, arity, ffi_ret_type, arg_types)) != FFI_OK) {
			printf("Error: ffi_prep_cif status=%d\n", ok);
			return false;
		}

		cifp = &cif;
	}

	ffi_ret r;
	ffi_call(cifp, FFI_FN(ptr->fn), &r, arg_values);

	cell tmp;
	int made = ffi_scalar(ptr->ret_type) ? ffi_to_term(ptr->ret_type, &r.val, true, &tmp) : 0;

	if (made < 0)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	if (!made)
		return false;

	q->accum = tmp;
	return true;
}

// Where a field or struct of alignment a starts, at or after offset n.

#define FFI_ALIGN_UP(n, a) (((n) + (a) - 1) / (a) * (a))

// Lays out struct type st as libffi does on its first call with it: each field at its own alignment, the whole
// padded to the widest. Packing a struct argument needs the layout before then, and libffi keeps what it finds.

static void ffi_layout(ffi_type *st)
{
	size_t size = 0;
	unsigned short align = 1;

	for (ffi_type **e = st->elements; *e; e++) {
		size = FFI_ALIGN_UP(size, (*e)->alignment) + (*e)->size;

		if ((*e)->alignment > align)
			align = (*e)->alignment;
	}

	st->size = FFI_ALIGN_UP(size, align);

#ifdef FFI_AGGREGATE_ALIGNMENT
	if (FFI_AGGREGATE_ALIGNMENT > align)
		align = FFI_AGGREGATE_ALIGNMENT;
#endif

	st->alignment = align;
}

// The ffi_type of struct sptr, built at the next free entry of types[] with each struct it nests after it, and
// laid out. *pdepth is left at the last entry used. NULL for an unknown nested struct, a field of no C type, or
// more fields or nesting than the arrays hold.

static ffi_type *build_struct_type(query *q, foreign_struct *sptr, nested_elements *nested, ffi_type *types, unsigned *pdepth)
{
	unsigned sarity = sptr->arity, depth = *pdepth + 1;

	if ((depth >= MAX_FFI_ARGS) || (sarity >= MAX_FFI_ARGS))
		return NULL;

	*pdepth = depth;
	ffi_type *st = &types[depth];
	ffi_type **elements = nested[depth].elements;
	st->size = st->alignment = 0;
	st->type = FFI_TYPE_STRUCT;
	st->elements = elements;

	for (unsigned cnt = 0; cnt < sarity; cnt++) {
		uint8_t tag = sptr->types[cnt];

		if (tag == FFI_TAG_STRUCT) {
			foreign_struct *inner = NULL;

			if (!sl_get(q->pl->fortab, sptr->names[cnt], (void*)&inner)) {
				printf("wrapper: not found struct: %s\n", sptr->names[cnt]);
				return NULL;
			}

			if (!(elements[cnt] = build_struct_type(q, inner, nested, types, pdepth)))
				return NULL;
		} else if (ffi_scalar(tag))
			elements[cnt] = ffi_type_of(tag);
		else {
			printf("wrapper: struct %s: field %u is no C type\n", sptr->name, cnt + 1);
			return NULL;
		}
	}

	elements[sarity] = NULL;
	ffi_layout(st);
	return st;
}

// Packs list l, a struct's name then its fields, into bytes at *off as C lays out st: each field at its own
// alignment, and a nested struct given as a list of the same form. A pointer or string field takes an address
// given as an integer, or text, pointed at where it is since it lasts the call, and is NULL for anything else,
// such as an unbound variable, whose cell is no address. *off is left past the struct's tail padding. False if it
// would not fit.

static bool ffi_pack_struct(query *q, const ffi_type *st, cell *l, pl_ctx l_ctx, uint8_t *bytes, size_t *off)
{
	size_t at = FFI_ALIGN_UP(*off, st->alignment), end = at + st->size;

	if (end > MAX_FFI_STRUCT_BYTES)
		return false;

	*off = end;
	PROLOG_LIST_HANDLER(l);

	if (!is_iso_list(l))
		return true;

	l = PROLOG_LIST_TAIL(l);
	l = deref(q, l, l_ctx);
	l_ctx = q->latest_ctx;

	for (ffi_type **e = st->elements; *e && is_iso_list(l); e++) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);
		pl_ctx h_ctx = q->latest_ctx;
		at = FFI_ALIGN_UP(at, (*e)->alignment);

		// A nested struct lies inside this one, so it fits.

		if ((*e)->type == FFI_TYPE_STRUCT) {
			size_t inner = at;
			ffi_pack_struct(q, *e, h, h_ctx, bytes, &inner);
		} else if ((*e)->type == FFI_TYPE_POINTER) {
			const void *ptr = is_smallint(h) ? (const void*)(size_t)get_smalluint(h)
				: (is_atom(h) || is_string(h)) ? (const void*)C_STR(q, h)
				: NULL;
			memcpy(bytes + at, &ptr, sizeof(ptr));
		} else
			ffi_pack_field(*e, h, bytes + at);

		at += (*e)->size;
		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	return true;
}

// The term [Name, Field, ...] for struct sptr, of type st, read from src at the offsets C lays its fields out at,
// a nested struct as a list of the same form, written at out: the number of cells, or 0 for a field it can't
// read. With out NULL it only counts them.

static unsigned ffi_struct_term(query *q, const foreign_struct *sptr, const char *name, const ffi_type *st, const uint8_t *src, cell *out)
{
	unsigned n = 0;
	size_t at = 0;

	// Each element is a '.' cell then its own; fix_list() sets each '.' to span the rest once all are written.

	if (out) make_instr(out+n, g_dot_s, NULL, 2, 0);
	n++;

	if (out && !make_cstring(out+n, name))
		return 0;

	n++;

	for (unsigned k = 0; st->elements[k]; k++) {
		const ffi_type *e = st->elements[k];
		at = FFI_ALIGN_UP(at, e->alignment);
		if (out) make_instr(out+n, g_dot_s, NULL, 2, 0);
		n++;

		if (e->type == FFI_TYPE_STRUCT) {
			foreign_struct *inner = NULL;

			if (!sl_get(q->pl->fortab, sptr->names[k], (void*)&inner))
				return 0;

			unsigned m = ffi_struct_term(q, inner, sptr->names[k], e, src + at, out ? out+n : NULL);

			if (!m)
				return 0;

			n += m;
		} else {
			cell tmp;

			if (!ffi_unpack_field(e, src + at, &tmp))
				return 0;

			if (out) out[n] = tmp;
			n++;
		}

		at += e->size;
	}

	if (out) make_atom(out+n, g_nil_s);
	n++;

	if (out) {
		out->num_cells = n;
		fix_list(out);
	}

	return n;
}

// '$struct_to_pointer'/2's layout, worked from foreign_struct as libffi's is from ffi_type, since its structs may
// have more fields than an ffi_type is built with here. The size and alignment C gives a field, or a whole struct:
// its fields at their alignment, the whole padded to the widest. False for a field of no C type, an unknown nested
// struct, or nesting past MAX_FFI_ARGS.

static bool fs_layout(query *q, const foreign_struct *sptr, unsigned depth, size_t *size, unsigned short *align);

static bool fs_field_layout(query *q, const foreign_struct *sptr, unsigned k, unsigned depth, size_t *size, unsigned short *align)
{
	uint8_t tag = sptr->types[k];

	if (tag == FFI_TAG_STRUCT) {
		foreign_struct *inner = NULL;

		if (!sl_get(q->pl->fortab, sptr->names[k], (void*)&inner)) {
			printf("wrapper: not found struct: %s\n", sptr->names[k]);
			return false;
		}

		return fs_layout(q, inner, depth + 1, size, align);
	}

	if (!ffi_scalar(tag)) {
		printf("wrapper: struct %s: field %u is no C type\n", sptr->name, k + 1);
		return false;
	}

	const ffi_type *t = ffi_type_of(tag);
	*size = t->size;
	*align = t->alignment;
	return true;
}

static bool fs_layout(query *q, const foreign_struct *sptr, unsigned depth, size_t *size, unsigned short *align)
{
	if (depth >= MAX_FFI_ARGS)
		return false;

	size_t at = 0;
	unsigned short widest = 1;

	for (unsigned k = 0; k < sptr->arity; k++) {
		size_t fsize;
		unsigned short falign;

		if (!fs_field_layout(q, sptr, k, depth, &fsize, &falign))
			return false;

		at = FFI_ALIGN_UP(at, falign) + fsize;

		if (falign > widest)
			widest = falign;
	}

	*size = FFI_ALIGN_UP(at, widest);

#ifdef FFI_AGGREGATE_ALIGNMENT
	if (FFI_AGGREGATE_ALIGNMENT > widest)
		widest = FFI_AGGREGATE_ALIGNMENT;
#endif

	*align = widest;
	return true;
}

enum { FS_OK, FS_TOO_MANY, FS_NO_TYPE };

// Puts the values in list l, struct sptr's fields in order and perhaps fewer of them, into blk at base as C lays
// them out: each field at its own alignment, and a nested struct as a list [Name, Field, ...] of its own. A pointer
// or string field takes an address given as an integer, or text, copied to *strs so the one block holds both, and
// is NULL for anything else. *end is left past the last field and *align raised to the widest. With blk NULL it
// only measures, *strs counting the text.

static int fs_pack(query *q, const foreign_struct *sptr, cell *l, pl_ctx l_ctx, unsigned depth, uint8_t *blk, size_t base, size_t *end, size_t *strs, unsigned short *align)
{
	PROLOG_LIST_HANDLER(l);
	size_t at = base;
	unsigned k = 0;

	for (; is_iso_list(l); k++) {
		if (k >= sptr->arity)
			return FS_TOO_MANY;

		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);
		pl_ctx h_ctx = q->latest_ctx;
		uint8_t tag = sptr->types[k];
		size_t fsize;
		unsigned short falign;

		if (!fs_field_layout(q, sptr, k, depth, &fsize, &falign))
			return FS_NO_TYPE;

		at = FFI_ALIGN_UP(at, falign);

		if (tag == FFI_TAG_STRUCT) {
			foreign_struct *inner = NULL;
			sl_get(q->pl->fortab, sptr->names[k], (void*)&inner);
			cell il_tmp, *il = h;
			pl_ctx il_ctx = h_ctx;

			if (is_iso_list(il)) {
				il = deref(q, list_tail(il, &il_tmp), il_ctx);
				il_ctx = q->latest_ctx;
			}

			size_t inner_end;
			int rc = fs_pack(q, inner, il, il_ctx, depth + 1, blk, at, &inner_end, strs, align);

			if (rc != FS_OK)
				return rc;
		} else if ((tag == FFI_TAG_PTR) || (tag == FFI_TAG_C_STR) || (tag == FFI_TAG_C_CSTR)) {
			if (is_atom(h) || is_string(h)) {
				size_t len = C_STRLEN(q, h);

				if (blk) {
					char *text = (char*)blk + *strs;
					memcpy(text, C_STR(q, h), len);
					text[len] = '\0';
					memcpy(blk + at, &text, sizeof(text));
				}

				*strs += len + 1;
			} else if (blk && is_smallint(h)) {
				const void *ptr = (const void*)(size_t)get_smalluint(h);
				memcpy(blk + at, &ptr, sizeof(ptr));
			}
		} else if (blk)
			ffi_pack_field(ffi_type_of(tag), h, blk + at);

		at += fsize;

		if (falign > *align)
			*align = falign;

		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	*end = at;
	return FS_OK;
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
	// Both are only ever touched by build_struct_type() and
	// ffi_pack_struct(), so a signature with no struct in it never
	// reads them. Zero them only when there is one.

	nested_elements nested[MAX_FFI_ARGS];
	ffi_type types[MAX_FFI_ARGS];

	// Same again for these five, which came to 6 KB of zeroing per call: only the
	// first `arity` entries of the four arrays are ever read, and bytes[] only by a
	// struct argument.

	ffi_type *arg_types[MAX_FFI_ARGS];
	void *arg_values[MAX_FFI_ARGS];
	void *s_args[MAX_FFI_ARGS];
	result cells[MAX_FFI_ARGS];
	_Alignas(16) uint8_t bytes[MAX_FFI_STRUCT_BYTES];

	ffi_type *ffi_ret_type = NULL;
	foreign_struct *ret_sptr = NULL;
	unsigned arity = ptr->arity - 1, pdepth = 0;
	size_t bytes_offset = 0;

	if (ptr->ret_type == FFI_TAG_VOID)
		arity++;

	bool uses_struct = ptr->ret_type == FFI_TAG_STRUCT;

	for (unsigned i = 0; !uses_struct && (i < arity); i++)
		uses_struct = ptr->types[i] == FFI_TAG_STRUCT;

	if (uses_struct) {
		memset(nested, 0, sizeof(nested));
		memset(types, 0, sizeof(types));
		memset(bytes, 0, sizeof(bytes));
	}

	memset(arg_values, 0, arity * sizeof(*arg_values));
	memset(s_args, 0, arity * sizeof(*s_args));
	memset(cells, 0, arity * sizeof(*cells));

	// With the cif built at registration, nothing reads arg_types[] at all.

	const bool prepped = ptr->cif != NULL;

	for (unsigned i = 0; i < arity; i++) {
		// A struct is checked here; anything else is checked as it is marshalled.

		if ((ptr->types[i] == FFI_TAG_STRUCT) && !is_iso_list(c) && !is_var(c))
			return throw_error(q, c, c_ctx, "type_error", ffi_err_of(ptr->types[i]));

		if (ptr->types[i] == FFI_TAG_STRUCT) {
			// Named by its list's head, which was read as a string whatever it was.

			if (is_var(c))
				return throw_error(q, c, c_ctx, "instantiation_error", "list");

			cell h_tmp, *h = deref(q, list_head(c, &h_tmp), c_ctx);
			pl_ctx h_ctx = q->latest_ctx;

			if (is_var(h))
				return throw_error(q, h, h_ctx, "instantiation_error", "atom");

			if (!is_atom(h))
				return throw_error(q, h, h_ctx, "type_error", "atom");

			const char *name = C_STR(q, h);
			foreign_struct *sptr = NULL;

			if (!sl_get(q->pl->fortab, name, (void*)&sptr)) {
				printf("wrapper: not found struct: %s\n", name);
				return false;
			}

			if (!(arg_types[i] = build_struct_type(q, sptr, nested, types, &pdepth)))
				return false;

			bytes_offset = FFI_ALIGN_UP(bytes_offset, arg_types[i]->alignment);
			arg_values[i] = &bytes[bytes_offset];

			if (!ffi_pack_struct(q, arg_types[i], c, c_ctx, bytes, &bytes_offset))
				return throw_error(q, c, c_ctx, "resource_error", "ffi_struct_bytes");
		} else {
			if (!prepped && !(arg_types[i] = ffi_type_of(ptr->types[i]))) {
				printf("Warning: struct ptr->type=%u\n", ptr->types[i]);
				return false;
			}

			int st = ffi_marshal(q, ptr->types[i], c, true, &cells[i], &s_args[i], &arg_values[i]);

			if (st == FFI_ARG_TYPE)
				return throw_error(q, c, c_ctx, "type_error", ffi_err_of(ptr->types[i]));

			if (st == FFI_ARG_NONE) {
				printf("Warning: struct ptr->type=%u\n", ptr->types[i]);
				return false;
			}
		}

		GET_NEXT_ARG(p2, any);
		c = p2;
		c_ctx = p2_ctx;
	}

	// The return type, for a cif that still has to be built. A struct return is the
	// only one whose ffi_type has to be assembled here; everything else is a lookup.

	if (prepped) {
		;
	} else if (ptr->ret_type == FFI_TAG_STRUCT) {
		if (!sl_get(q->pl->fortab, ptr->ret_name, (void*)&ret_sptr)) {
			printf("wrapper: not found struct: %s\n", ptr->ret_name);
			return false;
		}

		if (!(ffi_ret_type = build_struct_type(q, ret_sptr, nested, types, &pdepth)))
			return false;

		if (ffi_ret_type->size > MAX_FFI_RET_BYTES)
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "ffi_struct_bytes");
	} else if (!(ffi_ret_type = ffi_type_of(ptr->ret_type))) {
		printf("Warning: struct ptr->ret_type=%u\n", ptr->ret_type);
		return false;
	}

	// Pre-compiled at registration unless the signature returns a
	// struct, in which case ffi_ret_type was just built above and the
	// cif has to be prepared here as before.

	ffi_cif cif = {0};
	ffi_cif *cifp = (ffi_cif*)ptr->cif;

	if (!cifp) {
		ffi_status ok;

		if ((ok = ffi_prep(&cif, (const ffi_entry*)ptr, arity, ffi_ret_type, arg_types)) != FFI_OK) {
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
		// An output read back at its own C type.

		if (is_var(c) && IS_OUT(ptr->types[i]) && ffi_scalar(OUT_BASE(ptr->types[i]))) {
			uint8_t tag = OUT_BASE(ptr->types[i]);
			cell tmp;
			int made = ffi_to_term(tag, &cells[i], false, &tmp);

			if (made < 0)
				return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

			if (!made)
				return false;

			bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);

			if ((tag == FFI_TAG_C_STR) || (tag == FFI_TAG_C_CSTR))
				unshare_cell(&tmp);

			if (ok != true) return ok;
		}

		GET_NEXT_ARG(p2, any);
		c = p2;
		c_ctx = p2_ctx;
	}

	// A cstr return is the C side's to give away, so its string is freed once copied; a ccstr one is not.

	if (ffi_scalar(ptr->ret_type)) {
		cell tmp;
		int made = ffi_to_term(ptr->ret_type, &r.val, true, &tmp);

		if (made < 0)
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

		if (!made)
			return false;

		if (ptr->ret_type == FFI_TAG_C_STR)
			TPL_free(r.val.val_ffi_pointer);

		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);

		if ((ptr->ret_type == FFI_TAG_C_STR) || (ptr->ret_type == FFI_TAG_C_CSTR))
			unshare_cell(&tmp);

		if (ok != true) return ok;
	} else if (ptr->ret_type == FFI_TAG_STRUCT) {
		unsigned n = ffi_struct_term(q, ret_sptr, ptr->ret_name, ffi_ret_type, r.bytes, NULL);

		if (!n)
			return false;

		// Zeroed first, so that a name it ran out of memory for leaves no half-made term behind.

		cell *tmp = alloc_heap(q, n);
		CHECKED(tmp);
		memset(tmp, 0, sizeof(cell) * n);

		if (!ffi_struct_term(q, ret_sptr, ptr->ret_name, ffi_ret_type, r.bytes, tmp)) {
			for (unsigned i = 0; i < n; i++)
				unshare_cell(tmp + i);

			memset(tmp, 0, sizeof(cell) * n);
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
		}

		if (!unify(q, c, c_ctx, tmp, q->st.cur_ctx))
			return false;
	}

	return true;
}

// The directive code reads cells as written, so a goal hands it a copy with its variables' values in.

static bool bif_foreign_struct_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,list_or_nil);
	CHECKED(init_tmp_heap(q));
	cell *tmp = copy_term_to_tmp(q, q->st.instr, q->st.cur_ctx, false);
	CHECKED(tmp);
	return do_foreign_struct(q->st.m, tmp);
}

static bool bif_use_foreign_module_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,list_or_nil);
	CHECKED(init_tmp_heap(q));
	cell *tmp = copy_term_to_tmp(q, q->st.instr, q->st.cur_ctx, false);
	CHECKED(tmp);
	return do_use_foreign_module(q->st.m, tmp);
}

static bool bif_sys_struct_to_pointer_2(query *q)
{
	GET_FIRST_ARG(p1,list);
	GET_NEXT_ARG(p2,var);
	PROLOG_LIST_HANDLER(p1);

	// The name, every field and each tail are deref'd: a list built through variables holds var cells there, which
	// were read as they were, so library(plplot) copied its lists with findall/3 first to get round it.

	cell *c = PROLOG_LIST_HEAD(p1);
	c = deref(q, c, p1_ctx);
	pl_ctx c_ctx = q->latest_ctx;

	if (is_var(c))
		return throw_error(q, c, c_ctx, "instantiation_error", "atom");

	if (!is_atom(c))
		return throw_error(q, c, c_ctx, "type_error", "atom");

	const char *name = C_STR(q, c);
	foreign_struct *sptr = NULL;

	if (!sl_get(q->pl->fortab, name, (void*)&sptr)) {
		printf("wrapper: not found struct: %s\n", name);
		return false;
	}

	p1 = PROLOG_LIST_TAIL(p1);
	p1 = deref(q, p1, p1_ctx);
	p1_ctx = q->latest_ctx;

	// Measured first, since a string field's text goes after the struct, in the same block.

	size_t end = 0, strs = 0;
	unsigned short align = 1;
	int rc = fs_pack(q, sptr, p1, p1_ctx, 0, NULL, 0, &end, &strs, &align);

	if (rc == FS_TOO_MANY)
		return throw_error(q, p1, p1_ctx, "representation_error", "ffi_struct_fields");

	if (rc != FS_OK)
		return false;

	// Padded at the end as C pads it, so a C side that reads the whole struct stays inside what it was given, and
	// zeroed, as the padding is left.

	size_t size = FFI_ALIGN_UP(end, align), total = size + strs;
	uint8_t *ptr = TPL_calloc(1, total ? total : 1);

	if (!ptr)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	strs = size;
	fs_pack(q, sptr, p1, p1_ctx, 0, ptr, 0, &end, &strs, &align);

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

builtins g_ffi_bifs[] =
{
#if USE_FFI
	{"$dlopen", 3, bif_sys_dlopen_3, "+atom,+atom,-term", false, false, BLAH},
	{"$dlsym", 3, bif_sys_dlsym_3, "+atom,+symbol,-term", false, false, BLAH},
	{"$dlsym_deref", 3, bif_sys_dlsym_deref_3, "+atom,+symbol,-term", false, false, BLAH},
	{"$dlclose", 1, bif_sys_dlclose_1, "+term", false, false, BLAH},
	{"$register_function", 4, bif_sys_register_function_4, "+term,+atom,+list,+atom", false, false, BLAH},
	{"$register_predicate", 4, bif_sys_register_predicate_4, "+term,+atom,+list,+atom", false, false, BLAH},
	{"$struct_to_pointer", 2, bif_sys_struct_to_pointer_2, "+list,-integer", false, false, BLAH},
	{"$free_struct_pointer", 1, bif_sys_free_struct_pointer_1, "+integer", false, false, BLAH},

	{"foreign_struct", 2, bif_foreign_struct_2, "+atom,+list", false, false, BLAH},
	{"use_foreign_module", 2, bif_use_foreign_module_2, "+atom,+list", false, false, BLAH},
#endif

	{0}
};

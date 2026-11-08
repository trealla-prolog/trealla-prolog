#include "prolog.h"
#include "query.h"

// Stand-in for src/bif_ffi.c without USE_FFI: only the table and completion's walk of registered predicates are used.

builtins g_ffi_bifs[] =
{
	{0}
};

const void *ffi_registered_first(void)
{
	return NULL;
}

const char *ffi_registered_next(const void **cursor)
{
	(void)cursor;
	return NULL;
}

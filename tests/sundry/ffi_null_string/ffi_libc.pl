% strchr/3 bound from libc under whichever name this platform opens it by; ffi_bound/0 says whether one did.

:- module(ffi_libc, [strchr/3, ffi_bound/0]).

:- dynamic(ffi_bound/0).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, strchr, [cstr,sint], ccstr),
		assertz(ffi_bound)
	;	true
	).

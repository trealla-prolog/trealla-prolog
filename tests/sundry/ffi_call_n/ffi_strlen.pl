% strlen/2 bound from libc under whichever name this platform opens it by; ffi_bound/0 says whether one did.

:- module(ffi_strlen, [strlen/2, ffi_bound/0, libc/1]).

:- dynamic(ffi_bound/0).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, strlen, [cstr], ulong),
		assertz(ffi_bound)
	;	true
	).

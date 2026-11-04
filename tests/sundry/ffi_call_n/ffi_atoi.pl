% atoi/2 from libc, for ../ffi_call_n.pl to load at run time, after the clause calling it was compiled.

:- module(ffi_atoi, [atoi/2]).
:- use_module(ffi_strlen).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, atoi, [cstr], sint)
	;	true
	).

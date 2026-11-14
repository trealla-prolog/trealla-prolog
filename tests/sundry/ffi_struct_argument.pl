% A struct passed by value took its fields from a list and dropped any past the last field, where
% '$struct_to_pointer'/2 raised representation_error(ffi_struct_fields); a struct name never declared
% printed "wrapper: not found struct" and failed, in both; a nested struct given as no list packed as
% zeros. inet_ntoa() takes struct in_addr by value; 127.0.0.127 reads the same in either byte order.
% Skipped where no libc opens or it has no inet_ntoa.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail),
		catch(foreign_struct(in_addr_t, [uint32]), _, fail),
		foreign_struct(in_addr_box, [in_addr_t]),
		'$register_predicate'(H, inet_ntoa, [in_addr_t], ccstr)
	->	assertz(ffi_bound)
	;	true
	).

t(Label, Goal) :-
	(   \+ ffi_bound
	->  R = ok
	;   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

raises(Goal, Error) :-
	catch((Goal, fail), error(E, _), (E = Error -> true ; throw(error(E, _)))).

main :-
	t(by_value, (inet_ntoa([in_addr_t, 2130706559], S), S == '127.0.0.127')),
	t(fewer_values_are_zero, (inet_ntoa([in_addr_t], S2), S2 == '0.0.0.0')),
	t(too_many_values, raises(inet_ntoa([in_addr_t, 2130706559, 9], _), representation_error(ffi_struct_fields))),
	t(undeclared_struct, raises(inet_ntoa([no_such_struct, 1], _), existence_error(foreign_struct, no_such_struct))),
	t(undeclared_struct_pointer, raises('$struct_to_pointer'([no_such_struct, 1], _), existence_error(foreign_struct, no_such_struct))),
	t(nested_not_list, raises('$struct_to_pointer'([in_addr_box, 5], _), type_error(list, 5))),
	t(nested_unbound, raises('$struct_to_pointer'([in_addr_box, _], _), instantiation_error)),
	t(nested_too_many, raises('$struct_to_pointer'([in_addr_box, [in_addr_t, 1, 2]], _), representation_error(ffi_struct_fields))),
	t(nested_by_value, (re_register(in_addr_box), inet_ntoa([in_addr_box, [in_addr_t, 2130706559]], S3), S3 == '127.0.0.127')),
	t(nested_by_value_not_list, raises(inet_ntoa([in_addr_box, 5], _), type_error(list, 5))),
	t(nested_by_value_too_many, raises(inet_ntoa([in_addr_box, [in_addr_t, 1, 2]], _), representation_error(ffi_struct_fields))).

% struct { struct in_addr } is passed as struct in_addr is.

re_register(S) :-
	libc(L), catch('$dlopen'(L, 0, H), _, fail), !,
	'$register_predicate'(H, inet_ntoa, [S], ccstr).

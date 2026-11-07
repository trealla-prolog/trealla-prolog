% dynamic, discontiguous or multifile for an ISO builtin set an error the loader
% took as the end of the file, so everything after it was dropped without a
% word, while consult/1 reported success. Each is now refused with a permission
% error, as in SWI, and the load goes on. A module exporting an ISO builtin it
% does not define made use_module/1 crash; that export is now let be, as SWI
% does, and the builtin stays callable.

:- initialization(main).

:- consult(decls).
:- use_module(exports_builtin).

main :-
	check(rest_of_file_loaded, after(1)),
	check(list_neighbour_declared, predicate_property(listed(_), dynamic)),
	check(comma_neighbour_declared, predicate_property(commaed(_), dynamic)),
	check(findall_untouched, (findall(X, member(X, [a,b]), L), L == [a,b])),
	check(atom_length_untouched, (atom_length(abc, N), N == 3)),
	check(module_own_export, own(1)).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).

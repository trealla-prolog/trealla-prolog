% prolog_load_context/2 gave only module. As in SWI, source, file and directory now hold while a file loads (file and
% directory naming an included file inside it) and not in its initialization goals; dialect and module hold throughout.

:- initialization(main).

% The last path component, so no absolute path reaches the output.

base(Path, Base) :-
	(   sub_atom(Path, B, 1, A, S), sep(S),
	    \+ (sub_atom(Path, B2, 1, _, S2), B2 > B, sep(S2))
	->  sub_atom(Path, _, A, 0, Base)
	;   Base = Path
	).

sep('/').
sep('\\').

show(Where) :-
	forall(
		member(K, [source, file, directory, module, dialect, no_such_key]),
		(	prolog_load_context(K, V)
		->	(memberchk(K, [source, file, directory]) -> base(V, B) ; B = V),
			format("~w ~w = ~q~n", [Where, K, B])
		;	format("~w ~w: fails~n", [Where, K])
		)).

:- show(directive).
:- include('load_context/inc').

term_expansion(probe_term, probed) :-
	show(term_expansion).

probe_term.

main :-
	show(initialization).

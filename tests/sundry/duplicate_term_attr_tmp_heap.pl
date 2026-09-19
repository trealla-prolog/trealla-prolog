:- initialization(main).

% Copying a large attribute in duplicate_term/2 grew a nested scratch
% buffer, then restored the smaller outer one without its size, so the
% next copy wrote past its end. The attribute must be larger when copied
% than when put, or put_atts/2 has already grown the outer buffer.

:- use_module(library(atts)).
:- attribute big/1.

mk(0, []) :- !.
mk(N, [N|T]) :- N1 is N-1, mk(N1, T).

main :-
	put_atts(X, +big(V)), mk(100000, V),
	duplicate_term(f(X), f(Y)),
	get_atts(Y, +big(V2)), length(V2, N2),
	mk(50000, B), copy_term(B, B2), length(B2, M2),
	write(N2-M2), nl.

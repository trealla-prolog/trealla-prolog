% =../2 builds its list on the calling frame's heap but with the context of
% the term taken apart, which can be an older frame. A tail call reusing the
% frame then trimmed the list from under the callee (Logtalk's compiler hit
% this in '$lgt_valid_mode_template'/1 on loading).

:- initialization(main).

template(Pred) :- Pred =.. [_|Args], template_args(Args).
template_args([]).
template_args([Arg|Args]) :- ( ground(Arg) -> template_arg(Arg) ; throw(instantiation_error) ), template_args(Args).
template_arg(+(_)).
template_arg(-(_)).
template_arg(?(_)).
template_arg(+).
template_arg(-).

t_modes(R) :- ( template(foo(+integer, -list, ?atom)), template(bar(+, -)) -> R = ok ; R = failed ).

args_of(Pred, R) :- Pred =.. [_|Args], rest_of(Args, R).
rest_of([_|Rest], R) :- R = Rest.
t_rest(R) :- args_of(f(a(1), b(2), c(3)), R0), copy_term(g(h(i), j, k(l, m, n)), _), R = R0.

main :-
	forall(
		member(T, [t_modes, t_rest]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			write(T), write(': '), writeq(R), nl
		)
	).

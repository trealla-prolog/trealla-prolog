% sig_atomic/1 runs its goal as once/1, holding thread signals until it is
% done, even one sent while it blocks; they run again as soon as it ends.

:- initialization(main).

:- dynamic(done/0).

signal(G) :- thread_self(Self), thread_signal(Self, G).

t(Name, G) :-
	( catch(G, E, true) -> ( var(E) -> R = true ; R = E ) ; R = failed ),
	format("~w: ~q~n", [Name, R]).

worker(Main) :-
	catch(sig_atomic((thread_send_message(Main, waiting), thread_get_message(go), assertz(done))), E, true),
	thread_send_message(Main, result(E)).

blocked(E-D) :-
	thread_self(Main),
	thread_create(worker(Main), T, []),
	thread_get_message(Main, waiting),
	thread_signal(T, throw(stop)),
	thread_send_message(T, go),
	thread_get_message(Main, result(E)),
	thread_join(T, _),
	( done -> D = done ; D = not_done ).

main :-
	t(det, (sig_atomic(member(X, [a,b])), X == a)),
	t(fails, sig_atomic(fail)),
	t(throws, sig_atomic(throw(oops))),
	t(queued, (sig_atomic((signal(throw(stop)), assertz(done))), fail)),
	t(after_success, (sig_atomic(true), signal(throw(late)), true)),
	t(after_throw, (catch(sig_atomic(throw(oops)), _, true), signal(throw(late)), true)),
	retractall(done),
	blocked(R),
	format("blocked: ~q~n", [R]).

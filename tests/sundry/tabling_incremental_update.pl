% Changing an incremental predicate that a table still being computed
% has called raises permission_error(update, variant, Goal), as in SWI,
% and the change is not made. It used to go through after the call, so
% the table completed on the old clauses, was stamped after the change,
% and kept its stale answers until the next one. Changing it before the
% call, changing one the table never called, a table that is not
% incremental, and one that only used retract/1 or clause/2 on it are
% all allowed.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

:- dynamic(q/1).
:- incremental(q/1).
:- dynamic(other/1).
:- incremental(other/1).
:- dynamic(once_done/1).

q(1).

first(Key) :- \+ once_done(Key), assertz(once_done(Key)).

% Calls q/1, then changes it.

:- table direct/1 as incremental.

direct(X) :- q(X).
direct(_) :- first(direct), assertz(q(2)), fail.

:- table by_retract/1 as incremental.

by_retract(X) :- q(X).
by_retract(_) :- retract(q(1)), fail.

:- table by_retractall/1 as incremental.

by_retractall(X) :- q(X).
by_retractall(_) :- retractall(q(_)), fail.

:- table by_erase/1 as incremental.

by_erase(X) :- q(X).
by_erase(_) :- erase_ref(R), erase(R), fail.

:- dynamic(erase_ref/1).

% A table it calls, being computed itself, makes the change.

:- table outer/1 as incremental.
:- table inner/1 as incremental.

outer(X) :- q(X), X > 100.
outer(X) :- inner(X).
inner(_) :- assertz(q(5)), fail.

% Called only through a completed table.

:- table top/1 as incremental.
:- table mid/1 as incremental.

mid(X) :- q(X).
top(X) :- mid(X).
top(_) :- assertz(q(7)), fail.

% Allowed: before the call, an incremental predicate never called, and
% a table that is not incremental.

:- table early/1 as incremental.

early(_) :- first(early), assertz(q(4)), fail.
early(X) :- q(X).

:- table unrelated/1 as incremental.

unrelated(X) :- q(X).
unrelated(_) :- assertz(other(1)), fail.

:- table plain/1.

plain(X) :- q(X).
plain(_) :- first(plain), assertz(q(3)), fail.

% Allowed too: retract/1 and clause/2 are not calls, as in SWI, so a
% table may look at a predicate that way and then change it.

:- table only_retract/1 as incremental.

only_retract(x) :- retract(q(3)), retract(q(4)).

:- table by_clause/1 as incremental.

by_clause(x) :- clause(q(1), true), assertz(q(6)).

t(Name, G) :-
	catch(( findall(G, G, L0), msort(L0, L), R = L ), E,
		( E = error(permission_error(A, B, C), _) -> unqualified(C, G1), functor(G1, N, Ar), R = caught(A, B, N/Ar) ; R = caught(E) )),
	findall(Q, q(Q), Qs),
	write(Name), write(': '), writeq(R), write(' q='), writeq(Qs), nl.

% SWI qualifies the goal, as user:direct(_).

unqualified(_:G, G) :- !.
unqualified(G, G).

main :-
	t(direct, direct(_)),
	t('direct again', direct(_)),
	t(retract, by_retract(_)),
	t(retractall, by_retractall(_)),
	assertz(q(9), Ref),
	assertz(erase_ref(Ref)),
	t(erase, by_erase(_)),
	erase(Ref),
	t(nested, outer(_)),
	t('nested again', outer(_)),
	t('through completed', top(_)),
	t('before the read', early(_)),
	t('never read', unrelated(_)),
	t('not incremental', plain(_)),
	t('only retract', only_retract(_)),
	t('by clause/2', by_clause(_)),
	findall(O, other(O), Os),
	write(other=Os), nl.

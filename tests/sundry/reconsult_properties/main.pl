% Reconsulting a file: its predicates report the properties a first consult would give, a predicate
% gone from the file reports none, and declarations survive as in Scryer (SWI drops discontiguous).
% A second file overwriting a static predicate mid-call must not free the clauses under the call.

:- initialization(main).

% In the working directory, since consult/1 would look beside this file.

tmp_file(Name, File) :-
	working_directory(D, D),
	format(atom(File), "~s~w", [D, Name]).

write_file(F, Text) :-
	setup_call_cleanup(open(F, write, S), format(S, "~s", [Text]), close(S)).

props(Tag, H) :-
	findall(P, (predicate_property(H, P), memberchk(P, [dynamic, static, discontiguous, multifile, meta_predicate(_)])), Ps),
	msort(Ps, Ss),
	write(Tag-Ss), nl.

reconsulted(F) :-
	write_file(F, ":- discontiguous(foo/1).\nfoo(1).\nbar(1).\n:- dynamic(baz/1).\nbaz(1).\n:- meta_predicate(mp(0)).\nmp(G) :- call(G).\n"),
	consult(F), props(foo, foo(_)), props(bar, bar(_)), props(baz, baz(_)), props(mp, mp(_)),
	consult(F), props(foo_again, foo(_)), props(bar_again, bar(_)), props(baz_again, baz(_)), props(mp_again, mp(_)),
	write_file(F, "foo(2).\nbaz(2).\nmp(G) :- call(G).\n"),
	consult(F), props(foo_edited, foo(_)), props(bar_gone, bar(_)), props(baz_edited, baz(_)), props(mp_edited, mp(_)).

while_enumerating(F) :-
	write_file(F, ":- dynamic(e/1).\n:- discontiguous(e/1).\ne(1).\n"),
	consult(F),
	findall(P, (predicate_property(e(_), P), memberchk(P, [dynamic, discontiguous]), consult(F)), _),
	props(enumerated, e(_)),
	findall(X, (e(X), consult(F)), Xs), write(inside-Xs), nl,
	props(inside, e(_)).

overwritten(F1, F2) :-
	write_file(F1, "s(1).\ns(2).\ns(3).\n"),
	write_file(F2, "s(9).\n"),
	consult(F1),
	findall(X, (s(X), (X == 1 -> consult(F2) ; true)), L1), write(during-L1), nl,
	findall(X, s(X), L2), write(after-L2), nl.

main :-
	tmp_file('reconsult_properties_tmp1.pl', F1),
	tmp_file('reconsult_properties_tmp2.pl', F2),
	reconsulted(F1),
	while_enumerating(F1),
	overwritten(F1, F2),
	delete_file(F1),
	delete_file(F2).

% ...@//0 is Scryer's ... //0 at the end only: a rest other than [] is a
% representation_error. Over phrase_from_stream/2 it leaves the rest of
% the stream unread, where ... //0 reads it all to find the end.

:- initialization(main).

:- use_module(library(pio)).
:- use_module(library(dcgs)).
:- use_module(library(dif)).
:- use_module(library(freeze)).

t(Name, G) :-
	catch(( G -> R = true ; R = false ), error(E, _), R = E),
	writeq(Name-R), nl.

tail_ok --> "x", ...@ .

probe(File, Opts, GRBody) :-
	open(File, read, S, Opts),
	(	phrase_from_stream(GRBody, S)
	->	(	at_end_of_stream(S) -> R = at_end ; R = not_at_end )
	;	R = failed
	),
	close(S),
	writeq(Opts-GRBody-R), nl.

main :-
	t(whole, phrase(...@, "abc")),
	t(empty, phrase(...@, [])),
	t(prefix, phrase(("a", ...@), "abc")),
	t(wrong_prefix, phrase(("b", ...@), "abc")),
	t(in_rule, phrase(tail_ok, "xyz")),
	t(open_rest, phrase(...@, "abc", _)),
	t(nonempty_rest, phrase(...@, "abc", "c")),
	t(not_last, phrase((...@, "c"), "abc")),
	t(improper, phrase(...@, [a|b])),
	t(partial, (L1 = [a,b|T1], phrase(...@, L1), T1 == [])),
	t(unbound, (phrase(...@, L2), length(L2, 2))),
	t(dif_tail, (dif(T3, []), phrase(...@, [a|T3]), T3 = [_])),
	t(frozen_tail, (freeze(T4, bb_put(woke, T4)), phrase(...@, [a|T4]), bb_get(woke, []))),
	t(det, (setup_call_cleanup(true, phrase(...@, "abc"), Det = true), Det == true)),
	nl,

	% Three chunks of 4096: ...@ stops after the first.
	File = 'tmp.enl',
	open(File, write, W), format(W, "~*c", [10000, 0'x]), close(W),
	probe(File, [], ("xx", ...@)),
	probe(File, [reposition(false)], ("xx", ...@)),
	probe(File, [], ("xx", ...)),
	probe(File, [reposition(false)], ("xx", ...)),
	probe(File, [], ("xy", ...@)),
	delete_file(File).

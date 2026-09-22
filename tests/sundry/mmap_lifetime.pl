:- initialization(main).

:- use_module(library(pio)).
:- use_module(library(dcgs)).
:- use_module(library(lists)).

:- dynamic(kept/1).
:- dynamic(shelved/1).

% open/4's mmap(Ls) hands over a slice: a pointer into the mapping with no
% refcount. close/1 does not unmap it - the query does, when the open/4 is
% backtracked over or when the query ends - so a term still holding one is
% good for the rest of the query. To outlive the query it has to own its
% characters, which is what assertz/1 and friends give it on the way in.

write_bytes([], _).
write_bytes([B|Bs], S) :- put_byte(S, B), write_bytes(Bs, S).

make_file(File, Bytes) :-
	open(File, write, S, [type(binary)]),
	write_bytes(Bytes, S),
	close(S).

codes_of([], []).
codes_of([C|Cs], [N|Ns]) :- char_code(C, N), codes_of(Cs, Ns).

show(Name, G) :-
	(	catch(call(G), E, (write(Name-threw(E)), nl, fail))
	->	true
	;	write(Name-failed), nl
	).

% A nonterminal that hands back the input as it stands, walking nothing.
rest(Cs, Cs0, []) :- Cs = Cs0.

% ... and one that hands back a tail of it.
tail(Cs, Cs0, []) :- Cs0 = [_|Cs].

% Each directive is its own query, so the mapping is gone by the next one.

:- make_file('tmp.mmlife', [0'a, 0'b, 0xc3, 0xa9]).

:- setup_call_cleanup(open('tmp.mmlife', read, S, [mmap(M)]),
	( assertz(kept(M)), bb_put(mmlife, M) ),
	close(S)).

:- kept(X), codes_of(X, Ns), write(asserted(Ns)), nl.
:- bb_get(mmlife, Y), codes_of(Y, Ns), write(blackboard(Ns)), nl.

% Stored as a term, not as a pointer: the two agree with a fresh read.
:- setup_call_cleanup(open('tmp.mmlife', read, S, [mmap(M)]),
	( kept(X), ( X == M -> assertz(shelved(same)) ; assertz(shelved(differs)) ) ),
	close(S)).
:- shelved(W), write(stored(W)), nl.

main :-
	File = 'tmp.mmlife',

	% used after the close, within the query
	show(after_close, (
		setup_call_cleanup(open(File, read, S, [mmap(M)]), Cs = M, close(S)),
		codes_of(Cs, Ns), write(after_close(Ns)), nl)),

	% the same through phrase_from_file/2, walking nothing
	show(rest, (phrase_from_file(rest(A), File), codes_of(A, An), write(rest(An)), nl)),
	show(tail, (phrase_from_file(tail(B), File), codes_of(B, Bn), write(tail(Bn)), nl)),

	% a slice derived by a builtin, used after the close
	show(sub, (
		setup_call_cleanup(open(File, read, S2, [mmap(M2)]), sub_string(M2, 0, 2, 1, Sub), close(S2)),
		codes_of(Sub, Sn), write(sub(Sn)), nl)),

	% backtracking over the open/4 is what releases it, and a cut does not
	% lose that: neither may take the answer with it
	show(once, (
		once(( open(File, read, S3, [mmap(M3)]), close(S3) )),
		codes_of(M3, On), write(once(On)), nl)),

	% findall/3 backtracks over the open/4 to collect, which is what
	% releases the mapping: its answers must own their characters by then
	show(findall, (
		findall(Cs, ( open(File, read, S4, [mmap(M4)]), close(S4), Cs = M4 ), [F]),
		codes_of(F, Fn), write(findall(Fn)), nl)),

	( catch(delete_file(File), _, true) -> true ; true ).

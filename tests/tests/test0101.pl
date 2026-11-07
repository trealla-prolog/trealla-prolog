:- initialization(main).

test1(0) :- !, statistics(frames, Fs), statistics(choices, Cs), statistics(trails, Ts), statistics(slots, Ss), write([f,Fs,c,Cs,t,Ts,s,Ss]), nl, fail.
test1(N) :- N1 is N-1, test1(N1).

f :- true, true, true.
test2(0) :- !, statistics(frames, Fs), statistics(choices, Cs), statistics(trails, Ts), statistics(slots, Ss), write([f,Fs,c,Cs,t,Ts,s,Ss]), nl, fail.
test2(N) :- f, N1 is N-1, test2(N1).

f(_) :- true, true, true.
test3(0) :- !, statistics(frames, Fs), statistics(choices, Cs), statistics(trails, Ts), statistics(slots, Ss), write([f,Fs,c,Cs,t,Ts,s,Ss]), nl, fail.
test3(N) :- f(_), N1 is N-1, test3(N1).

f(X, X) :- true, true, true.
test4(0) :- !, statistics(frames, Fs), statistics(choices, Cs), statistics(trails, Ts), statistics(slots, Ss), write([f,Fs,c,Cs,t,Ts,s,Ss]), nl, fail.
test4(N) :- f(_, _), N1 is N-1, test4(N1).

f(_, g(_), g(g(_))) :- true, true, true.
test5(0) :- !, statistics(frames, Fs), statistics(choices, Cs), statistics(trails, Ts), statistics(slots, Ss), write([f,Fs,c,Cs,t,Ts,s,Ss]), nl, fail.
test5(N) :- f(I, g(N), g(g(_))), N1 is N-1, test5(N1).

g(X, Y) :- Y is X + 1, true, true, true.
test6(0) :- !, statistics(frames, Fs), statistics(choices, Cs), statistics(trails, Ts), statistics(slots, Ss), write([f,Fs,c,Cs,t,Ts,s,Ss]), nl, fail.
test6(N) :- g(N, _), N1 is N-1, test6(N1).

main :-
	write(test1), write(': '), test1(100000);
	write(test2), write(': '), test2(100000);
	write(test3), write(': '), test3(100000);
	write(test4), write(': '), test4(100000);
	write(test5), write(': '), test5(100000);
	write(test6), write(': '), test6(100000);
	true.


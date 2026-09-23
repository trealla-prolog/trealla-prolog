% A task that finishes with a choicepoint left must not stop to ask the
% toplevel for more answers.

:- initialization(main).

main :-
	call_task((member(X, [a,b]), writeln(X))),
	call_task((member(Y, [1,2]), sleep(0.01), writeln(Y))),
	wait,
	writeln(waited).

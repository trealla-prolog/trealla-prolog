% help/1,2, listing/0,1, using/0 and statistics/0 write to current output, so
% with_output_to/2 captures them; abolish/2 names its own option domain.

:- initialization(main).

:- dynamic(zz/1).
zz(1).
zz(f(X,Y,X)).

main :-
	with_output_to(string(S1), help(atom_length/2)), format("~s", [S1]),
	with_output_to(string(S2), help(replace/4)), format("~s", [S2]),
	with_output_to(string(S3), listing(zz/1)), format("~s", [S3]),
	with_output_to(string(S4), listing), (sub_string(S4, _, _, _, "zz(1).") -> writeln(listing_ok) ; writeln(listing_missed)),
	with_output_to(string(S5), using), (sub_string(S5, 0, _, _, "% user --> [") -> writeln(using_ok) ; writeln(using_missed)),
	with_output_to(string(S6), statistics), (sub_string(S6, 0, _, _, "% ") -> writeln(statistics_ok) ; writeln(statistics_missed)),
	with_output_to(string(S7), help(atom_length)), (S7 \== "" -> writeln(help1_ok) ; writeln(help1_missed)),
	catch(abolish(foo/1, [bad(x)]), error(E, _), (writeq(E), nl)),
	catch(abolish(foo/1, [force(true)]), error(E2, _), (writeq(E2), nl)),
	writeln(done).

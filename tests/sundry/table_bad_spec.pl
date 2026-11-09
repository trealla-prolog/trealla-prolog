% A malformed ':- table' spec matched no clause, so the directive ran as an undefined table/1 goal and raised
% existence_error; with `as` its options were also recorded for '/'/2. Each part is now named in its error, as
% dynamic/1 names it, and the rest of the file loads. A bare name is Name/0, as in SWI.

:- initialization(main).

:- use_module(library(tabling)).

:- table tb_a/a.
:- table 1/1.
:- table tb_b/(-1).
:- table tb_c/1 as incremental, tb_d/x as incremental.
:- table 3.
:- table tb_zero.

tb_zero.

tb_last.

main :-
	( catch(tb_last, _, fail) -> writeq(rest_loaded) ; writeq(rest_lost) ), nl,
	( catch(tabling:'$tbl_incremental_spec'(/, 2), _, fail) -> writeq(slash_recorded) ; writeq(nothing_for_slash) ), nl,
	( predicate_property(tb_zero, tabled) -> writeq(bare_name_tabled) ; writeq(bare_name_not_tabled) ), nl,
	( tb_zero -> writeq(bare_name_runs) ; writeq(bare_name_fails) ), nl,
	forall(member(Spec, [tb_e/a, 1/1, tb_e/(-1), tb_e/_, 3, (tb_f/1, 3), tb_e//x]),
		(	catch(tabling:wrappers(Spec, _, []), error(E, _), true),
			writeq(E), nl
		)).

% library(sqlite3): an in-memory database queried, its connection and statement as typed handles, and a closed
% connection raising rather than reaching freed memory. Run by ../sqlite3_bindings.sh, which skips it where
% libsqlite3 or the FFI is missing.

:- use_module(library(sqlite3)).
:- initialization(main).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> format("~w: ~q~n", [Label, Show]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

% A handle error by its formal, type and context, as its culprit holds an address

handle_error(Goal, Shape) :-
	catch((Goal, Shape = no_error), error(E, Ctx), (E =.. [F, T|_], Shape = error(F, T, Ctx))).

main :-
	t(open, (sqlite3_open(':memory:', C, R1), foreign_handle(C, T1)), R1-T1),
	t(exec, (sqlite3_exec(C, 'create table t (a integer, b text); insert into t values (1, ''x''), (2, ''y'')', 0, 0, _, R2)), R2),
	t(query, findall(Row, sqlite3_query(C, 'select a, b from t order by a', Row, _), Rows), Rows),
	t(statement_type, (sqlite3_prepare_v2(C, 'select 1', -1, St, _, _), foreign_handle(St, T2), sqlite3_finalize(St, _)), T2),
	t(empty_sql, (sqlite3_prepare_v2(C, '', -1, St2, _, R3), (var(St2) -> S2 = none ; S2 = some)), R3-S2),
	t(finalize_twice, (sqlite3_prepare_v2(C, 'select 1', -1, St3, _, _), sqlite3_finalize(St3, _),
		handle_error(sqlite3_finalize(St3, _), H1)), H1),
	t(connection_as_statement, handle_error(sqlite3_step(C, _), H2), H2),
	t(close, sqlite3_close(C, R4), R4),
	t(closed_query, handle_error(sqlite3_query(C, 'select 1', _, _), H3), H3),
	t(close_twice, handle_error(sqlite3_close(C, _), H4), H4).

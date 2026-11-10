% library(postgres): connecting, typed rows, parameters with NULL and non-ASCII text, affected counts, errors
% with their SQLSTATE, and transactions committed and rolled back, all on a temporary table. Run by
% ../postgres_bindings.sh, which skips it where libpq, the FFI or a server is missing; the connection string
% is $PG_TEST_CONNINFO, libpq's defaults if unset. Strings are shown as atoms.

:- use_module(library(postgres)).
:- use_module(library(lists)).
:- initialization(main).

show(S, A) :- string(S), !, atom_string(A, S).
show(T, A) :- compound(T), !, T =.. [F|As], maplist(show, As, Bs), A =.. [F|Bs].
show(T, T).

% A server error by its SQLSTATE alone, as its message follows lc_messages

t(Label, Goal, Show) :-
	catch(t_(Label, Goal, Show), error(pg_error(Code, Msg), Ctx), (atom(Msg), format("~w: raised ~q~n", [Label, error(pg_error(Code, '...'), Ctx)]))).

t_(Label, Goal, Show) :-
	(   catch(Goal, E, (E = error(pg_error(_, _), _) -> throw(E) ; true))
	->  (var(E) -> show(Show, S), format("~w: ~q~n", [Label, S]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

rows(C, Sql, Rows) :- findall(R, pg_query(C, Sql, R), Rows).
rows(C, Sql, Ps, Rows) :- findall(R, pg_query(C, Sql, Ps, R), Rows).

main :-
	(getenv('PG_TEST_CONNINFO', Info) -> true ; Info = ''),
	pg_connect(Info, C),
	t(create, pg_query(C, 'create temp table people (id int primary key, name text, height float8, ok bool, big numeric, n bigint)', R1), R1),
	t(insert, pg_query(C, 'insert into people values ($1,$2,$3,$4,$5,$6), (2, $7, null, false, 1.5, -9)',
		[1, "Ann", 1.75, true, 123456789012345678901234567890, 42, 'Bö''b'], R2), R2),
	t(select, rows(C, 'select * from people order by id', Rs3), Rs3),
	t(params, rows(C, 'select $1::text, $2::int, $3::text, $4::int + 1', ['', '$null$', "x y", 41], Rs4), Rs4),
	t(specials, rows(C, 'select ''NaN''::float8, ''-Infinity''::float8, ''NaN''::numeric, 2.50::numeric', Rs5), Rs5),
	t(no_rows, rows(C, 'select id from people where id > 10', Rs6), Rs6),
	t(update, pg_query(C, 'update people set name = upper(name)', R7), R7),
	t(missing_table, pg_query(C, 'select * from nosuch', _), x),
	t(div_zero, pg_query(C, 'select 1/0', _), x),
	t(bad_param, pg_query(C, 'select $1::int', [f(x)], _), x),
	t(rolled_back, pg_transaction(C, (pg_query(C, 'delete from people', _), fail)), x),
	t(raised_back, catch(pg_transaction(C, (pg_query(C, 'delete from people', _), throw(oops))), oops, true), yes),
	t(kept, rows(C, 'select count(*) from people', Rs8), Rs8),
	t(committed, pg_transaction(C, pg_query(C, 'delete from people where id = 2', _)), yes),
	t(left, rows(C, 'select id, name from people', Rs9), Rs9),
	t(empty_query, pg_query(C, '', R10), R10),
	pg_disconnect(C),
	t(refused, pg_connect('host=127.0.0.1 port=1 connect_timeout=2', _), x).

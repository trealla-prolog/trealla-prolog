% library(duckdb): in-memory and file databases, each column type read back (integers, HUGEINT, floats, DECIMAL
% exact and as floats, text, BLOB, dates, times, timestamps, UUID, ENUM, nested lists, NULL), results longer
% than one chunk, parameters, CSV files, affected counts, transactions committed and rolled back, one with a
% caught error not committing, and errors, messages shown only by kind as they vary by version. Run by
% ../duckdb_bindings.sh, which skips it where libduckdb or the FFI is missing. Strings are shown as atoms.
% Temporary files stay in '.'.

:- use_module(library(duckdb)).
:- use_module(library(lists)).
:- initialization(main).

show(S, A) :- string(S), !, atom_string(A, S).
show(T, A) :- compound(T), !, T =.. [F|As], maplist(show, As, Bs), A =.. [F|Bs].
show(T, T).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E) -> show(Show, S), format("~w: ~q~n", [Label, S])
	    ;   E = error(duckdb_error(_), Ctx) -> format("~w: raised ~q~n", [Label, error(duckdb_error('...'), Ctx)])
	    ;   format("~w: raised ~q~n", [Label, E])
	    )
	;   format("~w: failed~n", [Label])
	).

rows(DB, Sql, Rows) :- findall(R, duck_query(DB, Sql, R), Rows).
rows(DB, Sql, Ps, Rows) :- findall(R, duck_query(DB, Sql, Ps, R), Rows).

main :-
	duck_open(':memory:', DB),
	t(create, duck_query(DB, 'create table t (i int, b bigint, s varchar, f double, ok bool, d decimal(10,2), big decimal(30,5), h hugeint)', R1), R1),
	t(insert, duck_query(DB, 'insert into t values (1, 9000000000, ''hello world, past twelve bytes'', 1.5, true, 12.34, 123456789012345678901234.56789, 170141183460469231731687303715884105727), (2, null, ''é ☃'', null, false, -0.05, -1.5, -5)', R2), R2),
	t(select, rows(DB, 'select * from t order by i', Rs3), Rs3),
	t(small_ints, rows(DB, 'select (-128)::tinyint, 255::utinyint, (-32768)::smallint, 65535::usmallint, 4294967295::uinteger, 18446744073709551615::ubigint, 2.5::float', Rs4), Rs4),
	t(dates, rows(DB, 'select date ''2024-02-29'', date ''1900-01-01'', time ''13:45:30.25'', timestamp ''1969-12-31 23:59:59.5'', timestamp_s ''2000-01-01 00:00:01'', timestamp_ms ''2000-01-01 00:00:00.123''', Rs5), Rs5),
	t(uuid_enum, (duck_query(DB, 'create type mood as enum (''sad'', ''ok'', ''happy'')', _), rows(DB, 'select uuid ''550e8400-e29b-41d4-a716-446655440000'', ''happy''::mood', Rs6)), Rs6),
	t(lists_blobs, rows(DB, 'select [1, 2, null, 3], [[''a''], []], [], ''''::blob, ''ab\\x00''::blob, null', Rs7), Rs7),
	t(chunks, (rows(DB, 'select range from range(5000)', Rs8), length(Rs8, N8), last(Rs8, L8)), N8/L8),
	t(params, rows(DB, 'select $1 + 1, $2, $3 is null, $4 * 2', [41, "x y", '$null$', 1.25], Rs9), Rs9),
	t(param_count, duck_query(DB, 'select $1', [1, 2], _), x),
	t(param_type, duck_query(DB, 'select $1', [f(x)], _), x),
	F = 'tmp_duckdb_test.csv',
	setup_call_cleanup(open(F, write, W), format(W, "region,amount~nnorth,100.5~nsouth,20~nnorth,1100~n", []), close(W)),
	t(csv, rows(DB, 'select region, sum(amount) from ''tmp_duckdb_test.csv'' group by 1 order by 1', Rs10), Rs10),
	delete_file(F),
	t(update, duck_query(DB, 'update t set s = upper(s)', R11), R11),
	duck_query(DB, 'create table k (id int primary key)', _),
	duck_query(DB, 'insert into k values (1)', _),
	t(committed, duck_transaction(DB, duck_query(DB, 'insert into k values (2)', _)), yes),
	t(rolled_back, duck_transaction(DB, (duck_query(DB, 'insert into k values (3)', _), fail)), x),
	t(raised_back, catch(duck_transaction(DB, (duck_query(DB, 'insert into k values (4)', _), throw(oops))), oops, true), yes),
	t(caught_in_transaction, duck_transaction(DB, (duck_query(DB, 'insert into k values (5)', _),
		catch(duck_query(DB, 'insert into k values (1)', _), error(duckdb_error(_), _), true))), x),
	t(kept, rows(DB, 'select id from k order by id', Rs12), Rs12),
	t(syntax, duck_query(DB, 'selec 1', _), x),
	t(unsupported, duck_query(DB, 'select {''a'': 1}', _), x),
	t(cast, rows(DB, 'select {''a'': 1}::varchar', Rs13), Rs13),
	duck_close(DB),
	Db = 'tmp_duckdb_test.db',
	catch(delete_file(Db), _, true),
	t(file_db, (duck_open(Db, D1), duck_query(D1, 'create table p as select 7 as x', _), duck_close(D1),
		duck_open(Db, D2), rows(D2, 'select x from p', Rs14), duck_close(D2)), Rs14),
	catch(delete_file(Db), _, true),
	catch(delete_file('tmp_duckdb_test.db.wal'), _, true),
	t(bad_open, duck_open('no_such_dir/x/y.db', _), x),
	t(bad_handle, duck_close(foo), x),
	t(closed_query, (duck_open(':memory:', D3), duck_close(D3), handle_error(duck_query(D3, 'select 1', _), H1)), H1),
	t(close_twice, (duck_open(':memory:', D4), duck_close(D4), handle_error(duck_close(D4), H2)), H2).

% A handle error by its formal, type and context, as its culprit holds an address

handle_error(Goal, Shape) :-
	catch((Goal, Shape = no_error), error(E, Ctx), (E =.. [F, T|_], Shape = error(F, T, Ctx))).

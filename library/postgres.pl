:- module(postgres, [
	pg_connect/2,
	pg_disconnect/1,
	pg_query/3,
	pg_query/4,
	pg_transaction/2,
	pg_server_version/2,
	pg_lib_version/1,

	'PQconnectdb'/2,
	'PQfinish'/1,
	'PQstatus'/2,
	'PQerrorMessage'/2,
	'PQserverVersion'/2,
	'PQlibVersion'/1,
	'PQdb'/2,
	'PQuser'/2,
	'PQhost'/2,
	'PQport'/2,
	'PQexec'/3,
	'PQexecParams'/9,
	'PQresultStatus'/2,
	'PQresStatus'/2,
	'PQresultErrorMessage'/2,
	'PQresultErrorField'/3,
	'PQntuples'/2,
	'PQnfields'/2,
	'PQfname'/3,
	'PQftype'/3,
	'PQgetvalue'/4,
	'PQgetlength'/4,
	'PQgetisnull'/4,
	'PQcmdTuples'/2,
	'PQclear'/1
	]).

% PostgreSQL client over libpq (https://www.postgresql.org/docs/current/libpq.html)
%
% UBUNTU: sudo apt install libpq-dev
% MACOS: brew install libpq
%
%	?- pg_connect('host=localhost dbname=test', C),
%		pg_query(C, 'select id, name from people where id > $1', [10], Row).
%	Row = row(11, "Ann") ;
%	...
%
% A query that returns rows gives them as row(V1, ..., Vn) on backtracking, all fetched first;
% any other statement gives affected(N), the rows it changed. Integers, floats, numerics and
% booleans come back as integers, floats and true/false, NULL as '$null$', and everything else
% as strings, as do NaN and Infinity, which have no float here. Parameters $1, $2, ... are
% atoms, strings, numbers or '$null$', sent apart from the SQL so they need no quoting. Errors
% are error(pg_error(SQLSTATE, Message), _), both atoms, with connection for SQLSTATE when
% there's none.
%
% The 'PQ*' predicates are the C functions.

:- use_module(library(lists)).

:- use_foreign_module('libpq.so', [
	'PQconnectdb'([cstr], ptr),
	'PQfinish'([ptr], void),
	'PQstatus'([ptr], sint),
	'PQerrorMessage'([ptr], ccstr),
	'PQserverVersion'([ptr], sint),
	'PQlibVersion'([], sint),
	'PQdb'([ptr], ccstr),
	'PQuser'([ptr], ccstr),
	'PQhost'([ptr], ccstr),
	'PQport'([ptr], ccstr),
	'PQexec'([ptr,cstr], ptr),
	'PQexecParams'([ptr,cstr,sint,ptr,ptr,ptr,ptr,sint], ptr),
	'PQresultStatus'([ptr], sint),
	'PQresStatus'([sint], ccstr),
	'PQresultErrorMessage'([ptr], ccstr),
	'PQresultErrorField'([ptr,sint], ccstr),
	'PQntuples'([ptr], sint),
	'PQnfields'([ptr], sint),
	'PQfname'([ptr,sint], ccstr),
	'PQftype'([ptr,sint], uint),
	'PQgetvalue'([ptr,sint,sint], ptr),
	'PQgetlength'([ptr,sint,sint], sint),
	'PQgetisnull'([ptr,sint,sint], sint),
	'PQcmdTuples'([ptr], ccstr),
	'PQclear'([ptr], void)
	]).

:- meta_predicate(pg_transaction(+, 0)).

% Conn, a connection by a libpq connection string or URI, as 'host=h port=5432 dbname=d user=u'
% or 'postgresql://u@h/d'

pg_connect(Info, Conn) :-
	must_be_(text, Info, pg_connect/2),
	text_atom_(Info, A),
	'PQconnectdb'(A, C),
	(	C =:= 0
	->	throw(error(resource_error(memory), pg_connect/2))
	;	true
	),
	(	'PQstatus'(C, 0)						% CONNECTION_OK
	->	Conn = C
	;	'PQerrorMessage'(C, Msg0),
		'PQfinish'(C),
		trim_(Msg0, Msg),
		throw(error(pg_error(connection, Msg), pg_connect/2))
	).

pg_disconnect(Conn) :-
	must_be_(integer, Conn, pg_disconnect/1),
	'PQfinish'(Conn).

% Each row(...) SQL returns on backtracking, or affected(N) for a statement that returns none

pg_query(Conn, Sql, Row) :-
	pg_query_(Conn, Sql, [], Row, pg_query/3).

pg_query(Conn, Sql, Params, Row) :-
	pg_query_(Conn, Sql, Params, Row, pg_query/4).

pg_query_(Conn, Sql, Params, Row, Ctx) :-
	must_be_(integer, Conn, Ctx),
	must_be_(text, Sql, Ctx),
	must_be_(list, Params, Ctx),
	text_atom_(Sql, S),
	maplist(param_(Ctx), Params, Ps),
	length(Ps, N),
	setup_call_cleanup(
		'$array_to_pointer'(cstr, Ps, Vs),
		'PQexecParams'(Conn, S, N, 0, Vs, 0, 0, 0, Res),
		'$free_struct_pointer'(Vs)),
	(	Res =:= 0
	->	'PQerrorMessage'(Conn, Msg0),
		trim_(Msg0, Msg),
		throw(error(pg_error(connection, Msg), Ctx))
	;	true
	),
	setup_call_cleanup(
		true,
		result_(Res, Rows, Ctx),
		'PQclear'(Res)),
	member(Row, Rows).

param_(_, '$null$', 0) :- !.
param_(_, P, P) :- atom(P), !.
param_(_, P, P) :- string(P), !.
param_(_, P, A) :- number(P), !, number_codes(P, Cs), atom_codes(A, Cs).
param_(Ctx, P, _) :- var(P), !, throw(error(instantiation_error, Ctx)).
param_(_, P, S) :- is_list(P), catch(format(string(S), "~s", [P]), _, fail), !.
param_(Ctx, P, _) :- throw(error(type_error(text, P), Ctx)).

result_(Res, Rows, Ctx) :-
	'PQresultStatus'(Res, St),
	result_(St, Res, Rows, Ctx).

result_(1, Res, [affected(N)], _) :- !,		% PGRES_COMMAND_OK
	'PQcmdTuples'(Res, T),
	(	T == '' -> N = 0
	;	atom_number(T, N)
	).
result_(2, Res, Rows, _) :- !,				% PGRES_TUPLES_OK
	'PQntuples'(Res, NR),
	'PQnfields'(Res, NF),
	NF1 is NF - 1,
	numlist_(0, NF1, Cols),
	maplist(col_type_(Res), Cols, Types),
	NR1 is NR - 1,
	numlist_(0, NR1, Is),
	maplist(row_(Res, Cols, Types), Is, Rows).
result_(0, _, [affected(0)], _) :- !.		% PGRES_EMPTY_QUERY
result_(St, Res, _, Ctx) :-
	error_field_(Res, 0'C, Code),
	(	error_field_(Res, 0'M, Msg) -> true
	;	'PQresultErrorMessage'(Res, Msg0),
		(	Msg0 == '' -> 'PQresStatus'(St, Msg1) ; Msg1 = Msg0 ),
		trim_(Msg1, Msg)
	),
	throw(error(pg_error(Code, Msg), Ctx)).

% A diagnostic field of Res as an atom; 'PQresultErrorField' fails on the NULL it gives for one that
% isn't there

error_field_(Res, F, V) :-
	'PQresultErrorField'(Res, F, A),
	!,
	trim_(A, V).
error_field_(_, 0'C, unknown).

numlist_(L, H, []) :- L > H, !.
numlist_(L, H, [L|T]) :- L1 is L + 1, numlist_(L1, H, T).

col_type_(Res, I, Type) :-
	'PQftype'(Res, I, Oid),
	oid_type_(Oid, Type).

oid_type_(16, bool) :- !.
oid_type_(20, integer) :- !.
oid_type_(21, integer) :- !.
oid_type_(23, integer) :- !.
oid_type_(26, integer) :- !.
oid_type_(700, float) :- !.
oid_type_(701, float) :- !.
oid_type_(1700, numeric) :- !.
oid_type_(_, text).

row_(Res, Cols, Types, I, Row) :-
	maplist(value_(Res, I), Cols, Types, Vs),
	Row =.. [row|Vs].

value_(Res, I, J, Type, V) :-
	(	'PQgetisnull'(Res, I, J, 1)
	->	V = '$null$'
	;	'PQgetvalue'(Res, I, J, P),
		'PQgetlength'(Res, I, J, Len),
		'$pointer_to_text'(P, Len, S),
		convert_(Type, S, V)
	).

convert_(text, S, S).
convert_(integer, S, V) :- number_chars(V, S).
convert_(bool, S, V) :- ( S == "t" -> V = true ; V = false ).
convert_(float, S, V) :- ( catch(number_chars(N, S), _, fail) -> V is float(N) ; V = S ).
convert_(numeric, S, V) :- ( catch(number_chars(N, S), _, fail) -> V = N ; V = S ).

% Goal once inside BEGIN and COMMIT, rolled back if it fails or raises

pg_transaction(Conn, Goal) :-
	pg_query(Conn, 'BEGIN', _),
	(	catch(Goal, E, true)
	->	(	var(E)
		->	pg_query(Conn, 'COMMIT', _)
		;	catch(pg_query(Conn, 'ROLLBACK', _), _, true),
			throw(E)
		)
	;	pg_query(Conn, 'ROLLBACK', _),
		fail
	).

% The server's version as Major.Minor, from its integer 180001 as 18-1

pg_server_version(Conn, Major-Minor) :-
	'PQserverVersion'(Conn, V),
	V > 0,
	version_(V, Major, Minor).

pg_lib_version(Major-Minor) :-
	'PQlibVersion'(V),
	version_(V, Major, Minor).

version_(V, Major, Minor) :-
	(	V >= 100000
	->	Major is V // 10000,
		Minor is V mod 10000
	;	Major is V // 100,
		Minor is V mod 100
	).

trim_(A0, A) :-
	atom_chars(A0, Cs0),
	reverse(Cs0, R0),
	drop_ws_(R0, R),
	reverse(R, Cs),
	atom_chars(A, Cs).

drop_ws_([C|Cs], R) :- memberchk(C, [' ', '\n', '\t', '\r']), !, drop_ws_(Cs, R).
drop_ws_(Cs, Cs).

text_atom_(T, A) :- atom(T), !, A = T.
text_atom_(T, A) :- atom_chars(A, T).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(integer, X, Ctx) :- !, (integer(X) -> true ; throw(error(type_error(integer, X), Ctx))).
must_be_(list, X, Ctx) :- !, (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).
must_be_(text, X, Ctx) :- ((atom(X) ; string(X)) -> true ; throw(error(type_error(text, X), Ctx))).

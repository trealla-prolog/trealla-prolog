:- module(sqlite3, [
	sqlite3_query/4,
	sqlite_flag/2
	]).

:- use_foreign_module('libsqlite3.so', [
	sqlite3_open([cstr,-ptr], sint),
	sqlite3_exec([ptr,cstr,ptr,ptr,-ptr], sint),
	sqlite3_prepare_v2([ptr,cstr,ptr,-ptr,-ccstr], sint),
	sqlite3_step([ptr], sint),
	sqlite3_finalize([ptr], sint),
	sqlite3_column_count([ptr], sint),
	sqlite3_column_name([ptr,sint], ccstr),
	sqlite3_column_type([ptr,sint], sint),
	sqlite3_column_int64([ptr,sint], sint),
	sqlite3_column_double([ptr,sint], double),
	sqlite3_column_text([ptr,sint], ccstr),
	sqlite3_close([ptr], sint)
	]).

:- help(sqlite3_open(+atom,--stream,-integer),[iso(false),desc('Open an Sqlite3 database returning a connection (as a stream).')]).
:- help(sqlite3_exec(+stream,+atom,+integer,+integer,-integer,-integer),[iso(false),desc('Execute an SQL statement on an Sqlite3 database connection')]).
:- help(sqlite3_prepare_v2(+stream,+atom,+integer,-integer,-integer,-integer),[iso(false)]).
:- help(sqlite3_step(+stream,-integer),[iso(false)]).
:- help(sqlite3_finalize(+stream,-integer),[iso(false)]).
:- help(sqlite3_column_count(+stream,-integer),[iso(false)]).
:- help(sqlite3_column_name(+stream,+integer,-atom),[iso(false)]).
:- help(sqlite3_column_type(+stream,+integer,-integer),[iso(false)]).
:- help(sqlite3_column_int64(+stream,+integer,-integer),[iso(false)]).
:- help(sqlite3_column_double(+stream,+integer,-float),[iso(false)]).
:- help(sqlite3_column_text(+stream,+integer,-string),[iso(false)]).
:- help(sqlite3_close(+stream,-integer),[iso(false),desc('Close an Sqlite3 database connection.')]).

sqlite_flag('SQLITE_OK', 0).
sqlite_flag('SQLITE_ERROR', 1).
sqlite_flag('SQLITE_ABORT', 4).
sqlite_flag('SQLITE_BUSY', 5).
sqlite_flag('SQLITE_LOCKED', 6).
sqlite_flag('SQLITE_READONLY', 8).
sqlite_flag('SQLITE_FULL', 13).
sqlite_flag('SQLITE_ROW', 100).
sqlite_flag('SQLITE_DONE', 101).

sqlite_flag('SQLITE_INTEGER', 1).
sqlite_flag('SQLITE_FLOAT', 2).
sqlite_flag('SQLITE_TEXT', 3).
sqlite_flag('SQLITE_BLOB', 4).
sqlite_flag('SQLITE_NULL', 5).

:- help(sqlite_flag(+atom,-integer),[iso(false)]).
% WARNING: This is experimental!

sqlite3_query(Conn, Sql, Cols, Names) :-
	sqlite_flag('SQLITE_OK', SQLITE_OK),
	sqlite_flag('SQLITE_ROW', SQLITE_ROW),
	sqlite3_prepare_v2(Conn, Sql, -1, Statement, _, Ret), Ret =:= SQLITE_OK,
	repeat,
		(	( sqlite3_step(Statement, Ret2), Ret2 =:= SQLITE_ROW ) -> get_row(Statement, Cols, Names)
		;	( !, sqlite3_finalize(Statement, _Ret), fail )
		).

:- help(sqlite3_query(+stream,+string,-list,-list),[iso(false),desc('Query an Sqlite3 database connection and return a row as a list of column values and a list of column names. Bactracking may return more rows.')]).

get_col(_, 0, Cols, Cols, Names, Names) :- !.
get_col(Statement, Count, Col, Cols, Name, Names) :-
	Count > 0,			% or a caller's own list that doesn't match walks off column 0
	sqlite_flag('SQLITE_INTEGER', SQLITE_INTEGER),
	sqlite_flag('SQLITE_FLOAT', SQLITE_FLOAT),
	sqlite_flag('SQLITE_TEXT', SQLITE_TEXT),
	sqlite_flag('SQLITE_BLOB', SQLITE_BLOB),
	Count1 is Count - 1,
	sqlite3_column_name(Statement, Count1, NewName),
	sqlite3_column_type(Statement, Count1, Type),
	(	Type =:= SQLITE_INTEGER -> sqlite3_column_int64(Statement, Count1, Value)
	;	Type =:= SQLITE_FLOAT -> sqlite3_column_double(Statement, Count1, Value)
	;	Type =:= SQLITE_TEXT -> sqlite3_column_text(Statement, Count1, Value)
	;	Type =:= SQLITE_BLOB -> sqlite3_column_text(Statement, Count1, Value)
	;	Value = '(void)'
	),
	get_col(Statement, Count1, [Value|Col], Cols, [NewName|Name], Names).

get_row(Statement, Cols, Names) :-
	sqlite3_column_count(Statement, Count),
	get_col(Statement, Count, [], Cols, [], Names).

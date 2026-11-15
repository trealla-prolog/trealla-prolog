:- module(duckdb, [
	duck_open/2,
	duck_close/1,
	duck_query/3,
	duck_query/4,
	duck_transaction/2,
	duck_version/1
	]).

% DuckDB, the embedded analytical SQL database (https://duckdb.org)
%
% MACOS: brew install duckdb
% UBUNTU: libduckdb.so from https://duckdb.org/docs/installation
%
%	?- duck_open(':memory:', DB),
%		duck_query(DB, 'select region, sum(amount) from \'sales.csv\' group by 1', Row).
%	Row = row("north", 1200.5) ;
%	...
%
% duck_open/2 opens a database file, made if it isn't there, or ':memory:'. A query that returns
% rows gives them as row(V1, ..., Vn) on backtracking, all fetched first; any other statement
% gives affected(N). Parameters $1, $2 ... or ? are atoms, strings, numbers or '$null$', bound
% apart from the SQL. NULL is '$null$'. Values come back as:
%
%	BOOLEAN					true or false
%	integers, HUGEINT		integers
%	FLOAT, DOUBLE			floats
%	DECIMAL					an integer at scale 0, a float to 15 digits, else a string
%	VARCHAR, ENUM, UUID		strings
%	BLOB					a list of bytes
%	DATE					date(Y, M, D)
%	TIME					time(H, M, S)
%	TIMESTAMP and its kinds	timestamp(Y, M, D, H, Mn, S), in UTC; S a float when fractional
%	LIST					a list
%
% Other types, such as STRUCT, MAP and INTERVAL, raise a domain error: cast them in the SQL, as
% x::VARCHAR. Errors are error(duckdb_error(Message), _).

:- use_module(library(lists)).

:- foreign_struct(duck_result, [uint64, uint64, uint64, ptr, ptr, ptr]).

:- use_foreign_module('libduckdb.so', [
	duckdb_library_version([], ccstr),
	duckdb_open_ext([cstr, -ptr, ptr, -ptr], sint),
	duckdb_close([ptr], void),
	duckdb_connect([ptr, -ptr], sint),
	duckdb_disconnect([ptr], void),
	duckdb_free([ptr], void),
	duckdb_query([ptr, cstr, ptr], sint),
	duckdb_destroy_result([ptr], void),
	duckdb_result_error([ptr], ccstr),
	duckdb_result_return_type([duck_result], sint),
	duckdb_rows_changed([ptr], uint64),
	duckdb_column_count([ptr], uint64),
	duckdb_column_logical_type([ptr, uint64], ptr),
	duckdb_destroy_logical_type([ptr], void),
	duckdb_get_type_id([ptr], sint),
	duckdb_decimal_width([ptr], uint8),
	duckdb_decimal_scale([ptr], uint8),
	duckdb_decimal_internal_type([ptr], sint),
	duckdb_enum_internal_type([ptr], sint),
	duckdb_enum_dictionary_size([ptr], uint32),
	duckdb_enum_dictionary_value([ptr, uint64], cstr),
	duckdb_list_type_child_type([ptr], ptr),
	duckdb_fetch_chunk([duck_result], ptr),
	duckdb_data_chunk_get_size([ptr], uint64),
	duckdb_data_chunk_get_vector([ptr, uint64], ptr),
	duckdb_destroy_data_chunk([ptr], void),
	duckdb_vector_get_data([ptr], ptr),
	duckdb_vector_get_validity([ptr], ptr),
	duckdb_list_vector_get_child([ptr], ptr),
	duckdb_list_vector_get_size([ptr], uint64),
	duckdb_string_t_data([ptr], ptr),
	duckdb_prepare([ptr, cstr, -ptr], sint),
	duckdb_prepare_error([ptr], ccstr),
	duckdb_destroy_prepare([ptr], void),
	duckdb_nparams([ptr], uint64),
	duckdb_bind_null([ptr, uint64], sint),
	duckdb_bind_int64([ptr, uint64, sint64], sint),
	duckdb_bind_double([ptr, uint64, double], sint),
	duckdb_bind_varchar_length([ptr, uint64, ptr, uint64], sint),
	duckdb_execute_prepared([ptr, ptr], sint)
	]).

:- meta_predicate(duck_transaction(+, 0)).

duck_version(V) :-
	duckdb_library_version(A),
	atom_string(A, V).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Databases

% DB, duckdb(Database, Connection), for a file or ':memory:'

duck_open(Path, duckdb(D, C)) :-
	Ctx = duck_open/2,
	text_atom_(Path, P, Ctx),
	duckdb_open_ext(P, D, 0, Err, Rc),
	(	Rc =:= 0 -> true
	;	error_text_(Err, Msg),
		throw(error(duckdb_error(Msg), Ctx))
	),
	duckdb_connect(D, C, Rc2),
	(	Rc2 =:= 0 -> true
	;	by_handle_(duckdb_close, D),
		throw(error(duckdb_error('cannot connect'), Ctx))
	).

error_text_(0, 'cannot open') :- !.
error_text_(P, Msg) :-
	text_at_(P, Msg),
	duckdb_free(P).

% A NUL-terminated C string at P, which DuckDB still owns

text_at_(P, A) :-
	'$array_to_pointer'(ptr, [P], B),
	setup_call_cleanup(true, '$pointer_to_struct'(duck_cstr, B, [_, A]), '$free_struct_pointer'(B)).

:- foreign_struct(duck_cstr, [ccstr]).

duck_close(duckdb(D, C)) :-
	!,
	by_handle_(duckdb_disconnect, C),
	by_handle_(duckdb_close, D).
duck_close(DB) :-
	throw(error(type_error(duckdb, DB), duck_close/1)).

% DuckDB's destructors take the handle's address, and clear it

by_handle_(F, H) :-
	setup_call_cleanup('$array_to_pointer'(ptr, [H], B), call(F, B), '$free_struct_pointer'(B)).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Queries

duck_query(DB, Sql, Row) :-
	query_(DB, Sql, none, Row, duck_query/3).

duck_query(DB, Sql, Params, Row) :-
	must_be_(list, Params, duck_query/4),
	query_(DB, Sql, Params, Row, duck_query/4).

query_(DB, Sql, Params, Row, Ctx) :-
	(	nonvar(DB), DB = duckdb(_, C) -> true ; throw(error(type_error(duckdb, DB), Ctx)) ),
	text_atom_(Sql, S, Ctx),
	setup_call_cleanup(
		'$array_to_pointer'(uint64, [0,0,0,0,0,0,0,0], R),
		(	run_(Params, C, S, R, Ctx),
			rows_(R, Rows, Ctx)
		),
		(	duckdb_destroy_result(R), '$free_struct_pointer'(R) )),
	member(Row, Rows).

run_(none, C, S, R, Ctx) :- !,
	duckdb_query(C, S, R, Rc),
	result_ok_(Rc, R, Ctx).
run_(Params, C, S, R, Ctx) :-
	duckdb_prepare(C, S, St, Rc),
	setup_call_cleanup(true,
		(	(	Rc =:= 0 -> true
			;	(duckdb_prepare_error(St, Msg) -> true ; Msg = 'cannot prepare'),
				throw(error(duckdb_error(Msg), Ctx))
			),
			length(Params, N),
			duckdb_nparams(St, NP),
			(	N =:= NP -> true
			;	throw(error(domain_error(parameter_count(NP), Params), Ctx))
			),
			bind_(Params, 1, St, Ctx),
			duckdb_execute_prepared(St, R, Rc2),
			result_ok_(Rc2, R, Ctx)
		),
		by_handle_(duckdb_destroy_prepare, St)).

result_ok_(0, _, _) :- !.
result_ok_(_, R, Ctx) :-
	(	duckdb_result_error(R, Msg) -> true ; Msg = failed ),
	throw(error(duckdb_error(Msg), Ctx)).

bind_([], _, _, _).
bind_([P|Ps], I, St, Ctx) :-
	bind_one_(P, I, St, Rc, Ctx),
	(	Rc =:= 0 -> true
	;	(duckdb_prepare_error(St, Msg) -> true ; Msg = 'cannot bind'),
		throw(error(duckdb_error(Msg), Ctx))
	),
	I1 is I + 1,
	bind_(Ps, I1, St, Ctx).

bind_one_(P, _, _, _, Ctx) :- var(P), !, throw(error(instantiation_error, Ctx)).
bind_one_('$null$', I, St, Rc, _) :- !, duckdb_bind_null(St, I, Rc).
bind_one_(P, I, St, Rc, _) :- integer(P), P >= -(1<<63), P < 1<<63, !, duckdb_bind_int64(St, I, P, Rc).
bind_one_(P, I, St, Rc, _) :- float(P), !, duckdb_bind_double(St, I, P, Rc).
bind_one_(P, I, St, Rc, _) :- number(P), !, number_codes(P, Cs), atom_codes(A, Cs), bind_text_(A, I, St, Rc).
bind_one_(P, I, St, Rc, _) :- (atom(P) ; string(P)), !, bind_text_(P, I, St, Rc).
bind_one_(P, _, _, _, Ctx) :- throw(error(type_error(text, P), Ctx)).

bind_text_(T, I, St, Rc) :-
	setup_call_cleanup(
		'$text_to_pointer'(T, P, Len),
		duckdb_bind_varchar_length(St, I, P, Len, Rc),
		'$free_struct_pointer'(P)).

% A query's rows, or affected(N) for a statement that returns none

rows_(R, Rows, Ctx) :-
	'$pointer_to_struct'(duck_result, R, Res),
	duckdb_result_return_type(Res, Type),
	(	Type =:= 3
	->	duckdb_column_count(R, NC),
		NC1 is NC - 1,
		findall(I, between(0, NC1, I), Cols),
		maplist(column_type_(R, Ctx), Cols, Types),
		chunks_(Res, Types, Rows)
	;	Type =:= 1
	->	duckdb_rows_changed(R, N),
		Rows = [affected(N)]
	;	Rows = [affected(0)]
	).

chunks_(Res, Types, Rows) :-
	duckdb_fetch_chunk(Res, Ch),
	(	Ch =:= 0 -> Rows = []
	;	setup_call_cleanup(true, chunk_rows_(Ch, Types, Rows, Rest), by_handle_(duckdb_destroy_data_chunk, Ch)),
		chunks_(Res, Types, Rest)
	).

chunk_rows_(Ch, Types, Rows, Rest) :-
	duckdb_data_chunk_get_size(Ch, N),
	length(Types, NC),
	NC1 is NC - 1,
	findall(Vs, (between(0, NC1, I), nth0(I, Types, T), duckdb_data_chunk_get_vector(Ch, I, V), values_(T, V, N, Vs)), Cols),
	rows_of_(Cols, N, Rows, Rest).

% Rows from columns, each N long, ahead of Rest

rows_of_(_, 0, Rest, Rest) :- !.
rows_of_(Cols, N, [Row|Rows], Rest) :-
	heads_tails_(Cols, Hs, Ts),
	Row =.. [row|Hs],
	N1 is N - 1,
	rows_of_(Ts, N1, Rows, Rest).

heads_tails_([], [], []).
heads_tails_([[H|T]|Cs], [H|Hs], [T|Ts]) :- heads_tails_(Cs, Hs, Ts).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Types

% How a column's values are read, from its logical type

column_type_(R, Ctx, I, T) :-
	duckdb_column_logical_type(R, I, LT),
	setup_call_cleanup(true, type_(LT, T, Ctx), by_handle_(duckdb_destroy_logical_type, LT)).

type_(LT, T, Ctx) :-
	duckdb_get_type_id(LT, Id),
	type_(Id, LT, T, Ctx).

type_(1, _, bool, _) :- !.
type_(2, _, int(sint8), _) :- !.
type_(3, _, int(sint16), _) :- !.
type_(4, _, int(sint32), _) :- !.
type_(5, _, int(sint64), _) :- !.
type_(6, _, int(uint8), _) :- !.
type_(7, _, int(uint16), _) :- !.
type_(8, _, int(uint32), _) :- !.
type_(9, _, int(uint64), _) :- !.
type_(10, _, real(float), _) :- !.
type_(11, _, real(double), _) :- !.
type_(12, _, timestamp(86400000000), _) :- !.
type_(13, _, date, _) :- !.
type_(14, _, time, _) :- !.
type_(16, _, hugeint(signed), _) :- !.
type_(17, _, varchar, _) :- !.
type_(18, _, blob, _) :- !.
type_(19, LT, decimal(W, S, Int), _) :- !,
	duckdb_decimal_width(LT, W),
	duckdb_decimal_scale(LT, S),
	duckdb_decimal_internal_type(LT, Id),
	internal_(Id, Int).
type_(20, _, timestamp(86400), _) :- !.
type_(21, _, timestamp(86400000), _) :- !.
type_(22, _, timestamp(86400000000000), _) :- !.
type_(23, LT, enum(Int, Dict), _) :- !,
	duckdb_enum_internal_type(LT, Id),
	internal_(Id, Int),
	duckdb_enum_dictionary_size(LT, N),
	N1 is N - 1,
	findall(S, (between(0, N1, I), duckdb_enum_dictionary_value(LT, I, A), atom_string(A, S)), Ss),
	Dict =.. [dict|Ss].
type_(24, LT, list(T), Ctx) :- !,
	duckdb_list_type_child_type(LT, CT),
	setup_call_cleanup(true, type_(CT, T, Ctx), by_handle_(duckdb_destroy_logical_type, CT)).
type_(27, _, uuid, _) :- !.
type_(31, _, timestamp(86400000000), _) :- !.
type_(32, _, hugeint(unsigned), _) :- !.
type_(36, _, null, _) :- !.
type_(42, _, timestamp(86400000000000), _) :- !.
type_(Id, _, _, Ctx) :-
	(	type_name_(Id, Name) -> true ; Name = Id ),
	throw(error(domain_error(duckdb_column_type, Name), Ctx)).

internal_(2, sint8).
internal_(3, sint16).
internal_(4, sint32).
internal_(5, sint64).
internal_(6, uint8).
internal_(7, uint16).
internal_(8, uint32).
internal_(16, hugeint).

type_name_(15, interval).
type_name_(25, struct).
type_name_(26, map).
type_name_(28, union).
type_name_(29, bit).
type_name_(30, time_tz).
type_name_(33, array).
type_name_(35, bignum).
type_name_(39, time_ns).
type_name_(40, geometry).
type_name_(41, variant).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Vectors

% The N values of vector V, of type T, '$null$' where its validity mask says so

values_(_, _, 0, []) :- !.
values_(null, _, N, Vs) :- !, length(Vs, N), maplist(=('$null$'), Vs).
values_(T, V, N, Vs) :-
	duckdb_vector_get_data(V, D),
	duckdb_vector_get_validity(V, M),
	raw_(T, V, D, N, Raw),
	valid_(M, N, Bits),
	maplist(null_or_, Bits, Raw, Vs).

null_or_(0, _, '$null$').
null_or_(1, V, V).

% Validity as N bits, 1 for a value; no mask means all are values

valid_(0, N, Bits) :- !, length(Bits, N), maplist(=(1), Bits).
valid_(M, N, Bits) :-
	NW is (N + 63) // 64,
	'$pointer_to_array'(uint64, M, NW, Ws),
	word_bits_(Ws, N, Bits).

word_bits_(_, 0, []) :- !.
word_bits_([W|Ws], N, Bits) :-
	K is min(N, 64),
	K1 is K - 1,
	findall(B, (between(0, K1, I), B is (W >> I) /\ 1), Bs),
	append(Bs, Bits1, Bits),
	N1 is N - K,
	word_bits_(Ws, N1, Bits1).

raw_(bool, _, D, N, Vs) :- !,
	'$pointer_to_array'(uint8, D, N, Bs),
	maplist(bool_, Bs, Vs).
raw_(int(T), _, D, N, Vs) :- !, '$pointer_to_array'(T, D, N, Vs).
raw_(real(T), _, D, N, Vs) :- !, '$pointer_to_array'(T, D, N, Vs).
raw_(hugeint(Sign), _, D, N, Vs) :- !,
	N2 is 2 * N,
	'$pointer_to_array'(uint64, D, N2, Ws),
	huge_(Ws, Sign, Vs).
raw_(decimal(W, S, Int), _, D, N, Vs) :- !,
	(	Int == hugeint
	->	N2 is 2 * N, '$pointer_to_array'(uint64, D, N2, Ws), huge_(Ws, signed, Is)
	;	'$pointer_to_array'(Int, D, N, Is)
	),
	maplist(decimal_(W, S), Is, Vs).
raw_(date, _, D, N, Vs) :- !,
	'$pointer_to_array'(sint32, D, N, Ds),
	maplist(date_, Ds, Vs).
raw_(time, _, D, N, Vs) :- !,
	'$pointer_to_array'(sint64, D, N, Us),
	maplist(time_, Us, Vs).
raw_(timestamp(PerDay), _, D, N, Vs) :- !,
	'$pointer_to_array'(sint64, D, N, Ts),
	maplist(timestamp_(PerDay), Ts, Vs).
raw_(uuid, _, D, N, Vs) :- !,
	N2 is 2 * N,
	'$pointer_to_array'(uint64, D, N2, Ws),
	huge_(Ws, uuid, Is),
	maplist(uuid_, Is, Vs).
raw_(enum(Int, Dict), _, D, N, Vs) :- !,
	'$pointer_to_array'(Int, D, N, Is),
	maplist(enum_(Dict), Is, Vs).
raw_(varchar, _, D, N, Vs) :- !, strings_(D, N, text, Vs).
raw_(blob, _, D, N, Vs) :- !, strings_(D, N, bytes, Vs).
raw_(list(T), V, D, N, Vs) :- !,
	N2 is 2 * N,
	'$pointer_to_array'(uint64, D, N2, Es),
	duckdb_list_vector_get_child(V, CV),
	duckdb_list_vector_get_size(V, CN),
	values_(T, CV, CN, CVs0),
	CVs =.. [c|CVs0],
	lists_(Es, CVs, Vs).

bool_(0, false) :- !.
bool_(_, true).

% Each 128-bit value from its low and high 64-bit halves; a UUID's high half has its top bit flipped

huge_([], _, []).
huge_([Lo, Hi|Ws], Sign, [V|Vs]) :-
	(	Sign == signed, Hi >= 1 << 63 -> H is Hi - (1 << 64)
	;	Sign == uuid -> H is xor(Hi, 1 << 63)
	;	H = Hi
	),
	V is (H << 64) + Lo,
	huge_(Ws, Sign, Vs).

decimal_(_, 0, I, I) :- !.
decimal_(W, S, I, V) :-
	W =< 15, !,
	V is I / 10.0 ** S.
decimal_(_, S, I, V) :-
	A is abs(I),
	number_codes(A, Ds0),
	length(Ds0, L),
	(	L =< S -> Pad is S - L + 1, length(Zs, Pad), maplist(=(0'0), Zs), append(Zs, Ds0, Ds) ; Ds = Ds0 ),
	length(Ds, L1),
	K is L1 - S,
	length(Int, K),
	append(Int, Frac, Ds),
	(	I < 0 -> Sign = "-" ; Sign = "" ),
	format(string(V), "~s~s.~s", [Sign, Int, Frac]).

date_(Days, date(Y, M, D)) :-
	civil_(Days, Y, M, D).

% The proleptic Gregorian date Days after 1970-01-01

civil_(Days, Y, M, D) :-
	Z is Days + 719468,
	Era is Z div 146097,
	Doe is Z - Era * 146097,
	Yoe is (Doe - Doe // 1460 + Doe // 36524 - Doe // 146096) // 365,
	Y0 is Yoe + Era * 400,
	Doy is Doe - (365 * Yoe + Yoe // 4 - Yoe // 100),
	Mp is (5 * Doy + 2) // 153,
	D is Doy - (153 * Mp + 2) // 5 + 1,
	(	Mp < 10 -> M is Mp + 3 ; M is Mp - 9 ),
	(	M =< 2 -> Y is Y0 + 1 ; Y = Y0 ).

time_(Us, time(H, Mn, S)) :-
	clock_(Us, 86400000000, H, Mn, S).

timestamp_(PerDay, T, timestamp(Y, M, D, H, Mn, S)) :-
	Days is T div PerDay,
	Rem is T mod PerDay,
	civil_(Days, Y, M, D),
	clock_(Rem, PerDay, H, Mn, S).

% Hours, minutes and seconds in Rem units of a day of PerDay of them

clock_(Rem, PerDay, H, Mn, S) :-
	PerSec is PerDay // 86400,
	Secs is Rem // PerSec,
	Frac is Rem mod PerSec,
	H is Secs // 3600,
	Mn is Secs // 60 mod 60,
	S0 is Secs mod 60,
	(	Frac =:= 0 -> S = S0 ; S is S0 + Frac / PerSec ).

uuid_(I, S) :-
	format(atom(A0), "~16r", [I]),
	downcase_atom(A0, A),
	atom_chars(A, Cs0),
	length(Cs0, L),
	Pad is 32 - L,
	length(Zs, Pad),
	maplist(=('0'), Zs),
	append(Zs, Cs0, Cs),
	length(A1, 8), length(A2, 4), length(A3, 4), length(A4, 4),
	append([A1, A2, A3, A4, A5], Cs),
	format(string(S), "~s-~s-~s-~s-~s", [A1, A2, A3, A4, A5]).

enum_(Dict, I, S) :-
	I1 is I + 1,
	arg(I1, Dict, S).

% Each string of a duckdb_string_t array: its length first, then its text inline or a pointer to it

strings_(D, N, How, Vs) :-
	N4 is 4 * N,
	'$pointer_to_array'(uint32, D, N4, Ws),
	string_list_(Ws, D, How, Vs).

string_list_([], _, _, []).
string_list_([Len, _, _, _|Ws], At, How, [V|Vs]) :-
	duckdb_string_t_data(At, P),
	(	How == text -> '$pointer_to_text'(P, Len, V)
	;	'$pointer_to_array'(uint8, P, Len, V)
	),
	At1 is At + 16,
	string_list_(Ws, At1, How, Vs).

% Each list from its offset and length into the child values

lists_([], _, []).
lists_([Off, Len|Es], CVs, [L|Ls]) :-
	findall(X, (between(1, Len, K), J is Off + K, arg(J, CVs, X)), L),
	lists_(Es, CVs, Ls).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Goal once between BEGIN and COMMIT, rolled back if it fails or raises. A statement in it that
% failed, even one Goal caught, has aborted the transaction, where COMMIT would roll back without
% an error: that raises duckdb_error(_) instead.

duck_transaction(DB, Goal) :-
	duck_query(DB, 'BEGIN', _),
	(	catch(Goal, E, true)
	->	(	var(E)
		->	commit_(DB)
		;	catch(duck_query(DB, 'ROLLBACK', _), _, true),
			throw(E)
		)
	;	duck_query(DB, 'ROLLBACK', _),
		fail
	).

% DuckDB refuses every statement in an aborted transaction, so one probes it

commit_(DB) :-
	(	catch(duck_query(DB, 'SELECT 1', _), error(duckdb_error(_), _), Aborted = true),
		Aborted == true
	->	catch(duck_query(DB, 'ROLLBACK', _), _, true),
		throw(error(duckdb_error('a statement failed, so the transaction was rolled back'), duck_transaction/2))
	;	duck_query(DB, 'COMMIT', _)
	).

text_atom_(T, _, Ctx) :- var(T), !, throw(error(instantiation_error, Ctx)).
text_atom_(T, A, _) :- atom(T), !, A = T.
text_atom_(T, A, _) :- string(T), !, atom_string(A, T).
text_atom_(T, _, Ctx) :- throw(error(type_error(text, T), Ctx)).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(list, X, Ctx) :- (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).

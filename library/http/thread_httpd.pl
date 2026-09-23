:- module(thread_httpd, [http_server/2, http_stop_server/2]).

/** HTTP server, after SWI-Prolog's library(http/thread_httpd).

http_server(:Goal, +Options) serves HTTP on port(Port), calling
Goal(Request) for each request. Unlike SWI-Prolog's it serves one
request at a time and does not return until http_stop_server/2 is
called, so it is usually the last thing a program does.

Options:

  * port(?Port): an integer, Host:Port, or unbound for any free port.
  * ssl([certificate_file(File), key_file(File)]): serve HTTPS.

Other options, such as workers(N), are accepted and ignored.

Request is a list of fields: method(M) (lower case), path(P), search(Q)
(a list of Name=Value), request_uri(U), http_version(Major-Minor),
protocol(http|https), peer(ip(A,B,C,D)), host(H), port(P), input(S),
and each header as Name(Value). Text values are strings; content_length
is an integer.

Goal writes a CGI-style reply to current output: header lines, a blank
line, then the body. Status: Code sets the status, Location: alone makes
it a 302. Goal may instead throw http_reply(Reply), Reply one of
not_found(Path), forbidden(Path), moved(URL), moved_temporary(URL),
see_other(URL), bad_request(Why), method_not_allowed(Method, Path) or
server_error(Why). Anything else thrown, or Goal failing, is a 500.
*/

:- use_module(library(lists)).
:- use_module(library(sockets)).

:- meta_predicate(http_server(1, +)).

:- dynamic('$http_stop'/1).

http_server(Goal, Options) :-
	(memberchk(port(Port), Options) -> true ; Port = _),
	(	memberchk(ssl(SSL), Options) ->
		(memberchk(certificate_file(Cert), SSL) -> true ; Cert = 'fullchain.pem'),
		(memberchk(key_file(Key), SSL) -> true ; Key = 'privkey.pem'),
		SockOpts = [ssl(true), certfile(Cert), keyfile(Key)],
		Scheme = https
	;	SockOpts = [], Scheme = http
	),
	socket_server_open(Port, S, SockOpts),
	(Port = _:PortNum -> true ; PortNum = Port),
	format(user_error, "% Started server at ~a://localhost:~w/~n", [Scheme, PortNum]),
	retractall('$http_stop'(PortNum)),
	setup_call_cleanup(true, accept_loop(S, Goal, Scheme, PortNum), socket_server_close(S)).

%% http_stop_server(+Port, +Options)
%
% The server on Port stops once the request in hand is done.

http_stop_server(Port, _) :-
	assertz('$http_stop'(Port)).

accept_loop(S, Goal, Scheme, Port) :-
	(	retract('$http_stop'(Port)) ->
		true
	;	socket_server_accept(S, Peer, C, []),
		serve(C, Peer, Goal, Scheme, Port),
		accept_loop(S, Goal, Scheme, Port)
	).

serve(C, Peer, Goal, Scheme, Port) :-
	catch(serve_(C, Peer, Goal, Scheme, Port), E, report(E)),
	catch(close(C), _, true).

report(E) :-
	format(user_error, "% http_server: ~q~n", [E]).

serve_(C, Peer, Goal, Scheme, Port) :-
	(	read_request(C, Peer, Scheme, Port, Request) ->
		memberchk(method(Method), Request),
		run_goal(Goal, Request, Reply),
		send_reply(C, Method, Reply)
	;	send_reply(C, get, reply(400, [], "Bad Request\n"))
	).

% The request...

read_request(C, Peer, Scheme, Port, Request) :-
	read_line(C, Line),
	Line \== end_of_file,
	append(MethodCs, [' '|Rest], Line),
	append(URI, [' '|VersionCs], Rest),
	append("HTTP/", Ver, VersionCs),
	append(MajorCs, ['.'|MinorCs], Ver),
	number_chars(Major, MajorCs),
	number_chars(Minor, MinorCs),
	lower_chars(MethodCs, Lower),
	known_method(Lower, Method),
	read_fields(C, Fields0),
	(append(PathCs, ['?'|QueryCs], URI) -> true ; PathCs = URI, QueryCs = []),
	decode(PathCs, Path),
	query_pairs(QueryCs, Search),
	peer_term(Peer, PeerTerm),
	(select(host(HostPort), Fields0, Fields) -> host_only(HostPort, Host) ; Host = "localhost", Fields = Fields0),
	(QueryCs == [] -> SearchF = [] ; SearchF = [search(Search)]),
	append([
		[method(Method), path(Path), request_uri(URI)],
		SearchF,
		[http_version(Major-Minor), protocol(Scheme), peer(PeerTerm), host(Host), port(Port), input(C)],
		Fields
	], Request).

known_method("get", get).
known_method("post", post).
known_method("put", put).
known_method("patch", patch).
known_method("delete", delete).
known_method("head", head).
known_method("options", options).

host_only(HostPort, Host) :-
	(append(Host, [':'|_], HostPort) -> true ; Host = HostPort).

peer_term(Addr:_, ip(A,B,C,D)) :-
	(atom(Addr) -> atom_chars(Addr, Cs) ; Cs = Addr),
	split(Cs, '.', [As,Bs,Cs1,Ds]),
	maplist(number_chars, [A,B,C,D], [As,Bs,Cs1,Ds]),
	!.
peer_term(Peer, Peer).

query_pairs([], []) :- !.
query_pairs(Cs, Pairs) :-
	split(Cs, '&', Fields),
	exclude(==([]), Fields, Fields1),
	maplist(query_pair, Fields1, Pairs).

query_pair(Field, Name=Value) :-
	(append(NameCs, ['='|ValueCs], Field) -> true ; NameCs = Field, ValueCs = []),
	decode(NameCs, N),
	decode(ValueCs, Value),
	atom_chars(Name, N).

decode([], []) :- !.
decode(Cs, Decoded) :-
	to_string(Cs, S),
	urlenc(Decoded, S, []).

% Builtins such as urlenc/3 want a packed string, not a list of chars...

to_string(Cs, S) :- format(string(S), "~s", [Cs]).


split(Cs, Sep, [Field|Fields]) :-
	(	append(Field, [Sep|Rest], Cs) ->
		split(Rest, Sep, Fields)
	;	Field = Cs, Fields = []
	).

% getline/2 drops the line end, and gives an empty line as ''...

read_line(S, Line) :-
	(	getline(S, Line0) ->
		(	Line0 == '' -> Line = []
		;	append(Line, ['\r'], Line0) -> true
		;	Line = Line0
		)
	;	Line = end_of_file
	).

read_fields(S, Fields) :-
	read_line(S, Line),
	(	(Line == end_of_file ; Line == []) ->
		Fields = []
	;	header_field(Line, Field) ->
		Fields = [Field|Fields1],
		read_fields(S, Fields1)
	;	read_fields(S, Fields)
	).

header_field(Line, Field) :-
	append(NameCs, [':'|ValueCs], Line),
	NameCs \== [],
	!,
	lower_chars(NameCs, Lower),
	maplist(dash_underscore, Lower, Cs2),
	atom_chars(Name, Cs2),
	trim(ValueCs, Value0),
	(	Name == content_length ->
		catch(number_chars(Value, Value0), _, fail)
	;	Value = Value0
	),
	Field =.. [Name, Value].

dash_underscore(C0, C) :- (C0 == (-) -> C = '_' ; C = C0).

trim(Cs0, Cs) :-
	drop_ws(Cs0, Cs1),
	reverse(Cs1, R1),
	drop_ws(R1, R2),
	reverse(R2, Cs).

drop_ws([C|Cs], Out) :- memberchk(C, [' ','\t','\r']), !, drop_ws(Cs, Out).
drop_ws(Cs, Cs).

% Running the goal, its output captured as a CGI reply...

run_goal(Goal, Request, Reply) :-
	catch(
		(	with_output_to(string(Out), call(Goal, Request)) ->
			cgi_reply(Out, Reply)
		;	Reply = reply(500, [], "Internal Server Error: goal failed\n")
		),
		E,
		error_reply(E, Reply)
	).

error_reply(http_reply(R), Reply) :-
	http_reply(R, Reply),
	!.
error_reply(error(existence_error(http_location, Path), _), Reply) :-
	http_reply(not_found(Path), Reply),
	!.
error_reply(E, reply(500, [], Body)) :-
	report(E),
	format(string(Body), "Internal Server Error: ~q~n", [E]).

http_reply(not_found(Path), reply(404, [], Body)) :-
	format(string(Body), "Not Found: ~s~n", [Path]).
http_reply(forbidden(Path), reply(403, [], Body)) :-
	format(string(Body), "Forbidden: ~s~n", [Path]).
http_reply(bad_request(Why), reply(400, [], Body)) :-
	format(string(Body), "Bad Request: ~w~n", [Why]).
http_reply(method_not_allowed(Method, Path), reply(405, [], Body)) :-
	format(string(Body), "Method Not Allowed: ~w ~s~n", [Method, Path]).
http_reply(server_error(Why), reply(500, [], Body)) :-
	format(string(Body), "Internal Server Error: ~w~n", [Why]).
http_reply(moved(URL), Reply) :- redirect_reply(301, URL, Reply).
http_reply(moved_temporary(URL), Reply) :- redirect_reply(302, URL, Reply).
http_reply(see_other(URL), Reply) :- redirect_reply(303, URL, Reply).

redirect_reply(Code, URL, reply(Code, [location(Loc)], Body)) :-
	text_chars(URL, Loc),
	format(string(Body), "Moved to ~s~n", [Loc]).

text_chars(T, Cs) :-
	(atom(T) -> atom_chars(T, Cs) ; Cs = T).

% A CGI reply is header lines, a blank line and the body...

cgi_reply(Out, reply(Code, Headers, Body)) :-
	(	Out = ['\n'|Body0] -> Head = []
	;	Out = ['\r','\n'|Body0] -> Head = []
	;	append(Head, ['\n','\n'|Body0], Out) -> true
	;	append(Head, ['\r','\n','\r','\n'|Body0], Out) -> true
	;	Head = [], Body0 = Out
	),
	split(Head, '\n', Lines),
	cgi_fields(Lines, Fields),
	(	select(status(StatusCs), Fields, Headers),
		(append(CodeCs, [' '|ReasonCs], StatusCs) -> true ; CodeCs = StatusCs, ReasonCs = []),
		number_chars(N, CodeCs) ->
		(ReasonCs == [] -> Code = N ; Code = N-ReasonCs)
	;	memberchk(location(_), Fields) ->
		Code = 302, Headers = Fields
	;	Code = 200, Headers = Fields
	),
	Body = Body0.

cgi_fields([], []).
cgi_fields([Line0|Lines], Fields) :-
	(append(Line, ['\r'], Line0) -> true ; Line = Line0),
	(	Line == [] -> Fields = Fields1
	;	header_field(Line, F) -> Fields = [F|Fields1]
	;	Fields = Fields1
	),
	cgi_fields(Lines, Fields1).

% The reply...

send_reply(C, Method, reply(Code0, Headers, Body)) :-
	(Code0 = Code-Reason -> true ; Code = Code0, reason(Code, Reason)),
	utf8_length(Body, Len),
	format(C, "HTTP/1.1 ~d ~s\r\n", [Code, Reason]),
	(memberchk(content_type(_), Headers) -> true ; format(C, "Content-Type: text/plain; charset=UTF-8\r\n", [])),
	forall(
		(member(H, Headers), H =.. [Name, Value], Name \== content_length, Name \== connection),
		send_field(C, Name, Value)),
	format(C, "Content-Length: ~d\r\nConnection: close\r\n\r\n", [Len]),
	(Method == head -> true ; format(C, "~s", [Body])),
	flush_output(C).

send_field(C, Name, Value) :-
	atom_chars(Name, Cs0),
	field_case(Cs0, true, Cs),
	(atom(Value) -> atom_chars(Value, V) ; number(Value) -> number_chars(Value, V) ; V = Value),
	format(C, "~s: ~s\r\n", [Cs, V]).

field_case([], _, []).
field_case([C0|Cs0], Up, [C|Cs]) :-
	(	C0 == '_' -> C = (-), Up2 = true
	;	Up == true -> upper_char(C0, C), Up2 = false
	;	C = C0, Up2 = false
	),
	field_case(Cs0, Up2, Cs).

utf8_length(Cs, Len) :-
	foldl(utf8_length_, Cs, 0, Len).

utf8_length_(C, N0, N) :-
	char_code(C, X),
	(X < 0x80 -> B = 1 ; X < 0x800 -> B = 2 ; X < 0x10000 -> B = 3 ; B = 4),
	N is N0 + B.

lower_chars(Cs, Ls) :- maplist(lower_char, Cs, Ls).

lower_char(C, L) :-
	char_code(C, X),
	(X >= 0'A, X =< 0'Z -> Y is X + 32, char_code(L, Y) ; L = C).

upper_char(C, U) :-
	char_code(C, X),
	(X >= 0'a, X =< 0'z -> Y is X - 32, char_code(U, Y) ; U = C).

reason(200, "OK") :- !.
reason(201, "Created") :- !.
reason(204, "No Content") :- !.
reason(301, "Moved Permanently") :- !.
reason(302, "Found") :- !.
reason(303, "See Other") :- !.
reason(304, "Not Modified") :- !.
reason(307, "Temporary Redirect") :- !.
reason(308, "Permanent Redirect") :- !.
reason(400, "Bad Request") :- !.
reason(401, "Unauthorized") :- !.
reason(403, "Forbidden") :- !.
reason(404, "Not Found") :- !.
reason(405, "Method Not Allowed") :- !.
reason(500, "Internal Server Error") :- !.
reason(501, "Not Implemented") :- !.
reason(_, "Unknown").

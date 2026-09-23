:- module(http_open, [http_open/3]).

/** HTTP client, after SWI-Prolog's library(http/http_open).

http_open(+URL, -Stream, +Options) opens Stream on the body of the reply.
URL is text (atom or string) or a list of parts: protocol(http|https),
host(H), port(P), path(P) and search(Name=Value list).

Options are those of SWI-Prolog, less proxies and keep-alive:

  * method(+M): get (default), post, put, patch, delete, head or options.
  * post(+Data): send Data, making the method post unless one is given.
    Data is text (sent as text/plain), atom(A), string(S), codes(Cs),
    chars(Cs) - each optionally with a leading content type, as in
    string(Type, S) - or form(Name=Value list), sent urlencoded.
  * request_header(Name=Value), user_agent(Agent),
    authorization(basic(User, Password)): extra request headers.
  * status_code(-Code): also stops a reply that is not 2xx being an error.
  * headers(-Fields): the reply's header fields as Name(Value), Name the
    lower-case field name with '-' as '_', Value a string, except that
    content_length is an integer. status_code(Code) comes first.
  * header(+Name, -Value): one reply field, "" if there was none.
  * final_url(-URL): as a string, after any redirects.
  * size(-Size): the content length, when the reply gives one.
  * version(-Major-Minor): of the reply.
  * redirect(+Bool), max_redirect(+Max): redirects are followed, ten at
    most, unless redirect(false).
  * timeout(+Seconds): on the connection's reads.

A reply that is not 2xx throws existence_error(url, URL), or
permission_error(url, URL) for 401, 403, 405 and 407, with context
status(Code, Reason), unless status_code/1 is given.

The stream is the connection itself unless the reply was chunked, when
it is the decoded body in memory. Close it when done.
*/

:- use_module(library(lists)).
:- use_module(library(sockets)).

http_open(URL, Stream, Options0) :-
	must_be_list(Options0),
	url_parts(URL, Parts),
	(	\+ memberchk(authorization(_), Options0),
		\+ (is_list(URL), URL = [P|_], compound(P)),
		url_userinfo(URL, User, Password) ->
		Options = [authorization(basic(User, Password))|Options0]
	;	Options = Options0
	),
	http_open_(Parts, Stream, Options, []).

http_open_(Parts, Stream, Options, Visited) :-
	request_method(Options, Method),
	connect(Parts, S, Options),
	catch(
		(	send_request(S, Method, Parts, Options),
			read_reply_head(S, Version, Code, Reason, Fields)
		),
		E,
		(close(S), throw(E))
	),
	parts_url(Parts, URL),
	(	redirect(Code, Fields, Options, Loc)
	->	close(S),
		resolve_url(Parts, Loc, Parts2),
		parts_url(Parts2, URL2),
		Visited2 = [URL|Visited],
		check_redirect(URL2, Visited2, Options),
		redirect_options(Code, Options, Options2),
		http_open_(Parts2, Stream, Options2, Visited2)
	;	(memberchk(status_code(_), Options) ; between(200, 299, Code))
	->	body_stream(S, Method, Code, Fields, Stream),
		return_options(Options, URL, Version, Code, Fields)
	;	close(S),
		(map_error_code(Code, Error) -> true ; Error = existence_error),
		Formal =.. [Error, url, URL],
		throw(error(Formal, context(http_open/3, status(Code, Reason))))
	).

must_be_list(L) :-
	(	var(L) -> throw(error(instantiation_error, http_open/3))
	;	is_list(L) -> true
	;	throw(error(type_error(list, L), http_open/3))
	).

request_method(Options, Method) :-
	(	memberchk(method(Method0), Options) ->
		(	var(Method0) -> throw(error(instantiation_error, http_open/3))
		;	memberchk(Method0, [get,post,put,patch,delete,head,options]) -> Method = Method0
		;	throw(error(domain_error(method, Method0), http_open/3))
		)
	;	memberchk(post(_), Options) -> Method = post
	;	Method = get
	).

% A URL is parsed into url(Scheme, Host, Port, PathQuery), text as strings...

url_parts(URL, _) :-
	var(URL),
	!,
	throw(error(instantiation_error, http_open/3)).
url_parts(URL, url(Scheme, Host, Port, Path)) :-
	is_list(URL), URL = [P|_], compound(P),
	!,
	(memberchk(protocol(Scheme), URL) -> true ; Scheme = http),
	memberchk(host(Host0), URL),
	text_chars(Host0, Host),
	(memberchk(port(Port), URL) -> true ; default_port(Scheme, Port)),
	(memberchk(path(Path0), URL) -> text_chars(Path0, Path1) ; Path1 = "/"),
	(Path1 = ['/'|_] -> Path2 = Path1 ; Path2 = ['/'|Path1]),
	(	memberchk(search(Search), URL) ->
		form_encode(Search, Query),
		append([Path2, "?", Query], Path)
	;	Path = Path2
	).
url_parts(URL, url(Scheme, Host, Port, Path)) :-
	text_chars(URL, Cs),
	(	append(SchemeCs, [':','/','/'|Rest], Cs) ->
		lower_chars(SchemeCs, LowerCs),
		(	LowerCs == "https" -> Scheme = https
		;	LowerCs == "http" -> Scheme = http
		;	throw(error(domain_error(url, URL), http_open/3))
		)
	;	Scheme = http, Rest = Cs
	),
	split_authority(Rest, Auth, Path0),
	(append(Path1, ['#'|_], Path0) -> true ; Path1 = Path0),
	(	Path1 = [] -> Path = "/"
	;	Path1 = ['?'|_] -> Path = ['/'|Path1]
	;	Path = Path1
	),
	(append(_, ['@'|HostPort], Auth) -> true ; HostPort = Auth),
	(	append(Host, [':'|PortCs], HostPort), PortCs \== [] ->
		catch(number_chars(Port, PortCs), _, throw(error(domain_error(url, URL), http_open/3)))
	;	Host = HostPort,
		default_port(Scheme, Port)
	),
	(Host == [] -> throw(error(domain_error(url, URL), http_open/3)) ; true).

split_authority([], [], []).
split_authority([C|Cs], [], [C|Cs]) :-
	memberchk(C, ['/','?','#']),
	!.
split_authority([C|Cs], [C|Auth], Path) :-
	split_authority(Cs, Auth, Path).

default_port(http, 80).
default_port(https, 443).

text_chars(T, Cs) :-
	(	atom(T) -> atom_chars(T, Cs)
	;	string(T) -> Cs = T
	;	is_list(T) -> Cs = T
	;	number(T) -> number_chars(T, Cs)
	;	throw(error(type_error(text, T), http_open/3))
	).

parts_url(url(Scheme, Host, Port, Path), URL) :-
	(default_port(Scheme, Port) -> PortCs = [] ; number_chars(Port, Ns), PortCs = [':'|Ns]),
	atom_chars(Scheme, SchemeCs),
	append([SchemeCs, "://", Host, PortCs, Path], URL).

% Userinfo is only ever sent as Basic authorization...

url_userinfo(URL, User, Password) :-
	text_chars(URL, Cs),
	(append(_, [':','/','/'|Rest], Cs) -> true ; Rest = Cs),
	split_authority(Rest, Auth, _),
	append(UserInfo, ['@'|_], Auth),
	!,
	(append(User, [':'|Password], UserInfo) -> true ; User = UserInfo, Password = []).

% A Location is resolved against the URL it came from...

resolve_url(Base, Loc, Parts) :-
	(	append(_, [':','/','/'|_], Loc) -> url_parts(Loc, Parts)
	;	Loc = ['/','/'|_] ->
		Base = url(Scheme, _, _, _),
		atom_chars(Scheme, SchemeCs),
		append(SchemeCs, [':'|Loc], URL),
		url_parts(URL, Parts)
	;	Base = url(Scheme, Host, Port, Path0),
		(	Loc = ['/'|_] -> Path = Loc
		;	(append(P0, ['?'|_], Path0) -> true ; P0 = Path0),
			(append(Dir, ['/'|File], P0), \+ memberchk('/', File) -> true ; Dir = []),
			append(Dir, ['/'|Loc], Path)
		),
		Parts = url(Scheme, Host, Port, Path)
	).

redirect(Code, Fields, Options, Loc) :-
	memberchk(Code, [301,302,303,307,308]),
	\+ memberchk(redirect(false), Options),
	memberchk(location(Loc), Fields),
	Loc \== [].

check_redirect(URL, Visited, Options) :-
	(memberchk(max_redirect(Max), Options) -> true ; Max = 10),
	length(Visited, N),
	(	Max \== infinite, N > Max ->
		format(string(Comment), "max_redirect (~w) limit exceeded", [Max]),
		throw(error(permission_error(redirect, http, URL), context(http_open/3, Comment)))
	;	include(==(URL), Visited, Same), length(Same, Count), Count > 2 ->
		throw(error(permission_error(redirect, http, URL), context(http_open/3, "Redirection loop")))
	;	true
	).

% 307 and 308 repeat the request as it was, anything else becomes a GET
% unless it was a GET, HEAD or DELETE already...

redirect_options(Code, Options, Options) :-
	memberchk(Code, [307,308]),
	!.
redirect_options(_, Options0, Options) :-
	exclude(post_option, Options0, Options1),
	(	memberchk(method(M), Options1), \+ memberchk(M, [get,head,delete]) ->
		exclude(method_option, Options1, Options)
	;	Options = Options1
	).

post_option(post(_)).
method_option(method(_)).

connect(url(Scheme, Host, Port, _), S, Options) :-
	atom_chars(HostA, Host),
	(Scheme == https -> SockOpts = [ssl(true)] ; SockOpts = []),
	socket_client_open(HostA:Port, S, SockOpts),
	(memberchk(timeout(T), Options), T \== infinite -> set_stream(S, timeout(T)) ; true).

send_request(S, Method, Parts, Options) :-
	Parts = url(Scheme, Host, Port, Path),
	method_name(Method, UMethod),
	(default_port(Scheme, Port) -> HostHdr = Host ; format(string(HostHdr), "~s:~d", [Host, Port])),
	(memberchk(user_agent(UA0), Options) -> text_chars(UA0, UA) ; UA = "Trealla Prolog"),
	format(S, "~s ~s HTTP/1.1\r\nHost: ~s\r\nUser-Agent: ~s\r\nConnection: close\r\n", [UMethod, Path, HostHdr, UA]),
	(	memberchk(authorization(basic(User, Password)), Options) ->
		send_basic_auth(S, User, Password)
	;	true
	),
	forall(member(request_header(Name=Value), Options),
		(text_chars(Name, N), text_chars(Value, V), format(S, "~s: ~s\r\n", [N, V]))),
	(	memberchk(post(Data), Options) ->
		post_data(Data, Type, Body),
		utf8_length(Body, Len),
		text_chars(Type, TypeCs),
		format(S, "Content-Type: ~s\r\nContent-Length: ~d\r\n\r\n~s", [TypeCs, Len, Body])
	;	format(S, "\r\n", [])
	),
	flush_output(S).

send_basic_auth(S, User, Password) :-
	text_chars(User, U), text_chars(Password, P),
	format(string(Creds), "~s:~s", [U, P]),
	base64(Creds, Enc, []),
	format(S, "Authorization: Basic ~s\r\n", [Enc]).

post_data(Data, _, _) :-
	var(Data),
	!,
	throw(error(instantiation_error, http_open/3)).
post_data(form(Pairs), "application/x-www-form-urlencoded", Body) :- !,
	form_encode(Pairs, Body).
post_data(atom(A), "text/plain; charset=UTF-8", Body) :- !, text_chars(A, Body).
post_data(string(S), "text/plain; charset=UTF-8", Body) :- !, text_chars(S, Body).
post_data(chars(Cs), "text/plain; charset=UTF-8", Cs) :- !.
post_data(codes(Cs), "text/plain; charset=UTF-8", Body) :- !, atom_codes(A, Cs), atom_chars(A, Body).
post_data(atom(Type, A), Type, Body) :- !, text_chars(A, Body).
post_data(string(Type, S), Type, Body) :- !, text_chars(S, Body).
post_data(chars(Type, Cs), Type, Cs) :- !.
post_data(codes(Type, Cs), Type, Body) :- !, atom_codes(A, Cs), atom_chars(A, Body).
post_data(Data, "text/plain; charset=UTF-8", Body) :-
	catch(text_chars(Data, Body), _, throw(error(domain_error(post_data, Data), http_open/3))).

form_encode(Pairs, Body) :-
	must_be_list(Pairs),
	maplist(form_pair, Pairs, Encs),
	join(Encs, '&', Body).

form_pair(Pair, Enc) :-
	(	Pair = (Name=Value) -> true
	;	Pair =.. [Name, Value] -> true
	;	throw(error(domain_error(form_field, Pair), http_open/3))
	),
	text_chars(Name, NCs), text_chars(Value, VCs),
	(NCs == [] -> N = [] ; to_string(NCs, NS), urlenc(NS, N, [])),
	(VCs == [] -> V = [] ; to_string(VCs, VS), urlenc(VS, V, [])),
	append([N, "=", V], Enc).

join([], _, []).
join([X], _, X) :- !.
join([X|Xs], Sep, Out) :-
	join(Xs, Sep, Rest),
	append(X, [Sep|Rest], Out).

utf8_length(Cs, Len) :-
	foldl(utf8_length_, Cs, 0, Len).

utf8_length_(C, N0, N) :-
	char_code(C, X),
	(X < 0x80 -> B = 1 ; X < 0x800 -> B = 2 ; X < 0x10000 -> B = 3 ; B = 4),
	N is N0 + B.

% The reply...

read_reply_head(S, Major-Minor, Code, Reason, Fields) :-
	read_line(S, Line),
	(	Line \== end_of_file,
		append("HTTP/", Rest, Line),
		append(VerCs, [' '|Rest2], Rest),
		append(MajorCs, ['.'|MinorCs], VerCs),
		number_chars(Major, MajorCs),
		number_chars(Minor, MinorCs),
		(append(CodeCs, [' '|Reason], Rest2) -> true ; CodeCs = Rest2, Reason = []),
		number_chars(Code, CodeCs) ->
		true
	;	throw(error(syntax_error(http_status_line), http_open/3))
	),
	read_fields(S, Fields).

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
	field_name(NameCs, Name),
	trim(ValueCs, Value0),
	(	Name == content_length ->
		catch(number_chars(Value, Value0), _, fail)
	;	Value = Value0
	),
	Field =.. [Name, Value].

field_name(Cs, Name) :-
	lower_chars(Cs, Lower),
	maplist(dash_underscore, Lower, Cs2),
	atom_chars(Name, Cs2).

dash_underscore(C0, C) :- (C0 == (-) -> C = '_' ; C = C0).

trim(Cs0, Cs) :-
	drop_ws(Cs0, Cs1),
	reverse(Cs1, R1),
	drop_ws(R1, R2),
	reverse(R2, Cs).

drop_ws([C|Cs], Out) :- memberchk(C, [' ','\t','\r']), !, drop_ws(Cs, Out).
drop_ws(Cs, Cs).

% A chunked body is decoded into memory, anything else is read from the
% connection as it arrives...

body_stream(S, Method, Code, Fields, Stream) :-
	(	(Method == head ; Code < 200 ; Code == 204 ; Code == 304) ->
		close(S),
		open_string("", Stream)
	;	memberchk(transfer_encoding(TE), Fields),
		lower_chars(TE, "chunked") ->
		catch(read_chunks(S, Chunks), E, (close(S), throw(E))),
		close(S),
		(Chunks = [C0|Cs] -> concat_chunks(Cs, C0, Body) ; Body = ""),
		open_string(Body, Stream)
	;	Stream = S
	).

read_chunks(S, Chunks) :-
	read_line(S, Line),
	(	Line == end_of_file -> Chunks = []
	;	(append(HexCs, [';'|_], Line) -> true ; HexCs = Line),
		trim(HexCs, Hex),
		format(string(HexS), "~s", [Hex]),
		hex_chars(Len, HexS),
		(	Len =:= 0 ->
			read_fields(S, _),
			Chunks = []
		;	'$bread'(S, Len, Chunk),
			read_line(S, _),
			Chunks = [Chunk|Chunks1],
			read_chunks(S, Chunks1)
		)
	).

% Chunks are joined as bytes, since one can end part way through a
% character...

concat_chunks([], Body, Body).
concat_chunks([C|Cs], Acc0, Body) :-
	string_concat(Acc0, C, Acc),
	concat_chunks(Cs, Acc, Body).

return_options(Options, URL, Version, Code, Fields) :-
	ignore(memberchk(status_code(Code), Options)),
	ignore(memberchk(final_url(URL), Options)),
	ignore(memberchk(version(Version), Options)),
	ignore(memberchk(headers([status_code(Code)|Fields]), Options)),
	(	memberchk(size(Size), Options), memberchk(content_length(Len), Fields) ->
		Size = Len
	;	true
	),
	return_header_options(Options, Fields).

return_header_options([], _).
return_header_options([O|Os], Fields) :-
	(	O = header(Name, Value) ->
		(F =.. [Name, V], memberchk(F, Fields) -> Value = V ; Value = "")
	;	true
	),
	return_header_options(Os, Fields).

method_name(get, "GET").
method_name(post, "POST").
method_name(put, "PUT").
method_name(patch, "PATCH").
method_name(delete, "DELETE").
method_name(head, "HEAD").
method_name(options, "OPTIONS").

lower_chars(Cs, Ls) :- maplist(lower_char, Cs, Ls).

lower_char(C, L) :-
	char_code(C, X),
	(X >= 0'A, X =< 0'Z -> Y is X + 32, char_code(L, Y) ; L = C).

map_error_code(401, permission_error).
map_error_code(403, permission_error).
map_error_code(404, existence_error).
map_error_code(405, permission_error).
map_error_code(407, permission_error).
map_error_code(410, existence_error).

% Builtins such as urlenc/3 want a packed string, not a list of chars...

to_string(Cs, S) :- format(string(S), "~s", [Cs]).

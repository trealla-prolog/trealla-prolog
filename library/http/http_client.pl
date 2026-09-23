:- module(http_client, [
	http_get/3, http_post/4, http_put/4, http_patch/4, http_delete/3,
	http_read_data/3
	]).

/** HTTP client requests, after SWI-Prolog's library(http/http_client).

These take http_open/3's options, plus to(Type) saying how to return the
reply: string (the default), chars, codes or atom. A form reply read on
the server side comes back as a list of Name=Value, with Value a string.
*/

:- use_module(library(lists)).
:- use_module(library(http/http_open)).

http_get(URL, Data, Options) :-
	(memberchk(headers(Fields), Options) -> Options1 = Options ; Options1 = [headers(Fields)|Options]),
	http_open(URL, S, Options1),
	setup_call_cleanup(true, read_reply(S, Fields, Data, Options), close(S)).

http_post(URL, Data, Reply, Options) :-
	http_get(URL, Reply, [post(Data)|Options]).

http_put(URL, Data, Reply, Options) :-
	http_post(URL, Data, Reply, [method(put)|Options]).

http_patch(URL, Data, Reply, Options) :-
	http_post(URL, Data, Reply, [method(patch)|Options]).

http_delete(URL, Data, Options) :-
	http_get(URL, Data, [method(delete)|Options]).

read_reply(S, Fields, Data, Options) :-
	(	(memberchk(method(head), Options) ; memberchk(status_code(C), Fields), memberchk(C, [204,304])) ->
		Body = ""
	;	memberchk(content_length(Len), Fields) ->
		read_bytes(S, Len, Body)
	;	read_to_end(S, Body)
	),
	convert(Body, Data, Options).

%% http_read_data(+Request, -Data, +Options)
%
% Reads the body of a request received by http_server/2, using its
% input(Stream), content_length(Len) and content_type(Type) fields.

http_read_data(Request, Data, Options) :-
	memberchk(input(S), Request),
	(	memberchk(content_length(Len), Request) ->
		read_bytes(S, Len, Body)
	;	read_to_end(S, Body)
	),
	(	\+ memberchk(to(_), Options),
		memberchk(content_type(Type), Request),
		append("application/x-www-form-urlencoded", _, Type) ->
		form_decode(Body, Data)
	;	convert(Body, Data, Options)
	).

read_bytes(_, 0, "") :- !.
read_bytes(S, Len, Body) :-
	(	'$bread'(S, Len, Body0) -> Body = Body0
	;	throw(error(syntax_error(http_body_truncated), http_read_data/3))
	).

read_to_end(S, Body) :-
	'$bread'(S, _, Body0),
	(Body0 == [] -> Body = "" ; Body = Body0).

convert(Body, Data, Options) :-
	(memberchk(to(Type), Options) -> true ; Type = string),
	(	Type == string -> Data = Body
	;	Type == chars -> Data = Body
	;	Type == atom -> atom_chars(Data, Body)
	;	Type == codes -> atom_chars(A, Body), atom_codes(A, Data)
	;	throw(error(domain_error(http_data_type, Type), http_read_data/3))
	).

form_decode(Body, Pairs) :-
	split(Body, '&', Fields),
	exclude(==([]), Fields, Fields1),
	maplist(form_field, Fields1, Pairs).

split(Cs, Sep, [Field|Fields]) :-
	(	append(Field, [Sep|Rest], Cs) ->
		split(Rest, Sep, Fields)
	;	Field = Cs, Fields = []
	).

form_field(Field, Name=Value) :-
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


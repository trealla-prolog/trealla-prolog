:- module(llm, [
	llm_ask/2,
	llm_ask/3,
	llm_chat/3,
	llm_complete/3,
	llm_json/4,
	llm_embedding/3,
	llm_models/2,
	llm_set_options/1
	]).

% Large language models over the OpenAI-style HTTP API that llama.cpp's llama-server, Ollama,
% LM Studio, vLLM and hosted services serve
%
% MACOS: brew install llama.cpp; llama-server -m model.gguf
%	(or brew install ollama; ollama serve, with base_url('http://127.0.0.1:11434/v1'))
%
%	?- llm_ask("Name three primes.", Reply).
%	Reply = "2, 3 and 5." .
%
%	?- llm_chat([system-"Answer in one word.", user-"Capital of France?"], Reply, [temperature(0)]).
%
% Text is atoms, strings or chars, and replies are strings. Options, which llm_set_options/1 sets
% defaults for:
%
%	base_url(URL)		'http://127.0.0.1:8080/v1', llama-server's, unless LLM_BASE_URL says
%	api_key(Key)		sent as a bearer token; LLM_API_KEY if set
%	model(Name)			which model, for a server with several
%	temperature(T), top_p(P), max_tokens(N), seed(N), stop(Strings)
%	json_schema(Schema)	the reply must match Schema, given as JSON text or a json_chars//1 term
%	grammar(GBNF)		the reply must match a GBNF grammar (llama-server only)
%	timeout(Seconds)	on reads, 300 by default
%	response(-JSON)		the whole reply, as a json_chars//1 term
%
% A reply that isn't 2xx raises llm_error(Status, Message).

:- use_module(library(lists)).
:- use_module(library(json)).
:- use_module(library(http/http_client)).

:- dynamic(default_/1).

llm_set_options(Options) :-
	must_be_(list, Options, llm_set_options/1),
	forall(member(O, Options),
		(	must_be_(compound, O, llm_set_options/1),
			functor(O, N, A),
			functor(Old, N, A),
			retractall(default_(Old)),
			assertz(default_(O))
		)).

option_(O, Os) :- memberchk(O, Os), !.
option_(O, _) :- default_(O), !.

base_url_(Os, URL) :-
	(	option_(base_url(U), Os) -> true
	;	getenv('LLM_BASE_URL', U) -> true
	;	U = 'http://127.0.0.1:8080/v1'
	),
	text_chars_(U, Cs0),
	(	append(Cs, [/], Cs0) -> true ; Cs = Cs0 ),
	atom_chars(URL, Cs).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% One user message

llm_ask(Prompt, Reply) :-
	llm_ask(Prompt, Reply, []).

llm_ask(Prompt, Reply, Options) :-
	llm_chat_([user-Prompt], Reply, Options, llm_ask/3).

% Messages are Role-Content, Role system, user or assistant

llm_chat(Messages, Reply, Options) :-
	llm_chat_(Messages, Reply, Options, llm_chat/3).

llm_chat_(Messages, Reply, Options, Ctx) :-
	must_be_(list, Messages, Ctx),
	must_be_(list, Options, Ctx),
	maplist(message_(Ctx), Messages, Ms),
	sampling_(Options, Ctx, Fields),
	request_('/chat/completions', [string("messages")-list(Ms)|Fields], Options, Ctx, R),
	(	path_(R, [choices, 0, message, content], string(Cs)) -> true
	;	throw(error(llm_error(reply, R), Ctx))
	),
	chars_string_(Cs, Reply).

message_(Ctx, M, pairs([string("role")-string(Rs), string("content")-string(Cs)])) :-
	(	nonvar(M), M = Role-Content -> true
	;	throw(error(type_error(role_content, M), Ctx))
	),
	must_be_(atom, Role, Ctx),
	(	memberchk(Role, [system, user, assistant, developer, tool]) -> true
	;	throw(error(domain_error(role, Role), Ctx))
	),
	atom_chars(Role, Rs),
	text_(Content, Cs, Ctx).

% Text going on from Prompt, for a model without a chat template

llm_complete(Prompt, Text, Options) :-
	Ctx = llm_complete/3,
	must_be_(list, Options, Ctx),
	text_(Prompt, Ps, Ctx),
	sampling_(Options, Ctx, Fields),
	request_('/completions', [string("prompt")-string(Ps)|Fields], Options, Ctx, R),
	(	path_(R, [choices, 0, text], string(Cs)) -> true
	;	throw(error(llm_error(reply, R), Ctx))
	),
	chars_string_(Cs, Text).

% A reply to Prompt matching a JSON schema, read as a json_chars//1 term

llm_json(Prompt, Schema, JSON, Options) :-
	Ctx = llm_json/4,
	must_be_(list, Options, Ctx),
	llm_chat_([user-Prompt], Reply, [json_schema(Schema)|Options], Ctx),
	(	catch(phrase(json_chars(JSON0), Reply), _, fail) -> JSON = JSON0
	;	throw(error(llm_error(not_json, Reply), Ctx))
	).

% Vector, a list of floats, embedding Text; the server needs its embeddings turned on, as
% llama-server's --embeddings does

llm_embedding(Text, Vector, Options) :-
	Ctx = llm_embedding/3,
	must_be_(list, Options, Ctx),
	text_(Text, Cs, Ctx),
	model_field_(Options, Fs),
	request_('/embeddings', [string("input")-string(Cs)|Fs], Options, Ctx, R),
	(	path_(R, [data, 0, embedding], list(Ns)) -> true
	;	throw(error(llm_error(reply, R), Ctx))
	),
	maplist(number_float_, Ns, Vector).

number_float_(number(N), F) :- F is float(N).

% The ids of the models the server offers

llm_models(Names, Options) :-
	Ctx = llm_models/2,
	must_be_(list, Options, Ctx),
	request_('/models', get, Options, Ctx, R),
	(	path_(R, [data], list(Ds)) -> true
	;	throw(error(llm_error(reply, R), Ctx))
	),
	findall(N, (member(D, Ds), path_(D, [id], string(Cs)), atom_chars(N, Cs)), Names).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Requests

% The request fields the options give, after the model

sampling_(Os, Ctx, Fields) :-
	model_field_(Os, F0),
	findall(F, (sampling_option_(Name, Key, Type), O =.. [Name, V], option_(O, Os), field_(Type, Key, V, F, Ctx)), F1),
	(	option_(json_schema(S), Os)
	->	schema_(S, ST, Ctx),
		F2 = [string("response_format")-pairs([string("type")-string("json_schema"),
			string("json_schema")-pairs([string("name")-string("reply"), string("strict")-boolean(true),
				string("schema")-ST])])]
	;	F2 = []
	),
	(	option_(grammar(G), Os) -> text_(G, Gs, Ctx), F3 = [string("grammar")-string(Gs)]
	;	F3 = []
	),
	append([F0, F1, F2, F3], Fields).

model_field_(Os, F) :-
	(	option_(model(M), Os) -> text_(M, Ms, model), F = [string("model")-string(Ms)]
	;	F = []
	).

sampling_option_(temperature, "temperature", number).
sampling_option_(top_p, "top_p", number).
sampling_option_(max_tokens, "max_tokens", integer).
sampling_option_(seed, "seed", integer).
sampling_option_(stop, "stop", texts).

field_(number, K, V, string(K)-number(V), Ctx) :- must_be_(number, V, Ctx).
field_(integer, K, V, string(K)-number(V), Ctx) :- must_be_(integer, V, Ctx).
field_(texts, K, V, string(K)-list(Ss), Ctx) :-
	must_be_(list, V, Ctx),
	findall(string(Cs), (member(T, V), text_(T, Cs, Ctx)), Ss).

% A schema as JSON text or as a json_chars//1 term

schema_(S, T, Ctx) :-
	(	var(S) -> throw(error(instantiation_error, Ctx))
	;	json_term_(S) -> T = S
	;	text_(S, Cs, Ctx), catch(phrase(json_chars(T0), Cs), _, fail) -> T = T0
	;	throw(error(domain_error(json_schema, S), Ctx))
	).

json_term_(pairs(_)).
json_term_(list(_)).
json_term_(boolean(_)).

% R, the reply to a GET (Body get) or a POST of the pairs in Body, as a json_chars//1 term

request_(Path, Body, Os, Ctx, R) :-
	base_url_(Os, Base),
	atom_concat(Base, Path, URL),
	(	option_(timeout(T), Os) -> true ; T = 300 ),
	headers_(Os, Hs),
	(	Body == get -> Post = []
	;	phrase(json_chars(pairs(Body)), Js),
		Post = [post(chars('application/json', Js))]
	),
	append([Post, Hs, [timeout(T), status_code(Code), to(chars)]], HOs),
	http_get(URL, Reply, HOs),
	(	between(200, 299, Code) -> true
	;	error_message_(Reply, Msg),
		throw(error(llm_error(Code, Msg), Ctx))
	),
	(	catch(phrase(json_chars(R), Reply), _, fail) -> true
	;	chars_string_(Reply, S),
		throw(error(llm_error(not_json, S), Ctx))
	),
	(	memberchk(response(R0), Os) -> R0 = R ; true ).

headers_(Os, Hs) :-
	(	option_(api_key(K), Os) -> true
	;	getenv('LLM_API_KEY', K) -> true
	;	K = none
	),
	(	K == none -> Hs = []
	;	text_chars_(K, Ks),
		append("Bearer ", Ks, Bs),
		atom_chars(B, Bs),
		Hs = [request_header('Authorization'=B)]
	).

% The message of an OpenAI-style error reply, or its whole text

error_message_(Reply, Msg) :-
	(	catch(phrase(json_chars(J), Reply), _, fail),
		(	path_(J, [error, message], string(Cs)) -> true
		;	path_(J, [error], string(Cs))
		)
	->	true
	;	Cs = Reply
	),
	chars_string_(Cs, Msg).

% The value at a path of keys and list indexes in a json_chars//1 term

path_(J, [], J).
path_(pairs(Ps), [K|Ks], V) :-
	atom(K),
	atom_chars(K, Cs),
	memberchk(string(Cs)-J, Ps),
	path_(J, Ks, V).
path_(list(Ls), [I|Ks], V) :-
	integer(I),
	nth0(I, Ls, J),
	path_(J, Ks, V).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

text_(T, Cs, Ctx) :-
	(	var(T) -> throw(error(instantiation_error, Ctx))
	;	text_chars_(T, Cs) -> true
	;	throw(error(type_error(text, T), Ctx))
	).

text_chars_(T, Cs) :- atom(T), !, atom_chars(T, Cs).
text_chars_(T, Cs) :- string(T), !, Cs = T.
text_chars_(T, Cs) :- is_list(T), maplist(atom, T), Cs = T.

chars_string_(Cs, S) :- format(string(S), "~s", [Cs]).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(integer, X, Ctx) :- !, (integer(X) -> true ; throw(error(type_error(integer, X), Ctx))).
must_be_(number, X, Ctx) :- !, (number(X) -> true ; throw(error(type_error(number, X), Ctx))).
must_be_(atom, X, Ctx) :- !, (atom(X) -> true ; throw(error(type_error(atom, X), Ctx))).
must_be_(compound, X, Ctx) :- !, (compound(X) -> true ; throw(error(type_error(compound, X), Ctx))).
must_be_(list, X, Ctx) :- (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).

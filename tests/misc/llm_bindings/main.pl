% library(llm) against a live OpenAI-style server: models, chat, completion, stop strings, grammars and JSON
% schemas, non-ASCII text both ways, the whole reply, embeddings, and errors from the server and from bad
% arguments. Run by ../llm_bindings.sh, which skips it where no server answers at $LLM_TEST_BASE_URL. Free text
% depends on the model, so only its type is checked; what is shown is fixed by a grammar or schema.

:- use_module(library(llm)).
:- use_module(library(lists)).
:- initialization(main).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> format("~w: ~q~n", [Label, Show]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

% An error by its status and the error term's shape, as the message is the server's own

e(Label, Goal) :-
	catch((Goal, R = succeeded), error(E, _), shape_(E, R)),
	format("~w: ~q~n", [Label, R]).

shape_(llm_error(Code, Msg), llm_error(Code, M)) :- !, (string(Msg) -> M = string ; M = Msg).
shape_(socket_error(E, _), socket_error(E)) :- !.
shape_(E, E).

main :-
	getenv('LLM_TEST_BASE_URL', URL),
	llm_set_options([base_url(URL), temperature(0), seed(1), max_tokens(16)]),
	t(models, (llm_models(Ms, []), Ms = [M|_], atom(M)), yes),
	t(ask, (llm_ask("Say hello.", R1), string(R1)), string),
	t(chat, (llm_chat([system-'Be brief.', user-"Is the sky blue?", assistant-"Yes.", user-'And grass?'], R2, []), string(R2)), string),
	t(complete, (llm_complete("Once upon a time", R3, [max_tokens(8)]), string(R3)), string),
	t(stop, (llm_ask("Spell abc.", R4, [grammar('root ::= "a, b, c"'), stop([", c"])]), string(R4)), R4),
	t(grammar, (llm_ask("Pick a colour.", R5, [grammar('root ::= "red" | "green" | "blue"')]), memberchk(R5, ["red", "green", "blue"])), one_of_three),
	t(utf8, llm_ask('Répète «héllo»', R6, [grammar('root ::= "héllo wörld ✓ 日本"'), max_tokens(40)]), R6),
	t(json, (llm_json("Ann is 30.", '{"type":"object","properties":{"name":{"type":"string","enum":["Ann"]},"age":{"type":"integer","enum":[30]}},"required":["name","age"]}', J, [max_tokens(40)])), J),
	t(json_term, (llm_json("Yes or no?", pairs([string("type")-string("boolean"), string("enum")-list([boolean(true)])]), J2, [])), J2),
	t(response, (llm_ask("Hi.", _, [response(pairs(Ps))]), memberchk(string("choices")-list(_), Ps)), yes),
	(   getenv('LLM_TEST_EMBED_URL', EURL)
	->  t(embedding, (llm_embedding("hello world", V, [base_url(EURL)]), V = [F|_], float(F), length(V, N), N > 0), floats)
	;   format("embedding: floats~n")
	),
	atom_concat(URL, '/nosuch', Bad),
	e(not_found, llm_ask(hi, _, [base_url(Bad)])),
	e(refused, llm_ask(hi, _, [base_url('http://127.0.0.1:1/v1'), timeout(5)])),
	e(bad_number, llm_ask(hi, _, [temperature(hot)])),
	e(bad_integer, llm_ask(hi, _, [max_tokens(1.5)])),
	e(bad_role, llm_chat([wizard-hi], _, [])),
	e(not_message, llm_chat([hi], _, [])),
	e(bad_text, llm_ask(f(x), _)),
	e(unbound, llm_ask(_, _)),
	e(bad_schema, llm_json(hi, 'not json', _, [])),
	e(bad_options, llm_set_options(nope)).

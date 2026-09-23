:- use_module(library(http/http_server)).

:- http_handler(root(.), home, []).

home(Request) :-
	format("Content-type: text/html~n~n"),
	format("<html><body><h1>Home</h1><pre>~q</pre></body></html>~n", [Request]).

main :-
	http_server(http_dispatch, [port(8080)]).

:- module(http_server, [
	http_server/2, http_stop_server/2,
	http_handler/3, http_delete_handler/1, http_current_handler/2,
	http_dispatch/1, http_redirect/3, http_404/2,
	http_read_data/3
	]).

/** The HTTP server in one module, after SWI-Prolog's
library(http/http_server): library(http/thread_httpd),
library(http/http_dispatch) and http_read_data/3.
*/

:- use_module(library(http/thread_httpd), []).
:- use_module(library(http/http_dispatch), []).
:- use_module(library(http/http_client), []).

:- meta_predicate(http_server(1, +)).
:- meta_predicate(http_handler(+, :, +)).

http_server(Goal, Options) :- thread_httpd:http_server(Goal, Options).
http_stop_server(Port, Options) :- thread_httpd:http_stop_server(Port, Options).
http_handler(Path, Closure, Options) :- http_dispatch:http_handler(Path, Closure, Options).
http_delete_handler(Path) :- http_dispatch:http_delete_handler(Path).
http_current_handler(Path, Closure) :- http_dispatch:http_current_handler(Path, Closure).
http_dispatch(Request) :- http_dispatch:http_dispatch(Request).
http_redirect(How, To, Request) :- http_dispatch:http_redirect(How, To, Request).
http_404(Options, Request) :- http_dispatch:http_404(Options, Request).
http_read_data(Request, Data, Options) :- http_client:http_read_data(Request, Data, Options).

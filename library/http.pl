:- module(http, [
	http_open/3,
	http_get/3, http_post/4, http_put/4, http_patch/4, http_delete/3,
	http_read_data/3,
	http_server/2, http_stop_server/2,
	http_handler/3, http_delete_handler/1, http_current_handler/2,
	http_dispatch/1, http_redirect/3, http_404/2
	]).

/** The HTTP client and server in one module. See library(http/http_open),
library(http/http_client), library(http/thread_httpd) and
library(http/http_dispatch), which follow SWI-Prolog's.
*/

:- use_module(library(http/http_open), []).
:- use_module(library(http/http_client), []).
:- use_module(library(http/thread_httpd), []).
:- use_module(library(http/http_dispatch), []).

:- meta_predicate(http_server(1, +)).
:- meta_predicate(http_handler(+, :, +)).

http_open(URL, Stream, Options) :- http_open:http_open(URL, Stream, Options).
http_get(URL, Data, Options) :- http_client:http_get(URL, Data, Options).
http_post(URL, Data, Reply, Options) :- http_client:http_post(URL, Data, Reply, Options).
http_put(URL, Data, Reply, Options) :- http_client:http_put(URL, Data, Reply, Options).
http_patch(URL, Data, Reply, Options) :- http_client:http_patch(URL, Data, Reply, Options).
http_delete(URL, Data, Options) :- http_client:http_delete(URL, Data, Options).
http_read_data(Request, Data, Options) :- http_client:http_read_data(Request, Data, Options).
http_server(Goal, Options) :- thread_httpd:http_server(Goal, Options).
http_stop_server(Port, Options) :- thread_httpd:http_stop_server(Port, Options).
http_handler(Path, Closure, Options) :- http_dispatch:http_handler(Path, Closure, Options).
http_delete_handler(Path) :- http_dispatch:http_delete_handler(Path).
http_current_handler(Path, Closure) :- http_dispatch:http_current_handler(Path, Closure).
http_dispatch(Request) :- http_dispatch:http_dispatch(Request).
http_redirect(How, To, Request) :- http_dispatch:http_redirect(How, To, Request).
http_404(Options, Request) :- http_dispatch:http_404(Options, Request).

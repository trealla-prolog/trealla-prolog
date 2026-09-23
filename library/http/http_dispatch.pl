:- module(http_dispatch, [
	http_handler/3, http_delete_handler/1, http_current_handler/2,
	http_dispatch/1, http_redirect/3, http_404/2
	]).

/** Handler dispatch, after SWI-Prolog's library(http/http_dispatch).

http_handler(+Path, :Closure, +Options) registers Closure for Path, an
absolute path such as '/api/items' or root(Sub), root(.) being '/'. It
is called as call(Closure, Request). Options:

  * prefix: also handle any path below Path.
  * method(M), methods(Ms): the methods allowed, others get a 405.
  * id(Id): recorded, not otherwise used.

A path gets the handler registered for it, or failing that the prefix
handler with the longest path above it, or a 404.

Serve with http_server(http_dispatch, [port(Port)]).
*/

:- use_module(library(lists)).

:- meta_predicate(http_handler(+, :, +)).

:- dynamic('$http_handler'/4).

http_handler(Spec, Closure, Options) :-
	path_spec(Spec, Path),
	retractall('$http_handler'(Path, _, _, _)),
	(memberchk(prefix, Options) -> Prefix = true ; Prefix = false),
	assertz('$http_handler'(Path, Closure, Prefix, Options)).

http_delete_handler(Spec) :-
	path_spec(Spec, Path),
	retractall('$http_handler'(Path, _, _, _)).

http_current_handler(Path, Closure) :-
	'$http_handler'(Path, Closure, _, _).

path_spec(Spec, _) :-
	var(Spec),
	!,
	throw(error(instantiation_error, http_handler/3)).
path_spec(root(Sub), Path) :-
	!,
	(	Sub == '.' -> Path = "/"
	;	sub_path(Sub, Cs),
		Path = ['/'|Cs]
	).
path_spec(Spec, Path) :-
	(atom(Spec) -> atom_chars(Spec, Path) ; Path = Spec),
	(	Path = ['/'|_] -> true
	;	throw(error(domain_error(http_path, Spec), http_handler/3))
	).

sub_path(A/B, Cs) :-
	!,
	sub_path(A, As),
	sub_path(B, Bs),
	append(As, ['/'|Bs], Cs).
sub_path(A, Cs) :-
	(atom(A) -> atom_chars(A, Cs) ; Cs = A).

http_dispatch(Request) :-
	memberchk(path(Path), Request),
	(	find_handler(Path, Closure, Options) ->
		check_method(Request, Path, Options),
		call(Closure, Request)
	;	throw(http_reply(not_found(Path)))
	).

find_handler(Path, Closure, Options) :-
	'$http_handler'(Path, Closure, _, Options),
	!.
find_handler(Path, Closure, Options) :-
	findall(L-(C-O),
		(	'$http_handler'(P, C, true, O),
			prefix_of(P, Path),
			length(P, L)
		),
		Matches),
	Matches \== [],
	keysort(Matches, Sorted),
	last(Sorted, _-(Closure-Options)).

prefix_of(Prefix, Path) :-
	(	append(Prefix, _, Path) -> true
	;	append(Path, "/", Prefix)
	).

check_method(Request, Path, Options) :-
	memberchk(method(M), Request),
	(	memberchk(methods(Ms), Options) -> true
	;	memberchk(method(M1), Options) -> Ms = [M1]
	;	Ms = any
	),
	(	(Ms == any ; memberchk(M, Ms) ; M == head, memberchk(get, Ms)) -> true
	;	throw(http_reply(method_not_allowed(M, Path)))
	).

%% http_redirect(+How, +To, +Request)
%
% How is moved, moved_temporary or see_other. To is a URL or root(Sub).

http_redirect(How, To, _Request) :-
	(	To = root(_) -> path_spec(To, URL)
	;	URL = To
	),
	Reply =.. [How, URL],
	throw(http_reply(Reply)).

http_404(_Options, Request) :-
	memberchk(path(Path), Request),
	throw(http_reply(not_found(Path))).

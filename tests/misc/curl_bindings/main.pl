% library(curl): a file:// URL fetched into a file, its easy handle typed, and one cleaned up raising rather than
% reaching freed memory. Run by ../curl_bindings.sh, which skips it where libcurl or the FFI is missing. The
% files are kept in '.', and no path is printed.

:- use_module(library(curl)).
:- initialization(main).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> format("~w: ~q~n", [Label, Show]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

% A handle error by its formal, type and context, as its culprit holds an address

handle_error(Goal, Shape) :-
	catch((Goal, Shape = no_error), error(E, Ctx), (E =.. [F, T|_], Shape = error(F, T, Ctx))).

fetch(Url, To, Res) :-
	curlopt(xCURLOPT_WRITEDATA, _, WriteData),
	curlopt(xCURLOPT_URL, _, UrlOpt),
	curl_easy_init(C),
	setup_call_cleanup(open(To, write, S),
		(	stream_property(S, file(F)),
			curl_easy_setopt(C, WriteData, F, _),
			curl_easy_setopt(C, UrlOpt, Url, _),
			curl_easy_perform(C, Res)
		),
		close(S)),
	curl_easy_cleanup(C).

% A file:// URL for a file in '.', with / for Windows' \ and a third / before a drive letter

file_url(File, Url) :-
	working_directory(D0, D0),
	atom_chars(D1, D0),
	atomic_list_concat(Parts, '\\', D1),
	atomic_list_concat(Parts, '/', D),
	(	sub_atom(D, 0, 1, _, '/') -> Scheme = 'file://' ; Scheme = 'file:///' ),
	atomic_list_concat([Scheme, D, File], Url).

main :-
	In = 'curl_bindings_in.tmp', Out = 'curl_bindings_out.tmp',
	setup_call_cleanup(open(In, write, S), write(S, 'hello from curl'), close(S)),
	file_url(In, Url),
	t(fetch, (fetch(Url, Out, Res), read_file_to_string(Out, Got, [])), Res-Got),
	t(handle_type, (curl_easy_init(C1), foreign_handle(C1, T1), curl_easy_cleanup(C1)), T1),
	t(cleaned_up, (curl_easy_init(C2), curl_easy_cleanup(C2), handle_error(curl_easy_perform(C2, _), H1)), H1),
	t(cleanup_twice, (curl_easy_init(C3), curl_easy_cleanup(C3), handle_error(curl_easy_cleanup(C3), H2)), H2),
	t(not_handle, handle_error(curl_easy_perform(foo, _), H3), H3),
	delete_file(In), delete_file(Out).

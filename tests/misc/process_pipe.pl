% process_create/3's pipe(Stream) and pipe(Stream, StreamOptions)
% sub-options, for stdin/stdout/stderr. Needs real subprocesses, so this
% test belongs in tests/misc.
%
% Issue #1153: none of the three pipe(Stream) cases closed the *other*
% end of the pipe in the child, and posix_spawn() inherits every
% non-CLOEXEC fd. A stdin(pipe(In)) child ended up holding its own
% leaked copy of the write end, so it never saw EOF - even after the
% parent closed its own copy of In - and any child that reads stdin to
% completion (`cat`, a filter, ...) hung forever. stdin_pipe_eof and
% stdin_stdout_roundtrip below are exactly that scenario.
%
% pipe/2's StreamOptions accepts type(+Type) and encoding(+Encoding),
% matching what SWI-Prolog documents for SICStus compatibility - verified
% directly against swipl while adding this. Both variants share the same
% underlying fd-wiring helper in src/bif_os.c, so pipe_2_roundtrip below
% re-covers the #1153 deadlock through the pipe/2 spelling too.
%
% Every variable below is numbered rather than reused across t/2
% calls: they all share the one main/0 clause, so a name reused across
% calls would be the same variable throughout - already bound by the
% earlier test - not a fresh one.

:- initialization(main).

:- dynamic(saw_failure/0).

t(L, G) :-
	(  catch(G, E, (R = err(E)))
	-> (var(R) -> R = ok ; true)
	;  R = failed
	),
	(  R == ok
	-> true
	;  format("PROCESS_PIPE-FAIL ~w: ~q~n", [L, R]),
	   (  saw_failure -> true ; assertz(saw_failure) )
	).

main :-
	% --- stdout(pipe(_)) alone: read what the child writes.
	t(stdout_pipe,
	  ( process_create(echo, ['hello from pipe'], [stdout(pipe(Out1)), process(Pid1)]),
	    read_line_to_string(Out1, Line1),
	    close(Out1),
	    process_wait(Pid1, exit(0)),
	    Line1 == "hello from pipe"
	  )),

	% --- stderr(pipe(_)) kept separate from stdout(pipe(_)).
	t(stderr_pipe,
	  ( process_create(sh, ['-c', 'echo out-line; echo err-line 1>&2'],
	                    [stdout(pipe(Out2)), stderr(pipe(Err2)), process(Pid2)]),
	    read_line_to_string(Out2, OutLine2),
	    read_line_to_string(Err2, ErrLine2),
	    close(Out2), close(Err2),
	    process_wait(Pid2, exit(0)),
	    OutLine2 == "out-line",
	    ErrLine2 == "err-line"
	  )),

	% --- stdin(pipe(_)) alone, feeding a filter that must see EOF to
	% finish. Under #1153 this hung forever: close(In3) closed only the
	% parent's copy of the write end, and `cat` held its own leaked one.
	t(stdin_pipe_eof,
	  ( process_create(cat, [], [stdin(pipe(In3)), stdout(null), process(Pid3)]),
	    write(In3, one), nl(In3),
	    write(In3, two), nl(In3),
	    close(In3),
	    process_wait(Pid3, exit(0))
	  )),

	% --- stdin(pipe(_)) and stdout(pipe(_)) together, round-tripping
	% data through a filter. Same deadlock as above, plus checks the
	% transformed output comes back correctly.
	t(stdin_stdout_roundtrip,
	  ( process_create(tr, ['a-z', 'A-Z'],
	                    [stdin(pipe(In4)), stdout(pipe(Out4)), process(Pid4)]),
	    write(In4, 'round trip via stdin pipe'), nl(In4),
	    close(In4),
	    read_line_to_string(Out4, Line4),
	    close(Out4),
	    process_wait(Pid4, exit(0)),
	    Line4 == "ROUND TRIP VIA STDIN PIPE"
	  )),

	% --- all three piped at once.
	t(stdin_stdout_stderr,
	  ( process_create(sh, ['-c', 'cat; echo done 1>&2'],
	                    [stdin(pipe(In5)), stdout(pipe(Out5)), stderr(pipe(Err5)), process(Pid5)]),
	    write(In5, 'via all three pipes'), nl(In5),
	    close(In5),
	    read_line_to_string(Out5, OutLine5),
	    read_line_to_string(Err5, ErrLine5),
	    close(Out5), close(Err5),
	    process_wait(Pid5, exit(0)),
	    OutLine5 == "via all three pipes",
	    ErrLine5 == "done"
	  )),

	% --- pipe(Stream, StreamOptions): type(text), the default made explicit.
	t(pipe_2_type_text,
	  ( process_create(echo, ['pipe2 text'], [stdout(pipe(Out6, [type(text)])), process(Pid6)]),
	    read_line_to_string(Out6, Line6),
	    close(Out6),
	    process_wait(Pid6, exit(0)),
	    Line6 == "pipe2 text"
	  )),

	% --- pipe(Stream, StreamOptions): type(binary) is accepted and still
	% round-trips plain data correctly.
	t(pipe_2_type_binary,
	  ( process_create(echo, ['pipe2 binary'], [stdout(pipe(Out7, [type(binary)])), process(Pid7)]),
	    read_line_to_string(Out7, Line7),
	    close(Out7),
	    process_wait(Pid7, exit(0)),
	    Line7 == "pipe2 binary"
	  )),

	% --- pipe(Stream, StreamOptions): encoding(_) is accepted (Trealla is
	% UTF-8 throughout, so it has no separate effect - see open/4's own
	% encoding option) rather than rejected as an unknown option.
	t(pipe_2_encoding,
	  ( process_create(echo, ['pipe2 encoding'], [stdout(pipe(Out8, [encoding(utf8)])), process(Pid8)]),
	    read_line_to_string(Out8, Line8),
	    close(Out8),
	    process_wait(Pid8, exit(0)),
	    Line8 == "pipe2 encoding"
	  )),

	% --- an unrecognised StreamOptions entry is a domain_error, not a
	% silently-ignored option or (as a prior version of this code did) a
	% swallowed error that let process_create carry on regardless.
	t(pipe_2_bad_option,
	  ( catch(process_create(echo, [x], [stdout(pipe(_Out9, [bogus(1)]))]),
	          error(domain_error(stream_option, bogus(1)), _),
	          true)
	  )),

	% --- likewise an unrecognised type(_) value.
	t(pipe_2_bad_type,
	  ( catch(process_create(echo, [x], [stdout(pipe(_Out10, [type(weird)]))]),
	          error(domain_error(stream_option, type(weird)), _),
	          true)
	  )),

	% --- stdin(pipe(_, _)) and stdout(pipe(_, _)) together via the
	% pipe/2 spelling: the same #1153 deadlock scenario as
	% stdin_stdout_roundtrip above, but exercising the arity-2 path on
	% both ends at once.
	t(pipe_2_roundtrip,
	  ( process_create(tr, ['a-z', 'A-Z'],
	                    [stdin(pipe(In11, [type(text)])), stdout(pipe(Out11, [type(text)])), process(Pid11)]),
	    write(In11, 'round trip via pipe2'), nl(In11),
	    close(In11),
	    read_line_to_string(Out11, Line11),
	    close(Out11),
	    process_wait(Pid11, exit(0)),
	    Line11 == "ROUND TRIP VIA PIPE2"
	  )),

	(  saw_failure
	-> format("process_pipe: FAILURES above~n")
	;  format("process_pipe: all ok~n")
	).

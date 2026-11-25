% library(filesex). Builds a scratch tree under the working directory,
% exercises it, and removes it again.
%
% The path predicates are purely syntactic, so those checks use paths
% that never exist. Everything else runs against real files.

:- use_module(library(filesex)).
:- initialization(main).

:- dynamic(saw_failure/0).

t(L, G) :-
	(  catch(G, E, (R = err(E)))
	-> (var(R) -> R = ok ; true)
	;  R = failed
	),
	(  R == ok
	-> true
	;  format("FILESEX-FAIL ~w: ~q~n", [L, R]),
	   (  saw_failure -> true ; assertz(saw_failure) )
	).

base('tmp_filesex_test').

mkfile(Path, Text) :-
	open(Path, write, S),
	write(S, Text),
	nl(S),
	close(S).

main :-
	base(B),
	(  exists_directory(B) -> delete_directory_and_contents(B) ; true ),

	% --- path arithmetic, SWI's own documented examples
	t(rel_doc1, (relative_file_name('/home/janw/nice', '/home/janw/deep/dir/file', P1),
	             P1 == '../../nice')),
	t(rel_doc2, (relative_file_name(P2, '/home/janw/deep/dir/file', '../../nice'),
	             P2 == '/home/janw/nice')),
	t(rel_dir,  (relative_file_name('/home/janw/deep/dir/file', '/home/janw/', P3),
	             P3 == 'deep/dir/file')),
	t(rel_self, (relative_file_name('/a/b/c', '/a/b/c', P4), P4 == c)),
	t(dfp_join,  (directory_file_path('/a/b', 'c.txt', Q1), Q1 == '/a/b/c.txt')),
	t(dfp_slash, (directory_file_path('/a/b/', 'c.txt', Q2), Q2 == '/a/b/c.txt')),
	t(dfp_abs,   (directory_file_path('/a/b', '/x/y', Q3), Q3 == '/x/y')),
	t(dfp_split, (directory_file_path(D1, F1, '/a/b/c.txt'), D1 == '/a/b', F1 == 'c.txt')),
	t(dfp_root,  (directory_file_path(D2, F2, '/top'), D2 == '/', F2 == top)),

	% --- build a tree
	t(mkpath,    (make_directory_path('tmp_filesex_test/src/deep'), exists_directory(B))),
	t(ensure,    (ensure_directory('tmp_filesex_test/empty'),
	              exists_directory('tmp_filesex_test/empty'))),
	t(ensure_idem, ensure_directory('tmp_filesex_test/empty')),
	% ensure_directory creates at most one level, unlike make_directory_path
	t(ensure_one_level,
	              (catch(ensure_directory('tmp_filesex_test/no/deeper'), _, fail) -> fail ; true)),
	t(files,     (mkfile('tmp_filesex_test/a.txt', alpha),
	              mkfile('tmp_filesex_test/b.md', beta),
	              mkfile('tmp_filesex_test/src/c.txt', gamma),
	              mkfile('tmp_filesex_test/src/deep/d.txt', delta),
	              exists_file('tmp_filesex_test/a.txt'))),

	% --- links
	t(link_sym,  (link_file('tmp_filesex_test/a.txt', 'tmp_filesex_test/sym.txt', symbolic),
	              posix_file_type('tmp_filesex_test/sym.txt', T1), T1 == symlink)),
	t(link_hard, (link_file('tmp_filesex_test/a.txt', 'tmp_filesex_test/hard.txt', hard),
	              posix_file_type('tmp_filesex_test/hard.txt', T2), T2 == regular)),
	t(link_type_err, catch(link_file('tmp_filesex_test/a.txt', 'tmp_filesex_test/z', weird),
	              error(domain_error(link_type, weird), _), true)),

	% --- directory_member
	t(dm_flat,   (findall(N, (directory_member(B, M1, []),
	                          directory_file_path(_, N, M1)), L1),
	              msort(L1, S1), memberchk('a.txt', S1), \+ memberchk('c.txt', S1))),
	t(dm_glob,   (findall(N, (directory_member(B, M2, [matches('*.md')]),
	                          directory_file_path(_, N, M2)), L2),
	              L2 == ['b.md'])),
	t(dm_exclude,(findall(N, (directory_member(B, M3, [exclude('*.txt')]),
	                          directory_file_path(_, N, M3)), L3),
	              \+ memberchk('a.txt', L3))),
	t(dm_ext,    (findall(N, (directory_member(B, M4, [extensions(['.md'])]),
	                          directory_file_path(_, N, M4)), L4),
	              L4 == ['b.md'])),
	t(dm_type,   (findall(N, (directory_member(B, M5, [file_type(directory)]),
	                          directory_file_path(_, N, M5)), L5),
	              msort(L5, S5), memberchk(src, S5), \+ memberchk('a.txt', S5))),
	t(dm_rec,    (findall(N, (directory_member(B, M6, [recursive(true), matches('d.txt')]),
	                          directory_file_path(_, N, M6)), L6),
	              L6 == ['d.txt'])),
	t(dm_hidden, (mkfile('tmp_filesex_test/.dot', hidden),
	              findall(N, (directory_member(B, M7, []),
	                          directory_file_path(_, N, M7)), L7),
	              \+ memberchk('.dot', L7),
	              findall(N, (directory_member(B, M8, [hidden(true)]),
	                          directory_file_path(_, N, M8)), L8),
	              memberchk('.dot', L8))),
	t(dm_missing, catch(directory_member('tmp_filesex_test/nope', _, []),
	              error(existence_error(directory, _), _), true)),

	% A link back to an ancestor must not send the walk round in circles.
	t(dm_cycle,  (link_file('tmp_filesex_test', 'tmp_filesex_test/src/deep/loop', symbolic),
	              findall(M9, directory_member(B, M9, [recursive(true), follow_links(true)]), L9),
	              length(L9, N9), N9 < 100,
	              posix_unlink('tmp_filesex_test/src/deep/loop'))),

	% --- copy
	t(copy_dir,  (copy_directory('tmp_filesex_test/src', 'tmp_filesex_test/copy'),
	              exists_file('tmp_filesex_test/copy/c.txt'),
	              exists_file('tmp_filesex_test/copy/deep/d.txt'))),
	t(copy_link_kept, (copy_directory(B, 'tmp_filesex_test2'),
	              posix_file_type('tmp_filesex_test2/sym.txt', T3), T3 == symlink)),

	% --- modes
	t(chmod_int, (chmod('tmp_filesex_test/a.txt', 0o640),
	              posix_file_mode('tmp_filesex_test/a.txt', M10), M10 =:= 0o640)),
	t(chmod_sym, (chmod('tmp_filesex_test/a.txt', ugor),
	              posix_file_mode('tmp_filesex_test/a.txt', M11), M11 =:= 0o444)),
	t(chmod_add, (chmod('tmp_filesex_test/a.txt', 0o600),
	              chmod('tmp_filesex_test/a.txt', +(0o060)),
	              posix_file_mode('tmp_filesex_test/a.txt', M12), M12 =:= 0o660)),
	t(chmod_sub, (chmod('tmp_filesex_test/a.txt', -(0o060)),
	              posix_file_mode('tmp_filesex_test/a.txt', M13), M13 =:= 0o600)),
	t(chmod_bad, catch(chmod('tmp_filesex_test/a.txt', nonsense),
	              error(domain_error(file_mode, _), _), true)),

	% --- times
	t(set_time,  (set_time_file('tmp_filesex_test/a.txt', _, [modified(1234567.0)]),
	              set_time_file('tmp_filesex_test/a.txt', [modified(M14)], []),
	              M14 =:= 1234567.0)),
	t(read_times,(set_time_file('tmp_filesex_test/a.txt', [access(A1), modified(M15), changed(C1)], []),
	              float(A1), float(M15), float(C1))),
	t(time_now,  (set_time_file('tmp_filesex_test/a.txt', _, [modified(now)]),
	              set_time_file('tmp_filesex_test/a.txt', [modified(M16)], []),
	              M16 > 1234567.0)),
	t(time_changed_err, catch(set_time_file('tmp_filesex_test/a.txt', _, [changed(1.0)]),
	              error(permission_error(set, file_time, changed), _), true)),

	% --- names given as strings, as SWI takes any text; a number is still refused
	t(str_ensure, (ensure_directory("tmp_filesex_test/strdir"), exists_directory('tmp_filesex_test/strdir'))),
	t(str_copy,  (copy_directory("tmp_filesex_test/strdir", "tmp_filesex_test/strcopy"),
	              exists_directory('tmp_filesex_test/strcopy'))),
	t(str_link,  (link_file("tmp_filesex_test/a.txt", "tmp_filesex_test/strsym", symbolic),
	              posix_file_type('tmp_filesex_test/strsym', T4), T4 == symlink)),
	t(str_chmod, (chmod("tmp_filesex_test/a.txt", 0o640),
	              posix_file_mode('tmp_filesex_test/a.txt', M17), M17 =:= 0o640)),
	t(str_time,  (set_time_file("tmp_filesex_test/a.txt", [modified(M18)], []), float(M18))),
	t(str_contents, (delete_directory_contents("tmp_filesex_test/strcopy"),
	              exists_directory('tmp_filesex_test/strcopy'))),
	t(num_refused, catch(ensure_directory(42), error(type_error(atom, 42), _), true)),

	% --- delete. A dangling link is removed, not chased: delete_file/1
	% refuses one, so the recursive delete must unlink instead.
	t(delete_dangling, (make_directory('tmp_filesex_test/dang'),
	              link_file('no_such_target_at_all', 'tmp_filesex_test/dang/broken', symbolic),
	              delete_directory_and_contents('tmp_filesex_test/dang'),
	              \+ exists_directory('tmp_filesex_test/dang'))),
	t(del_contents, (delete_directory_contents('tmp_filesex_test/copy'),
	              exists_directory('tmp_filesex_test/copy'),
	              directory_files('tmp_filesex_test/copy', DF), msort(DF, ['.','..']))),
	t(del_all,   (delete_directory_and_contents(B), \+ exists_directory(B))),
	t(del_all2,  (delete_directory_and_contents('tmp_filesex_test2'),
	              \+ exists_directory('tmp_filesex_test2'))),
	t(del_missing, catch(delete_directory_and_contents('tmp_filesex_test'),
	              error(existence_error(directory, _), _), true)),

	(  saw_failure
	-> format("filesex: FAILURES above~n")
	;  format("filesex: all ok~n")
	).

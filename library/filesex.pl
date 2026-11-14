/*  library(filesex) - the SWI-compatible filesystem extensions.

	Emulates https://www.swi-prolog.org/pldoc/man?section=filesex.

	Split of labour: everything that is only path arithmetic -
	directory_file_path/3, relative_file_name/3 - is done here in Prolog
	and is purely syntactic, so it works on paths that do not exist. Real
	filesystem operations go through the posix_ builtins, which are thin
	wrappers over the matching POSIX calls.

	Notes:

	  * copy_file/2 and make_directory_path/1 are already Trealla
	    builtins and are not re-exported here; they are global, so code
	    written for SWI still finds them. One difference worth knowing:
	    SWI's copy_file/2 accepts a directory as its target, Trealla's
	    raises a permission_error. copy_directory/2 below does its own
	    target handling and is unaffected.

	  * The recursive delete and copy operate on symbolic links
	    themselves rather than their targets, as SWI documents. A link to
	    a directory is removed, not descended into and emptied.

	  * The posix_ layer is POSIX only. On Windows and WASI the delete,
	    copy and walk predicates fall back to the portable builtins and
	    treat every entry as a plain file or directory, so symlinks are
	    not recognised there. link_file/3, chmod/2 and set_time_file/3
	    raise resource_error(posix_files_unavailable). The path
	    predicates work everywhere and accept both / and \ as separators.

	  * chmod/2 takes an integer, a symbolic mode, or +Mode / -Mode to
	    add or revoke. set_time_file/3 cannot set changed(_) - the kernel
	    owns that - and rejects it rather than pretending.
*/

:- module(filesex, [
	directory_file_path/3,
	relative_file_name/3,
	ensure_directory/1,
	copy_directory/2,
	delete_directory_and_contents/1,
	delete_directory_contents/1,
	directory_member/3,
	link_file/3,
	chmod/2,
	set_time_file/3
	]).

:- use_module(library(error)).
:- use_module(library(lists)).

% --- path arithmetic ----------------------------------------------------
%
% Purely syntactic: no predicate below this comment touches the disk.

'$fx_sep'('/').
'$fx_sep'('\\').

'$fx_chars'(X, Cs) :- atom(X), !, atom_chars(X, Cs).
'$fx_chars'(X, Cs) :- is_list(X), !, Cs = X.
'$fx_chars'(X, _) :- throw(error(type_error(atom, X), filesex)).

% Splits on either separator; empty segments are kept, since a leading
% empty one is what marks an absolute path and a trailing one marks a
% directory.

'$fx_split'(Path, Parts) :-
	'$fx_chars'(Path, Cs),
	'$fx_split_'(Cs, [], Parts).

'$fx_split_'([], Acc, [P]) :-
	!,
	'$fx_rev_atom'(Acc, P).
'$fx_split_'([C|T], Acc, [P|Ps]) :-
	'$fx_sep'(C),
	!,
	'$fx_rev_atom'(Acc, P),
	'$fx_split_'(T, [], Ps).
'$fx_split_'([C|T], Acc, Ps) :-
	'$fx_split_'(T, [C|Acc], Ps).

'$fx_rev_atom'(Acc, A) :-
	reverse(Acc, Cs),
	atom_chars(A, Cs).

'$fx_absolute'(Path) :-
	'$fx_chars'(Path, [C|_]),
	'$fx_sep'(C).

'$fx_ends_sep'(Path) :-
	'$fx_chars'(Path, Cs),
	last(Cs, C),
	'$fx_sep'(C).

%% directory_file_path(?Directory, ?File, ?Path).
%
% Path is File within Directory, with exactly one separator between them.
% An absolute File is its own Path. With Path bound it splits instead.

directory_file_path(Dir, File, Path) :-
	nonvar(Dir), nonvar(File),
	!,
	(  File == ''
	-> Path = Dir
	;  '$fx_absolute'(File)
	-> Path = File
	;  Dir == ''
	-> Path = File
	;  '$fx_ends_sep'(Dir)
	-> atom_concat(Dir, File, Path)
	;  atomic_list_concat([Dir, '/', File], Path)
	).
directory_file_path(Dir, File, Path) :-
	nonvar(Path),
	!,
	'$fx_split'(Path, Parts),
	append(DirParts, [File], Parts),
	(  DirParts == []
	-> Dir = ''
	;  DirParts == ['']
	-> Dir = ('/')
	;  atomic_list_concat(DirParts, '/', Dir)
	).
directory_file_path(_, _, _) :-
	throw(error(instantiation_error, directory_file_path/3)).

%% relative_file_name(?Path, +RelToFile, ?RelPath).
%
% RelPath is Path expressed relative to RelToFile. As SWI has it the
% second argument names a *file*, so its last segment is dropped; give it
% a trailing separator to mean the directory itself.

relative_file_name(Path, RelTo, RelPath) :-
	nonvar(Path), nonvar(RelTo),
	!,
	'$fx_abs_segs'(Path, PSegs),
	'$fx_reldir_segs'(RelTo, RSegs),
	'$fx_drop_common'(PSegs, RSegs, P1, R1),
	length(R1, N),
	'$fx_ups'(N, Ups),
	append(Ups, P1, All),
	(  All == []
	-> RelPath = '.'
	;  atomic_list_concat(All, '/', RelPath)
	).
relative_file_name(Path, RelTo, RelPath) :-
	nonvar(RelTo), nonvar(RelPath),
	!,
	'$fx_reldir_segs'(RelTo, RSegs),
	(  RSegs == []
	-> Base = ('/')
	;  atomic_list_concat(RSegs, '/', Base0),
	   atom_concat('/', Base0, Base)
	),
	directory_file_path(Base, RelPath, Joined),
	absolute_file_name(Joined, Path, []).
relative_file_name(_, _, _) :-
	throw(error(instantiation_error, relative_file_name/3)).

% Absolute, with '.' and '..' resolved, as a list of non-empty segments.

'$fx_abs_segs'(Path, Segs) :-
	absolute_file_name(Path, Abs, []),
	'$fx_split'(Abs, Parts0),
	exclude(==(''), Parts0, Segs).

% The segments of the directory the second argument denotes.

'$fx_reldir_segs'(RelTo, Segs) :-
	(  '$fx_ends_sep'(RelTo)
	-> '$fx_abs_segs'(RelTo, Segs)
	;  '$fx_abs_segs'(RelTo, Segs0),
	   (  append(Segs, [_], Segs0)
	   -> true
	   ;  Segs = []
	   )
	).

'$fx_drop_common'([H|T1], [H|T2], P, R) :-
	!,
	'$fx_drop_common'(T1, T2, P, R).
'$fx_drop_common'(P, R, P, R).

'$fx_ups'(0, []) :- !.
'$fx_ups'(N, ['..'|T]) :-
	N > 0,
	N1 is N - 1,
	'$fx_ups'(N1, T).

% --- directories --------------------------------------------------------

%% ensure_directory(+Dir).
%
% Creates at most one directory, unlike make_directory_path/1.

ensure_directory(Dir) :-
	must_be(atom, Dir),
	(  exists_directory(Dir)
	-> true
	;  make_directory(Dir)
	).

% The entries of Dir, without the '.' and '..' that directory_files/2
% includes - descending into those would not terminate.

'$fx_entries'(Dir, Entries) :-
	directory_files(Dir, All),
	exclude('$fx_dot_entry', All, Entries).

'$fx_dot_entry'('.').
'$fx_dot_entry'('..').

%% copy_directory(+From, +To).
%
% Copies the contents of From into To recursively, creating To if
% needed. Symbolic links are recreated as links rather than followed.

copy_directory(From, To) :-
	must_be(atom, From),
	must_be(atom, To),
	(  exists_directory(From)
	-> true
	;  throw(error(existence_error(directory, From), copy_directory/2))
	),
	make_directory_path(To),
	'$fx_entries'(From, Entries),
	'$fx_copy_entries'(Entries, From, To).

'$fx_copy_entries'([], _, _).
'$fx_copy_entries'([E|Es], From, To) :-
	directory_file_path(From, E, Src),
	directory_file_path(To, E, Dst),
	'$fx_file_type'(Src, Type),
	'$fx_copy_one'(Type, Src, Dst),
	'$fx_copy_entries'(Es, From, To).

'$fx_copy_one'(directory, Src, Dst) :- !,
	copy_directory(Src, Dst).
'$fx_copy_one'(symlink, Src, Dst) :- !,
	posix_readlink(Src, Target),
	posix_symlink(Target, Dst).
'$fx_copy_one'(_, Src, Dst) :-
	copy_file(Src, Dst).

%% delete_directory_contents(+Dir).
%
% Empties Dir but keeps Dir itself.

delete_directory_contents(Dir) :-
	must_be(atom, Dir),
	(  exists_directory(Dir)
	-> true
	;  throw(error(existence_error(directory, Dir), delete_directory_contents/1))
	),
	'$fx_entries'(Dir, Entries),
	'$fx_delete_entries'(Entries, Dir).

'$fx_delete_entries'([], _).
'$fx_delete_entries'([E|Es], Dir) :-
	directory_file_path(Dir, E, Path),
	'$fx_file_type'(Path, Type),
	'$fx_delete_one'(Type, Path),
	'$fx_delete_entries'(Es, Dir).

% A symlink is unlinked, never descended into - otherwise a link
% pointing outside Dir would take the delete with it.

'$fx_delete_one'(directory, Path) :- !,
	delete_directory_and_contents(Path).
'$fx_delete_one'(_, Path) :-
	'$fx_posix'(posix_unlink(Path), delete_file(Path)).

%% delete_directory_and_contents(+Dir).

delete_directory_and_contents(Dir) :-
	delete_directory_contents(Dir),
	delete_directory(Dir).

% Runs Goal, or Fallback where the posix_ layer is unavailable.

'$fx_posix'(Goal, Fallback) :-
	catch(Goal, error(resource_error(posix_files_unavailable), _), Fallback).

'$fx_file_type'(Path, Type) :-
	'$fx_posix'(posix_file_type(Path, Type),
		(exists_directory(Path) -> Type = directory ; Type = regular)).

% --- walking ------------------------------------------------------------

%% directory_member(+Directory, -Member, +Options).
%
% Nondeterministically enumerates the paths inside Directory.
%
% Options: recursive(Bool) default false, follow_links(Bool) default
% true, file_type(regular|directory|symlink|...), extensions(List),
% hidden(Bool) default false, matches(Glob), exclude(Glob),
% exclude_directory(Glob), access(Access), file_errors(fail|error)
% default fail.
%
% Recursion is guarded against symlink cycles by realpath.

directory_member(Dir, Member, Options) :-
	must_be(list, Options),
	(  exists_directory(Dir)
	-> true
	;  throw(error(existence_error(directory, Dir), directory_member/3))
	),
	'$fx_member'(Dir, Member, Options, []).

'$fx_member'(Dir, Member, Options, Seen) :-
	'$fx_safe_entries'(Dir, Entries, Options),
	member(E, Entries),
	directory_file_path(Dir, E, Path),
	'$fx_visible'(E, Options),
	'$fx_walk'(Path, E, Member, Options, Seen).

'$fx_walk'(Path, E, Member, Options, Seen) :-
	'$fx_file_type'(Path, Type),
	(  '$fx_yields'(Path, E, Type, Options),
	   Member = Path
	;  Type == directory,
	   '$fx_opt'(recursive, Options, false, true),
	   \+ '$fx_glob_opt'(exclude_directory, E, Options),
	   '$fx_descend'(Path, Member, Options, Seen)
	;  Type == symlink,
	   '$fx_opt'(recursive, Options, false, true),
	   '$fx_opt'(follow_links, Options, true, true),
	   exists_directory(Path),
	   '$fx_descend'(Path, Member, Options, Seen)
	).

% The realpath of every directory entered is remembered, so a link back
% up the tree is entered once and not forever.

'$fx_descend'(Path, Member, Options, Seen) :-
	'$fx_posix'(posix_realpath(Path, Real), absolute_file_name(Path, Real, [])),
	\+ memberchk(Real, Seen),
	'$fx_member'(Path, Member, Options, [Real|Seen]).

'$fx_yields'(Path, E, Type, Options) :-
	(  memberchk(file_type(Want), Options)
	-> '$fx_type_matches'(Want, Type, Path)
	;  true
	),
	(  memberchk(extensions(Exts), Options)
	-> '$fx_has_ext'(E, Exts)
	;  true
	),
	\+ '$fx_glob_opt'(exclude, E, Options),
	(  memberchk(matches(Glob), Options)
	-> '$fx_glob'(Glob, E)
	;  true
	),
	(  memberchk(access(Access), Options)
	-> access_file(Path, Access)
	;  true
	).

% file_type(file) is SWI's spelling for a regular file; directory
% matches through a symlink, since that is what a caller means.

'$fx_type_matches'(file, Type, _) :- !, Type == regular.
'$fx_type_matches'(directory, Type, Path) :- !,
	(  Type == directory
	-> true
	;  Type == symlink,
	   exists_directory(Path)
	).
'$fx_type_matches'(Want, Type, _) :-
	Want == Type.

'$fx_has_ext'(Name, Exts) :-
	atom_chars(Name, Cs),
	member(Ext, Exts),
	'$fx_ext_chars'(Ext, ECs),
	append(_, ECs, Cs),
	!.

'$fx_ext_chars'(Ext, Cs) :-
	atom_chars(Ext, Cs0),
	(  Cs0 = ['.'|_]
	-> Cs = Cs0
	;  Cs = ['.'|Cs0]
	).

'$fx_visible'(E, Options) :-
	(  '$fx_opt'(hidden, Options, false, true)
	-> true
	;  \+ atom_concat('.', _, E)
	).

'$fx_opt'(Name, Options, Default, Value) :-
	Opt =.. [Name, V],
	(  memberchk(Opt, Options)
	-> V == Value
	;  Default == Value
	).

'$fx_glob_opt'(Name, E, Options) :-
	Opt =.. [Name, Glob],
	memberchk(Opt, Options),
	'$fx_glob'(Glob, E).

% file_errors(fail) - the default - skips a directory that cannot be
% read rather than aborting the whole walk.

'$fx_safe_entries'(Dir, Entries, Options) :-
	catch('$fx_entries'(Dir, Entries), E,
	      (  memberchk(file_errors(error), Options)
	      -> throw(E)
	      ;  Entries = []
	      )).

% --- glob ---------------------------------------------------------------
%
% Trealla has no wildcard_match/2, so the subset the directory_member
% options need is implemented here: *, ? and [...] classes.

'$fx_glob'(Glob, Name) :-
	atom_chars(Glob, G),
	atom_chars(Name, N),
	'$fx_glob_'(G, N).

'$fx_glob_'([], []).
'$fx_glob_'(['*'|G], N) :-
	(  '$fx_glob_'(G, N)
	;  N = [_|N1],
	   '$fx_glob_'(['*'|G], N1)
	),
	!.
'$fx_glob_'(['?'|G], [_|N]) :-
	'$fx_glob_'(G, N).
'$fx_glob_'(['['|G], [C|N]) :-
	'$fx_glob_class'(G, C, G1),
	'$fx_glob_'(G1, N).
'$fx_glob_'([C|G], [C|N]) :-
	C \== ('*'), C \== ('?'), C \== '[',
	'$fx_glob_'(G, N).

'$fx_glob_class'(G, C, Rest) :-
	(  G = ['^'|G0]
	-> Negated = true
	;  G = ['!'|G0]
	-> Negated = true
	;  G0 = G,
	   Negated = false
	),
	'$fx_class_upto'(G0, Set, Rest),
	(  Negated == true
	-> \+ '$fx_in_class'(Set, C)
	;  '$fx_in_class'(Set, C)
	).

'$fx_class_upto'([], _, _) :- fail.
'$fx_class_upto'([']'|T], [], T) :- !.
'$fx_class_upto'([C|T], [C|Set], Rest) :-
	'$fx_class_upto'(T, Set, Rest).

'$fx_in_class'([A,'-',B|_], C) :-
	char_code(A, CA), char_code(B, CB), char_code(C, CC),
	CC >= CA, CC =< CB,
	!.
'$fx_in_class'([C|_], C) :- !.
'$fx_in_class'([_|T], C) :-
	'$fx_in_class'(T, C).

% --- links, modes, times ------------------------------------------------

%% link_file(+OldPath, +NewPath, +Type).
%
% Type is `hard` or `symbolic`.

link_file(Old, New, Type) :-
	must_be(atom, Old),
	must_be(atom, New),
	(  Type == hard
	-> posix_link(Old, New)
	;  Type == symbolic
	-> posix_symlink(Old, New)
	;  throw(error(domain_error(link_type, Type), link_file/3))
	).

%% chmod(+File, +Spec).
%
% Spec is an integer, a symbolic mode, +Mode to add permissions, or
% -Mode to revoke them.

chmod(File, Spec) :-
	must_be(atom, File),
	(  integer(Spec)
	-> posix_chmod(File, Spec)
	;  Spec = +(Add)
	-> '$fx_mode'(Add, Bits),
	   posix_file_mode(File, Old),
	   New is Old \/ Bits,
	   posix_chmod(File, New)
	;  Spec = -(Sub)
	-> '$fx_mode'(Sub, Bits),
	   posix_file_mode(File, Old),
	   New is Old /\ \(Bits),
	   posix_chmod(File, New)
	;  '$fx_mode'(Spec, Bits),
	   posix_chmod(File, Bits)
	).

'$fx_mode'(Mode, Mode) :- integer(Mode), !.
'$fx_mode'(Mode, Bits) :-
	is_list(Mode),
	!,
	foldl('$fx_mode_add', Mode, 0, Bits).
'$fx_mode'(Mode, Bits) :-
	atom(Mode),
	!,
	'$fx_mode_bits'(Mode, Bits).
'$fx_mode'(Mode, _) :-
	throw(error(domain_error(file_mode, Mode), chmod/2)).

'$fx_mode_add'(M, Acc, Bits) :-
	'$fx_mode'(M, B),
	Bits is Acc \/ B.

'$fx_mode_bits'(suid, 0o4000) :- !.
'$fx_mode_bits'(sgid, 0o2000) :- !.
'$fx_mode_bits'(svtx, 0o1000) :- !.
'$fx_mode_bits'(Name, Bits) :-
	atom_chars(Name, Cs),
	append(Who, What, Cs),
	Who \== [],
	What \== [],
	'$fx_who_mask'(Who, WhoMask),
	'$fx_what_mask'(What, WhatMask),
	!,
	Bits is WhoMask /\ WhatMask.
'$fx_mode_bits'(Name, _) :-
	throw(error(domain_error(file_mode, Name), chmod/2)).

'$fx_who_mask'(Who, Mask) :-
	foldl('$fx_who_bit', Who, 0, Mask),
	Mask > 0.

'$fx_who_bit'(u, A, M) :- M is A \/ 0o4700.
'$fx_who_bit'(g, A, M) :- M is A \/ 0o2070.
'$fx_who_bit'(o, A, M) :- M is A \/ 0o1007.

'$fx_what_mask'(What, Mask) :-
	foldl('$fx_what_bit', What, 0, Mask),
	Mask > 0.

'$fx_what_bit'(r, A, M) :- M is A \/ 0o0444.
'$fx_what_bit'(w, A, M) :- M is A \/ 0o0222.
'$fx_what_bit'(x, A, M) :- M is A \/ 0o0111.
'$fx_what_bit'(s, A, M) :- M is A \/ 0o6000.
'$fx_what_bit'(t, A, M) :- M is A \/ 0o1000.

%% set_time_file(+File, -OldTimes, +NewTimes).
%
% Both lists hold access(Time), modified(Time) and/or changed(Time) with
% times as floating-point seconds. `now` may be given as a new time.
% changed(_) can be read but not set - the kernel owns it - so asking to
% set it raises a permission_error rather than silently doing nothing.

set_time_file(File, OldTimes, NewTimes) :-
	must_be(atom, File),
	must_be(list, NewTimes),
	posix_file_times(File, Access, Modified, Changed),
	'$fx_old_times'(OldTimes, Access, Modified, Changed),
	(  NewTimes == []
	-> true
	;  '$fx_new_time'(access, NewTimes, Access, NewAccess),
	   '$fx_new_time'(modified, NewTimes, Modified, NewModified),
	   (  memberchk(changed(_), NewTimes)
	   -> throw(error(permission_error(set, file_time, changed), set_time_file/3))
	   ;  true
	   ),
	   posix_set_file_times(File, NewAccess, NewModified)
	).

'$fx_old_times'(OldTimes, _, _, _) :-
	var(OldTimes),
	!,
	OldTimes = [].
'$fx_old_times'([], _, _, _).
'$fx_old_times'([T|Ts], A, M, C) :-
	'$fx_old_time'(T, A, M, C),
	'$fx_old_times'(Ts, A, M, C).

'$fx_old_time'(access(A), A, _, _).
'$fx_old_time'(modified(M), _, M, _).
'$fx_old_time'(changed(C), _, _, C).

'$fx_new_time'(Key, NewTimes, Current, Value) :-
	Opt =.. [Key, T],
	(  memberchk(Opt, NewTimes)
	-> (  T == now
	   -> get_time(Value)
	   ;  must_be(number, T),
	      Value = T
	   )
	;  Value = Current
	).

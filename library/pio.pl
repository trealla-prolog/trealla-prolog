/** Pure I/O.

   Our goal is to encourage the use of definite clause grammars (DCGs)
   for describing strings. The predicates `phrase_from_file/[2,3]`,
   `phrase_to_file/[2,3]`, `phrase_from_stream/2` and `phrase_to_stream/2`
   let us apply DCGs
   transparently to files and streams, and therefore decouple side-effects
   from declarative descriptions.
*/

:- module(pio, [phrase_from_file/2,
                phrase_from_file/3,
                phrase_from_stream/2,
                phrase_to_file/2,
                phrase_to_file/3,
                phrase_to_stream/2
               ]).

:- use_module(library(dcgs)).
:- use_module(library(freeze)).
:- use_module(library(error)).
:- use_module(library(lists), [member/2]).

:- meta_predicate(phrase_from_file(2, ?)).
:- meta_predicate(phrase_from_file(2, ?, ?)).
:- meta_predicate(phrase_from_stream(2, ?)).
:- meta_predicate(phrase_to_file(2, ?)).
:- meta_predicate(phrase_to_file(2, ?, ?)).
:- meta_predicate(phrase_to_stream(2, ?)).

phrase_from_file(NT, File) :-
    phrase_from_file(NT, File, []).

phrase_from_file(NT, File, Options) :-
    (   var(File) -> instantiation_error(phrase_from_file/3)
    ;   must_be(list, Options),
        (   member(Var, Options), var(Var) -> instantiation_error(phrase_from_file/3)
        ;   member(type(Type), Options) ->
            must_be(atom, Type),
            member(Type, [text,binary])
        ;   Type = text
        ),
		setup_call_cleanup(
			open(File, read, Stream, [mmap(Ms)|Options]),
			phrase(NT, Ms, []),
			close(Stream)
		)
	).

%% phrase_from_stream(+GRBody, +Stream)
%
%  True if grammar rule body GRBody covers the rest of Stream, read
%  lazily as a list of characters, a chunk at a time as GRBody needs it.
%
%  A repositionable stream is read again from the chunk's position when
%  backtracking comes back for it. Any other stream (a pipe, a socket,
%  user_input) keeps each chunk on the blackboard until GRBody is done.

phrase_from_stream(GRBody, Stream) :-
    stream_property(Stream, reposition(Reposition)),
    (   Reposition == true ->
        stream_property(Stream, position(Pos)),
        freeze(Ls, reposition_step(Stream, Pos, Ls)),
        phrase(GRBody, Ls)
    ;   next_chunk_key(Key),
        freeze(Ls, buffer_step(Stream, Key, Ls)),
        setup_call_cleanup(true, phrase(GRBody, Ls), delete_chunks(Key))
    ).

% How many chars to read from the stream in each step.
chars_to_read(4096).

reposition_step(Stream, Pos, Ls) :-
    set_stream_position(Stream, Pos),
    chars_to_read(N),
    '$get_chars'(Stream, N, Cs),
    (   Cs == '' ->
        Ls = []
    ;   append(Cs, Ls0, Ls),
        stream_property(Stream, position(Pos0)),
        freeze(Ls0, reposition_step(Stream, Pos0, Ls0))
    ).

% A chunk is stored with the key of the one after it, so revisiting it
% finds the same successor instead of reading the stream again.

buffer_step(Stream, Key, Ls) :-
    (   bb_get(Key, chunk(Cs, Key0)) ->
        true
    ;   chars_to_read(N),
        '$get_chars'(Stream, N, Cs),
        next_chunk_key(Key0),
        bb_put(Key, chunk(Cs, Key0))
    ),
    (   Cs == '' ->
        Ls = []
    ;   append(Cs, Ls0, Ls),
        freeze(Ls0, buffer_step(Stream, Key0, Ls0))
    ).

next_chunk_key(Key) :-
    (   bb_get(next_chunk_key, Key) ->
        true
    ;   Key = 0
    ),
    Key1 is Key + 1,
    bb_put(next_chunk_key, Key1).

delete_chunks(Key) :-
    (   bb_delete(Key, chunk(_, Key0)) ->
        delete_chunks(Key0)
    ;   true
    ).

%% phrase_to_stream(+GRBody, +Stream)
%
%  Emit the list of characters described by the grammar rule body
%  GRBody to Stream.
%
%  An ideal implementation of `phrase_to_stream/2` writes each
%  character as soon as it becomes known and no choice-points remain,
%  and thus avoids the manifestation of the entire string in memory.
%  See [#691](https://github.com/mthom/scryer-prolog/issues/691) for
%  more information.
%
%  The current preliminary implementation is provided so that Prolog
%  programmers can already get used to describing output with DCGs,
%  and then writing it to a file when necessary. This simple
%  implementation suffices as long as the entire contents can be
%  represented in memory, and thus covers a large number of use cases.

phrase_to_stream(GRBody, Stream) :-
        phrase(GRBody, Cs),
        must_be(chars, Cs),
        (   stream_property(Stream, type(binary)) ->
            (   '$first_non_octet'(Cs, N) ->
                domain_error(octet_character, N, phrase_to_stream/2)
            ;   true
            )
        ;   true
        ),
        % we use a specialised internal predicate that uses only a
        % single "write" operation for efficiency. It is equivalent to
        % maplist(put_char(Stream), Cs). It also works for binary streams.
        '$put_chars'(Stream, Cs).

phrase_to_file(GRBody, File) :-
        phrase_to_file(GRBody, File, []).


phrase_to_file(GRBody, File, Options) :-
	setup_call_cleanup(
		open(File, write, Stream, Options),
		phrase_to_stream(GRBody, Stream),
		close(Stream)
	).

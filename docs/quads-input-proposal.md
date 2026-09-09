# Proposal: input in answer descriptions (issue #1099)

Quads describe what a query *answers* and, since #1082, what it
*writes*. Issue #1099 asks them to describe what a query *reads*.

Revised after UWN's comments of 2026-08-05, which changed the design
substantially: this supersedes the first draft rather than amending it.
Written against `8e29354`.

## 1. The use case

Not `get_char/1` in isolation — the target is encoding the whole
[syntax conformity suite](https://www.complang.tuwien.ac.at/ulrich/iso-prolog/conformity_testing),
whose subject is `read/1`. UWN's shape:

```prolog
?- read(G_0), G_0.
   inputs("writeq('\\n')."), peeks("\n"), outputs("'\\n'"), G_0 = ... .  % s#1
   inputs("'\n"), syntax_error(...).                                     % s#2
   inputs(")\n"), waits.                                                 % s#3
```

Three things follow immediately, and the first draft got all three
wrong:

- `inputs`, `peeks`, `outputs` and a binding **all occur in one
  answer**. The draft proposed rejecting `inputs` together with `peeks`;
  that would reject s#1, the central case.
- `waits` is not a description of its own. It may follow `inputs`, as in
  s#3: the query consumes what it is given and *then* asks for more.
- `peeks/1` matters because a conforming reader must look one character
  past an end token to know it is one. s#1 says so explicitly.

`inputs/1` is always a list of characters and `peeks/1` a list of
exactly one, so no DCG body — an answer says exactly what was read, and
a nonterminal may stand for more than one string.

## 2. How it is checked

UWN's key observation is that the `0xff` sentinel does three jobs, not
one, and between them they make an ordinary file sufficient. Nothing
below needs a new stream type.

Given `inputs(Cs)` and optionally `peeks([P])`, the input file holds

```
Cs ++ [P] ++ [0xff]
```

and the checks are:

1. **Nothing is read past what was described.** Any read or peek beyond
   the described characters reaches `0xff`, which raises
   `representation_error(character)`. That is not the described answer,
   so the quad fails. Free — a ball outcome already fails to match a
   described solution.
2. **Everything described was read.** After the query, the harness reads
   the stream itself. With `peeks([P])` it must see `P` — still there,
   because a peek does not consume — and then the sentinel. Without a
   `peeks`, it must hit the sentinel at once.
3. **The query really was waiting.** For `waits`, the sentinel *is* the
   expected outcome: a query that asks for another character reaches it
   and raises `representation_error(character)`. If that error does not
   come, the query was not waiting.

Point 3 is what removes the need for a blocking stream, a `popen` pipe,
or a never-ready flag. It also removes the `waits`-versus-`loops`
ambiguity that the first draft spent a section on: they are no longer
the same observation, since `waits` is now an error rather than a
timeout.

Point 2 replaces the draft's proposed `'$capture_input_consumed'`
builtin. The draft reached for C because `stream_property/2` position
cannot distinguish a peek from a get — true, but beside the point. You
do not inspect the position; you keep reading and see what is there.

A peek that leaves no trace still cannot show, on its own, that it
*happened*: a reader that never peeks also satisfies `peeks([P])`. UWN's
answer is a fourth job for the sentinel — put it *in* the position to be
peeked. Anyone who peeks (or gets) there raises the representation
error; a reader that does not peek answers normally. So the two halves
are told apart by a second execution of the same query:

```prolog
?- read(X).
   inputs("1."), peeks("\n"), X = 1.   % what the peeked character is
   inputs("1."), waits.                % that the peek happened at all
```

The second is `waits` by point 3 above: the character `read/1` must look
at to confirm the end token (6.4.8) is the sentinel, so confirming it
raises the error rather than completing the term.

**Mechanism: option (a), a temporary file**, per UWN — "this remains
within standard Prolog (except for the timeout) and in this manner
problems like #1101 can be identified". The harness writes the
characters through an ordinary text stream, so the system does its own
UTF-8 encoding, then appends the sentinel byte through a binary stream.
Stream state afterwards does not matter; it need only be closable.

`inputs("")` means nothing is input. With no `peeks` either, nothing is
read at all — and the sentinel makes the harness *enforce* that.

## 3. Malformed shapes

Per solution:

- at most one `inputs/1`, one `peeks/1`, one `outputs/1`, one `waits` —
  a query is run once against one input;
- `inputs/1` a proper list of characters, `peeks/1` a list of exactly
  one;
- **`peeks(C), waits` is impossible.** A peek leaves `C` unconsumed, so
  every later read returns it and the query can never be left waiting.
  To wait it would have to have consumed `C`, which is
  `inputs([C]), waits`. (UWN raised this; it also falls out of §2 —
  `waits` is confirmed by reaching the sentinel, and a query that only
  peeked `C` never gets there.)

These belong with the #1074 malformed machinery — `malformed/2` in the
library, which since #1078 reports rather than aborting the load.

Note what is *not* rejected: `inputs` with `peeks` with `outputs` with a
binding, all in one answer. That is s#1.

## 4. Staging

**Stage 1 — grammar and shape. Implemented.**

`waits`, `inputs/1` and `peeks/1` accepted by `answer_description()` in
`parser.c` and by `answer_atom/1` and `answer_item/1` in the library;
the shape rules of §3 in `malformed/2`; `take_input/3` beside
`take_output/3`, and a described input **skipped** the way `sto` is.

Skipping matters: a suite already written in this notation loads and
runs, malformed uses are reported, and nothing passes on a claim that
was not verified.

**Stage 2 — interpretation. Implemented.**

`take_input/3` collects the three annotations into one spec;
`solution_expect/3` gives the outcome a solution describes, and
`input_expect/3` overrides it with the sentinel's representation error
when the answer says `waits`. `run_on_input/2` writes the file, runs the
attempt with current input on it, and checks what is left unread.

All of it is ordinary Prolog, which is the point: the harness has to run
on the systems whose conformity it reports on.

One restriction: a solution that names input must be the only one
described, since the query is run once against one input and a second
answer would need a second stream. Multi-answer input quads are not
rejected outright, but they will not pass.

**Stage 3 — the suite. Implemented for s#1..s#4, s#270, s#271.**

Encoded in `tests/issues/test1099.pl`, against the row numbering on
UWN's [syntax conformity
page](https://www.complang.tuwien.ac.at/ulrich/iso-prolog/conformity_testing).
Trealla passes all six: the two syntax errors (s#2, s#4), the wait
(s#3), and the peek-one-character discipline that s#1 and the s#270/
s#271 pair each test from a different angle - past an end token, and
past an end token immediately or non-immediately followed by a
comment. The rest of that page remains to encode.

## 5. Dependencies

#1101 (`peek_char/2` must not change the position of a stream) is
closed. Two fixes went in: the reported position now subtracts the
pushback (`052d819`), and a peek at an *ill-formed* sequence no longer
consumes it — it is remembered as pending and the next get takes it.
Both matter here, the second especially, since every check in §2 turns
on peeking at the sentinel being repeatable and non-destructive.

## 6. Open

- The timeout stays as a safety net. Nothing in §2 should block, but an
  implementation that waits on a file rather than reading the sentinel
  would otherwise hang the suite.
- `outputs/1` accepts a DCG body via `phrase/2`; `inputs/1` deliberately
  does not. The asymmetry is intended — worth a line in the docs so it
  does not read as an oversight.
- Whether a `waits` that is *not* reached — the query answered without
  asking for another character — should be reported differently from an
  ordinary mismatch. It is the interesting failure for conformity work.
- `with_output_to/2` does not nest, so a quad using `outputs/1` cannot
  be run inside one. `'$capture_output'` toggles `is_memory` and frees
  the buffer rather than saving and restoring it, so an inner capture
  cancels the outer one and its output escapes to the real stream:

  ```prolog
  ?- with_output_to(chars(O), (write(before),
         with_output_to(chars(I), write(inner)), write(after))).
     % writes 'innerafter' to stdout, O = [], I = []
  ```

  Worth its own issue. It bit the test for this work, which captures
  the report to keep it independent of the path it was invoked with.

# TCO in the THEN branch of if-then-else

Originally against `trealla-prolog/trealla` @ `b932785` ("Tidy up
parser.c a bit"). Re-verified against `1954a4e`.

## Status

| section | state |
|---|---|
| 1-2. Tail-position marking, `commit_any_choices()` | **landed** |
| 3. The `no_recov` pin | **landed** - the pins are removed, see the end of section 3 |
| 4. The accumulator idiom | **open** - the larger half of `giso`'s parse memory, see section 5's census |
| 5. Terms escaping a call | reference-counted arguments and `bagof/3`/`setof/3` **landed**; heap and clause terms **open** |
| 6. What a precise answer would free | **measured** - 99.9% of frames are unreachable; the trail holds them |
| 7. What a collector would free | **simulated** - 100% of frames in a recursive workload, 47% in chess; roots known |
| 8. What a collector may move | **audited** - compact frames and slots, renumber indices, leave the heap in place |
| 9. Whether to build it | **not now** - 7.7x memory for 3-23% time, a permanent tax, and nothing cheaper left |
| Addendum. Disjunction quadratic (#1106) | **landed** |

Section 4 is the live one and was re-measured on `1954a4e`; its numbers
below are current. Sections 1-3 and the addendum are kept as a record of
what was done and why.

Since v3.9.75 `commit_frame()` reuses a frame for any last call, not
only a recursive one: it tests `is_tail_call()`, and
`FLAG_INTERNED_RECURSIVE_CALL` is gone. The sections below describe the
recursion-only engine they were written against.

## The symptom

`(C -> T ; E)` at the end of a clause: a recursive call ending `E` is
tail-call optimised, the same call ending `T` is not.

```prolog
c_else(N) :- ( N =:= 0 -> true ; M is N-1, c_else(M) ).
c_then(N) :- ( N > 0  -> M is N-1, c_then(M) ; true ).
```

At 200,000 iterations, stock `tpl`:

| | active frames | TCOs |
|---|---|---|
| `c_else` | 3 | 200,000 |
| `c_then` | 200,003 | 0 |
| `c_soft_then` (`*->`) | 200,003 | 0 |

## Why

`commit_frame()` will only reuse a frame for a goal carrying
`FLAG_INTERNED_RECURSIVE_CALL`. That flag is set in `process_cell()`
(`src/module.c`) by one test:

```c
if (!is_directive && ((c + c->num_cells) >= (body + cl->cidx-1))) {
        c->flags |= FLAG_INTERNED_TAIL_CALL;
        if (parent && same functor/arity)
                c->flags |= FLAG_INTERNED_RECURSIVE_CALL;
}
```

"Does this cell end where the clause's cells end?" — pointer arithmetic
over the **source term**, applied by a flat loop over every cell. In
`p(N) :- ( C -> T ; E )` the term's last cell is the last cell of `E`,
so only `E`'s final goal is ever marked. `T` is followed in the term by
the entire else branch and never qualifies, however trivial `E` is.

Nothing else is in the way. `compile_term()` lays the construct out as

```
$succeed_on_retry(V,N1), <C>, !, $drop_barrier(V), <T>, $jump(N2), <E>, true
```

so the last goal of `T` is followed only by a jump to the landing that
ends the clause — and `is_last_call()` in `src/query.c` already walks
exactly that: it skips `$jump` with a positive offset and `true`
landings before concluding the clause is over. The run-time half of the
machinery was ready; the compile-time hint never arrived.

## The fix

**1. Mark tail positions structurally** (`src/module.c`)

A new `mark_tail_positions()` walks the control skeleton of the body
instead of reading the term's last cell, and marks every branch that
ends the clause:

- `(A , Tail)` → `Tail`
- `(Tail ; Tail)` → both
- `(_ -> Tail)`, `(_ *-> Tail)` → `Tail`
- `if(_, Tail, Tail)` → both
- anything else → the goal itself

It runs at the end of `process_clause()` and mirrors `process_cell()`'s
rules about which goals may be marked (builtins are left alone; only a
functor matching the predicate being loaded becomes a recursive call).

The mark is a hint, not a promise: `commit_frame()` still calls
`is_last_call()`, which checks the instruction stream that actually
follows the goal at run time. A mark that `compile_term()` lays out
differently than assumed costs a failed test, not correctness.

**2. Ask `commit_any_choices()` the right question** (`src/query.c`)

Enabling the branches exposed a live soundness bug that had to be fixed
alongside them. `commit_frame()` asked `ch->gen > f->chgen` — a
comparison of choice *generations*, which is not the same question as
"does a choicepoint need this frame" and got it wrong in both
directions.

*Too loose.* A choicepoint pushed by an earlier goal of the same clause
carries `gen == f->chgen` exactly, so it was invisible:

```prolog
p(0) :- !.
p(N) :- between(1,2,_), M is N-1, p(M).
```

`findall(x, p(3), L)` must yield 8 solutions. Stock `tpl` yields **4**
(`tpl -O0` yields 8) — the tail call recycled the frame `between/3`'s
choicepoint needed. The branches now eligible for TCO sit behind more of
these than a plain body does, so this had to go.

*Too tight*, had it simply been tightened to `>=` (which is what
`resume_frame()` uses). Generations do not order frames:
`commit_frame()` stamps a pending clause choice with `q->chgen`, the
generation of the frame the call goes on to create, and `$drop_barrier`
hands a frame back a generation it held earlier. So an ancestor's
choicepoint can carry `gen == f->chgen` while having nothing to do with
this frame. `samples/chess.pl` lost ~31k of its 20.7M tail calls that
way, every one of them `can_move/5` recursing under an outer clause
choice five frames up.

The frames answer it directly. `ch->st.fp` is the frame count when the
choicepoint was pushed, so `ch->st.fp > q->st.cur_ctx` means this frame
was already live and a retry restores into it; anything pushed earlier
belongs to an ancestor, and retrying that throws this frame away whole.
`commit_frame()` only reaches the test with `q->st.fp == cur_ctx + 1`.

The comparison is only half of it. `skip` counts the choicepoints
`commit_frame()` drops itself — the in-progress clause choice, plus the
`call/N` barrier when `is_last_call()` found one, which is why
`p(N) :- M is N-1, call(p, M)` keeps its TCO. **The comparison without
the skip count is not a smaller version of this fix, it is a crash**:
`call/N`'s barrier then reads as a choicepoint that needs the frame, TCO
is refused for the shape above, and `t4/1` in `tests/tests/test0107.pl`
dies in `undo_me()` backtracking through what it left behind. So it
takes the count, and `is_last_call()` has to report the barrier it
skipped:

```c
static bool commit_any_choices(const query *q, unsigned skip)
{
        if (q->st.cp <= skip)
                return false;

        const choice *ch = GET_CHOICE(q->st.cp - 1 - skip);
        return ch->st.fp >= q->st.fp;
}

// in commit_frame():
bool barrier = false;
bool tail_recursive = is_recursive_call(q->st.instr) && is_last_call(q, &barrier);
bool choices = commit_any_choices(q, barrier ? 2 : 1);
```

That crash also says something about the suite: `test0107` printed every
expected line and *then* died, so `tests/run.sh` — which diffed the
output and never looked at the exit status — scored it as a pass. The
runners now check both.

## Result

At 200,000 iterations, patched:

| | active frames | TCOs |
|---|---|---|
| `c_then` | 3 | 200,000 |
| `c_soft_then` | 3 | 200,000 |
| nested if-then-else | 3 | 200,000 |

3,000,000 iterations of `then_loop/1`: **591 MB → 5.5 MB**, 0.36 s → 0.28 s.

## Verification

- `tests/run.sh`: 329 passed / 1 failed, identical to the unpatched
  build. The one failure is `tests/issues-OLD/test056.pl`, which wants
  `crypto_data_hash/3` — an artefact of building `NOSSL=1` in this
  sandbox, and it fails the same way on stock. (On `1954a4e` the suite
reads 342/2; the two failures are `tests/issues/test0556.pl` and
`tests/issues-OLD/test0252.pl`, both long-standing Unicode
  tokenising bugs in `writeq`, and both unrelated to anything here.)
- The same suite under `make debug` (`-fsanitize=address`): no
  AddressSanitizer reports across the ~327 programs it completed.
- `tco-then-branch-tests.pl` (attached): deep recursion in `->`, `*->`
  and nested branches, plus solution counts for the nondeterministic
  cases that must *not* be optimised.
- No TCO lost on other shapes: `catch/3`, `findall/3`, `\+`, `once/1`,
  `call/N`, a preceding if-then-else, and a preceding builtin all still
  reuse frames exactly as before.
- `samples/chess.pl` (`tpl -q -g 'time(main),statistics,halt' -f
  samples/chess.pl`): every counter identical to stock — 20,705,572
  TCOs, 3,567,334 backtracks, 97,171,492 retries, 2,932,211 frame
  recovs, 128,081,142 matches — at 5.38 s vs 5.40 s.
  `samples/queens11.pl`: 1,513,160 TCOs on both. nrev: 1.06 s on both.
- `eyereasoner/eyelet` (`test-trealla`): all 88 inputs byte-identical to
  the committed `output-trealla/`, none slower than stock.

## 3. The `// FIXME: memory waste`

`push_succeed_on_retry_with_barrier()` set `f->no_recov = true` on the
current frame and never took it back, so a single `\+`, `ignore/1`, `\=`
or if-then-else anywhere in a body pinned that clause's frame for the
rest of the query. The unrecovered frame keeps `q->st.fp` above the
caller's `cur_ctx + 1`, so it costs the **caller** its TCO too:

```prolog
foo(N) :- ( N > 0 -> true ; true ).
loop(0) :- !.
loop(N) :- foo(N), M is N-1, loop(M).
```

`loop/1` runs in constant frames if `foo/1`'s body is anything else.
With the if-then-else: 600,002 frames per 300,000 iterations, 0 TCOs.
Same for `\+`, `ignore/1` and `\=`. (`once/1` escapes — it uses the
fail-on-retry barrier, which never set the pin.)

Still current on `1954a4e`. Two frames per iteration, and the memory
that implies — at 3,000,000 iterations:

| `foo/1`'s body | frames | peak RSS |
|---|---|---|
| `true` | 4 | 6 MB |
| `once(G)` | 4 | 6 MB |
| `( N > 0 -> true ; true )` | 6,000,003 | 897 MB |
| `\+ G`, `ignore(G)`, `N \= zzz` | 6,000,003 | 897 MB |

### What it was actually protecting

Not the choicepoint. `push_barrier()` stamps that one with `gen ==
f->chgen`, so `resume_frame()` and `commit_frame()` both see it while
it is live, and it is always gone — dropped by `$drop_barrier`, or
consumed by the retry it exists for — before the clause ends.

Removing it makes `tests/issues/test0338.pl` (clpb) lose solutions, so I
went looking for what breaks. Instrumenting every frame recovery, the
first divergence from the pinned run is `bdd_restriction/4`'s frame
being reclaimed. Disabling `trim_frame()`'s three effects one at a time:

| what | test0338 |
|---|---|
| don't clear the slots | still fails |
| don't lower `q->st.sp` | still fails |
| don't lower `q->st.fp` | **passes** |

So it is not the slot contents — it is the **frame index being
recycled**. Scanning every live frame, attribute list, the heap and the
trail for references to the frame at the moment it is reclaimed finds
exactly one class of holder: **12 trail entries**, no heap or attribute
references at all.

A trail entry names a variable as `(val_ctx, var_num)` — a frame index
and a slot number. `undo_me()` walks entries by index and clears the
slot each one names. Recycle the index and a later frame lands on it;
the next retry then unbinds a variable belonging to a completely
different predicate. In clpb that variable is a goal, hence
`instantiation_error`.

`trim_trail()` exists for precisely this — its own comment says a stale
entry against a recycled frame is the hazard — but it is only ever
called from `commit_frame()`. The **return path recycles frame indices
without ever cleaning the trail**, and `f->no_recov` was the plug.

### Three repairs, and what they each hit

**Sweep the trail when the frame is reclaimed.** Unbounded as written —
the window between the newest choicepoint's `tp` and the top of the
trail is the whole trail in a program that barely branches, and
`samples/takeuchi.pl` (via `eyereasoner/eyelet`) stops finishing. A
per-frame count of live entries fixes that: the count is almost always
zero — **10,580,000 recoveries in the test suite, none owing an entry**
— so the sweep only runs when there is something to find. Passes the
suite, chess, eyelet and ASan. Aborts Logtalk's `library/types` in
`malloc()`, because `reuse_frame()` leaves entries behind too and
`trim_trail()` only clears the run of them at the top.

**Stamp each entry with a frame incarnation** and skip mismatches in
`undo_me()`. Covers every recycler once you notice that `reuse_frame()`
is two of them — it replaces the frame's own variables *and* moves the
incoming frame's slots out from under the entries naming them, copied
without a `share_cell()` so the reference travels and the source is left
holding a dangling duplicate. Bumping both frames makes Logtalk's
`library/types` pass 149/149, ASan clean, suite 330/1, eyelet 88/88,
chess unchanged. Costs 4 bytes an entry (16 → 24 with alignment) and
about 1% on chess and queens11.

That one holds up for the trail. It still breaks
`examples/threads/primes`:

```prolog
spawn([Inf-Sup| Intervals], Acc, Primes, [primes(Inf, Sup, Acc, Acc2)| Goals]) :-
	threaded_once(primes(Inf, Sup, Acc, Acc2)),
	spawn(Intervals, Acc2, Primes, Goals).
```

`threaded_once/1` posts a goal holding `Acc` and `Acc2` — difference-list
variables in the caller's frames — and `collect/1` unifies the answers
back long after `spawn/4` has returned. Recover those frames and the
result comes back truncated in proportion to the thread count, tail
bound to whatever landed on the index. Nothing in the engine's view
knows the queue is holding them.

### What that says

The entries and references are not something these repairs introduce.
Counting the trail entries `undo_me()` applies to a frame beyond
`q->st.fp` — dead beyond doubt — gives **5,364 in one run of the Logtalk
types tests, and the same number in stock, with the pin, without it, and
under every repair above**. The pin does not stop them being made; it
keeps the indices they name out of circulation so nothing notices.

So `f->no_recov` is holding up at least three things that have no other
protection: stale trail entries, attributed variables through those same
entries, and frame references parked in thread queues. It stays until
frames are no longer the only place a variable lives, or until every
holder of a frame reference is accounted for. The patch documents this
where the bare FIXME was.

### Resolved

The pins are gone: the one in `push_succeed_on_retry_with_barrier()`,
which compiled if-then-else, soft-cut, `\+`, `ignore/1` and `\=` all go
through, and the ones in the runtime `do_if_then_else()` and
`do_soft_if_then_else()`. At 300,000 iterations `loop/1` above now runs in
4 frames and 8 MB with any of those in `foo/1`'s body, where it took
600,003 frames and 103-111 MB - the same as a body of `true` or `once/1`.

What made that safe is not pinned down. Stamping each frame index and
checking every entry `undo_me()` applies against the stamp it was trailed
with found none applied to a live frame that had since been reused, with
the pin or without it, across the test suite, `test0338`, chess and these
loops. `test0338` now passes without the pin, where it lost solutions when
the pin was first removed, and Logtalk runs clean, `examples/threads/primes`
included. The #841 fix, which restores a frame's slot layout on
backtracking, is one plausible change underneath.

Regression test: `tests/sundry/tco_control_constructs.pl`.

Section 6 comes back to this from the other side. Of 527 declines to
recover a frame in a parse, 471 were frames nothing reachable held, and a
trail entry named 459 of them - so an entry naming a frame by index is not
a detail of that one clpb failure, it is what stands behind most of the
memory. `docs/trail-frame-index.md` designs the change.

### Two other things the dive turned up

- `tests/tests/test0104.pl`'s expected output hardcodes variable numbers
  (`freeze:freeze(_398,true)`). Anything that changes how many frames get
  recovered renumbers them, so that test will fail on any future work
  here for cosmetic reasons. Confirmed on `1954a4e`: `-O0` alone shifts
  `_119` to `_122`.
- `once/1` escapes the pin entirely — it compiles to the fail-on-retry
  barrier, which never set it. So `once(G)` already costs its caller
  nothing, while `ignore(G)` and `\+ G` cost it everything. That
  asymmetry is invisible from the Prolog side.

## Files

Both changes are in the tree. The regression tests landed as
`tests/tests/test0108.pl`; `tests/tests/test0107.pl` covers the cut and
barrier cases that `is_last_call()` must not swallow. The patch files
this section used to name (`trealla-then-branch-tco.patch`,
`tco-then-branch-tests.pl`) were working files and are not in the repo.

## 4. The accumulator idiom, and why it is still not tail recursive

An unbound output variable carried down a recursion — the most common
shape in Prolog — gets no tail call at all:

```prolog
sum(N, A, S)  :- ( N > 0 -> A1 is A+N, M is N-1, sum(M, A1, S) ; S = A ).
sum2(0, A, A) :- !.
sum2(N, A, S) :- A1 is A+N, M is N-1, sum2(M, A1, S).
```

200,000 iterations: 200,003 and 200,002 frames. Drop the `S` argument
and the same predicate runs in 3.

Still current on `1954a4e`: at 3,000,000 iterations `sum/3` reaches
3,000,004 frames and 920 MB, against 4 frames and 6 MB for the same
predicate without the output argument. This is the most common shape in
Prolog, which makes it the costlier of the two open sections even though
the pin above is the more visible one.

`set_var()` is what stops it. Head unification binds the callee's fresh
`S` to the caller's, and:

```c
if ((c_ctx == q->st.fp) && (c_ctx != v_ctx) && !is_temporary(c) && !is_void(c)) {
        q->no_recov = true;
```

`commit_frame()` refuses TCO while `q->no_recov` is set, and
`push_frame()` copies it onto the new frame, so the frame is not
recovered on return either. The test is coarse: it fires for a binding
into *any* other frame, including an ancestor that will outlive
everything here.

Three attempts to sharpen it, all wrong:

1. **Restrict to targets in a frame that can be recycled** —
   `v_ctx >= q->st.cur_ctx`, mirroring the condition the compound branch
   right below it already uses. Both loops above then run in 2-3 frames
   with correct results. 24 tests in the suite break.
2. **Split the two uses** — narrow test for the TCO gate, wide one for
   the frame pin, and the reverse. Either alone breaks the same tests, so
   both uses are load-bearing.
3. **Ask at reuse time instead of bind time** — scan the incoming
   clause's slots, deref'd, for anything pointing into the frame the tail
   call is about to take over. Different corruption, same verdict.

The counterexample each time is `bagof/3`, and under (1) it narrows to
exactly one newly-allowed tail call in the whole run:

```prolog
% library/builtins.pl
sys_enum_runs_([K-[+V]|L], W, Q) :-
	sys_key_run_(L, K, R, H),
	(K = W, Q = [V|R], (H = [], !; true); sys_enum_runs_(H, W, Q)).
```

Suppressing TCO for that one predicate restores correct output, so the
flag is doing real work there — work that is not captured by where the
target variable lives, nor by what the incoming slots point at.

Minimal case:

```prolog
foo(a,b,c). foo(a,b,d). foo(b,c,e). foo(b,c,f). foo(c,c,g). foo(d,e,g).
?- bagof(C, foo(_,_,C), Cs), write(Cs), nl, fail.
% [c,d] [e,f] [g] [g]      correct
% [c,d] [e,f] [g] [_A|_B]  with (1) or (2)
% [c,d] [e,f,g] [g]        with (3)
```

This is the same shape as the thread-queue case above: a variable of one
frame is reachable from somewhere the engine's escape test cannot see.
Making it precise means tracking that reachability properly, not finding
a better predicate to evaluate at the binding.

A fourth attempt, on `8ade03d5`, used the parser's variable dispositions.
The test already skips `is_temporary(c)` and `is_void(c)`; adding
`!is_local(c)` also skips a head variable that occurs only at the top
level of the head and body. `mk_eq(X) :- X = f(a)` called in a tail loop
went from 200,002 frames to 3, and `sum2/3` above likewise; `sum/3` stayed
at 200,003, held by the if-then-else pin instead. The `bagof/3` case
above still printed correctly. But 7 suite tests broke:
`tests/tests/test0101`, `tests/issues/test0338` (unexpected failure),
`test0369`, `test0838`, `test1128` (quads, 2 of 4 failed), `test1138.sh`
and `tests/sundry/tco_tail_calls.pl`. The flags describe where a variable
occurs in the clause source, not what can reach it at run time - `Q` in
`sys_enum_runs_` above is LOCAL too - so they cannot stand in for the
reachability tracking either.

A fifth attempt, on `157af3c6`, retried attempt 1 - firing only when
`v_ctx >= q->st.cur_ctx` - after the pins of section 3 were removed and a
last call was stopped from reusing its frame while its clause still had
alternatives (`tests/sundry/tco_cut_scope.pl`). `sum/3` and `sum2/3` above
then ran 200,000 iterations in 3 frames and 7-8 MB, where they took 200,003
frames and 58-68 MB; the `bagof/3` case stayed correct, and chess searched
identically with 83,182 more TCOs and 63,382 fewer peak frames. Of the six
suite tests that broke, `cut_after_call.pl` was that frame-reuse bug, and
`test0338`, `test0369`, `test0838` and `test1127` came right by keeping the
wide test once the query had put an attribute (`q->attrs_used`).
`test1061` did not: clpz constraints posted inside a user predicate, as in
`grid(2, C, R)` with `C` and `R` fresh, find no solutions. Tracing shows why
no per-query gate can work: clpz makes 60-odd head bindings through its own
predicates, which the narrowed test lets through, before it puts its first
attribute, and the attribute terms it then stores refer to those frames.
`attrs_used` turns true only once the frames it was meant to protect are
already unpinned. A gate would have to be settled before any such code runs
- set, say, when a loaded clause can put an attribute - and would still
leave the thread-queue case above to Logtalk.

Section 5's census puts a number on this rule. In `giso`'s parse it fires
20 times a line against 4 for `pin_v`, and those two between them keep
1.76M frames, so this section is the larger half of that memory and a fix
for section 5 alone would not collapse it.

## 5. Terms escaping a call

Found through `~/giso`, a Logtalk sub-graph isomorphism benchmark. Its
`giso_07` test parses 1,000 small `.dot` files with a DCG, and under
Trealla the parse alone peaks at 942,627 frames and 572 MB, against 56 MB
under SWI. The same parser in plain Prolog behaves the same, so this is the
engine, not Logtalk. Two separate things keep those frames.

**Reference-counted arguments (landed).** A tail call passing a code string
(`atom_codes/2`, `read_line_to_codes/2`) or a bigint never reused its frame
while any older choicepoint existed. Head unification trails such values,
and `head_trailed_new_frame()` refused the reuse because `reuse_frame()`
moved them without their entries. `reuse_frame()` now moves those entries
to the reused frame and drops the frame's stale ones. A
`read_line_to_codes/2` loop over 88,700 lines went from 88,903 frames and
39 MB to 303 frames and 10 MB, at 0.11% more instructions on chess.
Regression test: `tests/sundry/tco_refcounted_args.pl`. It does not change
`giso_07`, which is held by what follows.

**Heap and clause terms escaping a call (open).** A call that binds a
caller's variable to a term it built keeps a frame per iteration of a
loop. At 100,000 iterations:

| escaping term | frames | heap cells | memory |
|---|---|---|---|
| (B1) `copy_term/2` copy, passed to the tail call | 100,003 | 300,006 | 41 MB |
| (B1) `copy_term/2` copy, returned | 200,003 | 300,005 | 51 MB |
| (B1) `findall/3` result, returned | 200,005 | 2,000,005 | 119 MB |
| (B1) `msort/2` result, returned | 200,003 | 700,005 | 60 MB |
| (B2) clause term with a variable, returned | 200,003 | 5 | 43 MB |
| (B2) `length/2` list of fresh variables | 300,003 | 700,005 | 118 MB |
| a DCG whose result is not returned | 4 | 5 | 9 MB |
| `dot_parser`'s line DCG returning `e(G,A,B)` | 1,200,003 | 5 | 312 MB |

B1 terms are fully instantiated and on the heap, so only their heap cells
must survive; frame recovery and `reuse_frame()` are what free them, by
winding `hp` back. `set_var()` pins these frames with the same `no_recov`
and `heap_pinned` it uses for B2. A fix could recover or reuse the frame
and leave `hp` alone, carrying a heap floor up to the frame that owns the
variable. But in these loops the escaped term dies an iteration later with
nothing to say so, and the floor would keep it: the `findall/3` case would
still hold 2M heap cells. It would save the frames, not give constant
memory.

B2 terms hold variables that live in the callee's frame, and the caller's
binding points at those slots, so the frame has to stay. `dot_parser`'s
line DCG is B2 throughout - its `e(G,A,B)` and the digit lists `int//1`
builds - so `giso` is B2. Freeing it needs variables that can live outside
frames: heap variables with structure copying on escape, or a collector
that reclaims unreferenced frames and heap. That is the frame-ownership
rework sections 3 and 4 come back to, not a change at the binding.

**`bagof/3` and `setof/3` (landed).** `giso`'s compile phase calls
`setof/3` for every dart, and the library versions made B2 terms of their
own. Each call wrapped every solution as `W-[+T]`, walked the list twice
more in Prolog building the result through head arguments, and found the
free variables with three helpers built the same way. One `setof/3` over
200,000 solutions peaked at 300,014 frames and took 145 ms, against 6
frames and 23 ms for `findall/3` plus `sort/2`, and in a recursive loop
each call left 10 frames. Now a goal with no free variables goes straight
to `findall/3`, then a non-empty check or `sort/2`, as in SWI, and
`'$free_variable_set'/3` in C replaces the helpers. That `setof/3` takes
24 ms and 6 frames, and the loop leaves 2 frames a call: the B1
`findall/3` row above, which any predicate returning a `findall/3` result
shows. Goals with free variables still take the Prolog path. `giso`'s
compile pattern went from 1.33 s to 1.00 s, against 0.98 s for
`findall/3` plus `sort/2` and 0.08 s under SWI; chess, which calls
neither, runs 0.11% more instructions. Regression test:
`tests/sundry/bagof_setof.pl`.

**What the parse holds, and which rule holds it (open).** Measured again on
`9df92c4e` in plain Prolog - each line read with `read_line_to_codes/2`,
parsed by the line DCG, asserted - across all four of `giso`'s data sets:

| data set | lines | frames | slots | trail | heap | RSS | SWI |
|---|---|---|---|---|---|---|---|
| g64000 | 151,918 | 1,605,342 | 8,631,399 | 4,258,612 | 308,956 | 687 MB | 44 MB |
| g128000 | 306,227 | 3,531,682 | 18,880,338 | 9,479,503 | 617,569 | 1.45 GB | 74 MB |
| g256000 | 615,551 | 7,954,757 | 42,233,006 | 21,629,843 | 1,236,222 | 3.12 GB | 133 MB |
| g512000 | 1,233,452 | 16,804,279 | 88,952,220 | 45,943,084 | 2,472,024 | 6.47 GB | 251 MB |

It is linear in lines at every size, about 11.6 frames, 56 slots and 28
trail entries a line, held to the end of the run. The heap is not the cost
- 2.5M cells at the largest - and neither is the database. The same parse
with its per-line loop failure-driven instead of recursive ends at 4,022
frames, 17,126 slots, 50 trail entries and 844 MB, in the same time (5.47 s
against 5.75 s) and with the same output.

A build counting each rule in `set_var()` (`-DPIN_CENSUS`) says which one
holds them. Over `g64000`:

| rule | events | per line |
|---|---|---|
| `set_var()` calls | 11,029,691 | 73 |
| var-var head rule (section 4) | 3,045,843 | 20 |
| `pin_query` | 603,177 | 4 |
| `pin_v` | 603,044 | 4 |
| `pin_cur` | 151,962 | 1 |
| ground clause term exempted | 8 | ~0 |
| frames pinned for the first time | 451,085 | 3 |

Four things follow. The recursive and failure-driven runs fire the *same*
events to within 0.1%, differing only in frames pinned for the first time
(451,085 against 2,076): what separates 6.47 GB from 844 MB is reclamation,
not the bindings. Discarding the parsed term changes nothing at all - a
loop that parses and drops the term has a census identical in every field
to one that asserts it - so this is the DCG's own threading, not the result
escaping. The ground-term exemption is useless here, firing 8 times in a
whole parse, because everything the DCG builds carries variable cells;
resolving ground escapes would not help. And the trail is a symptom: the
failure-driven parse makes the same 6.1M trailed bindings and peaks at 38
entries, because an entry is only added when the binding's frame is not the
newest, which stops being true as soon as a loop stops reusing its frame.
The var-var head rule of section 4 is the larger half of this and `pin_v`
the smaller, so neither section on its own would collapse it.

## 6. What a precise answer would free

An oracle build (`-DREACH_ORACLE`, `src/reach_oracle.h`) marks every frame
reachable from the real roots - the running continuation's chain of
ancestors, and each choicepoint's - and asks, at every decline to reuse or
recover a frame, whether that frame is among them. The frame under question
is not itself a root, or the answer would always be yes.

| workload | frames live at a sample | reachable from the roots |
|---|---|---|
| the line DCG over 20 files | 12,360 | 6.9 |
| the same over one file, every decline sampled | 448 | 7.1 |
| `sum/3`, section 4's shape | 9,955 | 3.0 |
| a callee returning `f(a,_)`, section 5's shape | 100,946 | 4.0 |

Almost none of it is live. A mechanism that knew would free about 99.9% of
the frames in all three shapes, so `giso`'s 6.47 GB is not held by anything
- the engine simply never asks a second time.

Sampling every decline over one file splits them in a way that says where
to start. Of 527 declines to **recover** a frame, 471 were frames nothing
reachable held, and 459 of those were named by a trail entry. Of 265
declines to **reuse** one, none: at the moment a tail call asks, the frame
really is still shared, and `sum/3` agrees at 200 of 200. The pins are
right when they are asked and stale shortly after, which is why no better
predicate at the binding can work (section 4's five attempts) and why the
question has to be asked again later.

That puts the trail first. A trail entry names a frame by index, which is
what made recycling an index corrupt clpb in section 3, and it is the only
real holder of 459 of those 471 frames. Entries that do not hold a frame
index are a prerequisite for relaxing any pin, not a saving on their own:
the sticky `no_recov` still blocks the recovery until the question is asked
again at return. Heap variables or a collector remain the general answer;
this is the part of it that the measurements say to build first.

The mark follows every live frame's slots, the terms their indirects point
at, and attribute lists; it does not follow the union-typed fields of a
choicepoint's saved state, so it can only over-estimate what is reachable.

## 7. What a collector would free, simulated

Section 6 asked whether a frame was still reachable at a decline. This asks
the collector's question instead: at a goal boundary, mark from the roots
and count both what is reclaimable and what a collector would have to fix
up. `-DGC_SIM`, `src/gc_sim.h`, sampling every N goals from `start()` just
after `q->total_goals++`, where `q->st` is canonical and no builtin is
mid-flight. Nothing moves; it only counts.

| workload | frames live of total | reclaimable | slots reclaimable |
|---|---|---|---|
| the line DCG over 20 files | 7 of 13,261 | 99.94% | 99.93% |
| the same over `g64000` | 8 of 898,363 | 100.00% | 100.00% |
| `sum/3`, section 4's shape | 4 of 116,228 | 100.00% | 100.00% |
| chess | 696,692 of 1,315,120 | 47.0% | 45.7% |

chess is the honest case: it holds 180,968 choicepoints at a sample, so
half of it really is live, though its worst sample was 7% live. The
recursive workloads are all garbage, continuously.

**None of it can be popped.** Splitting the dead frames by position, every
one of them lies below a live frame:

| workload | dead frames poppable off the top | stranded below a live one |
|---|---|---|
| the line DCG over `g64000` | 0 | 898,353 |
| `sum/3` | 0 | 116,224 |
| chess | 0 | 618,428 |

Dead slots the same: 1 poppable against 4,832,246 stranded in the parse, 0
against 4,466,584 in chess. The frame under the running one is always
live and everything dead sits beneath it, so popping the stack harder, a
sharper recovery test, or a nursery over the top region cannot free a
single frame. That is the structural reason section 4's five attempts and
the two tried in section 6's wake all failed: each was a way of freeing the
top.

**The fix-ups.** A holder that names a frame the mark does not traverse is
work a collector must do, and one that cannot be accounted for is a missed
root.

- **The trail carries nearly all of it.** 2,382,086 of 2,382,094 entries
  name a dead frame in the `g64000` parse, and 799,487 of 1,861,486 in
  chess. Renumbering or dropping them is not optional, which is what makes
  `docs/trail-frame-index.md` a prerequisite for this rather than the
  dead end it is on its own. No layout entry (#841) was ever among them.
- **Choicepoints are clean.** Not one of chess's 180,968 per sample named a
  dead frame, so marking the continuation each would resume into covers
  them.
- **Undo lists were empty** in all four workloads. Absence of evidence.
- **One root was missing, which is the point of simulating first.**
  `q->st.key`, the goal being dispatched, named a frame the mark called
  dead at every `sum/3` sample and at 6 of 15 `g64000` ones. Rooting it
  moves the live counts by 1 to 5 frames, so it costs nothing - but a
  collector that had not known would have freed the frame holding the
  arguments of the goal it was about to run.

- **And so is a choicepoint's saved goal**, once it can be read at all.
  `key`/`key_ctx` shared a union with the retry state builtins keep, so a
  saved `key_ctx` was unreadable from outside. Taking them out of the union
  costs 16 bytes a choicepoint and the suite passes 451/451 with it. The
  saved goal then turns out to name a frame the mark called dead at 13 of
  15 samples of the `g64000` parse; rooting it takes the live set from 7
  frames and 45 slots to 9 and 60. Chess has none.

So the roots are the running continuation, the continuation each
choicepoint would resume into, the dispatched goal, and each choicepoint's
saved goal; the only heavy fix-up is the trail; and the prize is every
frame in a recursive workload and about half in a backtracking one.

**Not yet cleared.** Heap liveness is not simulated: the parse's heap is
2.5M cells against 89M slots, so frames and slots are the prize, but a
moving collector would have to walk the heap too, and nothing here says
what that costs. The count of what a collection would free is also taken
at a goal boundary with `q->st` canonical; a collector that ran anywhere
else would have more to enumerate.

## 8. What a collector may move, and what it must renumber

An audit of every pointer that survives a goal boundary, to decide what a
collector could be. The answer turns on one asymmetry: **a frame is
referenced by index** - `(ctx, var_num)` - **while the heap is referenced
by raw `cell *`**. Raw heap pointers are held in too many places to
enumerate honestly, but nothing has to move a heap cell in order to
reclaim a frame. So: leave the heap where it is, compact frames and slots,
renumber indices.

**Renumbered when a frame's index changes.** All enumerable, and section
7's mark already walks most of it:

- every ref and indirect cell in a slot, its `val_ctx`
- the same fields in heap cells - a full scan, rewriting in place, moving
  nothing
- trail entries' `val_ctx`, which is 99.99% of them in the parse
- `f->prev`, and `f->idx`, which `get_ordered_slot_num()` numbers
  variables with
- each choicepoint's `st.cur_ctx`, `st.key_ctx` and `st.fp`, and `q->st`'s
  own copies

**Repointed when slot storage moves.** `f->slots` and `f->ovf`, `ch->slots`
and `ch->ovf`, `q->st.sp` and `q->st.sp_page` with each choicepoint's - and
a trap: a trail layout entry (#841) stores a `slot *` in its `attrs`
field, so the trail holds slot pointers as well as frame indices.

There is precedent for the mechanism. `layout_frame0()` already grows the
first slot page and rewrites every frame's `slots` and `ovf` by offset. It
is narrower than a compactor - its own comment notes it runs only while
nothing else can point into that page - but the frame walk and the offset
rewrite are there.

**Untouched, and this is what makes it tractable.** `q->st.instr`,
`f->instr` and each choicepoint's `st.instr` point into heap-built
instruction sequences, assigned in 27 places across `bif_control.c` and
`bif_predicates.c`; `q->st.key` and each choicepoint's saved key; the
`val_attrs` attribute lists; `tr->attrs`. Every one of them stays valid
because no heap cell moves. Undo items are malloc'd separately and freed on
backtracking, inside neither region. Clause-side state - `q->st.dbe`,
`q->st.pr`, iterators and the prefetch - is module-owned.

**What it costs.** A full heap scan per collection to rewrite `val_ctx`
fields: cheap for the parse at 2.5M cells, but the dominant cost in a
heap-heavy program, and exactly what a remembered set would avoid - the
pin sites in `set_var()` already fire on every old-to-young reference, so
the write barrier a generational scheme needs is in place and only its
bookkeeping is missing. Plus the trail rewrite.

Nothing in the audit blocks the approach.

## 9. Whether to build it

Not now, and the measurements say why on both sides.

**What it would gain.** `giso_10`'s parse holds 6.47 GB. The same parse with
its per-line loop failure-driven - the shape where frames are reclaimed -
holds 844 MB, and that 844 MB is database and heap, not frames. A collector
approaches the same figure, so about 7.7x, with the peak set by the
collection threshold rather than by the size of the input. `sum/3`'s
3,000,004 frames and 920 MB become bounded the same way. Sections 4 and 5
stop being on the critical path without being solved: the frames are still
pinned, they just stop mattering. Programs that exhaust memory today would
run, and the pins could become hints rather than verdicts, which would make
future work here cheaper to attempt.

**What it would cost.** A collection measured at 0.34ms for `sum/3`,
6.73ms for the parse and 96.73ms for chess. At one collection per 2M goals
chess pays 23% of its runtime to reclaim 47% of its frames, and 72 of those
97ms are the mark, which is proportional to live data and irreducible while
it is stop-the-world - so the workloads that pay most benefit least. Pauses
of that size are also unpredictable in a way an embedded or streaming user
would notice. Trail generation stamps cost +50% on a trail that peaks at
45.9M entries, and taking the saved key out of its union costs 16 bytes a
choicepoint, 6.6MB on chess. Then the permanent part: renumbering touches
slots, heap cells, trail entries, frame links, choicepoint state and the
query's own `ball`, `cont`, `variable_names` and `suspect`, and every future
feature that stores a `cell *` or a frame index has to register with the
collector from then on. The failure mode is silent wrong answers, which
this report has already shown to be expensive to diagnose. And it buys no
speed at all: the compile phase's remaining gap and the 0.9s parse are
untouched.

**Nothing cheaper is left.** Reclaiming the top of the stack is
arithmetically impossible here - 100% of dead frames are stranded below a
live one, in all three workloads (section 7). Dropping a frame's trail
entries is a no-op (`docs/trail-frame-index.md`). Narrowing the test at the
binding has now failed seven times: five in section 4, plus resolving ground
escapes and tidying the trail, both measured dead in section 6's wake.

**If it is ever built**, generational from the start - collect above the
newest choicepoint, with `set_var()`'s existing pin sites as the remembered
set, so chess's 696,692 live frames are never walked. A stop-the-world
sweep taxes backtracking code 20% for almost nothing.

**The honest alternative.** `giso`'s parse written failure-driven runs in
5.47s against 5.75s, produces identical output, and holds 844MB against
6.47GB. An application of that shape is better written that way, and the
measurements here are a better argument for writing it that way than for
carrying a collector.

---

# Addendum: the same subsystem, found from the other end

Found while working on native DCGs, which is how `...//0` came into it —
nothing here is DCG-specific. Repro: `disj_quadratic.pl` in the repo root.

## The symptom

A recursive predicate with **any goal after the recursive call** is
quadratic. n=20000, same logic three ways:

```prolog
two(A,B)   :- A = [_|C], two(C,B).            %    4 ms
two_t(A,B) :- A = [_|C], two_t(C,B), true.    % 1164 ms
fwd(A,B)   :- ( A = B ; A = [_|C], fwd(C,B) ). % 1215 ms
```

Driven by `call(G), R == []` with `R` unbound, so the caller backtracks
into every intermediate choice point. Calling `P(L,[])` directly prunes
the search and all three look linear — the cost only appears on re-entry.

`fwd` is not a disjunction problem: `;` compiles to
`$succeed_on_retry, LHS, $jump, RHS, true`, and that landing `true` is
simply a goal after the recursive call. `two_t` reproduces it with no
disjunction at all.

Not universal. Scryer runs the disjunction form at 1.06x its two-clause
form; SWI shows no difference. Trealla is 300-700x.

## What it is not

Ruled out by measurement, so as not to be re-tried:

- **Memory.** Byte-identical maximum RSS between the fast and slow forms.
- **`trim_heap()` in `retry_choice()`.** Disabling it outright changes
  nothing (1186ms -> 1239ms).
- **`trim_trail()`.** Breaks on the first retained entry; bounded.
- **The `no_recov` pin** that `succeed_on_retry` sets (see
  `norecov-notes.md`). Disabling it changes nothing (1163 -> 1173ms).
- **TCO.** Zero TCOs in *both* forms - `commit_any_choices()` correctly
  blocks frame reuse while the alternative branch or clause is live. The
  fast form is not winning by getting TCO.
- **Retries, backtracks, choice points, frame counts.** All identical:
  frames 20004, choices 6, backtracks 10000, retries 20101.
- **Goal dispatch.** Short-circuiting the no-op `true` in the main loop
  takes goals from 50,085,023 to 80,021 - level with the fast form - and
  time only from 1163ms to 889ms. The goal counter was the visible
  symptom, not the mechanism.

## What it is

The frame-unwind loop in `start()`:

```c
while (!q->st.instr || is_end(q->st.instr)) {
    if (resume_frame(q)) { proceed(q); continue; }
    ...
}
```

It walks the frame chain on every return and increments no counter,
which is why it survived all of the above.

When the recursive call is genuinely last, the frame's `ret_instr`
points straight at the caller's continuation and the loop exits after
about one iteration. With anything after the call - a user's `true`, or
the landing the compiler plants - each frame owns a distinct
continuation, so the chain unwinds one level at a time: O(depth) per
return, O(n^2) over O(n) returns.

## Two fixes that do not work

**Removing the landing** from `compile_term()`'s disjunction case gives
the full speedup (1163ms -> 4ms) and breaks 19 tests, including
`tests/misc/tabling.pl` and a dozen core control tests. Two mechanisms
read the instruction stream to decide whether a call is really last -
the positional test in `process_cell()` and `is_last_call()` in
`query.c` - so deleting the cell makes calls that were not last look
last, and `reuse_frame()` then discards continuations that were needed.
Wrong answers, not just slowness. The landing is doing two jobs: jump
target, and a barrier that keeps TCO honest.

**Skipping its dispatch** in the main loop is safe - it leaves the cell
in place so both mechanisms still see it - but only recovers 25%,
because the frame walk remains. The fix below leaves the cell in place
too, and addresses the frame walk instead.

## What would work, and what was done

Collapse the continuation: when setting up a call whose only remaining
continuation is no-op landings, point the new frame past them at the
parent's continuation. That is exactly the information `is_last_call()`
already computes, applied to the return chain rather than to frame
reuse.

**Done** (`1954a4e`), and smaller than expected, because `push_frame()`
already had the optimisation — the block commented "Avoid long chains of
useless returns" — but tested only whether the cell immediately after
the call was the clause end. The two mechanisms differed only in reach.
The walk is now factored out as `skip_landings()` and shared by both, so
they cannot drift apart again:

```c
const cell *next_cell = skip_landings(q->st.instr + q->st.instr->num_cells);
```

Nothing is removed from the instruction stream, so `process_cell()`'s
positional test and `is_last_call()` still see every cell — which is what
broke 19 tests when the landing itself was deleted.

n=40000: `dots_disj` 6426ms -> 14ms, `dots_trail` 6545ms -> 16ms, all
four forms now within noise of each other. Solution sets and their order
are unchanged, `tests/misc/tabling.pl` passes, and differential testing
across `samples/` and `library/` found no output differences. Regression
test: `tests/issues/test1106.pl`, which asserts the ratio between the
trailing-goal form and the last-call form rather than any absolute time.

`dots_trail` in `disj_quadratic.pl` is still the better case to hand to
other systems, having no disjunction in it at all — and note Trealla now
runs it in 16ms where SWI takes 1655ms and Scryer 3712ms, both of which
are quadratic on that shape.

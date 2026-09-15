# TCO in the THEN branch of if-then-else

Originally against `trealla-prolog/trealla` @ `b932785` ("Tidy up
parser.c a bit"). Re-verified against `1954a4e`.

## Status

| section | state |
|---|---|
| 1-2. Tail-position marking, `commit_any_choices()` | **landed** |
| 3. The `no_recov` pin | **landed** - the pins are removed, see the end of section 3 |
| 4. The accumulator idiom | **open** - unchanged |
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

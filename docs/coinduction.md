# Coinduction status

**Status: not supported.** Logtalk skips its coinduction example on Trealla,
because the Trealla adapter declares the feature unsupported. With that flag
forced on, 19 of the 29 tests pass. All 10 failures come from cyclic terms
passing through `bagof/3`, which Trealla has refused by design since #989.
Support would need `findall/3` to keep cycles. That change was made once and
reverted the same day, and the reason was not recorded.

Checked 2026-11-02 on main at `459701f3`, with Logtalk 3.103.0-b01, against
SWI-Prolog 10.1.19 and Scryer.

## How Logtalk decides

`adapters/trealla.pl:292` has:

```prolog
'$lgt_prolog_feature'(coinduction, unsupported).
```

That read-only flag does three things:

- `examples/coinduction/tester.lgt` prints `(not applicable)` and runs nothing.
- The `coinductive/1` directive throws `resource_error(coinduction)`
  (`core/core.pl:11020`).
- A compile-time lint that warns when a unification in a clause body would
  build a cyclic term is skipped (`core/core.pl:14438`). Its comment says it
  is guarded because it "would end badly" on a backend without cyclic terms.
  Trealla runs it without trouble.

Adapters that declare coinduction supported:
- SWI-Prolog, SICStus, YAP, ECLiPSe, CxProlog and Shumway.
- XVM, when started with `--enable-occurs-check`.

Adapters that declare it unsupported:
- GNU, Ciao, B-Prolog, JIProlog, Quintus, Tau, XSB and Trealla.

This Logtalk has no Scryer adapter.

## Forcing the flag

Copy the adapter and its integration file to a scratch directory, so that
`~/logtalk3` stays untouched:

```bash
S=/path/to/scratch
mkdir -p $S/adapters $S/integration
sed "s/'\$lgt_prolog_feature'(coinduction, unsupported)/'\$lgt_prolog_feature'(coinduction, supported)/" \
	$LOGTALKHOME/adapters/trealla.pl > $S/adapters/trealla.pl
cat > $S/integration/logtalk_tp.pl <<EOF
:- ensure_loaded('../adapters/trealla.pl').
:- ensure_loaded('$LOGTALKHOME/paths/paths.pl').
:- ensure_loaded('$LOGTALKHOME/core/core.pl').
EOF
cd $LOGTALKHOME/examples/coinduction
timeout 120 tpl -l $S/integration/logtalk_tp.pl -g "logtalk_load(tester),halt"
```

The banner then reports `coinduction: supported`, and the example compiles
cleanly.

## Results with the flag forced

19 of 29 pass. They are `arithmetic_2`, `simple_1` to `_3`, `binary_1` to
`_3`, `streams_2`, `filter_1`, `lists_1` to `_4`, `counter_1`, `sieve_1` and
`graph_1` to `_4`. Many of them build cyclic terms and check them with `==`.
The other 10 all collect solutions with `bagof/3`:

| test | Trealla's result | cause |
|---|---|---|
| `streams_1` | `type_error(acyclic_term, _)` from `'$queue'/1` | cyclic answers |
| `automata_1` | same | cyclic answers |
| `nested_1` | same | cyclic answers |
| `cyclic_paths_1` | same | cyclic answers |
| `cyclic_paths_2` | same | cyclic answers |
| `shared_paths_1` | same | cyclic answers |
| `tangle_1` | same | cyclic answers |
| `arithmetic_1` | fails | cyclic term in the goal |
| `lists_5` | fails | cyclic term in the goal |
| `lists_6` | fails | cyclic term in the goal |

**Cyclic answers.** `findall/3` queues each answer through `'$queue'/1`, which
raises `type_error(acyclic_term, _)` when its copy has had to cut a cycle
(`bif_sys_queue_1()` in `src/bif_predicates.c`). `bagof/3` is built on
`findall/3`.

**Cyclic goal.** `bagof/3` and `setof/3` start with `acyclic_term(G0)`
(`library/builtins.pl:631` and `:657`), so a cyclic list among the goal's free
variables makes them fail without an error. Removing that guard alone would not
help. The free variables become the witness that `findall/3` queues, so they
would then hit the type error above.

## Other Logtalk tests behind the same flag

The flag also gates cyclic-term tests in 27 of Logtalk's Prolog conformance
suites (`tests/prolog/control/throw_1` and 26 under
`tests/prolog/predicates`), plus `tests/logtalk/directives/coinductive_1`.
Normally those tests are skipped. With the flag forced, as above, all of them
pass except the three below. A handful of tests in `arg_3`, `functor_3`,
`keysort_2`, `length_2` and `univ_2` are skipped for other reasons.

| test | Trealla's result | cause |
|---|---|---|
| `lgt_bagof_3_22` | fails | cyclic goal `X = f(X,Y,Z)` hits the `bagof/3` guard |
| `lgt_setof_3_36` | fails | same, in `setof/3` |
| `commons_length_2_27` | fails, error expected | `length/2` fails on a cyclic list, as on any non-list |

The `length/2` behaviour is by design: GNU, Scryer, ECLiPSe and Ciao also fail
on a non-list, while Logtalk expects SWI's type error. Six other `length_2`
tests fail for the same reason without the flag. `copy_term_2` passes all 19;
before 2026-11-02 it failed one, an inner-subterm cycle.

To run one of these suites with the flag forced, change to its directory and
run the forced `tpl -l ... -g "logtalk_load(tester),halt"` command from above.

## What Trealla does with cyclic terms

Each row takes a term such as `X = f(X)`.

| operation | Trealla | SWI | Scryer |
|---|---|---|---|
| `=`, `==`, `compare/3`, `acyclic_term/1` | correct | correct | correct |
| `copy_term/2`, cycle through the root | kept | kept | kept |
| `copy_term/2`, `copy_term_nat/2`, cycle closing on an inner subterm | kept | kept | kept |
| `duplicate_term/2`, `copy_term/3`, cycle closing on an inner subterm | **cut silently** | kept | kept |
| `findall/3` with a cyclic answer | `type_error(acyclic_term)` | kept | kept |
| `bagof/3`, `setof/3` with a cyclic goal | fail | succeed | succeed |
| `assert` family, `recorda`, `recordz` | `representation_error(cyclic_term)` | `representation_error(cyclic_term)`; records keep cycles | `representation_error(term)` |
| `nb_setval/2`, `bb_put/2`, `bb_update/3` | `representation_error(cyclic_term)` | `nb_setval/2` keeps them | no such builtins |
| `b_setval/2`, `bb_b_put/2` | kept | kept | no such builtins |

The inner-subterm case: with `P = g(Q), Q = h(P)`, `copy_term(x(P), C)` used to
give `C = x(g(h(_)))`, which is acyclic. `copy_term(P, D)` always kept the
cycle. `copy_term/2` and `copy_term_nat/2` now keep both (see below).
`duplicate_term/2`, `copy_term_with_attributes/2` and `copy_term/3`, which is
built on it, still cut the inner case. They copy attribute values with a nested
call to the same copier, which would share its cycle bookkeeping, so cycle
closing stays off for them (`do_copy_term()` in `src/bif_predicates.c`).

## Why a stored copy cannot hold a cycle

In Trealla a cycle exists only through a variable bound in a frame slot. Stored
copies are contiguous, self-contained blocks of cells with no frame of their
own:
- the findall queue
- clauses
- global variables and blackboard entries

When the copier reaches a back-edge, it has no slot to point the edge at, so it
writes a fresh variable and sets `q->cycle_dropped`. Since `d4849c4a` and
`459701f3`, every one of these copies checks that flag and raises an error, so
no cycle is cut silently.

`copy_term/2` keeps a cycle through the root by making the back-edge point at
the target variable, which is then bound to the copy (`cycles_back()` in
`src/heap.c`).

`copy_term/2` keeps a cycle that closes lower down by binding the back-edge's
fresh variable, once the copy is on the heap, to wherever that slot's value was
copied (`close_clone_cycles()`, from #1121). It finds out where by recording,
for each bound variable it passes through, the offset its value is copied to.
Recording costs a map entry per bound variable, so the copier skips it on the
first pass. It copies again with recording on only when the first pass had to
cut a cycle (`record_clone_defs`). Until 2026-11-02 it recorded only list tails,
and on every copy. Because of that:
- A cycle closing on an argument or a list head was cut.
- Every acyclic copy of a list paid for the recording. A 20,000-element list
  built by recursion now copies in about a third of the instructions.
- The only cost to acyclic copies is the test for whether to record, which adds
  about 1% on deep compound terms and 0.3% on giso_07. Chess is unchanged.

`b_setval/2` and `bb_b_put/2` keep cycles because their values still refer to
the caller's variables, which are pinned while the value lives.

None of these tricks works for a copy that has to outlive the term it came from.

SWI and Scryer store terms as pointer graphs, so a cycle is just a pointer.
The same difference makes Trealla's copies exponential on heavily shared terms,
because a shared subterm is written out again each time the copier reaches it.

## History

- **#989** (flexoron): `bagof/3` returned answers whose cycles had been
  replaced by fresh variables, so they looked plausible and were wrong.
- **`1516083f`** (2026-08-19) made `findall/3` keep cycles. While copying, it
  recorded each back-edge as a pair of offsets into the copy. When `findall/3`
  emptied the queue, it rejoined each edge by unification, once the answers
  were real terms in a frame. Acyclic terms were untouched. Its message lists
  what was still outstanding:
  - inner-subterm cycles in `copy_term/2`, fixed on 2026-11-02;
  - `assert` losing cycles, which now raises an error;
  - `write/1` cycle detection, which has since been fixed;
  - the `bagof`/`setof` guard, which is still open.
- **`a65d70f1`** reverted it the same day, with no reason given.
- **`5d6f078b`** then made `'$queue'/1` refuse cycles instead, which is the
  current behaviour.
- **`d4849c4a`** (2026-11-02): the `assert` family raises
  `representation_error(cyclic_term)` instead of storing a clause with its
  cycle cut.
- **`459701f3`** (2026-11-02): `nb_setval/2`, `bb_put/2` and `bb_update/3` do
  the same.
- **2026-11-02**: `copy_term/2` and `copy_term_nat/2` keep a cycle that closes
  on an inner subterm, using the second pass described above.

## What support would take

1. **Make `findall/3` keep cycles.** This fixes 7 of the 10 failures. The
   change in `1516083f` no longer applies, because `src/heap.c` has changed a
   lot since August. It would have to be written again, and the reason for the
   revert should be known first.
2. **Remove the `acyclic_term(G0)` guard** from `bagof/3` and `setof/3`. With
   step 1, this fixes the last 3. `keysort/2` and `sort/2` would then see
   cyclic witnesses; `compare/3` has handled cycles since `9b7a087b`.
3. **Rerun the example with the flag forced on.** Then ask Paulo Moura to
   change the adapter line.

For step 1, the second pass that `copy_term/2` now uses already finds where each
cyclic slot's value lands in a copy. Those are the offsets `1516083f` recorded
by hand. Storing them with each queued answer and closing the cycles when the
queue is emptied is likely the bulk of what remains.

Coinduction was not pursued on 2026-11-02, as the change is to the engine and
Trealla is in maintenance mode.

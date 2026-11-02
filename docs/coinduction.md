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

## What Trealla does with cyclic terms

Each row takes a term such as `X = f(X)`.

| operation | Trealla | SWI | Scryer |
|---|---|---|---|
| `=`, `==`, `compare/3`, `acyclic_term/1` | correct | correct | correct |
| `copy_term/2`, cycle through the root | kept | kept | kept |
| `copy_term/2`, cycle closing on an inner subterm | **cut silently** | kept | kept |
| `findall/3` with a cyclic answer | `type_error(acyclic_term)` | kept | kept |
| `bagof/3`, `setof/3` with a cyclic goal | fail | succeed | succeed |
| `assert` family, `recorda`, `recordz` | `representation_error(cyclic_term)` | `representation_error(cyclic_term)`; records keep cycles | `representation_error(term)` |
| `nb_setval/2`, `bb_put/2`, `bb_update/3` | `representation_error(cyclic_term)` | `nb_setval/2` keeps them | no such builtins |
| `b_setval/2`, `bb_b_put/2` | kept | kept | no such builtins |

The inner-subterm case: with `P = g(Q), Q = h(P)`, `copy_term(x(P), C)` gives
`C = x(g(h(_)))`, which is acyclic. `copy_term(P, D)` keeps the cycle. The
coinduction tests don't depend on this, but it is a wrong answer given without
an error.

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
`src/heap.c`). `b_setval/2` and `bb_b_put/2` keep cycles because their values
still refer to the caller's variables, which are pinned while the value lives.
Neither trick works for a copy that has to outlive the term it came from.

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
  - inner-subterm cycles in `copy_term/2`, which are still open;
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

Fixing the inner-subterm cycle in `copy_term/2` is separate from this, but is
the same kind of problem.

Coinduction was not pursued on 2026-11-02, as the change is to the engine and
Trealla is in maintenance mode.

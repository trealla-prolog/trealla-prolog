# Coinduction status

**Status: works from v3.14.42, but Logtalk doesn't know it yet.** With the
adapter's coinduction flag forced on, all 29 tests in Logtalk's coinduction
example pass, as do the flag-gated tests in Logtalk's conformance suites, apart
from one by-design `length/2` case. Logtalk still skips the example on
Trealla, because the Trealla adapter declares the feature unsupported. The
change to that line is requested in
[LogtalkDotOrg/logtalk3#265](https://github.com/LogtalkDotOrg/logtalk3/issues/265).

Two changes made the example pass:
- `findall/3` keeps cyclic answers. Before, it refused them with
  `type_error(acyclic_term)` (#989).
- `bagof/3` and `setof/3` accept a cyclic goal. Before, they failed on one.

Checked 2026-11-02 on main, with Logtalk 3.103.0-b01, against SWI-Prolog
10.1.19 and Scryer.

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

The banner then reports `coinduction: supported`. To run another flag-gated
suite, change to its directory and run the same `tpl` command.

## Results with the flag forced

**The coinduction example:** all 29 tests pass. Before the `findall/3` and
`bagof/3` changes, 10 of them failed, all while collecting solutions with
`bagof/3`:

| tests | Trealla's result then | cause |
|---|---|---|
| `streams_1`, `automata_1`, `nested_1`, `cyclic_paths_1`, `cyclic_paths_2`, `shared_paths_1`, `tangle_1` | `type_error(acyclic_term, _)` from `'$queue'/1` | cyclic answers |
| `arithmetic_1`, `lists_5`, `lists_6` | failed | cyclic term in the goal |

**The conformance suites.** The flag also gates cyclic-term tests in:
- 27 of Logtalk's Prolog conformance suites (`tests/prolog/control/throw_1`
  and 26 under `tests/prolog/predicates`);
- `tests/logtalk/directives/coinductive_1`.

Normally those tests are skipped. With the flag forced, all of them pass except
`commons_length_2_27`. A handful of tests in `arg_3`, `functor_3`,
`keysort_2`, `length_2` and `univ_2` are skipped for other reasons.

`commons_length_2_27` expects an error from `length/2` on a cyclic list. Trealla
fails instead, as it does on any non-list, and that is by design: GNU, Scryer,
ECLiPSe and Ciao also fail on a non-list, while Logtalk expects SWI's type
error. Six other `length_2` tests fail for the same reason without the flag.

Three more suites failed before 2026-11-02 and now pass in full:
- `lgt_bagof_3_22` and `lgt_setof_3_36` call the cyclic goal `X = f(X,Y,Z)`.
- `copy_term_2` failed one test, a cycle closing on an inner subterm.

## What Trealla does with cyclic terms

Each row takes a term such as `X = f(X)`.

| operation | Trealla | SWI | Scryer |
|---|---|---|---|
| `=`, `==`, `compare/3`, `acyclic_term/1` | correct | correct | correct |
| `copy_term/2`, `copy_term_nat/2` | kept | kept | kept |
| `duplicate_term/2`, `copy_term/3`, cycle closing on an inner subterm | **cut silently** | kept | kept |
| `findall/3`, `findnsols/4` with a cyclic answer | kept | kept | kept |
| `bagof/3`, `setof/3` with a cyclic goal or witness | succeed | succeed | succeed |
| `assert` family, `recorda`, `recordz` | `representation_error(cyclic_term)` | `representation_error(cyclic_term)`; records keep cycles | `representation_error(term)` |
| `nb_setval/2`, `bb_put/2`, `bb_update/3` | `representation_error(cyclic_term)` | `nb_setval/2` keeps them | no such builtins |
| `b_setval/2`, `bb_b_put/2` | kept | kept | no such builtins |

`duplicate_term/2`, `copy_term_with_attributes/2` and `copy_term/3`, which is
built on it, still cut a cycle that closes on an inner subterm. With
`P = g(h(P))`, `duplicate_term(x(P), C)` gives an acyclic `C`. These copy
attribute values with a nested call to the same copier, which would share its
cycle bookkeeping, so cycle closing stays off for them (`do_copy_term()` in
`src/bif_predicates.c`).

## How copies keep a cycle

In Trealla a cycle exists only through a variable bound in a frame slot. A copy
is a contiguous block of cells. When the copier reaches a back-edge, it has no
slot to point the edge at, so it writes a fresh variable and sets
`q->cycle_dropped`. Each kind of copy then deals with that in its own way.

**`copy_term/2`.** A cycle through the root is kept by pointing the back-edge
at the target variable, which is then bound to the copy (`cycles_back()` in
`src/heap.c`). A cycle closing lower down is kept by binding the back-edge's
fresh variable to wherever that slot's value was copied, once the copy is on
the heap (`close_clone_cycles()`, from #1121).

To know where each value went, the copier records, for each bound variable it
passes through, the offset its value is copied to. That costs a map entry per
bound variable. So the first pass records nothing, and the copier copies again
with recording on (`record_clone_defs`) only when the first pass had to cut a
cycle. Until 2026-11-02 it recorded only list tails, and on every copy.
Because of that:
- A cycle closing on an argument or a list head was cut.
- Every acyclic copy of a list paid for the recording. A 20,000-element list
  built by recursion now copies in about a third of the instructions.
- The only cost to acyclic copies is the test for whether to record, which adds
  about 1% on deep compound terms.

**The findall queue.** An answer is queued as a block of cells long before it
is drained into a list, so it needs more than `copy_term/2` does.

When the first copy cuts a cycle, `'$queue'/1` copies the answer again with
recording on (`clone_cyclic_for_queue()`). Each back-edge variable in that copy
names a recorded slot. It is replaced with a one-cell placeholder atom, and a
`qcycle` saying where it sits and where its slot's value starts is kept with
the queue.

The placeholder matters. The drain copies each answer with `noderef`, which
still dereferences a live variable, so a back-edge to a cycle that existed
before `findall/3` started would be expanded. That would shift every recorded
offset after it. An atom is copied as it is.

Once the answers are a list, `close_queued_cycles()` turns each placeholder
into a new variable bound to its subterm. It binds directly, as
`close_clone_cycles()` does, so an `occurs_check` flag cannot refuse it. This
covers all three drains:
- `findall/3`;
- `findnsols/4`'s last drain;
- the `'$list'/1` chunks `findnsols/4` returns along the way.

An acyclic answer takes exactly the old path. A findall loop and giso_07 run
slightly fewer instructions than before, because `bagof/3` and `setof/3` no
longer walk the goal for `acyclic_term/1`. Chess is unchanged.

**Clauses, global variables and blackboard entries** outlive the term they came
from, and have nowhere to rejoin a cycle. Since `d4849c4a` and `459701f3` they
raise `representation_error(cyclic_term)`. Before, they stored the copy with
its cycle cut.

**`b_setval/2` and `bb_b_put/2`** keep cycles because their values still refer
to the caller's variables, which are pinned while the value lives.

SWI and Scryer store terms as pointer graphs, so a cycle is just a pointer.
The same difference makes Trealla's copies exponential on heavily shared terms,
because a shared subterm is written out again each time the copier reaches it.

## History

- **#989** (flexoron): `bagof/3` returned answers whose cycles had been
  replaced by fresh variables, so they looked plausible and were wrong.
- **`1516083f`** (2026-08-19) made `findall/3` keep cycles. It recorded
  (back-edge, anchor) offset pairs while copying, and unified them after the
  drain. It was reverted the same day by **`a65d70f1`**, with no reason given.
  **`5d6f078b`** then made `'$queue'/1` refuse cycles instead.
- **`d4849c4a`** (2026-11-02): the `assert` family raises
  `representation_error(cyclic_term)` instead of storing a clause with its
  cycle cut.
- **`459701f3`** (2026-11-02): `nb_setval/2`, `bb_put/2` and `bb_update/3` do
  the same.
- **`b829f9d7`** (2026-11-02): `copy_term/2` and `copy_term_nat/2` keep a cycle
  that closes on an inner subterm, using the second pass.
- **`43e2895a`** (2026-11-02): `findall/3` and `findnsols/4` keep cyclic
  answers, using the same second pass, with placeholders instead of 1516083f's
  hand-recorded offsets. `bagof/3` and `setof/3` lose their `acyclic_term/1`
  guard. `tests/issues/test0989.pl` now checks that the issue's cases come back
  whole. Released, with `b829f9d7`, as v3.14.42.
- **[LogtalkDotOrg/logtalk3#265](https://github.com/LogtalkDotOrg/logtalk3/issues/265)**
  (2026-11-02) asks for the adapter change below.

## What is left

1. **The Trealla adapter has to declare coinduction supported.** This is
   requested in
   [LogtalkDotOrg/logtalk3#265](https://github.com/LogtalkDotOrg/logtalk3/issues/265),
   which suggests a version check in the style of the XVM adapter's. It was
   tested both ways: a 3.14.42 build runs the example and passes all 29 tests,
   and with the threshold one release higher it prints `(not applicable)`.

   ```prolog
   '$lgt_prolog_feature'(coinduction, Coinduction) :-
   	current_prolog_flag(version_data, trealla(Major, Minor, Patch, _)),
   	(	(Major,Minor,Patch) @>= (3,14,42) ->
   		Coinduction = supported
   	;	Coinduction = unsupported
   	).
   ```

2. **`duplicate_term/2` and `copy_term/3`** still cut a cycle that closes on an
   inner subterm, as described above.
3. **`commons_length_2_27`** stays an expected failure, along with the other
   `length/2` non-list cases.

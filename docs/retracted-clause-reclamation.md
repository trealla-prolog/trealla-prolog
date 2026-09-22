# Retracted clauses under concurrent readers

**Throughput fixed in `254d547e`. The memory half fixed for every single-threaded
shape, in `c9d7adb1`, `0469873e` and `30662303`, which also found that the title
of this document is misleading: the leak never needed concurrent readers. What
they do not fix is the concurrent case itself, which still needs a handshake.
See "It is not a concurrency bug" below.**

## The symptom

`make misc` appeared to hang in `tests/misc/db_purge_window.pl` after
`adde3399` made each predicate call about 30% faster. It had not hung. The
test runs a churner that asserts and retracts clauses of `p/2` in rounds,
alongside four threads that enter and leave `p/2` as fast as they can,
and it went from about 0.2s to 16s.

## The cause

`retract` stamps a clause's `dbgen_retracted` and puts it on `pr->dirty`,
but leaves it linked in the chain. `leave_predicate()` unlinked the dirty
clauses only when `pr->refcnt` reached zero. Threads reading the same
dynamic predicate in a tight loop keep that count above zero indefinitely,
so retracted clauses piled up in the chain and every lookup walked them.
The predicate never reaches the 500 live clauses that would build an
index, so each lookup was a linear walk.

Counting the largest dirty list while readers were inside:

| readers | largest dirty list | run |
|---|---|---|
| 2 | 148 | 0.107s |
| 4 | 29,740 | 6.24s |
| 8 | 30,000 | 28.3s |

30,000 is the churner's entire output, 200 rounds of 150 retractions: with
eight readers not one clause was reclaimed during the run. Profiling the
slow phase puts `match_head` at 17,247 samples against 1,697 waiting on the
lock. The bug predates `adde3399`: without it the same build takes 26.4s at
eight readers and 36.7s at twelve. The speedup only moved the threshold
from eight readers to four.

## Why the obvious fix is wrong

Unlinking a retracted clause while readers are inside breaks the logical
update view. By `can_view()`, a reader that entered at generation `D` sees
a clause retracted at `R` exactly when `R > D`, so a reader that entered
before a retraction is entitled to that clause, and one walking forward
from before it would silently skip it. Waiting for a zero count is a crude
but correct way of making sure no such reader is left.

## The fix

A retracted clause leaves the chain once every reader that entered before
its retraction has gone. `leave_predicate()` runs drains: it bumps the
global generation to `G` - an empty transaction, invisible to the view -
and counts the readers already inside, all of which entered before `G`.
Each of those leaving decrements the count; readers entering later have a
generation of at least `G` and never do. At zero, no reader can see a
clause retracted at or before `G`, and those come out of the chain onto
`pr->delinked`.

Three things make the count exact:

- `enter_predicate()` takes the module lock when multithreaded, so a
  drain's count of the readers present cannot race an arrival.
- A reader carries the generation it entered at in `run_state.pr_dbgen`,
  next to `pr`. `run_state` is copied whole into every choicepoint, so every
  leave site passes the generation from the same place it takes `pr`.
- `predicate_delink()` never touches the unlinked clause's own `next`, so a
  reader standing on it when it comes out can still step off.

`tests/misc/db_luv_drain.pl` holds the view to it. A reader pauses partway
through `q/1` while a churner retracts the half ahead of it and enters and
leaves `q/1` thousands of times, so drains try to complete around it. It
passes 8 in 8 against the fix and fails 8 in 8 against a build whose drains
ignore the readers that predate them, with the reader collecting only the
1,000 clauses left in front of it.

| readers | before | after |
|---|---|---|
| 4 | 15.4s | 0.16-0.29s |
| 8 | 26.4s | 0.85s |
| 12 | 36.7s | 1.12s |

`db_purge_window.pl` passed 30 in 30 and `db_concurrency.pl` 15 in 15;
the suite, `make misc` and Logtalk run clean. Single-threaded dynamic loops
are unchanged. Chess runs 0.4% more instructions: 0.1% is `run_state`
growing by 8 bytes, measured with a build that adds only that field, and the
rest is the drain checks on the dynamic `enter` and `leave` path, which
chess reaches constantly through `ply_depth/1`.

## It is not a concurrency bug

Measured in September 2026, 100,000 assert-and-retract cycles leaving one
clause live, single threaded throughout:

| loop shape | freed when | peak RSS |
|---|---|---|
| nothing, for scale | - | 9.95 MB |
| failure driven, `fail` backtracks into `between/3` | on each retry | 10.2 MB |
| `forall/2` | query teardown | 42.3 MB |
| deterministic, retract matches the last clause | query teardown | 55.2 MB |
| deterministic, predicate has other clauses | query teardown | 56.8 MB |

Counters on the two disposal paths confirm it: the deterministic loops put
100,000 of 100,000 rules on a list that is drained only in `query_destroy()`.
So the rule is not "readers hold retracted clauses", it is **a retracted
clause is freed only when the query backtracks past the retract, or the query
ends**. Concurrency multiplies it - 4 readers 106 MB, 8 readers 258 MB - but a
deterministic state-update loop, which is the obvious way to write one, keeps
every clause it ever retracts. A long-running query grows without bound on one
thread.

## What `c9d7adb1` frees

A clause is flagged `is_purgeable` at assert time when it is a fact whose head
arguments are all atomic. That is exactly the shape neither hard root can
reach: a compound head argument can be bound to, by `retract/1` or by
matching, and the binding outlives the call; and a body means both a cell
range to run and a compiled `cl->alt` block whose length is not recorded, so
it cannot be range-checked. `purge_reclaimed()` then frees such clauses off
`q->dirty` at `start()`'s goal boundary, once 512 have accumulated, after
scanning `q->st.instr`, the live frames and the choicepoints for a pointer
inside the clause or a choicepoint still naming it.

| workload | before | after |
|---|---|---|
| `forall(..., (assertz, retract))` | 42.3 MB | 9.95 MB, which is the floor |
| deterministic, predicate has other clauses | 56.8 MB | 24.6 MB, and 10.3 MB after `0469873e` |

99,840 of 100,000 are freed early; only the last sub-threshold batch waits.
Chess pays 0.16% more instructions for the goal-boundary check and giso is
unchanged.

Four things that look like obvious extensions are not, and cost real time to
learn:

- **Do not move rules from the undo list to `q->dirty`.** Rules on the undo
  list are freed with `clear_clause()` alone; everything on `q->dirty` pays
  `index_remove_clause()` in `query_destroy()`, an `sl_rem()` per rule against
  an index that can hold hundreds of thousands of entries with many sharing a
  key. Rerouting them added **80 seconds** to `giso`'s full tester, all of it
  after the last line of output, which reads as a hang. The test phase was
  unaffected, so nothing short of running that tester to completion would have
  caught it.
- **A purge pass that frees nothing must raise its own threshold.** Otherwise
  the count stays above it and every later goal walks the whole list again.
- **The purge must remove index entries itself.** `reclaim_rule()` only does
  so while `pr->cnt` is non-zero, so an index can still name the rule.
- **The flag belongs with the other clause bits.** Put between `num_vars` and
  `arg_sig`, it padded `clause` and shifted the flexible `cells[]` array,
  costing 3.5% of wall on chess for no change in instruction count.

## The second leak, in retract itself, fixed in `0469873e`

Retracted rules were not the whole of it. `match_clause()` called
`import_term()` for every clause it tried, which allocates a detached copy of
the clause and registers it with `undo_on_backtrack(..., UNDO_CELLS)`. The copy
is needed in general - unifying against a clause can leave the caller pointing
into cells that retract is about to take away - but the undo list releases it
only when backtracking undoes past it, so a deterministic loop kept one copy
and one 48-byte undo item per call.

Found by counting live allocations per exact size in the allocator: a 48-byte
class with 199,982 live of 650,641 made, absent from a control that asserted
without retracting. Resolving `__builtin_return_address(1)` as an offset from
`tpl_malloc` named `import_term+44` and `undo_on_backtrack+40` directly. That
technique is worth reaching for again; it took minutes where reading the code
had not worked.

The fix reuses `is_purgeable` from `c9d7adb1`: a fact whose arguments are all
atomic cannot be pointed into, because `unify()` copies such values into the
caller's slots, so `retract` matches against the clause itself and allocates
nothing. `clause/2` still gets a copy, since it hands the body back.

With the clause pool held at 50,000 and only the number of retracts varying:

| retracts | before | after |
|---|---|---|
| 50,000 | 35.0 MB | 28.4 MB |
| 200,000 | 54.1 MB | 28.7 MB |
| 800,000 | 131.2 MB | 28.5 MB |

**Measure a per-call leak with the pool size held fixed.** The first
measurement of this pre-asserted N clauses and then retracted N, so a
transient peak proportional to the pool read as growth per iteration, and an
imaginary 107-byte-per-call residual was reported here after the real leak was
already fixed. Vary the loop length against a fixed pool and the distinction
is immediate.

## The undo path, fixed in `30662303`

A rule on an undo list was freed only when backtracking undid past it, so the
one shape `c9d7adb1` did not reach - a deterministic loop whose `retract`
matches the last clause, which is the branch `reclaim_rule()` sends to
`undo_on_backtrack()` - still kept every rule until teardown.

Moving those rules to `q->dirty` is the obvious fix and it is the wrong one:
that is the change that cost 80 seconds above. `purge_undo_rules()` instead
walks `q->undo` and each live choicepoint's list in place, freeing any
`UNDO_RULE` item that passes the same two tests. It is sound because the undo
item's whole action is that free, so doing it early is the same work done
sooner, and index entries are gone either way - `reclaim_rule()` runs before
`leave_predicate()` destroys the index.

Every single-threaded shape now sits at the floor, which is 9.95 MB for a
query that touches no database at all:

| workload, 100,000 cycles | before any of this | now |
|---|---|---|
| `forall(..., (assertz, retract))` | 42.3 MB | 9.96 MB |
| deterministic, predicate has other clauses | 56.8 MB | 10.14 MB |
| deterministic, retract matches the last clause | 55.2 MB | 10.16 MB |
| 800,000 retracts against a fixed pool | 131.2 MB | 28.6 MB, flat |

Chess is 124.10 G instructions against 124.13 G, and `giso`'s full tester 63.5
seconds against 63.9, so none of it is paid for in time.

## What is still open: memory

Threads. The figures above are all single threaded, and the purge is gated to
that, because `clause_holds_instr()` scans only the current query: another
thread can hold `q->st.instr`, a frame's `instr` or a choicepoint's inside a
clause, and nothing here can see it. A churner against continuous readers
still holds everything it retracts - 100 MB with 4 readers, 258 MB with 8 -
exactly as it did before any of this work.

That needs the handshake described at the end of this document, bringing every
thread to its goal boundary for a round, where `start()` already checks each
thread's signals. An earlier attempt at one foundered because threads park on
their innermost query rather than on `t->q`.

## How it used to be, before `c9d7adb1`

Nothing is freed any earlier than before. Unlinked clauses wait on
`pr->delinked`, and are reclaimed exactly as dirty ones always were, when
the count reaches zero. So continuous concurrent readers still keep them
all: the chain stays short, but memory grows without bound for as long as
the readers keep going.

Freeing sooner needs a lifetime that predicate readers do not give.
After a last-match leave a query goes on running the clause body out of
the rule's own cells - `commit_frame()` reads `next_instr` from the clause
and then leaves - which is the crash at `q->st.instr` that
`db_purge_window.pl` exists to catch. And `retract` binds a caller's
variables to terms inside a clause's cells. A clause can only be freed
once no query is executing its body or holding terms out of it, which
means a grace period on queries rather than on the predicate's readers.
That is not attempted here.

## Could reader positions free them? Simulated

A query records where it is up to in a predicate: `q->st.dbe`, and the
`st.dbe` and prefetch iterator of each choicepoint. `assert_commit()` gives
every clause a `db_id` that increases with its place in the chain, readers
only move forward, and prefetch iterators run in `db_id` order - so a
clause below the lowest position of every active reader can never be
reached by iterating again. That alone does not make it safe to free,
because matching does not copy a clause. `match_head()` unifies the goal
against `get_head(cl->cells)` in the new frame, and `set_var()` makes a
caller's variable an indirect pointer straight into those cells, which
outlives the call. And after a last-match leave a query runs the body out
of the clause with no position recorded at all. So a round would need
three roots:

| root | what reaches the clause | cost |
|---|---|---|
| iteration | a reader's position at or before it | a low-water mark per predicate |
| running code | `q->st.instr`, a frame's `instr` or a choicepoint's `st.instr` inside it | frames and choicepoints |
| a bound term | an indirect cell in a slot or on the heap pointing into it | a scan of slots and heap |

A simulation measured which of them actually hold retracted clauses
(`-DFREE_SIM`, `src/free_sim.h`, not landed). Each retracted rule is
recorded by value when it is retracted - its cell range, the range of its
compiled body (`alt_len`, recorded by `compile_clause()` because the block
does not carry its own length), its owner - and dropped when
`clear_clause()` frees it, so a scan never reads a rule another thread may
have freed. Each thread samples its own state at its goal boundary.

Three controls prove each detector fires when it should and only then:

| control | running code | bound term | nothing |
|---|---|---|---|
| a compound bound out of a clause, then the clause retracted | 0 | 1 | 0 |
| the same with an integer, which is copied rather than shared | 0 | 0 | 1 |
| a clause that retracts itself and runs on | 1 | 0 | 0 |

The workloads:

| workload | retracted | unfreed at a sample | running code | bound term | iteration |
|---|---|---|---|---|---|
| churner, 4 readers | 30,000 | 21,505 | 0 | 0 | 324 |
| churner, 8 readers | 30,000 | 22,056 | 0 | 0 | 374 |
| a reader paused mid-iteration (`db_luv_drain.pl`) | 1,001 | 957 | 0 | 0 | 123 |

Across every sample of every thread, no retracted clause was ever held by
running code or by a bound term: the readers bind `_` to integers, which
are copied, and nothing runs a retracted body. Iteration is the only root
that held anything. It was measured per predicate rather than per position -
any choicepoint iterating the predicate counts every retracted clause of it -
so each sample is all or nothing, and an average of 324 in 21,505 means only
about 1.5% of sampled moments had an iteration in flight at all. The exact
low-water mark can only be tighter. It was not computed exactly because a
choicepoint's saved position can be stale - a barrier copies `q->st` whole -
and following a stale rule pointer in a simulation reads freed memory; a
real implementation would record each position's `db_id` as the reader
advances. The paused reader is the logical update view doing its job: it
sits before all 1,001 retracted clauses for the whole pause, so iteration
holds every one of them, and they become freeable only once it moves past.

Two caveats. The samples are per thread, not a snapshot across threads, so
the iteration figures do not add up across threads; the zeros for running
code and bound terms do, since they aggregate every sample. And the
bound-term scan covers the whole heap, dead cells included, so it
over-counts: its zero is a reliable zero.

So a round that freed retracted clauses would find nearly everything below
the iteration low-water mark freeable. The low-water mark and the running-
code check are cheap; the bound-term scan is the expensive part, empty in
these workloads but required for safety, since the controls show both
roots arise in programs that hold such references. The piece not yet
designed is the handshake bringing every thread to its goal boundary for a
round, where `start()` already checks each thread's signals.

# Retracted clauses under concurrent readers

**Throughput fixed in `254d547e`; the memory half is still open.**

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

## What is still open: memory

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

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

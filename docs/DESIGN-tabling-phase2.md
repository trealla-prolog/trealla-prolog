# Tabling, Phase 2 — design (v4)

Design only for items 3–6. Items 1 and 2 have since been implemented;
their sections are kept as written for the record, with the outcome
noted below.

**v2 folded in two things:** the self-review in
`REVIEW-tabling-phase2.md`, and the effect of the Phase 1 review fixes
now landed on v3.2.0 (`tabling-review-fixes-all.patch` — handle
validation, iterative trie walks, best-effort indexing, failed-insert
unwind). Sections marked **[changed in v2]** differ materially from v1.

**v4 revises item 6 only**, from a review against the driver, SWI
and XSB's test suite: general continuations are not its first step,
the delay list and the three SLG operations are now spelled out, it is
staged so that a stratified `tnot/1` can ship first, and it no longer
changes existing behaviour. See **[changed in v4]** there.

**v3 revises item 3 only**, from reading the code rather than
reasoning about it. Two of v2's framings there were wrong: attribution
is an SCC-level problem, not a continuation-level one, and invalidation
must be validate-on-read rather than invalidate-on-write given that
tables are per-thread. See **[changed in v3]** markers in that section.
The one assumption SCC attribution rests on has since been measured
(1157 checks, 0 mismatches) and is marked **[settled]** there. v3 also
records what items 1 and 2 turned up:

- **Item 1 (restraints) — done.** Three SWI-named flags, default
  `infinite`, `resource_error` on breach.
- **Item 2 (answer subsumption) — done.** `:- table p(_,_,min)`, keyed
  on the non-aggregated arguments, with the worklist re-pairing pass
  the section predicted; that pass is load-bearing and has a negative
  control.
- **Item 4 (shared completed tables) — done.** `:- table p/1 as
  shared`. Handle question resolved in favour of a **shared slot
  table**, not two-level handles: a two-level handle ties a published
  table's lifetime to the producer thread's slot array, but the
  producer may exit while readers still hold handles. Bit 31 of the
  slot index says which array a handle indexes. Measured 5 threads on
  one table: peak RSS **15.2 MB shared vs 32.1 MB private**; wall time
  indistinguishable at that size, so the win here is memory.
- **Item 5 (trie-path reconstruction) — done.** The parent-pointer
  question the section says to settle first was settled by measuring,
  not arguing: 8 bytes per trie NODE against 24 per image CELL, and
  trie nodes are shared between answers with common prefixes while
  images never were. Measured A/B (parent pointers present in both, so
  this isolates the image): 100k answers **132.3 MB → 113.7 MB**; 2k
  50-element lists **51.2 MB → 45.8 MB**. The saving slightly exceeds
  the raw image bytes because each image was also its own malloc.
  Time cost is real but modest and only shows when reading dominates:
  600k reconstructions ran **0.16s → 0.19s**, and ordinary
  build-then-read workloads were indistinguishable.
- **Item 3 (incremental tabling) — done**, to the v3 design below:
  per-SCC attribution, validate-on-read, `:- incremental(q/1)` plus
  `:- table p/1 as incremental`. The null case measured free (4M calls,
  median 0.46s hook vs 0.465s without, interleaved).
- **Item 6 stage A (stratified `tnot/1`) — done**, to the v4 design:
  `permission_error(tnot, incomplete_table, G)` on a loop through the
  negation, SWI's own error terms otherwise. Against XSB's suite, via
  `util/xsb_tnot_check.py`, it answers wrongly nowhere; what it does
  not answer it refuses. See §6 for what building it turned up.
- **Item 6 stage B (delaying and simplification) — done**, with
  `call_delays/2` and `undefined/0` from stage D brought forward to
  observe it. Every one of XSB's 1010 `wfs_tests` checks agrees, no
  program anywhere in its suite answers wrongly, and a program without
  `tnot/1` runs the same driver at the same cost. Building it turned
  up a use-after-free in `shift/1` itself; see §6.
- **Item 6 stage C (answer completion) — done.** An answer held up
  only by a positive loop is now false, as in SWI and XSB, and the six
  programs in XSB's suite that needed it agree. Simplification was
  rewritten with it, to propagate instead of rescanning: it was
  quadratic in the length of a chain. See §6.
- **Item 6 stage D (the user surface) — done**, which completes item
  6. The toplevel shows an undefined answer as SWI does, and SWI's
  `delays_residual_program/2`, `call_residual_program/2` and
  `answer_residual/2` read the residual program. See §6.
- **Early completion — done**, after item 6, for XSB's p29: a ground
  call with an unconditional answer is settled, so its producer is cut,
  its other clauses unrun.
  - *The signal.* `'$tbl_add_answer'` succeeds only then, and fails
    otherwise, after adding, as it did for a duplicate; the suspension
    builtins and the moded join fail after recording. So a producer
    step succeeds exactly when there is nothing more to produce, and
    the two fresh-table loops end in a cut, to `catch/3`'s barrier, in
    place of `fail`; `delim/3`, for resumed continuations, ends in a
    cut too. Groundness is the variant trie walk's own variable count.
  - *Not done:* marking the table complete early. Later calls in the
    same SCC still suspend on it rather than read it, which costs time,
    not answers, and keeps incremental tables and rollback on an
    exception as they were.
  - *Its limit, SWI's too:* a continuation resumed after the table is
    settled runs until it next answers. `s :- s, nat(_), fail. s.`
    loops in both.
  - *Cost.* None on plain tabling (fib −0.3%, a transitive closure
    −1.5%); ground reachability 30% fewer instructions. An
    if-then-else in the loop cost 0.5%, and `has_vars()` on each new
    table 0.65%, before the cut and the trie's count replaced them.

  `wfs_tests`: all 1014 checks agree, p29 included.
  `tests/sundry/tabling_early.pl` against SWI.
- **A Phase 1 soundness bug, found while starting item 1 and fixed
  first.** `completion/0` marked an *escaping* nested SCC's tables
  COMPLETE before `'$tbl_pop_scc'` discovered the escape, caching a
  partial (often empty) table permanently. The existing cycle test
  could not catch it: its cycle has no answers, so a wrongly-completed
  empty table is indistinguishable from a correct one. This is the
  concrete instance of v2's own warning about unexercised tests.

**Item 5 hit the same kind of cross-item surprise as item 4**, and from
item 2 again: a subsumptive table's trie *deliberately omits the
aggregated argument* — that omission is precisely how answers collide
and combine — so its path cannot reproduce the one value that matters.
Subsumptive tables therefore keep their image; everything else drops
it. Removing that carve-out does not degrade gracefully, it breaks
subsumptive tables outright, which is how it was confirmed to be
load-bearing rather than assumed.

**Item 4 met one thing the doc could not have predicted:** item 3 had
landed in between, and *invalidation mutates a completed table* (drops
answers, resets to FRESH) — the exact opposite of the immutability
publication promises. `incremental` + `shared` together would let any
reader thread free memory another is walking. It is refused at
declaration time rather than supported unsafely.

Its one real limitation, stated plainly: `abolish_all_tables/0`
**retires** published tables (unpublishes them so the next call
recomputes) but does not free them until `pl_destroy()`. Freeing under
a live reader is the silent, intermittent use-after-free this design
exists to avoid, and doing it earlier needs the generation-keyed
deferred reclamation described below, which is not built. So abolish
has correct semantics at the cost of holding the old table's memory
until teardown.

**Two things item 3 only learned by building it:**

- **Module resolution.** `'$tbl_set_pred_incremental'` first resolved
  against `q->st.m`, which during `incremental/1` is `tabling` — the
  *library's* module. It missed the user's predicate entirely and
  silently created an empty one inside the library. The right module
  for an unqualified name in a directive is `pl->m`, the one being
  consulted, which is the same answer `set_dynamic_in_db` gets from
  `p->m`.
- **`incremental` must not be a prefix operator.** At a priority loose
  enough for `:- incremental q/1` it can no longer be the right operand
  of `as` (xfx 700, max 699), so it would break `:- table p/1 as
  incremental` — and it would make a common word an operator for every
  user of the library. Hence `:- incremental(q/1)` with parens.

Baseline: v3.2.0 plus that patch. Suite now 392 passed / 0 failed, and
building warning-free under NOTHREADS (the WASI configuration) as well
as with threads. ASAN is clean too, but read that narrowly: on macOS
arm64 LeakSanitizer does not run, so those runs establish the absence
of memory *errors* and say nothing about leaks — see the cross-cutting
note below.

---

## Order **[changed in v2]**

| # | Item | Gives | Size | Risk | Status |
|---|------|-------|------|------|--------|
| 1 | Restraints | runaway answer *set* → `resource_error` | S | low | **done** |
| 2 | Answer subsumption | aggregate at insert; bounded tables | M | medium | **done** |
| 3 | Incremental tabling | tables survive assert/retract | L | medium | **done** |
| 4 | Shared completed tables | threads stop recomputing | M | **high** | **done** |
| 5 | Trie-path reconstruction | drops per-answer images | M | medium | **done** |
| 6 | `tnot` / well-founded semantics | correct negation through recursion | S, then XL | high | **done** (stages A–D) |

v1 had sharing second, on the grounds that it was "the single biggest
win available". That was wrong on three counts: it has the most
dangerous failure mode (a cross-thread use-after-free, silent and
intermittent, and invisible to ASAN because the `pl_destroy()` sweep
hides it), it is worth nothing to single-threaded users or any WASM
embedding, and it was the one item whose central design question v1
failed to answer. It is now fourth and its risk is marked high.

Items 1 and 2 both touch `bif_tbl_add_answer_2`, and are now adjacent
so that work is done once.

General nondeterministic continuations were a separate item in v1.
They are not scheduled on their own merits — the limit they lift (a
worker suspending inside an if-then-else *condition*) is narrow and
documented. v2 and v3 made them the first step of item 6; **[changed in
v4]** item 6 does not need them either, so they are not scheduled at
all.

---

## 1. Restraints

**What it gives.** A tabled predicate whose answer set is infinite
stores answers until the process is OOM-killed:

    :- table as//0.
    as --> [].
    as --> [a], as.

    ?- phrase(as, Ls).      % unbound: diverges, killed on memory

The value is diagnostic. A diverging table is currently
indistinguishable from a bug in the user's program: no message, no
partial output, and exit code 137 like any other OOM.

**[changed in v2] The justification is narrower than v1 claimed.** v1
also leaned on stack overflow from deep answers. That is fixed —
`trie_walk` iterates on the last argument and `trie_free` uses an
explicit worklist, so a 1,000,000-element answer now inserts and tears
down cleanly where 100,000 used to segfault. Restraints are still
needed for unbounded answer *sets*; they are no longer needed to
prevent that particular crash.

**Where it goes.** `bif_tbl_add_answer_2` → `trie_insert_` → `twalk`.
Three counters:

- *answer count per table* — `unsigned n_answers` on `struct table_`.
- *answer size* — `twalk` already carries `oom`, `attvar` and the
  unwind bookkeeping; add depth and node counters alongside.
- *subgoal size* — the same walk, from `'$tbl_variant_table'`.

**[changed in v2] The failure path is now free.** `trie_insert_`
unwinds whatever a failed walk created (Phase 1 review #4), so a
restraint breach can simply fail the walk the way `attvar` does and
cleanup happens automatically. In v1 this needed its own handling.

**Flags.** SWI's names, so documentation transfers:
`max_table_answer_size`, `max_table_subgoal_size`,
`max_answers_for_subgoal`. Default `infinite`; opt-in, nothing changes
for existing programs.

**Behaviour on breach.** `error` only. `suspend` needs answer
completion from item 6; `abstract` needs subgoal abstraction, which
changes what a variant *is*. An honest `resource_error` is the win.

**[changed in v2] Open question — restraints inside a merged SCC.** A
breach raises, and `'$tbl_reset_incomplete'` is wired to the exception
path. That covers the simple case. It does *not* obviously cover a
fresh variant completed in a nested SCC whose tables were merged into
the parent on pop. Which tables are incomplete then — the inner set,
or the merged set? This needs answering by reading the merge path, not
by reasoning about it. It is the one part of this item that is not
routine, and it is where Phase 1's bugs lived.

**Test.** `as//0` must raise instead of being killed — assert the
*exit code*, since the current failure is 137 and an output-only check
passes on a killed process. A bounded table must be unaffected;
`phrase(as,[a,a,a])` must still be true.

**Size.** Small, except for the SCC question above.

---

## 2. Answer subsumption

**What it gives.** Aggregation at insert instead of storing every
answer:

    :- table path(_,_,min).

Also *bounds* tables that would otherwise be unbounded, approaching the
same problem as item 1 from the other side.

**[changed in v2] It is not just insert logic.** The answer trie is
keyed on the **whole answer term** — that is what makes duplicate
detection free. Subsumption needs it keyed on the *non-aggregated*
arguments, so `path(a,b,3)` and `path(a,b,5)` collide and can be
combined. That is a structural change to how the trie is used, possibly
a second index or a per-predicate key construction. v1 framed this as a
change to what happens after a lookup, which would send an implementer
to the wrong place.

**The other hard part.** Today answers are append-only: a consumer that
has seen one is done. Subsumption lets an existing answer be
*updated*, so every consumer that read the old value must run again.
That changes the worklist protocol in `'$tbl_pop_worklist'` /
`'$tbl_wkl_work'`, whose `unproc_ans`/`unproc_susp` pairing assumes
append-only.

**[changed in v2]** The invariant this breaks is now documented in the
code, at the choice-point elision in `'$tbl_get_answer'` — that
elision is only sound because completed tables are immutable. The
implementer will meet the comment where they need it.

**Recommendation.** `min`/`max` over standard order first: monotone, so
updates move one way and the fixpoint argument stays simple. General
user-supplied lattices raise the question of whether the operation is
monotone at all, and a non-monotone one will not terminate — if wanted
later, it needs a documented obligation on the user.

**Size.** Medium.

**[done: the rest of SWI's modes, after item 6]** `min`/`max` on one
argument was all there was, and anything else was read wrong without a
word: `:- table p(_, first)` took `first` as part of the key and tabled
every answer, and two moded arguments raised. Now SWI's set:
- *The modes.* `first` (or `-`), `last`, `min`, `max`, `sum`,
  `lattice(PI)` and `po(PI)`, with `_`, `index` or `+` for a key
  argument, and any number of moded arguments, each joined on its own.
  An unknown mode is SWI's `domain_error(tabled_mode, Mode)`, a wrong
  arity its `lattice_arity`/`po_arity` one.
- *Where they are joined.* The answer trie skips every moded argument
  (a mask, so a moded argument must be among the first 62). One `first`,
  `last`, `min` or `max` is joined in C, as `min`/`max` were. The rest
  — `sum`, a user predicate, several arguments — continue from
  `'$tbl_add_answer'` into `'$tbl_moded_join'`, in Prolog, only when the
  key already holds an answer, and its `'$tbl_moded_replace'` takes the
  same update path. Plain tables never see it.
- *The obligation the recommendation asked for.* A lattice or partial
  order is the user's to keep monotone, as in SWI; a join that fails
  leaves the stored answer, and one that throws raises.
- *A moded argument bound at call raises*, SWI's
  `uninstantiation_error`. It used to make a table of its own and answer
  as if that value were the best: `cost(x, 3)` succeeded with the
  minimum at 2. The wrapper checks with inline `var/1`, and the error
  is let through a worker's catch, as `tnot/1`'s misuse errors are.
- *Cost.* Plain tabling is unchanged. A `min` table doing nothing but
  joins runs about 25 instructions more per answer.

Tests: `tests/sundry/tabling_modes.pl`, checked against SWI.

---

## 3. Incremental tabling **[changed in v3 — substantially; now IMPLEMENTED]**

**What it gives.** Tables survive changes to the dynamic predicates
they depend on, instead of the user calling `abolish_all_tables/0` and
paying for full recomputation.

**What it needs.** A dependency graph (which dynamic predicates a table
consulted), invalidation on assert/retract, lazy re-evaluation on next
call, and `:- table p/1 as incremental` / `:- dynamic q/1 as
incremental`.

**[v2 said] The hard part is attribution, not the graph.** The
difficulty is knowing *which table you are currently computing*, and it
is not "the top of a stack": a suspended consumer resumes later via
`delim/3`, running on behalf of a table that is not lexically current
at that moment. The dependency must be attributed to the table the
*continuation* belongs to.

**[changed in v3] That framing is right about the problem and wrong
about the granularity.** It leads an implementer to bracket `delim/3`
— the one place that sees both the first run of a worker and every
later resumption — with a push/pop "current table". **That bracket is
unsound.** `activate/3` drives `delim/3` with a failure-driven loop
(`delim(...), fail ; true`) and `reset/3` is nondeterministic; that is
precisely how multiple answers are collected. A push/pop pair pops on
the first solution and never re-pushes on redo, so every answer after
the first is attributed to whatever happens to be on the stack. The
symptom is a silently under-recorded dependency, i.e. a stale table —
the worst available failure mode for this feature, and invisible to any
test that only checks the first answer.

**[changed in v3] Attribute per SCC, not per table.** Push/pop against
the *existing* `'$tbl_push_scc'` / `'$tbl_pop_scc'` bracket. Everything
between those two is deterministic on exit — `activate/3`'s loop drains
and `completion/0` recurses to a fixpoint — which is why Phase 1 works
at all, and it makes the bracket safe for exactly the reason the
`delim/3` one is not. Three further points in its favour:

- an SCC is already the unit of *completion*, so it is the natural unit
  of *invalidation*;
- over-invalidating within an SCC costs nothing real: mutually
  recursive tables have to be recomputed together anyway;
- `top->fresh_head` already *is* the list of tables to flush the
  collected dependencies onto at `'$tbl_mark_all_complete'`.

This dissolves the v2 problem rather than solving it: at SCC
granularity you never need to know which continuation you are inside.

**[settled] Is the `TT` in `dep(Ball, C, Wrapper, TT)` always a table
of the SCC currently being completed?** Yes, as far as measurement
goes. A temporary `'$tbl_dep_scc_check'/1` comparing `t->scc` against
`tbl_scc_id(s)` where `completion/0` pulls each dep off the worklist:
**1157 checks, 0 mismatches** — 1126 across `tabling.pl`,
`tabling_subsumption.pl`, `tabling_restraints.pl` and
`dcg_tabling.pl`, plus 31 from cases written to attack the merge path
specifically (mutual recursion entered from the non-recursive side —
the shape that exposed the Phase 1 premature-completion bug; a
three-way cycle entered mid-chain; a cycle reached through `findall/3`,
forcing nested SCC completion instead of suspension; left recursion
over a cyclic graph entangled with a second table; and a re-run after
`abolish_all_tables/0`). A full `make test` sweep also reported none.

The check *counted* as well as compared, deliberately: the first run
printed nothing at all, which reads as a clean result but was actually
`tpl` honouring only the last `-g` and dropping the report goal. A
diagnostic that cannot tell "0 mismatches" from "never ran" is not a
diagnostic.

This is evidence, not proof — it does not cover every possible program
shape. If that margin matters, keep the comparison as a debug-build
assertion rather than deleting it with the scaffolding.

**[changed in v3] Table→table edges are also needed.** v2 lists only
"which dynamic predicates a table consulted". If table A calls table B
and B depends on `q`, then a change to `q` must invalidate A as well.
Record the edge at `'$tbl_variant_table'` when a table is looked up
while an SCC is active, and let a transitive walk handle the rest.

**[changed in v3] Invalidate on read, not on write.** This is the
constraint v2 missed entirely. `tbl_state` hangs off
`thread->tabling_state` — tables are **per-thread** — while the
database and `pl->dbgen` are shared. Eager invalidation from the
asserting thread therefore means writing to another thread's tables:
the same cross-thread hazard that makes item 4 "high risk", silently
inherited by an item nobody would think to look at for it. Invert it:

- `uint64_t last_modified` on `struct predicate_`, stamped
  `++pl->dbgen` on assert/retract (the counter already exists and is
  already bumped there);
- each table stores its dependencies plus the generation it completed
  at;
- `'$tbl_variant_table'` re-validates on lookup, **in the owning
  thread**. No cross-thread mutation anywhere.

A single `pl->dbgen` equality check short-circuits the whole walk when
nothing has changed since the table completed, which is the common
case.

**[changed in v3] Invalidation is abolish semantics, not
`'$tbl_reset_incomplete'`.** That builtin deliberately *keeps* answers
(`t->unproc_ans = t->first_ans`); invalidation needs a full drop. Now
that item 2 has landed this is sharper than it looks: `leaf->value` in
the answer trie points at live `tbl_ans` structs, so freeing answers
without also clearing the trie leaves dangling pointers straight in the
dedup path.

**Risk.** The graph is straightforward; the hook is in a hot path
everything uses. It belongs in `enter_predicate()` — once per *call*,
not once per clause tried, and it already touches `pr` and writes
`f->dbgen` — guarded by a bitfield test that is false for everything
unless opted in. Measure `make test` wall time with no incremental
predicates declared anyway; the null case must be free, and "should be"
is not a measurement.

**[changed in v3] Syntax.** `:- table p/1 as incremental` parses as
`table(as(p/1, incremental))`. Item 2's mode-spec clause matches any
compound that is not `/` or `//`, so it *swallowed* this: it tabled a
predicate literally named `as`/2 and left `p/1` untabled with no
diagnostic. Fixed ahead of this item (an `as`/2 clause ordered before
the mode-spec clause, which now rejects unimplemented options loudly)
— but it is worth noting as the shape of bug this directive syntax
invites.

The `:- dynamic q/1 as incremental` half is disproportionately
expensive: `dynamic` is handled at *parse* time in C, walking a list
that expects `/` or `//` shapes, and `as` lands in its
predicate-indicator error path. Prefer a separate `:- incremental q/1.`
directive handled entirely in `library(tabling)` by term expansion onto
a builtin. Same expressiveness, no parser surgery.

**[changed in v3] Do not store raw `predicate *`.** `destroy_predicate`
runs only at module teardown and abolish merely sets `is_abolished`, so
raw pointers *mostly* survive — but module unload is a live hazard, and
Phase 1's review already paid for this exact lesson once when raw table
pointers became `(serial, index)` slots. Key dependencies by (module,
functor, arity), or reuse the slot pattern.

**Size.** Large, but smaller than v2 implies: validate-on-read removes
the cross-thread machinery, and SCC attribution removes the
continuation-tracking machinery. The graph plumbing and the abolish
semantics are still real work.

**Deep chains of escaping SCCs were cubic, now linear.** Found by XSB's
tabsimp_seq, a 20000-node cycle, while building item 6: a chain where
each nested SCC escapes to its parent (`r(A,N) :- step(A,B,N),
r(B,N)` round a cycle) took 18 s at 4000 nodes against SWI's 0.02 s.
Three costs, all in the merge and completion this item added or
extended:

- `'$tbl_pop_scc'` re-tagged every table of the child to the parent's
  id, a walk of the child's whole list per level. Now the list is
  spliced and tables keep their id: SCC ids grow with depth, so the
  SCC being completed owns every active table whose id is at least its
  own, and `tbl_in_top_scc()` asks exactly that.
- It re-added each of the child's dependencies to the parent through
  `tbl_scc_add_dep`'s linear dedup scan. Now table edges are spliced
  whole, and a stamp on the table keeps one SCC from recording it
  twice; predicate edges, one per incremental dynamic predicate called,
  keep their own short distinct list. None of it is recorded at all
  until a table is declared `as incremental`, since only such a table
  ever keeps it.
- `'$tbl_mark_all_complete'` copied the SCC's dependencies to each of
  its incremental tables, n tables by n edges. Now they share one
  refcounted set, predicate edges first.

20000 levels now take 0.1 s, 100000 0.4 s (SWI: 0.09 s and 0.28 s),
incremental or not; `tests/sundry/tabling_deep_scc.pl` keeps it so, and
times out on v3.14.21. A rolled-back table is also unlinked from the
worklist now, rather than only flagged out of it, so a later enqueue
cannot link it twice.

Still conservative: a chain's tables hold edges to their own SCC, so
after any database change the dependency walk hits its depth cap and
recomputes them. Correct, and as before.

---

## 4. Shared completed tables **[changed in v2 — substantially; now IMPLEMENTED]**

**What it gives.** N threads tabling the same predicate currently do N
times the work.

**The invariant.** Phase 1 rejected locking shared tables and that
reasoning holds: the leader's critical section spans `completion/0`, a
*Prolog* loop running arbitrary user code between `'$tbl_*'` calls, and
no lock survives that. Per-builtin locks would make the tries
memory-safe while still letting thread B read thread A's half-built
table as complete — quietly wrong instead of loudly refused.

The way through is that completed tables are immutable: build
privately with no locking, publish on completion, read after lookup.

**[changed in v2] Say the locking precisely.** v1 said a reader
"checks the registry first and, on a hit, reads it without a lock".
Taken literally that is a data race — the registry is mutable shared
state, and reading a pointer out of it unsynchronised gives no
happens-before edge to the table contents. The correct statement:

- the table is fully built and never written again **before**
  publication;
- publication happens under a mutex;
- **lookup also happens under that mutex** — it is the acquire against
  the publisher's release that makes the contents visible;
- only after lookup returns does the reader touch the table, and by
  then it is immutable.

Not "read without a lock" — "one short lock to find it, then no lock to
use it". Publication and lookup are short and contain no user code,
which is what makes a mutex sound there and unsound around
`completion/0`.

**[changed in v2] Reclamation: decided, not enumerated.** v1 offered
"a refcount, or an epoch scheme". Refcounting fails for a specific
reason: a reader can hold a table across `completion/0`, which runs
arbitrary user code and may block on I/O, suspend or throw. A refcount
can therefore be held for unbounded time, and `abolish_all_tables/0`
cannot wait on it without risking deadlock against user code. **Use
deferred reclamation keyed on the existing `generation` counter.**

**[changed in v2] Half of that mechanism now exists.** The Phase 1
review fix replaced raw-pointer handles with `(serial, index)` pairs
into a slot array: releasing a slot bumps its serial, so a stale handle
validates as `type_error(table_handle, _)` instead of dereferencing
freed memory. That is exactly the stale-handle detection deferred
reclamation needs — a published table can be retired without waiting
for readers, because readers that come back late are detected.

**[changed in v2] But the slot array is per-thread, and that is now the
main obstacle.** It hangs off `tbl_state`, which hangs off `thread`.
A handle minted by thread A is meaningless in thread B by construction.
Sharing therefore needs a handle redesign, one of:

- a **shared slot table** for published tables, with thread-private
  slots for unpublished ones, and a bit in the handle saying which;
- a **two-level handle** — owner thread id plus slot — validated
  against the owner's array.

Decide this before writing any of the registry. It was invisible when
v1 was written because handles were raw pointers, which are
thread-agnostic and unsafe for a different reason.

**Other requirements.** Publish only tables that completed without an
exception, and — once item 1 lands — without hitting a restraint; a
truncated table is not a complete table. Opt-in
(`:- table p/1 as shared`), default private, matching today.

**Test.** `test_threads` currently asserts four threads agree *and*
that each did its own work via a marker count. With sharing on, that
count should show exactly one thread did the work. Keep both; they are
each other's negative control.

**[changed in v2] Threadless build.** `bif_tabling.c` already has
`#if USE_THREADS` around the per-thread state. This item is meaningless
under `NOTHREADS` and must compile out cleanly — and `NOTHREADS` is the
WASI configuration, which is what embedders ship. v1 never mentioned
it.

**Measure.** Wall time for N threads on the same predicate, shared vs
private, plus peak RSS — reported the way Phase 1 reported 200
sequential tabling threads at 9 MB vs 59 MB.

**Size.** Medium mechanism, high risk, and the handle question is a
prerequisite.

---

## 5. Trie-path answer reconstruction **[now IMPLEMENTED]**

**What it gives.** Every answer stores a full `cell *image` — a real
second copy, `copy_term_to_tmp` then `TPL_malloc` then `dup_cells` — in
addition to its path in the answer trie. Dropping the image means
`'$tbl_get_answer'` rebuilds each answer by walking the trie path.

**[changed in v2] The memory win is unconditional; only the time cost
is a trade.** v1 said "which way that goes depends entirely on the
ratio of answers stored to answers consumed", which is true of the time
and false of the memory — the trie exists either way. The question is
only whether reconstruction cost is acceptable, not whether the saving
is real.

**[changed in v2] There is no parent pointer.** `struct tnode_` has
`child`, `sibling`, `hnext`, `index`, `nchildren`, `value`, `is_leaf` —
nothing pointing up. Reconstruction walks leaf-to-root, so it needs
either a parent pointer on every node (8 bytes each, partly cancelling
the saving) or the path recorded another way. This was not in v1 and it
is the thing to settle first.

**[changed in v2] The case is stronger than it was.** Answers of
1,000,000 cells are now reachable, since the recursion limit is gone.
Each one carries a full image.

**Recommendation.** Still measure first, but measure the right thing:
reconstruction time per answer, and the parent-pointer overhead against
the image bytes saved.

**Size.** Medium.

---

## 6. `tnot` and well-founded semantics **[changed in v4 — substantially]**

**What it gives.** Correct semantics for negation through recursion,
and SWI's `tnot/1`. Today tabling is least-model only and negation is
plain `\+`, which is sound only when negation is stratified. Through a
negative loop it gives answers no model allows:

```prolog
:- table win/1.
win(X) :- move(X,Y), \+ win(Y).
move(a,b). move(b,a). move(b,c).
```

WFS (SWI's `tnot/1`) says `win(a)` false, `win(b)` true, `win(c)`
false. Trealla's `\+` says `win(a)` true, whichever goal is called
first; with `p :- \+ q.` and `q :- \+ p.` both tabled, `p` and `q` both
succeed where WFS makes them `undefined`. **SWI's own `\+` gives exactly
the same answers as ours on both programs**, so the compatibility gap
is `tnot/1` and what sits behind it, not `\+`. This is what most of
SWI's tabling complexity exists to support, and it is the most valuable
item on the list.

**[changed in v4] Not a prerequisite: general continuations.** v2 and
v3 listed "general nondeterministic continuations" as the likely first
step. The driver already suspends a consumer with `reset`/`shift`,
each `shift` in final-call position of its own clause, and `tnot/1`
can suspend on a negative dependency the same way: a
`shift(neg_call(Goal, T))` from its own clause, recorded as a negative
suspension. The limit general continuations would lift — suspending
inside an if-then-else condition, `\+` or `findall/3` — applies
equally to positive calls today and is unrelated to negation. Dropped
from this item.

**[changed in v4] The core state: a per-derivation delay list.** v3
said only that `struct tbl_ans_` grows one. The harder question is
where it lives while a derivation runs:

- *Undone on backtracking.* A derivation's delay list is a
  backtrackable per-query value, empty in every program that does not
  use `tnot/1`.
- *Saved with every suspension.* A suspended derivation resumes with
  its own delays, so `dep/4` grows a field and `'$tbl_wkl_work'`
  restores it before the continuation runs.
- *Read at insert.* `'$tbl_add_answer'` stores a non-empty list on the
  answer, which makes it conditional; an answer reached by both a
  conditional and an unconditional derivation keeps the unconditional
  one.
- *Grown by consumers.* Reading a conditional answer, from the
  worklist or from a complete table, adds a positive delay on that
  answer to the reader's list.
- *Visible to the caller.* An answer returned to untabled code carries
  its delays out; SWI's `call_delays/2` and the toplevel read them
  there.

**[changed in v4] Three mechanisms, not one.** v3 called the second
fixpoint "answer completion". SLG has three distinct operations, and
keeping them apart is what makes staging possible:

- *Delaying.* When an SCC has no positive work left and still has
  negative suspensions, those whose table now holds an unconditional
  answer fail. The rest resume with `tnot(G)` added to their delay
  list, which may post new conditional answers, and the loop goes on.
  An incomplete table with no answers yet must *not* count as false
  here: a delayed literal may still post to it.
- *Simplification.* Once the SCC is at a fixpoint and its tables are
  decided, a delayed literal that has become true (its table ended
  with no answers, or the answer it waited on became unconditional) is
  removed from every delay list holding it; one that has become false
  removes the delay lists holding it, and an answer left with none is
  dropped. Propagate until nothing changes. What still has delays is
  `undefined`.
- *Answer completion.* Removing conditional answers whose only
  remaining support is a positive loop among themselves, which
  simplification leaves `undefined` when they are false. Narrow, and
  separately stageable.

**[changed in v4] Stages.** Each ships alone with the suite green:

- **A. Stratified `tnot/1`, error on a negative loop.** `tnot(G)` on a
  complete table checks for an answer; on a fresh one it completes G in
  its own SCC, as Phase 1 already does for every fresh call, then
  checks. If that SCC *escapes*, or G's table is already active, the
  loop runs through this negation, so raise rather than guess. The
  escape test is already built: any escape from an SCC entered under
  `tnot/1` is a cycle through it.
  Small, gives SWI's syntax, and turns silently wrong answers into an
  error. **[done]** Three things the design above did not say:
  - *The driver swallowed the refusal.* Every exception inside a tabled
    worker fails that branch (kept for the Desouter library's
    behaviour), so a negative-loop error raised in a tabled predicate
    became plain failure and a wrong answer again. `worker_error/1` now
    lets `tnot/1`'s own error terms through and fails the rest as
    before; nothing else raises them, so programs without `tnot/1` are
    unaffected.
  - *Negation return.* An incomplete table that already holds an
    answer makes `tnot/1` fail instead of raising: with no delays every
    stored answer is true. SLG's rule, and it refuses fewer programs,
    eg. `p :- tnot(q). q. q :- p.` answers `p` false.
  - *The fresh path is a copy.* `tnot_/4` duplicates
    `start_tabling_(fresh, ...)` rather than sharing a helper, which
    would add a frame per nesting level to every positive tabled call:
    the cost commit 101bf186 removed.

  Measured with `util/xsb_tnot_check.py` (XSB's dialect translated on
  the way in), wrong answers: **0**. `wfs_tests`: 430 subgoal checks
  answered, 580 refused, from fresh tables and cumulatively.
  `neg_tests`: 18 of 26 programs answered, 7 refused. `delay_tests`:
  87 of 89 refused, as expected of a suite about delays. Four programs
  are not run: three use XSB-only syntax (`:- edb` in q6 and q7, a C
  `#include` in dret_test; q6 is disabled in XSB's own `test.sh` too),
  and **p29 loops**. That one
  is not about negation: its ground `w(0)` gets an answer from its
  first clause while the second enumerates an infinite untabled
  predicate, and SWI and XSB stop because a ground call is complete at
  its first answer. Trealla has no such early completion, and loops on
  the positive-only version too. **[early completion done since: p29
  answers, see below.]**
- **B. Delaying and simplification.** The delay list, conditional
  answers, negative suspensions on the worklist. This is the bulk of
  the item and rewrites completion. **[done]** As designed, with these
  departures and finds:
  - *Completion was not rewritten.* The waiting `tnot/1` calls come off
    the worklist as one more batch once positive work runs out, so
    `completion/0` is the same clause as before, and the leader's
    caller's delays are set aside in `'$tbl_push_scc'` and restored in
    `'$tbl_pop_scc'`. Each was first a goal of its own: a delaying
    branch in `completion/0` cost 1.5% and a reset per worker another
    1.2% on a run of 45000 small tables, so neither stayed. Measured in
    instructions retired against v3.14.21 with no `tnot/1` in sight:
    that run 0.5% under, a 40000-answer closure 0.5% over, which is the
    size of the layout effect of adding one unused clause.
  - *The delay list* is the `b_setval/2` variable `'$tbl_delays'`,
    read and set by the C builtins themselves behind a per-thread flag
    that turns on at the first delay; elements are table handles, as
    item 3's edges are, never pointers.
  - *A use-after-free in `shift/1`, there since reset/shift came in.*
    It built the continuation on the heap with `copy_cells_by_ref`,
    which takes no references, and the heap releases what it trims, so
    every resumption of a continuation holding a refcounted constant
    (a bigint, or a long atom that went through `term_expansion`, as
    every tabled clause does) dropped one reference too many. Positive
    tabling had it too, and so did plain `reset/3`; it only showed once
    delaying freed suspension images while the SCC was still running.
    Now `dup_cells_by_ref`, with `tests/sundry/shift_shares_cells.pl`.
  - *Mode-directed tables refuse a conditional answer*:
    `permission_error(aggregate, conditional_answer, _)`. SWI accepts it
    and reports the minimum as unconditional, which it is not.
  - *Suspending inside `findall/3`* fails, for `tnot/1` as for a
    positive call, where SWI raises "Cannot catch continuation through
    findall/3". Unchanged from before; making `shift/1` raise there
    would change `reset/3` for everyone.

  `util/xsb_tnot_check.py` now reads truth values through
  `call_delays/2`. Wrong answers: **0**, under ASan too. `wfs_tests`:
  all 1010 checks agree. `neg_tests`: 22 of 26 programs agree.
  `delay_tests`: 72 of 89. What is left: six programs where an atom
  held up only by a positive loop of undefined answers comes out
  undefined instead of false, which is stage C's job; two where XSB
  differs from SWI and `tnot/1` follows SWI (named, line by line, in
  the checker); and thirteen that need XSB-only calls such as
  `get_residual/2`, which stage D's residual API could serve. Stage C
  settled the six.
  - *Deep escaping SCC chains were cubic, and not because of item 6.*
    tabsimp_seq, a 20000-node game cycle, timed out at first: each
    nested SCC escapes to its parent, and merging them was cubic in
    positive tabling too. Fixed on its own first (see item 3's note);
    tabsimp_seq now takes 0.11 s, against SWI's 0.09 s.
- **C. Answer completion.** **[done]** Not as a pass after
  simplification but together with it, in `tbl_settle_answers`, which
  replaces stage B's `tbl_simplify`:
  - *Simplification propagates.* Stage B rescanned every conditional
    answer until nothing changed, quadratic when a chain settles
    against creation order: 16000 tables took 1.05 s (SWI 0.33 s).
    Now every delay element is classified once, the lists waiting on
    an answer or a negation in this SCC are indexed by it, and each
    decision visits only those: 0.09 s, and 100000 tables 0.6 s.
  - *Answer completion counts support.* After that, an answer is
    supported if one of its lists has every positive element on a
    supported answer or on one outside the SCC; a negation in doubt
    withdraws no support. Linear, by counting down per list as
    supporters are found, the same way. The unsupported are false;
    that propagates as above, and support is counted again until none
    lacks it.
  - *Cost.* Programs that never delay a literal never reach it. On
    40000 two-table SCCs with undefined answers it is 0.9% more
    instructions than stage B: two allocations and the support count
    per SCC.

  `delay_tests`: 78 of 89 agree; the six "incomplete" are gone, and
  no check anywhere is incomplete or wrong. XSB's `ac_tests` cannot
  run as is (it reads XSB internals for residuals), but run through
  `call_delays/2` its programs give exactly SWI's answers and XSB's
  expected truth values. `tests/sundry/tabling_tnot.pl` has
  weidong7, the `test_d1`/`test_d2` pair and parts of the ladder.
- **D. The user surface.** `undefined/0`, `call_delays/2`,
  `delays_residual_program/2`, and the toplevel printing SWI's
  "% WFS residual program" and marking an answer `undefined`.
  **[done]** `undefined/0` and `call_delays/2` came with stage B. The
  rest:
  - *The residual program is read from delay elements*, with three
    builtins: `'$tbl_goal_delem'` maps a delay goal back to its table
    and answer (its own variant table first, then any of the same
    predicate), `'$tbl_delem_answers'` lists the answers a negation
    stands for, and `'$tbl_answer_delays'` gives an answer's delay
    lists. A Prolog walk in `library(tabling)` builds SWI's clause list
    from them, each answer and each head once, and with them
    `call_residual_program/2` and `answer_residual/2`. They live in
    `library(tabling)`, not a `library(wfs)`: re-exporting does not
    carry the `table` operator, so a facade would not save a SWI
    program its `use_module(library(tabling))`.
  - *SWI's order.* SWI puts the latest literal of a delay list first
    and the latest delay list of an answer first; `call_delays/2` now
    does the same, which changes only the order of its conjunction.
  - *What does not match.* A delay element names an answer as stored,
    not the instantiated goal, so a binding made after the call does
    not show: `p1(X, _) :- p13a(a, X, Z), Z = c.` reads `p13a(a,b,_)`
    in the residual where SWI reads `p13a(a,b,c)`. Truth values are
    unaffected. A delay goal with no table gives no clause, where SWI
    gives `foo :- ???`.
  - *The toplevel.* `dump_vars()` asks whether the answer has delays.
    If it has, the residual program is printed before the bindings and
    the delays after them, or `undefined` with the flag
    `toplevel_list_wfs_residual_program` false, as in SWI. Both run in a
    subquery: run in the query itself, as attributed variables' goals
    are, the first left an answer with choicepoints unable to run the
    second, and its delays went missing. Trealla's
    `portray_clause/1` prints a clause on one line, so the residual is
    laid out SWI's way by the library.
  - *A toplevel bug, not tabling's, found on the way.* Since v3.9.75,
    a query whose last goal called a user predicate lost its earlier
    bindings at the toplevel: `X = 1, foo(a).` answered `true.` The
    last call reused frame 0, which holds the query's variables, once
    any last call could reuse its caller's frame (1d17fc29). Fixed
    separately (ce7137a2), with `tests/sundry/toplevel_last_call.sh`.

  Tests: `tests/sundry/tabling_residual.pl` against SWI's output,
  `tests/sundry/tabling_toplevel.sh` for the toplevel.

**[changed in v4] Semantics to match.** SWI is the reference, since
compatibility is the point:

- `tnot/1` on an untabled predicate is an error ("The argument of
  tnot/1 is not tabled").
- A non-ground `tnot/1` is existential: with `q(a)` in the table,
  `p(Z) :- tnot(q(Z))` fails rather than floundering.
- `\+` is left exactly as it is.

**[changed in v4] Interactions with items 2–5.** Items 4 and 5 both
met surprises from earlier items; settle these before building:

- *Item 2 (answer subsumption).* A conditional answer has no defined
  combination under `min`/`max`. Refuse it with an error rather than
  invent a lattice over truth values.
- *Item 3 (incremental).* The table→table edge that invalidation
  walks is recorded at `'$tbl_variant_table'`, so `tnot/1` must look
  its table up there like a positive call does, or a change below it
  leaves the negation stale. Invalidation must also drop delay lists with
  the answers they belong to.
- *Item 4 (shared).* Safe if simplification ends before publication,
  so a published table is still never written again.
- *Item 5 (trie-path answers).* A delay list is not on the trie path,
  so `tbl_ans` grows a pointer: NULL in the common case. One answer may
  be derived several ways with different delays, so the pointer is to
  a set of alternative delay lists, not one.
- *Completion order.* `'$tbl_get_answer'` takes no choicepoint on the
  last answer, which is sound only because a complete table never
  changes. Simplification therefore has to finish before
  `'$tbl_mark_all_complete'`, never after.

**Risk.** High: stage B rewrites completion, the part of Phase 1 that
took longest to get right. **[changed in v4]** v3 called this the only
item that changes existing behaviour. It need not: `tnot/1` does not
exist today and `\+` is untouched, so **a program that does not call
`tnot/1` must give identical answers.** That is the invariant to hold,
with the whole existing tabling suite unchanged and the cost of an
empty delay list measured as item 3's null case was. A flag checked in
`start_tabling` is still worth having, for A/B testing of the new
completion rather than as a semantic switch.

**[changed in v4] Test-first.** XSB's own suite, in
`~/xsb-code/xsbtests`, is a ready oracle: `wfs_tests` (75 programs),
`neg_tests` (27) and `delay_tests` (92). Each `wfs_tests` file opens
with a `query(Name, Goal, Atoms, True, Undefined)` line naming which
atoms should come out true and which undefined. Stage A must pass the
stratified ones and raise, not answer, on the rest; B and C must pass
them all. Check SWI's answers too, where the two differ.

**Recommendation.** Own branch, existing tabling suite green
throughout, stages in order.

**Size.** Stage A small. B was large, as expected. C should be small:
a support pass over what simplification leaves, at the same point in
completion. D is mostly the toplevel and the residual program.

---

## Where these features exist elsewhere

Phase 2's six items are not novel; they are the feature set the mature
tabling implementations already carry. Useful both as a specification
to check against and as a reminder of what "done" looks like.

| # | Item | XSB | SWI | YAP | B-Prolog / Picat |
|---|------|-----|-----|-----|------------------|
| 1 | Restraints | yes (call/answer abstraction) | yes — this doc borrows its flag names | no | no |
| 2 | Answer subsumption | yes (lattice + min/max) | yes (mode-directed) | yes (mode-directed) | **originated it** — `:- table path(+,+,min)` |
| 3 | Incremental | yes | yes | yes | no |
| 4 | Shared completed tables | yes (private vs shared) | yes (`as shared`) | yes (OPTYap) | no |
| 5 | Trie-path reconstruction | yes — tries *are* the answer store | yes | yes | n/a (linear tabling, no SLG tries) |
| 6 | `tnot` / WFS | the reference implementation | yes | partial | no |

Systems at Phase 1's level — variant tabling, least model, none of the
above — are **Scryer** (a port of Desouter's delimited-continuation
library, the same starting point SWI had) and **Ciao**. SICStus, GNU
Prolog and ECLiPSe have no tabling at all.

Two things worth taking from the right-hand columns. Item 5 is the odd
one out: XSB, SWI and YAP all reconstruct answers from the trie and
none keeps a second full image, so Trealla's per-answer image was the
outlier rather than the norm. And B-Prolog reached items 2 and 5's
equivalents without ever doing item 6, so this doc's ordering — WFS
last — has precedent.

---

## Benchmarks

Measured against SWI-Prolog 10.1.14 and XSB on the same machine, three
interleaved runs each (this host drifts; alternating avoids order
bias). All three engines returned identical answers on every workload —
worth stating, since a fast wrong answer is not a benchmark.

Startup baselines, subtracted where noted: Trealla 0.03s / 8.4 MB, SWI
0.01s / 13.9 MB, XSB 0.04s / 13.0 MB.

| workload | Trealla | SWI | XSB |
|----------|---------|-----|-----|
| **tc** — left-recursive closure, 90k answers | 0.15s / 49 MB | 0.08s / 35 MB | 0.06s / 23 MB |
| **bigans** — 100k answers, enumerated 6x | 0.25s / 374 MB | 0.09s / 87 MB | 0.09s / 49 MB |
| **variants** — 200k distinct subgoals | 5.2s / 1219 MB | 0.74s / 758 MB | 0.08s / 139 MB |

Trealla is ~2x SWI on the classic closure benchmark. `variants` is the
outlier at 7x SWI and 65x XSB, so it was decomposed rather than left as
a number.

**Per-table cost is not the problem.** Running the same 50k distinct
tables *flat* (each completing before the next) against *nested* (each
a fresh variant called from inside the previous) separates the two
costs:

| engine | bytes/table | bytes/nesting level |
|--------|-------------|---------------------|
| Trealla | 816 | **6100** |
| SWI | 858 | 3074 |
| XSB | 363 | 247 |

Trealla's per-table footprint is slightly *better* than SWI's. The
whole gap is per-nesting-level, where it is 2x SWI and 25x XSB. Time
behaves the same way: flat and nested cost Trealla the same (0.43s vs
0.45s), so the expense is table *creation*, not the fixpoint.

**It is the tabling driver, not general memory behaviour.** The same
50k-deep recursion with no tabling at all costs Trealla 22.9 MB (~300
bytes/level) against SWI's 31.9 MB — competitive, and better in
absolute terms. Tabling multiplies Trealla's per-level cost ~20x (300 →
6100) where SWI's goes ~8x and XSB's ~6x. So this is not a failure to
reclaim frames in deep recursion; it is what `run_scc` → `catch/3` →
`activate` → `delim` → `catch/3` → `reset/3` costs once per nested
table. XSB pays 247 bytes because its SLG-WAM has no Prolog-level
driver at all.

That is the number to attack if this workload matters: fewer frames
per nesting level in `library(tabling)`, not the C data structures.

**Follow-up (v3.11).** The C data structures were in fact half of it:
`tbl_slot_alloc` scanned every slot for a free one, making table
creation quadratic (25k/50k/100k flat tables: 3.3/9.9/34.5 G
instructions). A free-index stack makes it linear (1.4/2.4/4.5 G), and
50k flat tables went 0.40s → 0.11s. On the driver side, `run_scc` and
`activate` were folded into `start_tabling_(fresh, ...)` and the fresh
path calls `reset/3` directly instead of through `delim/3`: 50k nested
tables 150 → 125 MB peak RSS (~2.9 → ~2.3 KB/level). Folding
`native_start_tabling` in as well measured neutral (last-call already
reclaims it), as did dropping the `tabling:` qualification (~30 bytes).
What remains is mostly the engine's cost per `catch/3`/`reset/3` layer:
untabled recursion through `call/1` alone costs ~1.1 KB/level.

**Phase 2's own cost.** Against the pre-phase-2 baseline, after the fix
below: answer-heavy workloads got *cheaper* (tc 54.7 → 49.2 MB, bigans
390 → 374 MB, from item 5 dropping images), table-heavy workloads are
level (nested 312.4 → 313.9 MB, variants 1222 → 1219 MB). Time is
within noise except bigans at +9%, which is item 5's reconstruction
cost and was measured in isolation at the same figure.

**One regression found and fixed by this exercise.** Phase 2 initially
cost +15% peak RSS on nesting-heavy workloads (nested 312 → 359 MB).
Struct growth did not explain it — per-table cost had gone *down*. It
was a single Prolog-level call: `apply_table_specs/2`, added to the
driver's per-fresh-table path, costing ~1 KB of frames per nesting
level. Stubbing it out returned exactly the baseline figure, which is
how it was confirmed rather than guessed. It is now guarded by
`'$tbl_has_specs'`, a C-side flag set the first time any spec is
declared, so the majority of programs — which declare none — skip it
entirely.

---

## Explicitly not doing

- **GMP.** Declined in Phase 1, still declined.
- **Subgoal abstraction.** Changes what a variant is, and would have to
  be designed against the trie's canonical-cell representation. Revisit
  only if restraints prove insufficient in practice.
- **Call subsumption** (as opposed to answer subsumption). Variant
  tabling only, as documented — a different indexing strategy on the
  variant trie, large project, narrow gain.

---

## Cross-cutting

**`throw_error()` returns TRUE.** It raises by setting `q->did_throw`
and the builtin returns that value, so the natural helper idiom
`if (!check(q)) return false;` reads backwards: the check "succeeds",
control falls into the body, and the pending ball is lost. Symptom is a
silent *failure* where a throw was expected. Items 1 and 2 both add
error paths to `bif_tbl_add_answer_2`.

**ASAN cannot see peak memory.** The `pl_destroy()` sweep frees
everything at exit, so a lifetime bug shows up as RSS, not as a leak
report. Items 4 and 5 are both memory work — measure RSS, do not trust
a clean ASAN run.

**[changed in v2] Do not trust an unexercised test either.** While
fixing Phase 1 review #4 I wrote a memory test whose failing insert
rejected on the *first* argument, so nothing was created before the
failure and the A/B came back byte-identical. The fix was fine; the
test proved nothing. For each item below, check the test actually
reaches the code before believing its result.

**Leaks that are not ours.** There is a real memory issue in this
tree, and it has nothing to do with tabling: `open/4` with the
`mmap(Ls)` option maps the whole file and *nothing ever unmaps it* —
`munmap` appears nowhere in the source, and `unshare_cell_` has no
slice branch. `phrase_from_file/[2,3]` uses that option, so any program
DCG-parsing files in a loop retains every file it has read. Do not
chase it here.

Reproduce it with a 256 KB file — peak RSS went 9.1 MB (1 open) to
58.9 MB (200 opens), one whole mapping retained each time, linear in
file size:

    walk([]).
    walk([_|T]) :- walk(T).
    once(F) :- open(F, read, S, [mmap(Ls)]), walk(Ls), close(S).
    run(N) :- ( between(1,N,_), once('some-256k-file'), fail ; true ).

A fix was prototyped and then dropped, so save the rediscovery: the
raw slice (`FLAG_CSTR_SLICE`, `val_str`/`str_len`) exists only to carry
this one mapping and has ~11 references in the tree, so having `open/4`
copy into an ordinary refcounted string deletes the representation, the
leak and the lifetime question together. It works — flat RSS, suite
green, and `Ls` may then safely outlive its stream — but it also made
walking the resulting list cost roughly 32 bytes per input byte, which
was never explained. That number is the blocker, not the fix.

The earlier note in this section described it as a
"string-slice-on-backtrack" leak of ~53 KB, citing a
`LEAK-string-slice-on-backtrack.md` that was never in the tree — which
is why the mechanism is spelled out above rather than cited. All three
parts were wrong: it is file-sized rather than 53 KB, it is not
backtracking-specific (a straight-line open that closes its stream
leaks just the same), and the mechanism is mmap rather than slicing.
The conclusion it drew — not ours, don't chase it here — was right.

**No leak checker on this machine will confirm any of that.**
LeakSanitizer is unsupported on macOS arm64 (`detect_leaks is not
supported on this platform`), so an ASAN run here checks memory
*errors* only, and a clean one says nothing about leaks. macOS `leaks`
does work, but tracks `malloc` and so misses the mmap case entirely —
both tools came back clean on a run that was demonstrably retaining
memory. RSS growth is the only signal that catches it.

**Test-first is cheap here.** Every item has a failing test writable
before implementation: the `as//0` divergence, a min-aggregated path
table, a table surviving an `assertz`, the thread marker count.
Writing them first also settles the surface syntax while it is still
cheap to change.

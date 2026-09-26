# Key chains: indexing that keeps database order without a copy

**Status: built** on the `key-chains` branch (`630ed482`, `9b95a0fe`,
`0b013e5c`). Designed 2026-09-26 against `ecf0ac47`, after the WMD and `giso`
work recorded in `docs/wmd-profile.md`. The design below is kept as written;
**As built** records where the build departed from it and why, and
**Results** replaces the estimates with measurements.

## The problem

An indexed lookup today (`find_key()` in `src/query.c`):

1. descends a skiplist (`idx1` on the first argument, `idx2` on `idx2_arg`,
   or `idx3` on both) that holds **one entry per clause**;
2. walks every entry whose key compares equal, and runs the atomic-argument
   candidate filter on each;
3. if more than one survives, **copies them into a temporary skiplist sorted
   by `db_id`** so they come back in database order, and hands that to the
   choicepoint as `q->st.iter`.

Step 3 exists because the index cannot be iterated lazily in place: a
choicepoint would hold a skiplist node across backtracking, and nodes are
freed by `sl_rem()` and by index rebuilds. It also means every candidate is
walked and filtered up front, even when the caller commits on the first.

Where the time goes, WMD sub-pattern query (share of `find_key()`, which is
about 68% of the run):

| share | what |
|---|---|
| ~45% | skiplist descent |
| ~16% | candidate filter (about 75 candidates per `has_topic/3` lookup) |
| ~12% | walking candidates (`sl_next_key`) |
| ~9% | cloning the goal for the composite key |
| ~7% | building and destroying the temporary skiplist |

## The idea

**Index each distinct key once, and chain that key's clauses through the
rules themselves, in database order.**

- The skiplist maps a key to a small **key head**: first and last rule with
  that key, and a count. One skiplist node per distinct key, not per clause.
- Each rule carries a `next`/`prev` link per index, threading together the
  clauses that share its key for that index.
- `assertz` links at the key's tail, `asserta` at its head, so each chain is
  in database order by construction. That is already how `sl_app()` and
  `sl_set()` order equal keys today (`sl_set` descends with `< 0` and lands
  before a key's run, `sl_app` with `<= 0` and lands after it).
- A lookup finds the key head once, sets `q->st.dbe` to its first rule, and
  records which link to follow. `next_key()` then follows that link, exactly
  as an unindexed call follows `r->next` through `pr->head` now.

No copy, no temporary skiplist, no allocation per lookup, and candidates are
produced lazily.

### Why this is safe to iterate lazily

Walking a chain of rules across backtracking is already done, and already
safe, for every unindexed call. The logical update view keeps it sound:

- a retracted rule stays linked until no reader can see it
  (`drain_complete()`), or until the predicate's reference count reaches zero
  (`leave_predicate()`); `can_view()` filters it in the meantime;
- `predicate_delink()` unlinks it from `pr->head` only then, and a reader is
  never parked on a rule it cannot see;
- the rule is freed later still, from `q->dirty`.

Key chains follow the same rules by unlinking from each key chain **inside
`predicate_delink()`**, at the same moments as the main chain. A choicepoint
holds a rule pointer, never an index node, so rebuilding or destroying an
index while a choicepoint is live cannot leave it dangling. That is the
property that walking the existing skiplist in place would lack.

## As built

Where the build departed from the design:

- **Key heads copy their key.** The skiplist node's key is a cell inside the
  key head, not inside a clause, because the clause the key came from may be
  reclaimed while others with the same key remain.
- **Keys with one clause have no key head.** On WMD 480,785 of 551,303 keys
  had a single clause, and a key head for each cost about 30 MB. The node's
  value is then the rule itself, tagged in its low bit, and its key is the
  rule's own cell, as a per-clause entry's was. A second clause gets a key
  head built and linked first, then swapped into the node in place by
  `sl_replace()`, so a reader finds the key throughout. A singleton whose
  clause has been unlinked (retracted, awaiting reclaim) is simply taken over
  by the next clause with that key, and removed with its rule at reclaim.
- **Chain membership is a bit, not a generation.** `clause.is_kchained` says a
  rule is on the current index's chains. `index_free()`, now the one place an
  index is destroyed, clears it on every rule in `pr->head`, and an abandoned
  build clears what it set. A generation number cost 8 bytes a rule.
- **`idx3` stays.** See **The composite index**: `giso` needs it.
- **Two bound keys walk the shorter chain when it is short.** With no `idx3`,
  a goal with both key arguments atomic and bound looks up both key heads,
  and a missing key on either means no candidates. It walks the shorter chain
  if it has at most `SHORT_CHAIN` (8) clauses, or while `idx3` is not yet
  built. Only a long shorter chain counts towards building `idx3`, and once
  built, lookups go straight to it without consulting the chains.
- **The `retract/1` and `clause/2` loops advance with `next_key()`,** so they
  stay on the chain a lookup chose, as `match_head()` does.

Found on the way and fixed on `main` separately, since they were bugs there
independent of this design: the comparator asymmetry (`a9eb7707`); a retract
or `clause/2` retry resuming from a stale prefetch position, and choicepoints
releasing predicate references they never held (`aea6ae00`); and clauses
freed while a binding still pointed into them (`9b61185e`).

## Which keys

Only atomic keys can be grouped. Grouping needs "equal key" to behave like
equality, and for compound keys `index_cmpkey()` does not: `f(X)` compares
equal to both `f(a)` and `f(b)`, which do not equal each other.

- **Chained keys:** small and big integers, floats, and atoms other than
  strings. Each has a key head.
- **Everything else** (compounds, strings, which can unify with lists): stays
  as it is today, one skiplist entry per clause, with the prefetch copy when a
  lookup finds several. These clauses live in a separate per-index **overflow**
  skiplist, so the key-head skiplist holds only atomic keys.
- A goal key that is chainable walks its key head's chain only. No clause in
  the overflow can unify with it: a compound cannot unify with an atomic, and
  a string is a list.
- A goal key that is compound or a string searches the overflow only, as now.
  A string goal cannot unify with an atom or a number.
- Variables in a key argument gate the index off, as now (`is_var_in_first_arg`,
  `is_var_in_idx2_arg`).

**A comparator fix comes first.** `index_cmpkey_()` returns −1 both for
float against rational and for rational against float, so its ordering is not
antisymmetric there. Today that can at worst misplace a rare entry. Key heads
depend on the ordering to decide which keys are the same, so fix it (or leave
rationals out of the chainable set) before building on it.

## Data structures

```c
typedef struct keyhead_ {
	rule *first, *last;
	unsigned count;			// live and retracted-but-linked
} keyhead;

struct rule_ {
	...
	rule *kprev[2], *knext[2];	// per index: [0] idx1, [1] idx2
};

struct predicate_ {
	...
	skiplist *idx1, *idx2;		// now: atomic key -> keyhead*
	skiplist *ovf1, *ovf2;		// per-clause entries for compound/string keys
};

// run_state gains which chain dbe is on: 0 = pr->head, 1 = idx1, 2 = idx2.
```

**Memory.** Four pointers per rule is 32 bytes on every clause of every
predicate, indexed or not. That is the simplest layout. If it matters, the links can
live in a side array allocated only when a predicate is indexed, leaving an
8-byte pointer in the rule. Against that, today's `idx1`/`idx2` hold a
skiplist node per clause each (key, value and a forward array, plus malloc
overhead, roughly 40-50 bytes). On WMD the key-head skiplists shrink a lot:

| | clauses | distinct keys | per key |
|---|---|---|---|
| `has_topic/3` arg 2 | 115,788 | 17,011 | 6.8 |
| `author/3` arg 1 | 141,561 | 55,696 | 2.5 |
| `include/2` arg 1 | 44,158 | 40 | 1,104 |
| `purchase/4` arg 1 | 34,241 | 34,018 | 1.0 |

So for indexed predicates this should be a net saving, not a cost.

## Operations

**Build** (`build_predicate_index()`): walk `pr->head` in order. For each
index, a chainable key finds or creates its key head (`sl_get`, then
`sl_set` of a new one) and appends the rule. Anything else goes to the
overflow with `sl_app`, as now. Published under the same discipline as today:
built completely, then `pl_publish_barrier()`, then `pr->idx1` set last.

**Assert** (`assert_commit()`): the same per clause, linking at the tail for
`assertz` and at the head for `asserta`.

**Retract.** Nothing at retract time: the rule stays on its chains, flagged
by `dbgen_retracted`, like on the main chain.

**Delink** (`predicate_delink()`): unlink from each key chain (O(1), doubly
linked), adjust the key head's first/last/count. Same lock, same moment as
the main chain. An emptied key head stays in the skiplist for now.

**Reclaim** (`reclaim_rule()` / `index_remove_clause()`): overflow entries are
removed with `sl_rem()` as now. Key heads whose count has reached zero are
removed from the skiplist and freed here, when no reader is inside the
predicate.

**Lookup** (`find_key()`): choose the path as now. For a chainable key,
`sl_get` the key head and set `q->st.dbe = kh->first` and the chain id. No
prefetch, and no `iter`/`iter_single`. The per-clause candidate filter goes:
`match_head()` already applies the clause-signature filter on a chain walk,
and has the goal's dereferences to hand.

**Next** (`next_key()`): `dbe = dbe->knext[chain - 1]`.

**Has next** (`has_next_key()`): the chain-walk case generalises from
`r->next` to "next on this chain", with the same bound-argument checks.

**Destroy** (all the `sl_destroy(pr->idx1)` sites): free the key heads and
clear the rule links. Since a choicepoint holds rules, not index nodes, the
existing destroy points stay safe. The rules' own links must be reset if the
index is rebuilt, because a rebuild relinks them.

`match_rule()` and `match_clause()` (retract, clause) use `find_key()` and
`next_key()`, so they get the same behaviour without separate changes.

## The composite index

**Measured: `idx3` cannot go.** In `giso_07` the both-bound lookups (about
900,000, on `submap_/7`) average about 80 clauses on the first argument's
chain and about 1,250 on the second's, for about 1.6 actual matches: walking
the shorter chain means about 80 candidates where `idx3` finds the 1.6 almost
directly. On WMD it is the other way round: `include/2`'s second argument has
one clause per key, so walking that chain beats `idx3`'s descent, goal copy
and prefetch. Hence the rule above: short chain walked, long ones left to
`idx3`. The design as first written follows.

With a count on each key head, a goal with both key arguments bound can look
up both key heads and **walk the shorter chain**, leaving the other argument
to the signature filter. For `include/2`, where the first argument has 1,104
clauses per key and the second has one, that picks the one-clause chain.

That may make `idx3` unnecessary, along with its clone of the goal and the
`composite_index_wanted()` threshold. It would not help where both chains are
long and only their intersection is short, which is the case `idx3` was
built for in `giso`'s `e/3` (about 92 candidates for 1.5 matches when keyed
on the first argument alone). **Measure before deciding:** first keep `idx3`
and add shorter-chain selection, then check `giso_07`'s `e/3` chain lengths
for both arguments. If the shorter chain is short, drop `idx3`.

## Results

Against `main` at `v3.11.14`, which already had the fixes found on the way,
so what remains is the design's own effect.

WMD, `data.01` (582k facts), two interleaved runs each. "First" is the first
query, including building the indexes; "later" the mean of ten more.

| | `main` | key chains | |
|---|---|---|---|
| single, first | 301 ms | 238 ms | −21% |
| single, later | 15.7 ms | 13.1 ms | −17% |
| sub, first | 684 ms | 494 ms | −28% |
| sub, later | 291 ms | 219 ms | −25% |
| wall, single / sub | 1.26 / 4.42 s | 1.15 / 3.49 s | |
| footprint, single / sub | 230.5 / 248.8 MB | 236.3 / 242.7 MB | +5.8 / −6.1 MB |

`giso`'s full tester (`tests::run`, ten tests), one run each; all pass on both:

| | `main` | key chains | |
|---|---|---|---|
| giso_07 CPU | 2.43 s | 2.29 s | −5.8% |
| giso_08 | 4.73 s | 4.38 s | −7.4% |
| giso_09 | 9.78 s | 8.88 s | −9.2% |
| giso_10 | 22.45 s | 20.29 s | −9.6% |
| total wall | 42.57 s | 39.09 s | −8.2% |
| peak RSS | 5.271 GB | 5.236 GB | −35 MB |

Memory: a rule is 208 bytes against `main`'s 176 (two chain links each way),
which moves most WMD facts up one allocator size class; the index itself is
smaller (one entry per distinct key, and none beyond the entry for a key with
one clause), and on WMD the two roughly cancel.

Threads: `tests/misc/key_chains_threads.pl` has readers check that clauses no
writer touches are always found, by either indexed argument and both, while
writers churn the same keys. The branch passed 100 runs of 100; `main` failed
it 2 runs in 30, a first-argument lookup finding none of a key's ten
permanent clauses. There every clause has its own skiplist node, inserted and
removed in the same run of keys readers are descending without the lock;
here a key with a stable chain keeps one node. Compound and string keys still
use per-clause overflow nodes, so that race remains possible for them. ASan
found no memory errors on either build.

Before building, the estimate was:

It removes the prefetch copy (~7% of lookup time on WMD sub), makes the walk
and filter lazy (~28%, saved only where a caller stops early), shrinks
index memory, and may remove `idx3`.

It does **not** remove the descent. With most WMD keys having one to seven
clauses, a skiplist over distinct keys is only one or two levels shallower
than one over clauses. The hash index, which removed the descent, gave 40-55%
on WMD (`docs/wmd-profile.md`); this should give a good deal less there.
Estimate 10-20% on WMD, possibly more on `giso`, where lookups find more
candidates and backtracking often stops early. To be measured, not assumed:
profile shares have overstated the gain twice in this work.

## Risks

- **Lifetime.** The design rests on key-chain unlinking happening exactly
  where main-chain unlinking does. Any path that frees or relinks a rule
  without going through `predicate_delink()` would leave a dangling link.
  Audit: `retract_from_db`, abolish, reconsult, `erase/1`, and the undo path
  in `reclaim_rule()`.
- **Threads.** Readers walk chains without the module lock, as they walk
  `pr->head` today. Linking a new rule must publish its own links before the
  predecessor's `knext`, as the main chain does. `tests/misc/jit_index.pl`
  and `tests/misc/db_purge_window.pl` race these paths and must stay green.
- **Rebuild with live choicepoints.** An index rebuilt while choicepoints sit
  on a chain relinks rules those choicepoints will follow. Either rebuild
  only when `refcnt` is zero (as the flag recheck already does), or keep the
  links valid across a rebuild.
- **Determinism.** `has_next_key()` decides whether a choicepoint survives. A
  chain walk that answers differently from today would change which calls
  leave choicepoints: correct, but visible in toplevel output and in tests.

As built: unlinking is in `predicate_delink()`, and `index_remove_clause()`
unlinks first for the property purge, which reclaims before delinking. A
choicepoint holds rules, and `index_free()` leaves their links alone. An index
is only destroyed with no reader inside the predicate, or by abolish, which
first takes the old rules out of `pr->head`; so a rebuild, which relinks the
rules it chains, never relinks one a parked reader can still reach. The suite, `tests/misc`
and the logical-update-view tests pass unchanged.

## Plan (done)

1. Fix the float/rational asymmetry in `index_cmpkey_()`, as its own commit.
   Done on `main` (`a9eb7707`): it was losing clauses, not just misplacing them.
2. Key chains for `idx1` and `idx2` with the overflow skiplists; keep `idx3`.
   `--index-check` must verify chain candidates against a full scan, as it
   does skiplist candidates now. Done; it verifies 1.18M WMD lookups clean.
3. New tests: iterate an indexed predicate while `asserta`, `assertz` and
   `retract` hit the same key, and check answer order and logical update view
   semantics against SWI. Plus the existing threading tests. Done:
   `tests/sundry/index_key_chains.pl` (identical to SWI) and
   `tests/misc/key_chains_threads.pl`.
4. Measure: per-lookup microbenchmark, WMD single and sub, `giso_07`, memory.
   See **Results**.
5. Shorter-chain selection for two bound arguments; measure; drop `idx3` if
   it adds nothing. Done: `idx3` stays, see **The composite index**.

## Alternatives already tried

- **Hash index on atomic keys** (arg 1, `idx2_arg`, and the pair), beside the
  skiplists: WMD single −55%, sub −40%, `giso_07` −9%, about 40 MB more on
  WMD, +379 lines. Declined for the code it adds. Kept as a patch only.
- **Walking the existing skiplist run in place**: each key's entries are
  already in clause order, but a choicepoint would hold a skiplist node that
  `sl_rem()` or a rebuild can free.
- **Lock-free iterator per query**: −7% per lookup, about 1% on `giso`. Not
  kept.
- **`idx0`** (whole ground heads): removed; lookups through `idx1` were
  cheaper.

Key chains and hashing are complementary. A hash from key to key head would
remove the descent too, and would be smaller than the declined hash index,
because the chains already provide order and lifetime.

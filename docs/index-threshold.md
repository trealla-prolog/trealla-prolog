# Why clause indexing starts at 500 clauses

**Verdict: leave the threshold alone.** Lowering it was measured across a
sweep and makes things worse from 32 clauses downwards, badly. The reason is
that an indexed lookup carries fixed per-lookup costs a clause walk does not,
and those were measured too. Recorded against `432457a4` so the number stops
being folklore.

## The sweep

`assert_commit()` starts building an index at 500 clauses. Chess, whose
largest predicate has 36 clauses, therefore has no index at all, and its calls
walk the chain. Lowering the threshold so they get one:

| threshold | instructions | wall | head unification attempts |
|---|---|---|---|
| 500 (current) | 123.90 G | 5.37 s | 67.7 M |
| 100 | 123.88 G | 5.02 s | 67.7 M |
| 32 | 157.49 G (+27%) | 6.59 s | 62.0 M |
| 16 | 228.53 G (+84%) | 9.63 s | 62.0 M |
| 8 | 233.11 G (+88%) | 9.89 s | 61.0 M |
| 4 | 319.51 G (+158%) | 13.67 s | 59.8 M |
| 2 | 1031.70 G (+733%) | 42.96 s | 60.3 M |

The index is doing its job: attempts fall from 67.7 M to about 60 M, so it
discriminates better than the first-argument signatures that replace it below
the threshold. It is the cost of each lookup that runs away.

## Where that cost is

Profiling chess at a threshold of 16, by self time: `clone_term_to_tmp_internal`
771, malloc and free together 427, `unify_args` 357. Two things an unindexed
call never pays:

**The key clone.** `find_key()` clones the whole goal to the tmp heap before
consulting the index, because `index_cmpkey_()` walks cells with `p1++` and
cannot dereference: the key has to be self-contained and contiguous.

Priced by cloning twice and taking the difference, on `giso_07`: compiling
+6.2%, iso +4.3%, **3.5% of the run**. Parsing is unaffected, it barely does
indexed lookups.

It cannot be skipped when the arguments are already atomic, because they
almost never are. Counting on `giso_07`:

```
CLONE lookups=1088767  no_var_args=16  (0.0%)
```

Sixteen lookups in a million. Goal arguments in real code are variables bound
at run time; a goal written with constants is what a probe looks like, not a
program. Dereferencing per argument instead of cloning works only for the
single-argument keys (`idx1`, `idx2`) - `idx0` needs the whole ground head
contiguous and `idx3` needs head shape for `index_cmpkey2()` - and `idx0`
alone serves over half of `giso`'s `e/3` lookups. Teaching the comparator to
dereference instead would put that work on every comparison in the descent,
which is 24% of the compile phase on its own.

**The prefetch.** A lookup with more than one surviving candidate builds a
temporary skiplist so results come back in database order: a `calloc` for the
list, a `MAX_LEVELS` header node, a mutex init, and a node malloc per entry.

Priced the same way, on `giso_07`: compiling +1.7%, iso +2.2%, **1.3% of the
run**. Less than it looks like it should be, because only 125,975 of 1,088,767
lookups allocate at all - the single-candidate path skips it through
`iter_single`, and the composite index cut multi-candidate lookups sharply.
What remains averages 8.9 entries, so the per-list costs amortise.

```
PREFETCH lists=125975  entries=1123398  avg=8.9
```

## What this means

About 5% of fixed overhead per indexed lookup on `giso`, and neither half comes
off cheaply. Against a 92,000-clause predicate it is invisible. Against a
16-clause one it is most of the work, which is what the sweep shows.

500 is not a tuned number so much as a safe distance from the point where that
overhead stops paying. Somewhere between 32 and 100 is where it actually turns
over on chess, and moving it there would gain nothing measurable while risking
workloads with different shapes.

The way to make small predicates indexable is to make an indexed lookup cheap -
no key copy and no allocation - not to lower the threshold over the top of the
costs. That is a different and much larger piece of work. In the meantime the
cheap stand-in is in `match_head()`: per-clause signatures of the first three
head arguments, which reject a clause for three instructions and no allocation
at all (`eb2683ae`, `432457a4`).

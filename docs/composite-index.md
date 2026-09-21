# A composite index for two bound arguments

**Landed.** Built on demand, so only predicates that are actually queried on
two columns pay for it. An earlier eager version was measured and shelved;
what changed is below.

## What it is

`find_key()` keys on the first argument alone. A goal with the first argument
and `idx2_arg` both bound therefore gets every clause sharing the first, and
the candidate filter (`9df92c4e`) rejects the rest one at a time - about 92
candidates for 1.5 matches in `giso`'s edge table.

`idx3` is keyed on both arguments together. Its keys are whole clause heads,
like `idx0`'s, and `index_cmpkey2` compares the two components in order,
leaving every other argument to the candidate filter. The comparator takes the
predicate as its `param`, which is where it reads `idx2_arg` from; that slot
is free because `_C_STR` never uses its first macro argument.

## Built on demand

Nothing is built until a predicate has seen `COMPOSITE_INDEX_THRESHOLD` (100)
lookups with both key arguments bound. `composite_index_wanted()` counts them
on the read path - approximately, since the count only decides when to build
and the build re-checks under the lock.

The build itself is `build_predicate_composite_index()`, which walks the live
chain and publishes `idx3` only once it is complete, so a reader already
walking the predicate sees either no index or a whole one. That is the same
discipline `build_predicate_index()` uses for just-in-time indexing, and
`tests/misc/jit_index.pl` races both builds against four concurrent readers.

A clause asserted with a variable in either key argument cannot be ordered, so
it destroys `idx3` and sets `no_idx3`: the shape it answers no longer holds
for that predicate, and it is not rebuilt.

## What it does

| case | before | after | SWI |
|---|---|---|---|
| two bound arguments, 64,000 facts | 0.180 s | 0.130 s | 0.136 s |
| single integer key (must not regress) | 0.081 s | 0.081 s | - |
| `giso_07` instructions retired | 102.87 G | 100.15 G | - |
| `giso_07` compiling / iso | 1.059 / 1.304 s | 1.032 / 1.264 s | - |
| `giso_10` (Andrew, whole test) | about 30 s | under 25 s | - |

Candidates visited, `giso_07`:

| predicate | before | after |
|---|---|---|
| `$giso#0.submap_#6/7` | 43,492,614 (avg 51.6) | 795,398 (avg 2.7) |
| `e/3` | 22,396,854 (avg 28.4) | 11,491,361 (avg 14.6) |

Correct: `--index-check` verified the composite lookups against a linear scan
with 0 mismatches, including after a variable-keyed clause drops the index;
answers and clause order are identical to an unpatched build. The suite passes
459/459 and `misc` 36/36.

## Why the gain grows with the data

Removing 98% of candidate visits is worth only about 3% at `giso_07`, because
the candidate filter had already made each visit cheap - what remains there is
real unification and backtracking, not walking. At `giso_10`, eight times the
data, the same walk chases pointers through a structure far past the cache and
the same change is worth about 17%. The shape that pays is a large fact table
with an unselective first argument queried on two columns, and it pays more
the larger it gets.

## What it costs

Peak RSS on `giso_07` went from 620.7 MB to 629.3 MB, +1.4%. The eager version
cost +11% on every large fact base, whether or not anything queried it that
way, and made a single-integer-key probe 10% slower for an index it never
used. Building on demand is what removed both.

# Profiling the WMD subgraph-isomorphism queries

Yin & Kogge, *Subgraph Isomorphism: Prolog vs. Conventional* (arXiv
2511.13600) find one fixed pattern in an IARPA AGILE graph with two SWI
programs. The code is at <https://github.com/claireyyin/WMD-prolog>:
`singlequery.pl` (the pattern as one 35-goal rule) and `subpatternquery.pl`
(six sub-pattern rules), over facts converted from CSV by `read2prolog.py`.
Measured 2026-09-24 on `main` after `v3.11.1`.

**Summary:** Trealla loads the data fastest of the four systems and answers
correctly, but its queries run 2.5-3x behind SWI and Scryer. The cause is
the fixed cost of an indexed point lookup, not backtracking.

## Running it

Delete lines 4-5 of each program (a hard-coded `consult`) and pass the
dataset on the command line. `data.01.csv` (the largest supplied, 25 MB) is
converted by pointing `read2prolog.py`'s `main()` at it; it gives 582k
facts, about 350k of them edges. The `_rN` files are scaled down from it. Every
system must answer `1128501731262832684` (Person1).

```
once_p(P) :- ( find_person1(P) -> true ; P = none ).
b(N) :-
	statistics(cputime, L0), once_p(P), statistics(cputime, L1),
	( between(1, N, _), once_p(_), fail ; true ),
	statistics(cputime, L2),
	format("~w first ~3f ms, later ~3f ms~n", [P, (L1-L0)*1000, (L2-L1)*1000/N]).
```

What each system needed:

- **Trealla**: nothing. It reports an `existence_error` for SWI's
  `style_check/1`, which is harmless here.
- **SWI 10.0.2**: nothing (warnings about clauses that are not together).
- **XSB**: its compiler cannot store integers above 32 bits in `.xwam` and
  silently substitutes 2147483647, so every object ID collapses to one value
  and the search explodes. Facts must be loaded with `load_dyn/1`. There is no
  `dif/2`; every call here has both arguments bound, so `X \== Y` stands in.
  The `xsb` wrapper also looks for a config dir matching the OS release, so
  after an OS upgrade run `config/<old>/bin/xsb` directly.
- **Scryer**: 0.10.0 (Homebrew) returns `none`, because first-argument
  indexing misses integers of 2^55 and above when a predicate has more than
  one clause (`q(1). q(36028797018963968).` then `?- q(36028797018963968).`
  fails). Master `450d738` has it fixed. Scryer also replaces rather than
  appends to clauses that are not together in the file, and the converter
  interleaves `topic/1` with `topic/3`, so each dataset needs
  `:- discontiguous(...)` at the top.

## Results, `data.01` (582k facts)

ms per query; "first" includes building indexes on the first call. The total
wall time covers loading plus 11 single-query runs.

| | single, first | single, later | sub, first | sub, later | total wall | peak RSS |
|---|---|---|---|---|---|---|
| SWI 10.0.2 | 150 | 5.5 | 298 | 138 | 3.0 s | 183 MB |
| Scryer master | 100 | 5.7 | 204 | 110 | 5.6 s | 816 MB |
| Trealla | 453 | 17.4 | 837 | 328 | 1.4 s | 247 MB |
| XSB, `index/2` declared | 450 | 448 | 16,127 | 16,135 | 12.3 s | 4.9 GB |
| XSB, default indexing | 2,970 | 2,980 | - | >300 s | 39.6 s | 99 MB |

The paper's slowdown in SWI at 2-4M edges is not reproducible: neither the 2 GB
reference dataset nor the generator is in the repo.

## First lead: backtracking through facts (a partial red herring)

A per-lookup probe showed Trealla at ~170 ns per answer against SWI's 66 ns
when enumerating `purchase(_,_,185785,_)`, 11,910 answers from 34,241
clauses. Measured on its own: **3,779 instructions per answer against SWI's
1,509**.

Two findings about the data, which explain that goal:

- **11,907 of the 34,241 `purchase/4` facts have `_` as the product**:
  `read2prolog.py` writes `_` when a sale has no product. All of them match
  any product, so 11,907 of the 11,910 "answers" come from those facts, not
  from the product the goal names. Both systems do this work.
- **Because of that, argument 3 cannot get an index.** `build_predicate_index()` takes
  `idx2` as the first argument after the first with no variable in any
  clause head (argument 2, the seller, here). A goal binding only argument 3
  falls back to walking the clause chain.

On that fallback path two costs were avoidable, and both are fixed
(`142f275f`, `3cca820b`):

1. **`match_head()` skipped the per-clause signature filter** whenever the
   predicate had an index, assuming `find_key()` had already narrowed the
   candidates. On the fallback it had not, so every non-matching clause paid for
   `try_me()`, `unify()` and `undo_me()`. The filter now also applies
   when the lookup produced no iterator. **3,779 → 2,553 instr/answer.**
2. **`find_key()` never called `setup_key()` on the fallback**, so
   `has_next_key()`'s look-ahead had no bound-argument flags and ran a
   whole-head compare on every clause it passed. Calling it there gave 2,553 →
   2,397. Then `setup_key()` records `key_args_checked` when every bound
   argument is atomic and among the first three; the per-argument tests
   already decide a match in that case, so the whole-head compare is skipped.
   **→ 2,225 instr/answer** (−41% in all; 501 enumerations 1.03 s → 0.64 s,
   SWI 0.60 s). Suite 467/467.

**Neither change moved the real queries** (single 17.4 → 17.8 ms, sub 342 →
340 ms, within noise). This goal was not where they spend their time.

## The real cost: indexed point lookups

Profiling `find_person1/1` itself: `sl_find_key` and `index_cmpkey_` take
over half the samples, with `clone_term_to_tmp` and malloc/free after them.
In `singlequery.pl`, `purchase(Person1, _, 2869238, BBDate)` produces ~11,910
candidates. For each one, the next goal,
`purchase(Person1, Person3, 271997, PCDate)`, does a first-argument lookup:
~12k indexed lookups per query.

Measured on its own (34,018 distinct buyers, one lookup each, per pass):

| | instructions per lookup |
|---|---|
| Trealla | ~6,670 |
| SWI | ~1,160 |

**5.7x as many instructions per lookup**, and that is the query gap. Inside
`find_key()`:

| share | where |
|---|---|
| ~52% | `sl_find_key()` descent: ~16 levels, compares through a function pointer to `index_cmpkey_()`, each touching a different clause's cells |
| ~26% | `clone_term_to_tmp()` of the whole goal |
| rest | candidate filter, iterator taken under the skiplist's mutex, prefetch list when more than one candidate survives |

`docs/index-threshold.md` priced the same two fixed costs on `giso` (clone
3.5% of the run, prefetch 1.3%) and noted that avoiding the clone by
dereferencing per argument works for single-argument keys (`idx1`, `idx2`)
but not `idx0` (needs the whole ground head) or `idx3` (needs head shape).
Here the lookups that matter are `idx1`: `purchase/4` has variables in heads,
so `idx0` is off for it.

## Dropping `idx0`

`idx0` keyed whole ground heads and served only fully ground goals on
predicates whose heads are all ground. `build_predicate_index()` still created
it for every predicate and filled it with every ground head, even for
predicates with a variable in some head, where no lookup could ever read it.
`assert_commit()` and `index_remove_clause()` then kept it up to date.

Measured with it switched off (three alternating runs; answers unchanged):

| `data.01` | single, first | single, later | sub, first | sub, later | peak RSS |
|---|---|---|---|---|---|
| with `idx0` | 458 ms | 16.8 ms | 851 ms | 327 ms | 247 / 258 MB |
| without | 268 ms | 15.1 ms | 653 ms | 275 ms | 228 / 246 MB |

The later-query gain shows that where `idx0` was used, a lookup through it
cost more than going through `idx1`: its descent compares whole heads at every
step, while `idx1` compares one argument and leaves the rest to the candidate
filter. `giso_07`, where `idx0` served over half the `e/3` lookups, went 2.69 s
→ 2.62 s CPU (compiling about −4%, iso about +1%), RSS 614 → 606 MB.

So `idx0` is gone. Suite 467/467; `--index-check` verified 1.18M indexed
lookups on both WMD programs with 0 mismatches. `is_var_in_head` stays: the
determinism test in `commit_frame()` still reads it.

## Next steps, most contained first

1. **Clone only when needed.** For the `idx1`/`idx2` paths, use the
   dereferenced key argument directly, and deref goal arguments in the
   candidate filter; clone only when `idx3` is chosen. ~26% of a lookup here,
   and with `idx0` gone this now covers nearly every indexed lookup.
2. **The iterator mutex.** Every `sl_find_key()` on a non-tmp list takes
   `l->guard` to pop an iterator from the free list. A per-query iterator,
   like `tmp_iter`, avoids it.
3. **Descent cost.** A hash table on atomic first-argument keys (SWI's
   approach) makes a point lookup O(1) and touches one bucket instead of ~16
   scattered clause heads. Larger: the skiplist also serves ordering and
   compound keys, so this would sit beside it rather than replace it.
4. **Choose `idx2` by use, not by position.** It is fixed at build time to
   the first variable-free argument. SWI builds indexes on demand for the
   arguments goals actually bind. Would not have helped `purchase/4` (the
   `_` products), but would help any predicate queried on argument 3 or later.

Scratch harness (not kept): the programs with lines 4-5 removed, a `b/1`
timing driver as above, `pb.pl` running
`( between(1,N,_), purchase(_,_,185785,_), fail ; true )`, and `pl.pl`
looking up every distinct buyer once per pass. Per-answer and per-lookup
figures are (instructions at N=501 − at N=1) / 500 / count, from
`/usr/bin/time -l`.

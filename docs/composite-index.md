# A composite index for two bound arguments

**Built, measured, not landed.** Against `947ab421`. Recorded so the
measurements are not lost and the tradeoff is explicit if anyone wants it
later.

## What it was

`find_key()` keys on the first argument alone. A goal with the first two
arguments bound therefore gets every clause sharing the first, and the
candidate filter (`9df92c4e`) rejects the rest one at a time - about 94
candidates for 1.5 matches in `giso`'s edge table.

The change adds `idx3`, keyed on the first argument and `idx2_arg`
together, with whole clause heads as keys and a comparator
(`index_cmpkey2`) that compares the two components in order. A goal that
leaves the second key unbound compares equal on that component, by the same
variable rule `index_cmpkey_` already uses, so the lookup degrades to
exactly what the first-argument index would have found. It is maintained
everywhere `idx1` and `idx2` are, and built with them at the 500-clause
threshold.

## What it did

Correct: `--index-check` verified 14,240 indexed lookups against a linear
scan with 0 mismatches, the suite passed 451/451, and all 13 lookup shapes
in the probe returned counts identical to an unpatched build.

| case | main | with idx3 | SWI |
|---|---|---|---|
| per-dart lookups | 0.185 s | 0.151 s | 0.068 s |
| first two arguments bound | 0.100 s | 0.070 s | 0.033 s |
| arguments 1 and 3 bound | 0.101 s | 0.098 s | 0.033 s |
| `giso`'s compile pattern | 0.214 s | 0.170 s | 0.120 s |
| single integer key | 0.113 s | 0.124 s | 0.057 s |

Chess was unaffected: instructions within run-to-run noise, RSS identical.

## What it cost

Peak RSS on a workload holding 277,000 dynamic facts went from 181 MB to
201 MB, +11%, about 72 bytes a clause. The index is built eagerly for every
predicate past the threshold with a suitable second argument, whether or
not anything ever queries it that way - which is also why the
single-integer-key probe came out 10% slower, `pk/2` paying for an index it
never uses.

## Why it was not landed

The candidate filter had already taken `giso`'s compile pattern from 1.33 s
to about 0.19 s. This is 21% of what remained, roughly 40 ms, on a test
whose parse alone is 0.9 s. That does not pay for +11% memory on every
large fact base.

## If it is revisited

- **Fold it into `idx1`** rather than adding a third index: key the primary
  index on both arguments. Memory-neutral, since the degrade-on-unbound
  property means it still answers first-argument lookups. The wrinkle is
  that asserting a clause with a variable in that argument breaks the
  ordering, so that case has to rebuild the index on the first argument
  alone.
- **Or build on demand**, counting lookups that would have benefited. That
  spares predicates nobody queries that way, but building an index from the
  read path needs care: predicates are shared between threads.

The shape that pays is a fact table with an unselective first argument
queried on two columns. Nothing currently measured is dominated by it.

## Where the time goes now

Profiling the lookup loop after the candidate filter: `index_cmpkey_` 25%,
`find_key` 13%, the skiplist descent and duplicate walk 13% between them,
malloc and free 6%. A further 12% appeared to be `\+` and `forall/2` resolving
the predicate by name through `call_check` and `search_predicate`, with
`strcmp` and a mutex. Measuring it separately showed that reading was
wrong: `call_check` re-resolves only for `call/N` and for zero-arity
goals, and it costs 27ns and 100ns a call respectively. The 12% was the
probe's own `forall(Goal, true)`, whose `true` is exactly the zero-arity
case, twice a dart.

What that measurement did show is `forall/2` itself: over 300,000
iterations against a direct call, `forall` with a compound action cost
about 380ns a call where SWI's costs 63ns. That has since been chased.
`forall/2` is compiled as its own two-barrier construct, with a builtin
for goals only known at run time (`e4d04c63`), which took its overhead
over a direct call from about 380ns to 120ns. A negation's body is
compiled rather than left as a term (`4f329834`), and the callability
check that cloned a body to the tmp heap on every call is emitted only
where the body could fail it (`adde3399`): `\+` over a conjunction runs
32% faster, and `once/1`, `ignore/1` and `call/1` over one 27-29%.

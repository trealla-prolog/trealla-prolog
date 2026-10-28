# Finding a clause from its reference

**Proposal: put the predicate in the reference, and add no new index.**
`erase/1` and `clause/3` with a bound reference find the clause by walking every
clause of every dynamic predicate in every module. The cost of one call grows
with the whole dynamic database, not with the predicate the clause belongs to.
If the reference names its predicate, the walk shrinks to that predicate's
chain. That removes the worst case and touches no clause lifecycle. A global
reference-to-clause index would make the lookup constant time, but it has to
stay in step with every path that frees a clause, and those paths are where
#1171's use-after-frees were. Leave that until a per-predicate walk is shown to
matter.

**Done: option B.** A reference is now `0000000000000b7a:1:4:userfoo`: the
16 hex digits, then `:Arity:ModuleLength:`, the module name and the predicate
name. `find_clause_ref()` in `src/bif_database.c` walks that predicate's chain,
and falls back to the old walk over every dynamic clause when the named
predicate does not hold the clause. With 100,000 unrelated dynamic clauses,
`assertz/2` then `erase/1` went from 2,120 µs to 6.6 µs. As expected, erasing
within one large predicate is unchanged (62 µs at 20,000 clauses), and so is
the qprolog bootstrap (about 10.5 s), whose records all sit in `'$record'/2`.

Measured 2026-10-28 on main at `95579250`, Apple Silicon.

## The cost

`find_in_db()` (`src/module.c`) loops over modules, then predicates, then each
predicate's clause chain, skipping non-dynamic predicates and retracted clauses,
until `dbgen_created` matches. The chain still holds clauses that have been
retracted but not yet purged, so the walk passes over those as well.

| workload | per call |
|---|---|
| `assertz(small(J), R), erase(R)`, no other dynamic clauses | 13 µs |
| the same, with 10,000 clauses in an unrelated `big/1` | 56 µs |
| the same, with 100,000 clauses in `big/1` | 2,120 µs |
| `erase/1` on each of 20,000 `d/2` clauses, refs collected first | 67 µs |
| `retract(d(k, _))` on the same 20,000 | 0.65 µs |

The 100,000 row grows faster than linearly: the walk misses the cache on every
clause it passes. With the recorded database (`95579250`), the qprolog compiler
bootstrap under tpl takes 2.2× the time of a shim that retracts by key
(11.4 s against 5.0 s) for only 8% more instructions, and this walk accounts
for the difference.

Callers: `erase/1` via `erase_from_db()`, and `clause/3` with a bound reference
(`src/bif_database.c`). `instance/2` and `recorded/3` reach it through
`clause/3`.

## How a clause lives and dies

A clause is made in one place, `src/module.c` (`r->dbgen_created =
++pl->dbgen`), so references are unique within a Prolog instance. The
reference handed to Prolog is that number printed as 16 hex digits, and nothing
in it says which predicate the clause belongs to. `tests/misc/clause_refs.pl`
treats the printed form as an implementation detail, so it can change.

Clauses leave in two ways.

**Retracted first, freed later.** `remove_from_predicate()` sets
`dbgen_retracted`, and the clause stays linked until no query can still be
reading it. It is then freed from one of the following, each of which already
calls `index_remove_clause()` beforehand where an index exists:

- `purge_reclaimed()` and `query_purge_dirty_list()` (`q->dirty`)
- `purge_undo_rules()` and `undo_list_drain()` (undo lists)
- `release_rule()`, `leave_live()` and `pl_destroy()` (adopted and retained)
- `detach_clauses()`, which marks every clause retracted before handing it on

**Freed without a retract.**

- `destroy_predicate()` frees the whole chain, at module teardown.
- `purge_properties()` delinks and frees `'$predicate_property'` clauses on
  reconsult.

`find_in_db()` already ignores retracted clauses, so for the first group a
reference stops resolving as soon as the clause is retracted, long before it is
freed. Only the second group frees a clause that a lookup could still return.

## Options

**A. A global reference-to-clause map.** A hash or skiplist from
`dbgen_created` to `rule *`. Each entry is added when a dynamic clause is
committed, and removed when the clause is retracted and on the two
freed-without-retract paths. The lookup is O(1) or O(log n) and fixes every row
above. The risks:

- **Dangling pointers.** A missed path leaves a pointer into freed memory, and
  the lookup cannot detect it.
- **Thread safety.** The map is shared by every thread, so it needs its own lock
  on assert, retract and lookup.
- **A new invariant.** It spans the paths
  [the clause-freeing work](retracted-clause-reclamation.md) unified, which is
  the kind of change that produced #1171.

**B. The reference names its predicate.** Print the reference as the module,
name and arity alongside `dbgen_created`, with the name length-prefixed so any
atom round-trips. `find_in_db()` looks the predicate up through the module's
predicate index, which it already maintains, and walks only that chain. Nothing
new has to be kept in step: an unknown or abolished predicate, or a clause
retracted or gone, still just fails. This fixes the cross-predicate rows (the
2,120 µs case becomes the 13 µs one). It does not fix a single large predicate:
20,000 records under one key are still a 20,000-clause walk. The cost is a
longer reference atom, made each time `clause/3` returns a reference.

**C. Purge sooner.** Unlink retracted clauses from the chain earlier, so the
walk has less to pass over. That helps the drain pattern a little, at no
asymptotic gain, and it moves the purge, which is the delicate part.

**D. Leave it.** It costs nothing until a program erases by reference while
holding a large dynamic database. The recorded database is now such a program.

## Recommendation

Do B. It is local to `clause_ref_to_buf()`, `clause_ref_from_buf()` and
`find_in_db()`, adds no lifetime rules, and removes the case that grows with
unrelated data.

Reconsider A only if a real program shows the per-predicate walk. The recorded
database is the likely one, since it keeps every record in `'$record'/2`, but
records are usually erased just after `recorded/3` finds them near the front of
their key. Records are first-argument indexed by key, so a smaller
alternative to A is to give `erase/1` the key as a hint and walk only that
key's index chain.

Before landing either, rerun `tests/sundry/abolish_in_use.pl` and
`tests/misc/db_other_query.pl` under ASan, as for any change near clause
freeing.

# Reworking the call path

**Verdict: do not rework it.** On a real program Trealla is already within
27% of SWI, and the call path is not where most of that 27% goes. The 5× gap is
real but confined to tight resolution loops. Closing it would take a new engine,
because the cost is spread across every stage of a call and rests on the term
representation that all 631 builtins are written against. This note gives the
measurements so the idea can be weighed without being re-derived.

Measured 2026-10-27 on main at `2e6ec0cd`, Apple Silicon, against SWI-Prolog 10.1.17.

## Where Trealla stands

| workload | Trealla | SWI | ratio |
|---|---|---|---|
| `walk([_\|T]) :- walk(T).`, 100M steps | 4.50 s, 130.7G instructions | 0.90 s, 26.1G instructions | **5.0×** |
| per step | 1,307 instructions, 45 ns | 260 instructions, 9 ns | 5.0× |
| `samples/chess.pl` | 2.48 s, 55.0G instructions | 1.96 s, 43.4G instructions | **1.27×** |

Time follows instruction count in both rows, so the gap is work done, not
stalls. `append/3` over a 1M-element list sits with the walk: about 75 ms in
Trealla against 18 ms in SWI and 4–6 ms in GNU Prolog, ECLiPSe and Ciao, with
the time to build the list subtracted. Defining the same
two clauses in `user` instead of `library(lists)` makes no difference.

## Where a call goes

Leaf samples from an `-O3 -g` build (same instruction count as the release
build), with inlined helpers charged to themselves through `atos -i`, then
grouped by stage.

| stage | walk | chess | main functions |
|---|---|---|---|
| unification and dereferencing | 26% | 42% | `unify_head`, `set_var`, `unify_internal`, `unify_args`, `deref`, `deref_from_slot` |
| clause selection | 25% | 20% | `match_head`, `setup_key`, `find_key`, `index_cmpkey_`, `can_view`, `cell_signature`, `has_next_key` |
| frames and TCO | 24% | 7% | `reuse_frame`, `commit_frame`, `check_slot`, `check_frame`, `try_me`, `trim_heap` |
| choicepoints and backtracking | 9% | 11% | `push_choice`, `undo_me`, `retry_choice`, `drop_choice` |
| dispatch loop | 7% | 6% | `start_`, `interrupt_pending` |
| predicate entry and exit | 5% | 1% | `enter_predicate`, `leave_predicate` |
| builtins | - | 4% | `$member/3` |
| trail | 2% | 1% | `get_trail`, `trim_trail`, `add_trail` |

No function reaches 12% in either. A walk step passes through about forty
functions, each contributing a few percent:
- predicate lookup and view check
- index key setup and the signature filter
- a 128-byte choicepoint copy
- clearing the new frame's slots
- generic head unification over flattened cells, with every variable reached
  through a frame index and a slot page
- `set_var()`'s pinning rules
- the last-match and TCO tests
- moving slots and scanning the trail in `reuse_frame()`
- the dispatch loop's per-goal checks

Sampling overstates some of these. `enter_predicate()` and `leave_predicate()`
return early for static predicates, yet sample at 5% of the walk. Removing the
dispatch loop's per-goal checks (reclaim purge, interrupt and alarm poll,
thread signals) was measured directly: −1.6% instructions on walk, −1.4% on
chess, against 1.7% sampled for `interrupt_pending`.

## Why it is spread out

Terms are flattened cell arenas shared between frames, and a variable is a
slot number read relative to a frame (`docs/ARCHITECTURE.md`). Every access is
a frame lookup followed by a slot lookup, which is why `deref` and
`deref_from_slot` together are 13% of chess. Around that, a call keeps
invariants that a WAM does not have, or keeps differently:
- the logical update view (`dbgen`, `can_view()`)
- frame recovery and pins (`docs/tco-then-branch-report.md`, `docs/norecov-notes.md`)
- TCO conditions that read choicepoint depth
- attributed-variable trailing
- signal delivery between goals

Each stage is already tuned: signature filtering, key chains and `has_next_key()`
shortcuts are all recent work.

## Options

**1. Contained trims.** Each is a change of its own, with a ceiling already measured.

| change | ceiling | status |
|---|---|---|
| fold the per-goal checks into one attention flag, and poll alarms every N goals | 1.4% chess, 1.6% walk (measured) | not built |
| skip the clause choicepoint when one candidate is left | 0.7% chess (`docs/deferred-choicepoint.md`); built 2026-10-27: −12% `append/3`, +1% chess cycles | declined |
| copy only part of the choicepoint | slower | declined (`docs/deferred-choicepoint.md`) |
| skip refcounting for static predicates | none: already skipped | - |

**2. Compiled clause selection and heads.** This would add a per-predicate
switch on the first argument (type, then constant) holding exact candidate lists.
It would replace `setup_key()`, the signature filter and `has_next_key()`'s scans,
and would also tell `match_head()` before unification whether an alternative
exists. Clause heads would compile to match instructions, as SWI's `H_*` codes
do, instead of going through generic `unify_head()`.
- **Ceiling:** clause selection is 20–25% of a call. The head-unification
  dispatch is roughly half of the unification share, because dereferencing,
  `set_var()` and pinning stay as they are.
- **Realistic gain:** perhaps 10–20% on chess and 25–35% on the walk.
- **Cost:** it rewrites `match_head()`, `find_key()`, `setup_key()`,
  `has_next_key()`, `unify_head()` and the clause compiler. Switch tables must
  follow `assert`/`retract` under the logical update view, and must coexist with
  the existing index, composite keys and key chains. Determinism detection is the
  usual casualty of changes here: the v3.11.8 regression passed the Trealla
  suite and was caught only by Logtalk `library/grammars`.

**3. A new engine.** This means WAM-style structure copying, direct pointers,
argument registers and compiled clauses. It is the only route to the 5× on
resolution. But the cell-plus-context pair is the builtin API:
- 1,137 argument fetches through the `GET_*_ARG` macros
- about 4,500 lines handling contexts
- 442 `deref` calls

Frame recovery, attributes, tabling, engines and threads are all built on frames
too. This would rewrite most of the 74k lines in `src/`. On chess-like programs
the prize is at most SWI's 1.27×.

## Recommendation

Leave the call path as it is. Option 1's attention flag is the only change cheap
enough to consider, and at 1.5% it is below the bar used for `trim_heap` (4.2%
instructions on giso, declined because the wall time did not move). Option 2 is
worth revisiting only if a real workload is shown to be dominated by tight
resolution, not by unification of structures. Option 3 is the BinProlog-style
rewrite already judged not worthwhile.

## Method

- **Profile:** a scratch copy built with `make OPT='-O3 -g'`, then `dsymutil`,
  `xctrace record --template 'Time Profiler'`, an export of the `time-profile`
  table, and leaf PCs symbolized with `atos -i` (an `0x0` between addresses
  splits the inline chains). 4,571 samples for the walk and 2,495 for chess.
- **Instructions:** `/usr/bin/time -l` instructions retired, single runs, which
  vary by about 0.03%.
- **SWI:** `swipl -g 'main,halt' chess.pl`. The goal must come before the file,
  or SWI passes it to the program.

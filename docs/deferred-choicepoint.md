# Not copying a choicepoint that is dropped straight away

**Verdict: do not build this.** Two variants were built and measured against
`9bba8893`. Both made chess slower. This is the record, with the numbers, so
the idea is not proposed again on the strength of a profile alone.

## The observation

`match_head()` raises a choicepoint before trying a clause, and
`commit_frame()` drops it again as soon as the match turns out to be the last
one. Counting it, for chess:

| | |
|---|---|
| calls | 37,706,984 |
| commits | 34,477,430 |
| of those, `last_match` | 30,552,582 (**88.6%**) |

So nearly nine calls in ten pay for a choicepoint that is discarded a moment
later, and `push_choice()` copies the whole of `run_state` to make it.

## What it is worth

Sampling says `push_choice` plus `drop_choice` is 3.4% of chess self time.
That is easy to disbelieve, because the work is memory traffic and a sampler
attributes the resulting misses to whoever stalls next, so it was measured
directly: add a second, redundant `push_choice` body per call and see what the
workload does.

| one extra push per call | instructions | user time |
|---|---|---|
| into one static scratch (stays in L1) | +0.50% | +0.5% |
| into a 4096-entry rotating buffer | +0.92% | **+3.2%** |

The truth is between them and near the top, because a push that is dropped
again reuses one hot slot. Call it 3% of chess, almost none of it visible in
instruction counts. The ceiling is real.

## Why it cannot be collected

To skip the push you must know, **before** unifying, that no alternative will
be wanted. The honest predictor is `!has_next_key(q)`, and it fires far less
often than the waste occurs:

| workload | single clause in chain | `!has_next_key` pre-unify | actually `last_match` |
|---|---|---|---|
| chess | 11.5% | 22.0% | 88.6% |
| giso parse | 66.4% | 70.6% | 73.2% |

The two-thirds gap in chess is calls that qualify only through `is_det`, which
reads `head_has_vars` - known after the head has unified, when the choicepoint
already exists. So a pre-unification test caps out at 22% of 3%, about 0.7% of
chess.

## The variant that looked like it would work

Keep raising the choicepoint - so `q->st.cp` is unchanged and
`commit_frame()`'s TCO thresholds (`q->st.cp > (barrier ? 2u : 1u)`) do not
move - but copy only the fields something reads before the decision, and leave
the rest until `commit_frame()` knows the choicepoint survives. The read set
is genuinely small:

| reader | needs | when |
|---|---|---|
| `needs_trail()` | `st.fp` | every binding during head unification |
| `undo_me()` | `st.tp` | a clause fails to match |
| `head_trailed_new_frame()` | `st.tp` | TCO test |
| `commit_any_choices()` | `st.fp` | TCO test |
| `release_prefetch()` | `st.iter`, `st.iter_owner` | the choicepoint is dropped |

plus `st.hp`, `st.hp_num`, `st.sp`, `st.sp_page` because trying a clause moves
them, and `st.cp` because `push_choice()` records it before its own increment.
Ten fields of about sixteen.

## What happened

| | instructions | wall |
|---|---|---|
| main | 142.19 G | 5.71 s |
| ten fields written individually | 142.88 G (+0.49%) | 5.72 s |
| `run_state` reordered, two block copies | 142.72 G (+0.37%) | 5.77 s |

Both slower, and the suite stayed at 460/460 and 37/37 throughout, so this was
a performance answer and not a correctness one.

`ch->st = q->st` is already close to optimal: the compiler knows the layout
and turns 128 bytes into about eight wide `STP` pairs. Ten chosen fields are
twenty instructions, so the cheaper-looking push is dearer. Worse, those ten
are scattered through `run_state`, so a partial write still touches nearly
every cache line of it and saves no traffic either.

Reordering `run_state` to put them together fixes both of those and still
loses. The deferred half costs a call and a second copy on the 11.4% of
commits that keep the choicepoint, and the reorder pushes `instr`, `dbe`, `pr`
and `m` past the first 56 bytes - fields the whole engine touches constantly,
which is a worse trade than the one being bought.

## What to do instead

Nothing here. The headroom on this path is next door: unification is about 70%
of chess self time, and `unify_interned` alone samples at 1003 against
`match_head`'s 457 and push-plus-drop's 109.

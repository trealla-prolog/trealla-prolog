# Trail entries that do not hold a frame index

A design, not a change. It follows from section 6 of
`docs/tco-then-branch-report.md`, which measured what a precise answer to
"is this frame still reachable" would free.

## The problem

A trail entry is

```c
struct trail_ {
	cell *attrs;
	pl_ctx val_ctx;
	uint32_t var_num;
};
```

16 bytes, and `val_ctx` is a frame **index**. `undo_me()` walks the entries
above the current choicepoint's `st.tp`, resolves each `val_ctx` to a frame
and clears the slot `var_num` names. Recycle an index - which is what
`trim_frame()` does when it lowers `q->st.fp` - and a later frame lands on
it, so a subsequent undo clears a slot belonging to an unrelated
predicate. Bit 31 of `var_num` marks a layout entry (#841), which names a
frame the same way and restores its `actual_slots` and overflow run.

Section 3 hit this from one end: with a pin removed, clpb
(`tests/issues/test0338.pl`) lost solutions, disabling only the `fp`
lowering fixed it, and a scan at the moment of recovery found 12 trail
entries as the sole holders of the frame.

Section 6 hit it from the other. Sampling every decline over one parsed
file, 471 of 527 declines to recover a frame were frames nothing reachable
held - and a trail entry named 459 of them. The index dependency is not a
detail of one clpb failure; it is what stands behind most of `giso`'s
retained frames.

## Who reads the trail

Any change has to keep all of these working:

- `undo_me()` - the undo itself, walking down to a choicepoint's `st.tp`.
- `trim_trail()` - pops entries off the top that name the frame just
  committed, stopping at the newest choicepoint's `tp`, and does nothing
  at all while `q->undo_hi_tp` is set, an attribute hook being mid-flight.
- `reuse_frame()` - compacts the region from the previous choicepoint's
  `tp`, dropping entries that name the frame being taken over and
  retargeting those that name the new one. Only when `!q->attrs_used`,
  because attribute hooks hold absolute trail positions in
  `q->undo_lo_tp` / `q->undo_hi_tp`.
- Choicepoints save `st.tp`; `catch/3` and the retry barriers restore
  through them.

## The requirement

Once a frame's index has been recovered or taken over, no later
`undo_me()` may apply an entry trailed for the old occupant to the new
one. Undos that are still needed must keep their order and their count,
and an attribute hook's absolute positions must stay valid while it runs.

## Options

**A. Stamp entries with a frame generation.** Each frame carries a
generation, bumped when its index is taken over; the entry stores it;
`undo_me()` skips an entry whose stamp does not match the frame's. The
frame struct already declares a `pl_ctx idx` that nothing reads, so the
frame side is free. The entry side is not: `var_num` has one spare bit
(bit 30 - `MAX_LOCAL_VARS` is `1<<30`) and `val_ctx` needs its full range,
the parse reaching 16.8M frames, so the entry grows to 24 bytes. That is
+50% on a trail that peaks at 45.9M entries. Section 3 already stamped
frames and checked every undo against the stamp as a diagnostic, and found
no entry applying to a live frame that had since been reused, across the
suite, `test0338`, chess and the loops.

**B. Drop the entries when the frame goes.** On recovery, remove the
entries naming that frame, extending `trim_trail()`'s pop loop into a
compaction of the same shape `reuse_frame()` already runs. The safety
argument is local: recovery requires `q->st.fp == q->st.cur_ctx + 1` and
`!resume_any_choices(q, f)`, so every live choicepoint is older than the
frame, backtracking to any of them throws the frame away whole, and an
entry naming it can never need to be undone. Cost is O(entries above the
newest choicepoint) per recovery, each entry dropped once, and nothing per
binding. It also returns the trail memory rather than growing it.

**C. Thread a per-frame list of entries** and unlink on recovery. 8-16
bytes an entry plus maintenance on every binding, for the same guarantee B
gives for free.

**D. Leave it to a collector** that renumbers or drops entries as it
reclaims frames. This is where the general answer ends up, but it needs
the collector first.

## Recommendation

**B, with A held in reserve.** B costs nothing per binding, reuses
machinery that exists, frees memory instead of adding it, and its argument
is confined to the recovery path. If a path turns up where an entry naming
a recovered frame is still reachable - attributed variables are the
candidate, since hooks hold absolute positions - A covers it, and the two
compose: stamp the entries, drop the ones you safely can.

## What it unlocks, and what it does not

On its own this frees trail memory and makes index recycling safe. It does
**not** recover frames: `f->no_recov` is sticky and still blocks
`resume_frame()`. Section 6 measured the pins as right when asked and
stale shortly after, so the follow-on is to ask again at return instead of
trusting the pin. That is a separate design, and this one is its
prerequisite.

## Staging

1. Land B behind the existing `attrs_used` / `undo_hi_tp` guards. No
   behaviour change expected beyond a smaller trail.
2. Rerun the oracle: "held only by the trail" should fall to about zero
   while "unreachable" stays near 89%.
3. Separately, relax the recovery pin and let the oracle, the suite and
   `giso` say what that costs.

## Tests

`tests/issues/test0338.pl` first - it is the original corruption. Then the
`tco_*` tests in `tests/sundry`, the attributed-variable paths (dif,
freeze, clpz), `catch/3` across a recovery, and engines and threads, which
read frames through a zero base and must keep doing so. Expect
`tests/tests/test0104.pl` to need its variable numbers updated for
cosmetic reasons, as section 3 notes. A full Logtalk run is Andrew's.

## Measurements to take

`giso`'s parse RSS and `max_trails` at all four sizes; chess instructions
retired; `make test`; and the oracle's counts before and after, which is
the number this is aimed at.

## Open questions

- Do attribute hooks need absolute trail positions to survive a frame
  recovery, or only within one unification? `undo_lo_tp` / `undo_hi_tp`
  suggest the latter, and the existing guards assume it.
- Can an entry below the newest choicepoint's `tp` name a frame being
  recovered? The recovery conditions say no, but `$drop_barrier` and the
  succeed-on-retry barriers move choicepoints about and should be checked
  rather than assumed.
- Layout entries (#841) name a frame by index too. B drops them with the
  rest; A would have to stamp them, and their `var_num` bits are already
  spoken for.

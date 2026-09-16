# Trail entries that do not hold a frame index

**Verdict: do not build this.** It was designed, built and measured against
`334f53e3`. It is sound and it does nothing. This is the record, so that the
same idea is not proposed a third time.

The design followed from section 6 of `docs/tco-then-branch-report.md`,
which found that of 527 declines to recover a frame in a parse, 471 were
frames nothing reachable held, and a trail entry named 459 of them. The
inference - that entries naming frames by index are what stands behind the
retained memory - was wrong, and the measurements below say why.

## The problem it aimed at

A trail entry is 16 bytes, `{cell *attrs; pl_ctx val_ctx; uint32_t
var_num}`, and `val_ctx` is a frame **index**. `undo_me()` resolves it to a
frame and clears the slot `var_num` names. Recycle an index - which is what
`trim_frame()` does when it lowers `q->st.fp` - and a later frame lands on
it, so an undo clears a slot of an unrelated predicate. Section 3 hit
exactly this: with a pin removed, clpb (`tests/issues/test0338.pl`) lost
solutions, disabling only the `fp` lowering fixed it, and 12 trail entries
were the sole holders of the frame at the moment of recovery.

## What was built

On recovery, drop the entries naming that frame, compacting the way
`reuse_frame()` already does, skipped while `q->attrs_used` or
`q->undo_hi_tp` is set because attribute hooks hold absolute trail
positions. The safety argument is local and holds: recovery requires
`q->st.fp == q->st.cur_ctx + 1` and `!resume_any_choices(q, f)`, and the
latter compares generations, so every live choicepoint predates the frame,
backtracking to any of them discards the frame whole, and an entry naming
it can never need to be undone.

Scanning has to start at the frame's own trail mark, not at the newest
choicepoint's. A new `tp` field in `frame_`, set in `push_frame()` and
`reuse_frame()`, records the trail top when the frame starts. Without it,
entries that are *not* dropped get rescanned at every recovery: the first
cut ran the parse in 2.94 s against 0.61 s, 4.8x slower. With it, the parse
runs in 0.585 s against 0.610 s, the suite passes 451/451, and chess is
bit-identical in every counter.

## What it actually did

Nothing. Counting the entries it scanned and dropped:

| workload | recoveries | entries scanned | dropped |
|---|---|---|---|
| chess | 2,930,708 | **0** | 0 |
| the line DCG over `g64000` | 1,036 | 15 | 15 |

By the time a frame is recovered its trail region is already empty:
`trim_trail()` pops a frame's entries on the commit path, before
`resume_frame()` ever sees it. And the frames that *do* hold entries -
`giso`'s 4.2M of them - are pinned, so they never reach recovery at all.
`giso`'s `max_trails` is unchanged to the entry: 4,258,612 either way.

That is the flaw in the inference. The 459 frames a trail entry named are
frames that were never recovered, which is precisely why their entries are
still there. The entries are a symptom of the frames being kept, not the
cause.

## The experiment that settled it

With the drop in place, relax the recovery pin - recover regardless of
`f->no_recov`, leaving `hp` alone when `f->heap_pinned`:

- `tests/issues/test0338.pl` fails again, the clpb corruption returns, and
  a net test dies with it. Trail safety is not what makes relaxation safe.
- The ceiling, had it been sound: frames 1,605,342 to 811,393, slots 8.6M
  to 5.9M, trail 4.26M to 3.69M. Half the frames, not the 89% the decline
  counts suggested.

Section 6's oracle already said 11% of recovery declines are frames that
really are reachable. Clearing their slots is what corrupts, and no amount
of trail hygiene changes that.

## Corrections to the first draft of this document

- It said the frame's `pl_ctx idx` field is unused and could carry a
  generation for free. It is used: `get_ordered_slot_num()` in
  `src/query.h` numbers variables with it. A rename caught it at compile
  time.
- It said the drop is O(entries above the newest choicepoint) per
  recovery, "each entry dropped once". Only the dropped ones are; the rest
  are rescanned, which is where the 4.8x came from.
- The arithmetic for option A still stands: `var_num` has one spare bit
  (bit 30, `MAX_LOCAL_VARS` being `1<<30`) and `val_ctx` needs its full
  range at 16.8M frames, so a generation stamp grows the entry to 24 bytes.

## What is left

The pin is the cause, and lifting it needs a liveness answer at return.
Section 6's oracle computes one by marking from the roots, at O(live
frames) a call, which is far too expensive to run in the engine as it
stands. Making that cheap - a reference count that reaches zero, or a
collector that answers it in batches - is the frame-ownership rework, and
nothing smaller has survived measurement yet.

One question this left untested: with the pin relaxed *and* the drop
active, `test0338` still fails, so either the entries that corrupt it lie
outside the scanned region - below the frame's mark, or below the newest
choicepoint - or the trail was never the whole story in section 3 either.
Worth knowing before anyone revisits the generation stamp.

# GUSTTO v2

A Grand Unified System of Tasks and Thread Objects.

**The idea in one line:** a thread object holds a *task* rather than a
dedicated query, and a pool of worker threads picks up ready tasks, runs
them until they suspend, and puts them back. Threads and tasks stop
being two mechanisms and become one schedulable thing.

v1 tried to get there by reimplementing threads on top of tasks, and ran
aground on the obvious objection: a blocking thread would stall every
other thread sharing the scheduler. v2 turns that over. With a pool of N
workers a blocked worker costs 1/N rather than everything, and the pool
can grow when workers block. That is a real answer, not a hope.

**This is exploratory.** Each phase is a checkpoint we can back out of,
and phase 2 is deliberately a place to stop and see how it behaves
before committing to the pool. Nothing here has to be finished to be
worth having.

**Where this stands (2026-09-09).** Phases 0, 1, 2, 4 and 5 are done;
phase 3 stopped deliberately at option B and option C is deferred, not
rejected. One thing the git log will not tell you: there is no phase 6
section here because there was no phase 6. The three commits messaged
"Gustto phase 6" (`2a9d9ec7`, `f34c0b8e`, `4047654f`, 24-25 Aug) are
phase 5 finishing itself - `task_create/2`, `recv/2`, `task_cancel/1`,
and the `library(actors)` -> `library(actors/{threads,tasks})` rename -
and they are written up in the phase 5 section, where the work belongs.


## Why this fits Trealla specifically

**A task is already a resumable continuation.** `q->yielded`, a pushed
choice point, and three places to park in — the ready FIFO, the timer
min-heap, the io poll list. A thread today is a pthread stack, which
nothing can resume. So putting a task inside a thread object is not a
reorganisation, it is the thing that gives threads suspend/resume at
all, and suspend/resume is the whole precondition for pooling.

**The interpreter is flat.** `findall`/`bagof` run through choice points
in the main loop rather than re-entering the solver, so a task can
suspend at nearly any instruction boundary. Only four places nest
`start()`, and they become no-suspend zones — which they already are for
tasks today:

| Nested `start()` | Where |
|---|---|
| `format/3` `~@` | `src/bif_format.c:743` |
| `with_output_to` and friends | `src/bif_streams.c:2588`, `:2667` |
| engines | `src/bif_misc.c:494`, `:506`, via `execute()`/`query_redo()` |
| thread signal delivery | `src/bif_threads.c:835` |

**The cell layer is already thread-ready.** Refcounts and `dbgen` are
`_Atomic int64_t` (`pl_refcnt`, `src/internal.h:54`).

**What is left of threads is worth keeping.** After the v1 rip-out the
surviving API is `thread_create/3`, `thread_send_message` /
`thread_get_message` / `thread_peek_message`, queues and mutexes — the
SWI-shaped surface. That is what phase 2 re-implements, not replaces.


## Phases

Ordered so that each one is independently testable and the risky part
comes second, not last.

### Phase 0 — hoist the scheduler from `query` to `prolog` — done

The single enabling change. It turned out **not** to be a pure refactor,
in exactly one place, and that place was a bug:

- the scheduler is now one per **thread object**, reached through
  `sched_get()`, and freed with that thread rather than by
  `query_destroy()`. It was per `prolog` at first, which was wrong - see
  the correction below.
- ownership is tracked separately: a task still sits on its spawner's
  `q->tasks` registry, and every query above it counts it in a new
  `q->num_subtasks`. The scheduler answers *what can run*, the registry
  answers *whose is it*. That split is what lets `wait/0` still mean
  "until my own work is done" over shared queues.
- `wait/0` therefore returns when the caller's whole **subtree** is
  done, at any depth, rather than when its direct children are
- `query_destroy()` now calls `sched_release()` to unlink its tasks
  before tearing them down. With a per-query scheduler this was free -
  the queues died with the query - but they outlive any one query now,
  and anything left in them would dangle.

**The semantic change:** a task that spawned another and did not wait
used to have that child silently discarded, because the child went into
a scheduler nothing would ever drain. It now runs. Two cases in
`tests/sundry/task_ownership.pl` changed to say so, and both are marked
with what they used to report. Nothing else moved: nesting order, spawn
FIFO, and the two-phase `end_wait/0` behaviour are unchanged.

A consequence worth naming: a long-running task spawned and abandoned
deep in a call now keeps a top-level `wait/0` alive until it finishes,
where before it would have been discarded and the wait would have
returned. **That is intended.** wait/0 means wait, and a task nobody
waited for is still work that was asked for - quietly dropping it was
the surprising behaviour, not this. Anyone who wants a task they do not
intend to wait for should say so explicitly rather than rely on a gap
in the chain to bin it.

Verified with the full suite under `-fsanitize=address` as well as
optimised - the unlink-before-destroy path is where a dangling task
would show up, and it is clean. `library(concurrent)` is unaffected,
`future_any/2` early exit included.

Today `scheduler *sched` lives on `query` (`src/internal.h:827`),
allocated lazily by `push_task()`, and only turns over when a parent
calls `wait/0`. `q->tasks` is that parent's registry. A thread object
has no parent to drive it, so none of this can schedule one.

- move `sched` and the task registry to `prolog`
- `wait/0` becomes "run until *my* children are done" rather than "run
  the only scheduler there is"; tasks already carry `->parent`, so the
  ownership test exists
- audit `end_wait/0` and `q->end_wait` against the new ownership — the
  flag currently belongs to whoever called `wait/0`

**Correction, found in phase 3.** The paragraph above used to say the
queues were untouched by any other thread. That was wrong. Any real
thread whose query spawns a task and calls `wait/0` drives a scheduler,
and with one per `prolog` that meant several threads driving the same
queues with nothing serialising them. Two threads each draining forty
tasks of their own crashed six runs in ten - SIGSEGV, SIGBUS and
SIGABRT. Clean before phase 0, so phase 0 introduced it.

The fix is not a lock. Holding one across `start(task)` would serialise
exactly what the pool exists to parallelise, and releasing it around
`poll()` breaks the lockstep walk of the io list. The error was going
straight to per-instance when what "not owned by a query" actually
required was per **thread object** - which outlives any one query, and
only ever has one thread in it, so there is nothing to serialise.

Tasks inherit `thread_ptr` from whoever spawned them, so a task of a
thread's query lands on that thread's queues rather than the main
thread's. `tests/misc/thread_mailbox.pl` has the case that crashed.

Still one worker per thread object: tasks do not run in parallel with
each other.

### Phase 1 — make blocking primitives suspend — partly done

**Done: a receive inside a task no longer holds the scheduler.** Both
wait points in `do_match_message()` - the empty queue and the
no-match walk - now go through one `do_wait_message()`, which parks a
task on the timer heap instead of putting it on the condvar. Siblings
run meanwhile. A real thread still sleeps on the condvar, which is right
for it: it has nothing else to hold up.

Unifying the two waits also removed the duplication that caused the
timeout bug fixed just before this - there is now one place a deadline
is checked, not one place that checks and one that forgot to.

The deadline had to move onto the query (`q->msg_deadline`). A parked
task is retried from the top of the builtin, so a deadline recomputed on
re-entry would reset the clock and never expire; `q->retry` distinguishes
a resumption from a fresh call.

**Done: `send/1`, `recv/1` and `await/0` are gone**, along with the
signal machinery in `sched_run()` that existed only to serve `await/0`,
and `q->yield_now` whose only job was stopping a plain `yield/0` being
mistaken for a message. `library(concurrent)` keeps its whole public
API, ported onto the shared database, with `future_any/2`'s early exit
carried by `end_wait/0`.

**Done in phase 3: the wait-list.** A parked task is now woken by the
send rather than by its next poll. A queue carries `msg_waiters`, the
tasks parked on it; `queue_to_chan()` promotes them under the same
`t->guard` the message list uses, so a message cannot slip in between a
task deciding to park and becoming visible to the sender.
`MSG_TASK_POLL_MS` remains as a backstop, so correctness does not
depend on the wakeup arriving.

Measured, a real thread sending to a parked task: **1240-4630us before,
87-149us after.**

**Done since, by polling rather than by a wait-list:
`thread_join/2` and mutex acquisition** no longer stall the scheduler
(`1c479f4e`). Both took the same shape as the receive above - a
blocking C call with the interpreter mid-builtin - and both are fixed
the cheap way rather than the right one: a task parks with
`do_yield(q, MSG_TASK_POLL_MS)` and retries, where a real thread keeps
its blocking `pthread_join()` / `acquire_lock()`. So a task waiting on
a join or a mutex costs a wakeup every `MSG_TASK_POLL_MS` rather than
nothing, unlike the receive, which the send wakes directly. The
wait-list treatment is still the endpoint; the poll is what stopped one
waiting task holding up its siblings in the meantime.

Two related fixes came with it. `thread_join/2` on a detached thread now
throws `domain_error(not_joinable)` (`bbfd4aa9`): a detached thread
retires itself, so there was nothing to join and `pthread_join()` on one
is undefined - it happened to fall into the neighbouring error, which is
why it read as working until ASan aborted on it. And the join's poll
still waits out an `at_exit` goal, which runs after `t->is_finished` is
set, though not the thread's whole goal. Thread exit and
`thread_property/2`'s status reporting were corrected alongside
(`0d9a2e47`).

The rest of this section is the original plan, for what remains.

`thread_get_message/2` blocks in a C loop on a condvar —
`suspend_thread()` at `src/bif_threads.c:597`, inside
`do_match_message` — with the interpreter state sitting mid-builtin.
A task cannot suspend there, because the C stack cannot unwind. It has
to be restructured so the task parks on the queue's wait-list and
`thread_send_message/2` wakes it. Same for `thread_join/2` and mutex
acquisition.

**This is the actor mailbox.** A queue holding parked tasks, woken by a
send, is exactly what the actor model needs — so it gets built once and
serves both. Which is also why v1's "actors as a capstone" was the wrong
shape: the mailbox is not the reward for finishing, it is the mechanism
that makes the rest work.

**`send/1`, `recv/1` and `await/0` go rather than move.** Two reasons,
and the second is the stronger:

- `library(concurrent)` can work around them. That has been tried:
  porting `future/3` and `await/2` onto the shared database keeps the
  whole public API, and `future_any/2` keeps its early exit because
  `end_wait/0` releases a `wait/0` with tasks still queued. All four
  cases in `samples/test_concurrent.pl` behaved as before.
- `recv/1` is the *worse* of the two selective receives we have. Given a
  queue of 1,2,3,4 and a receive of 3, `recv/1` leaves `[4,1,2]` — the
  skipped messages rotate to the back — where
  `thread_get_message/2` leaves `[1,2,4]`, scanning without disturbing
  the queue. The thread mailbox is already Erlang-correct; `recv/1`'s
  rotation is not a design worth preserving.

So the thread mailbox becomes *the* mailbox, and phase 4's actor
addressing is added to it rather than to a second mechanism. What
`send/1` had that it lacks is only reach: it can address `q->parent`,
where a thread queue is addressed by id.

### Phase 2 — thread objects hold tasks

- `thread.q` becomes the task
- `thread_create/3` creates a task, not a pthread
- **`get_self()` — done.** It found the current thread by scanning for
  `t->id == pthread_self()`, which cannot survive threads becoming
  tasks: in a pool `pthread_self()` is the *worker*. Split in two. The
  six normal call sites now use `get_self_query(q)`, reading
  `q->thread_ptr` — the same idiom already used in `query.h`,
  `toplevel.c`, `bif_os.c` and `bif_tabling.c` — which is both correct
  under tasks and drops an O(2048) scan from the mutex path. The
  pthread-scanning version survives for one caller only: the SIGALRM
  handler in `bif_os.c`, which runs with no query to ask.
- **Signals — resolved, and the machinery is gone.** The SIGALRM handler
  asked `pthread_self()`, which under a pool is the worker rather than
  the thread object that armed the timer, and a signal handler cannot
  safely ask which task it was running.

  The answer was already half-built. `USE_POLLED_ALARMS` existed as the
  fallback for hosts with no usable per-thread timer (Windows, WASI,
  OpenBSD, NetBSD) and keys its deadline to the *thread object* - which
  is exactly the identity that survives pooling. It is now the only
  path: the handler, the `timer_create` shim, the `setitimer` fallback
  and the `pthread_kill` are all deleted, along with `get_self()`, whose
  only caller the handler was. Four platform variants collapse to one.

  A dedicated orchestrator thread doing `sigwait()` would also have
  worked, and would have lifted the async-signal-safety constraint that
  forced the intrusive list. It is not needed: with no signal there is
  nothing to route.

  **The cost, and how it was paid.** Polling only gets a chance once per
  pass round the scheduler, which is why `SCHED_MAX_SLEEP_MS` was 5 on
  those platforms - an idle scheduler waking two hundred times a second
  in case a timeout was due. `sched_wait()` now asks
  `next_alarm_delay()` and sleeps until the nearest deadline instead, so
  the cap is back to 250ms and bounds interrupt latency only. Measured:
  a 500ms limit fires at 501ms with the scheduler idle, 505ms in a plain
  `sleep/1` (its 10ms slice). Nested timers behave as before - the inner
  limit fires and the outer survives.

  **One thing this does not cover.** A blocking read in a non-task query
  had SIGALRM to break it with `EINTR`; polling cannot. That limitation
  already shipped on the four polled platforms and now applies
  everywhere, and it shrinks to nothing once threads are tasks and I/O
  parks on the scheduler.

  **Follow-on (2026-08-25): hit in practice, stopgapped, not fixed
  properly.** Logtalk's `linda` library timeout tests hung on main:
  `call_with_timeout/3` around a socket `read_term/2` blocked forever in
  `do_read_term()`/`eat_space()`'s blocking `getline()` - exactly the gap
  above, not a new bug.

  The real fix - non-task sockets going non-blocking so I/O genuinely
  parks on the scheduler, what "shrinks to nothing" means above - touches
  every blocking-read call site in `bif_streams.c` that assumes a read
  either returns data or blocks, not just `do_read_term`. That's
  phase-2-and-beyond work, so a stopgap landed instead:
  `tpl_wait_fd_readable()` (`network.c`) polls the fd in short,
  alarm-aware slices (capped at `next_alarm_delay()` or 250ms) ahead of
  the blocking call, and sets `errno = EINTR` on timeout so it slots into
  the `errno == EINTR` handling the SIGALRM removal above left in place.
  Wired into `do_read_term()`'s `tpl_getline()` calls and, via a new
  `parser.is_socket` flag, `eat_space()`/`get_token()`'s raw `getline()`.
  Sockets stay blocking-mode; no other read call site's behaviour
  changes.

  **Still open.** This is a second, narrower polling mechanism sitting
  next to the scheduler's, not a replacement for it. Revisit once phase 2
  lands and non-task socket I/O can park on the scheduler for real -
  `tpl_wait_fd_readable()` should retire at that point.

  (Separately: Ctrl-C during that hang used to crash - `pl_destroy()`
  freeing a thread struct an OS thread was still running inside. A
  general shutdown race predating this section, not caused by the polled
  alarm; fixed properly in `bif_threads.c` rather than worked around -
  `cancel_and_join()` + `retire_cancelled_thread()` confirm an OS thread
  has actually stopped before its struct is freed, and shutdown rescans
  the live list each pass instead of taking one snapshot.)

  Fallout: `thread_initialize()` and the table are now unconditional,
  because `q->pl->main_thread` is dereferenced by `interrupt_pending()`
  whether or not the build has threads. As `threads[0]` in a fixed array
  it existed for free; allocated, it has to be made. The threadless
  (WASI) build was where that showed up.
- thread identity becomes id + mailbox + task, with no pthread in it

**Access to the table is now funnelled — done.** Every one of the ~50
places that looked a thread up or walked the table goes through four
functions in `bif_threads.c`: `find_thread_by_id()`, `main_thread()`,
`next_thread_after()` and `next_of_kind()`, plus a `for_each_thread()`
macro over the first two. Nothing else knows it is an array. `MAX_THREADS`
went from ~26 references in that file to four, two of which are inside
the accessors.

The awkward case was the six property predicates, which enumerate one
kind of object and resume across backtracking from an id saved in
`q->st.v1`. They now ask `next_of_kind(pl, id, TK_THREAD|TK_QUEUE|TK_MUTEX)`,
which is the same question and stays meaningful when the storage is no
longer indexable. That also collapsed six copies of a twenty-line
double-scan into four lines each.

**Two places still know the storage**, and both genuinely change with it
rather than being oversights:

- `new_thread()` — the allocator, which becomes malloc plus insert
- `tabling_destroy()` in `bif_tabling.c`, which deliberately sweeps
  *every* slot including inactive ones and the main thread, so it wants
  "every struct ever allocated" rather than "every live thread". Under a
  free list that is the map plus the free list, and belongs next to them.

**The fixed table is gone — done.** `thread threads[MAX_THREADS]` is
replaced by, on the `prolog` instance:

- a skiplist keyed by id, for O(log n) lookup. Keys are the raw integer:
  the default comparator already compares pointers as integers, so no
  custom compare was needed, and id 0 (a NULL key) works.
- an intrusive doubly-linked list of live entries, kept in increasing id
  order. This exists because iteration must not allocate or lock: the
  SIGALRM handler walks the table, and `sl_first()` does both.
- a FIFO free list of retired structs, and a monotonic id counter.

`new_thread()` takes the oldest retired struct or mallocs one;
`retire_thread()` unlinks it and appends it to the free list. Nothing is
freed before `threads_destroy()` at instance teardown.

Two details that only showed up in the doing:

- **The id key is dropped at reuse, not at retirement.** Retire and
  delete immediately, and a stale handle stops knowing what kind of
  object it named - `write/1` on a destroyed queue printed
  `'$thread'(1)` instead of `'$queue'(1)`. Keeping the key until the
  struct is handed out again preserves that, and `get_thread()` still
  rejects the id because it tests `is_active`. The free list is FIFO for
  the same reason: taking the oldest struct first keeps a stale handle
  readable for as long as possible, which the fixed table gave for free
  by cycling through its slots.
- **`thread_initialize()` already existed** and asserts the main thread
  gets id 0. Adding a second initialiser silently stole that id and the
  assert fired; the table creation belongs in the one that was already
  there.

Verified: 5000 message queues (was capped at 2048); ids monotonic across
destroy/create so a retired id is never reissued; a message to a retired
id gets `existence_error` rather than reaching a stranger; both suites at
baseline, and clean under `-fsanitize=address` including a churn of 300
queues, 300 mutexes and 200 threads created and destroyed.

**`max_threads` is gone.** It reported `MAX_ACTUAL_THREADS` (2048),
which became a lie the moment the cap did. Reporting the O/S ceiling
instead would have made it identical to `os_threads` - two names for one
number, the same objection that retired `hardware_threads`. So it was
removed rather than redefined: querying or setting it is now
`domain_error(prolog_flag, max_threads)`, like any flag that does not
exist. Nothing in the tree or in Logtalk referenced it.

**`cpu_count` is gone too**, and for a subtler reason: it was true and
still useless. It reported logical CPUs, which reads as "how much
parallelism there is" and is the one thing an application would size a
pool from - and on a hybrid machine that answer is wrong by a wide
margin. On an Apple M4 (4 performance + 6 efficiency cores) it said 10,
where an efficiency core is 6.0x slower than a performance one for
interpreter work: 20M iterations of an arithmetic loop take 1378ms on a
P core and 8225ms on an E core. A pool of 10 threads with an even split
therefore waits on the slow six - 16728ms against 2112ms for the same
loop across four threads, so taking the flag at its word is 8x slower
than ignoring it. There is no portable fix, either: the
performance-core count needs `sysctlbyname hw.perflevel0.logicalcpu` on
macOS, `/sys/devices/cpu_core/cpus` or `cpu_capacity` on Linux
depending on the vendor, and `EfficiencyClass` from
`GetSystemCpuSetInformation` on Windows. A number that is only safe to
use after you already know the answer is not worth reporting. Querying
or setting it is now `domain_error(prolog_flag, cpu_count)`;
`detect_cpu_count()` went with it, having had no other caller.
`samples/skynet_mixed.pl` is where the measurements came from.

What is left says one true thing each:

| Flag | Means |
|---|---|
| `os_threads` | POSIX threads the O/S will give this process |
| `threads` | whether this build has them at all |

**The original plan, for reference.** `thread threads[MAX_THREADS]`
(`src/internal.h:1034`) is a 2048-entry inline array in the `prolog`
struct, and a thread's channel *is* its array index — that index is what
gets boxed into the Prolog term with `FLAG_INT_THREAD`. Replacing it
with a skiplist, the way the rest of the system stores things, is the
right move once identity is being reworked anyway:

- it removes the cap that v1 wrongly blamed on the O/S
- `new_thread()`'s linear scan for a free slot goes away entirely;
  allocation becomes malloc plus insert
- ids stop being *reused*, which kills a real class of bug: today slot
  `n` can be freed and reissued while a message in flight still names it

It is probably also a performance *win*, not a cost. The ~26
`MAX_THREADS` sites are scans that walk all 2048 entries
unconditionally, however few threads are live — `get_self()` among them,
which sits on the mutex path, and `thread_cancel_all()`,
`do_unlock_all()` and the tabling cleanup. Iterating a skiplist
(`sl_first` / `sl_next`) is O(live), so those all get cheaper. Only
keyed lookup goes the other way, O(1) array index to O(log n), and that
is the smaller effect by some distance.

So the real cost is the ~40 `threads[n]` index sites to convert, and
two things that need care.

**Lifetime — settled.** The worry was that a `thread *` into a fixed
array is stable forever, so malloc'd entries could dangle. The exposure
is real and wide: seventeen places in `bif_threads.c` hold one across a
lock release or a wait, including `do_match_message()` — which now parks
a task there — and `thread_join()` across `pthread_join()`.

But the array is *already recycled*. `new_thread()` hands out slots by
`pl->thr_cnt++ % MAX_THREADS`, so a stale pointer can already be looking
at a different, live thread. That reframes it: a free list of retired
structs has **exactly the same hazard profile as today**, and needs no
refcounting, no tombstones and no epoch scheme.

So:

- thread structs come from a free list and go back to it when a thread
  retires; nothing is freed before `pl_destroy()`. Memory is bounded by
  *peak concurrent* threads rather than total ever created, which is
  what makes this better than simply never freeing.
- ids become monotonic and live *in* the struct rather than being the
  slot index. Memory is reused, ids are not — so the bug where a message
  in flight names an id since handed to a different thread goes away.
  That is the one of the two that actually bites.

Net: better than today on every axis, with no new class of hazard. The
stale-pointer risk that remains is precisely the one already shipped,
and worth fixing on its own rather than inside this.

**The implicit main thread.** `&pl->threads[0]` means "the main thread"
in `bif_os.c` (`:504`, `:619`, `:670`, `:688`), `toplevel.c`,
`bif_tabling.c` and `query.h`. It has to become an explicit object
rather than an index that happens to be zero.

**Sequencing — phase 2 prepares, it does not switch.** Threads stay
pthreads until phase 3, so true concurrency is never lost in between.
The reason is phase 0: `wait/0` now waits for the caller's whole
subtree, which leaves nothing good for a thread-task to be owned by. Own
it and every `wait/0` blocks on it and `query_destroy()` kills it; disown
it and nothing drives it, so a detached thread nobody blocks on would
never run. Workers pulling from the queues is what resolves that, and
workers are phase 3.

**When the switch does happen:** threads on tasks, single worker, no pool: it
should already run, and how it behaves is the most informative thing we
can learn before building phase 3. A compute-bound thread will starve
its worker at this point, because preemption does not land until phase
3 — that is expected, not a bug to chase.

### Phase 3 — the worker pool — stopping at option B

**Option B is where this stops for now.** The queues stay per thread
object with a lock. Option C - one shared set of queues, N workers,
thread objects as tasks - is deferred, not rejected.

**The precondition for C:** database mutation must stop needing an
instance-wide lock. Every assert/retract takes `prolog_lock` - that is
how the concurrency crash was fixed - so N workers doing database work
all contend on one lock. C would buy parallelism and hand it straight
back, which for Prolog, where the database is the program, is most of
the workload. Lifting that means epoch or QSBR-style reclamation, which
is a larger job than C itself. Doing C first ships the complexity and
none of the benefit.

**What would settle it:** the fraction of a representative db-heavy
program's time spent inside `prolog_lock`. Small, and the objection
collapses. Large, and C cannot pay off until the lock goes. Worth
measuring before committing either way.

**Measured.** 8 real threads (`thread_create/3` - this is Option B's
own model, already real parallelism), all `assertz`/`retract` on one
shared predicate, bounded to one live fact per thread so no call scans
a growing table: held-time-under-lock rises from 9.3% of wall clock at
1 thread to 36.0% at 8, and the *per-acquisition* hold time itself
grows 7.3x (101ns to 740ns) - cache-line bouncing on the mutex and the
skiplist under contention, not fixed serial work. Throughput peaks at
2 threads (~1.38M lock ops/sec) and *collapses* past that - 8 threads
is worse than 1. Large, per the gate above: C should not be built yet.

**But the workload was adversarial on purpose - all contention, one
predicate, no module diversity - and that turns out to matter a lot.**
The same benchmark spread across 8 separate modules (one dynamic
predicate each, one thread each, zero cross-module dependency) still
serializes fully on `prolog_lock`, since it does not know modules
exist - measured at 2.80s, barely different from the single-module
shape. Adding a real per-module lock (hashed module pointer, bucketed
across ~50 call sites in `bif_database.c`, `leave_predicate()` and
`query_purge_dirty_list()`) cut that to ~1.05s, a genuine ~2.7x, with
the single-module case unchanged (3.42s vs 3.62s baseline, within
noise) - confirming the lock split costs nothing when there is no
diversity to exploit. One methodology note worth keeping: the first
pass of that experiment hashed the module pointer with a plain
shift-and-mod and measured *no* improvement at all - module structs
turned out to come from an allocator with >=2048-byte alignment, so
every pointer's low bits were zero and all nine modules hashed into
the same bucket. Silent zero-signal, not a crash; only caught by
instrumenting the hash itself to print which bucket each module
landed in. Fixed with a proper (Fibonacci) hash. Lesson: verify a
sharding scheme actually shards before trusting a null result from it.

**So the real answer has two parts.** For contention concentrated on
one predicate - the adversarial case above - nothing short of lifting
`prolog_lock` entirely (the epoch/QSBR route, still a bigger job than
C) moves the needle. For contention that is naturally spread across
modules, a per-module lock is a far cheaper, more surgical win than
either C or the full reclamation rewrite, and is worth doing on its
own regardless of whether C ever happens - it already helps Option B,
today, independent of the worker-pool question.

**Implemented.** `module` got a real `lock guard` field
(`src/internal.h`), initialized in `module_create()` and torn down in
`module_destroy()` - not the throwaway pointer-hash bucket table used
to measure this. `prolog_lock_mod()`/`prolog_unlock_mod()`
(`src/prolog.h`) replace `prolog_lock()`/`prolog_unlock()` at every
call site in `bif_database.c` that mutates a predicate
(assert/asserta/assertz/retract/retractall/abolish - seven call sites,
each with the target module already in hand as `q->st.m`), plus
`leave_predicate()` (has it as `pr->m`). A module created by
`module_duplicate()` (the `:- attribute` shadow modules) redirects to
its `orig`'s lock inside the helper itself, matching `find_module()`'s
own redirect - an alias and its original must never serialize
independently. The one genuine complication was
`query_purge_dirty_list()`: `q->dirty` accumulates rules from whatever
predicates a query touched over its lifetime, possibly spanning
several modules, so it cannot take one lock for the whole pass - it
locks per rule's own `r->owner->m` instead. All three paths agree on
the same per-module lock, consistently - `module_lock()`'s original
removal (`src/module.h:72`) was exactly this going wrong once already:
assert/retract on one lock, the dirty-list purge on a different one,
so neither excluded the other. Do not repeat that shape.

Verified clean: `make test`/`make misc` at baseline, 40/40 stress runs
of `tests/misc/db_concurrency.pl` and `db_purge_window.pl`, and a
`make debug` (ASan) pass of the same.

**A separate, pre-existing bug surfaced during that ASan pass, unrelated
to this work - found and fixed.** `retry_choice()` -> `undo_list_drain()`
(`src/query.c`) frees a retracted rule on backtrack, and two readers
could still be touching it:

- `find_key()` (reads `pr->head`, parks a rule pointer in `q->st.dbe`)
  ran *before* `enter_predicate()` (the refcount `leave_predicate()`
  checks before it may reclaim) at all three call sites -
  `match_head()`, `match_rule()`, `match_clause()`. A purge already
  committed to running could free what `find_key()` had just handed
  back, before the increment that was supposed to prevent exactly that
  had happened.
- `commit_frame()` called `leave_predicate_and_drop()` - which can take
  the refcount to zero and trigger that same reclaim - and only *then*
  read `cl->alt`/`cl->cells`/`cl->cidx` to build the continuation, out
  of the clause it had just given up its claim on.

The first instinct - wrap `enter_predicate()`'s increment in the same
lock `leave_predicate()` uses - made it dramatically worse (17/20
crashes in release, vs ~10% under ASan before), because a lock there
doesn't protect the read that already happened; it just makes the
reader wait out the purge that is about to free what it is holding.
The actual fix is ordering, not locking: enter before finding, and
read what `commit_frame()` needs before dropping the reference. No new
lock. Reproduced on unmodified `main` (3/30 runs of
`db_purge_window.pl` under ASan) and unmodified `gustto` (6/30) alike,
predating GUSTTO and this per-module work; neither implicated. Fixed
on `main` (`0bf14578`) and ported to `gustto` (`592dae04`); verified
0/80 interleaved against baseline's 8/80 on `main`, and 0/60 on
`gustto` specifically for `db_purge_window.pl` and `db_concurrency.pl`
each, under ASan.

**What B already banked:** blocking receives park instead of stalling
siblings, a send wakes a parked task in ~90us rather than ~5ms, the
fixed 2048-entry thread table is gone, and timeouts key off the thread
object rather than which pthread caught a signal. C's marginal gain
over that is parallel execution - gated as above - plus having one
mechanism instead of two.

**What C still has to pay for regardless:** streams are per-instance
and unguarded, and making a whole term-write atomic means touching ~52
sites in a 6,800-line file; a compute-bound task starves a worker
without preemption; FFI and file I/O deadlock the pool without a growth
valve.



Ownership was the question, and it came down to one thing: *is
cross-thread queue mutation ever needed?* If yes, a lock is required
whichever way the queues are owned, and ownership stops buying safety -
it only buys locality. The wait-list needs it, so:

- **the scheduler has a lock.** It guards the ready list, the timer
  heap, the io list, and the `sched_where`/`heap_idx` each task carries
  saying which one it is on. Never held across `start()` - that would
  serialise what the pool exists to parallelise - nor across `poll()`.
- **only the owning thread runs the scheduler.** Other threads only
  ever *promote*. That keeps the pollfd scratch single-writer, which is
  why it needs no locking of its own.
- **promotion after `poll()` goes by owner, not by lockstep.** The old
  loop walked the io list in step with the descriptor array and assumed
  neither had changed. With the lock released across `poll()` that no
  longer holds, so it uses the task pointer already saved per slot in
  `pfd_owners` and checks `sched_where` to see whether the task is
  still parked on io at all.
- **a self-pipe** sits in slot 0 of every poll set, so a thread that has
  just promoted a task can cut short the sleep of the thread that owns
  it.

Threads are still pthreads. What this buys is the wait-list and a
locking discipline that a pool can be built on; what it does not buy is
workers. Option C - one set of queues, N workers, thread objects as
tasks - is mostly merging the queues and deciding who *is* a worker:
dedicated threads, or a query that calls `wait/0` becoming one until its
own subtree is done. The latter needs no threads at all in the
single-threaded case, and `num_subtasks` already gives the exit
condition.

#### Phase 3 — the original plan, for what option C still has to do

Kept as written, the same way phase 1 keeps its own plan above: what
follows is what option C was specified to do, not a description of what
is there now.

- N workers pulling from one shared ready queue. **Not per-CPU queues.**
  Affinity and work stealing fall together: the only reason to steal is
  to rebalance queues that affinity made uneven. A task migrating
  between workers is safe because exactly one worker holds it at a time
  and the queue's own synchronisation supplies the barrier on handoff —
  release/acquire, not affinity. Cache locality was never the argument;
  a task's working set is its own malloc'd heap and frames.
- **A growth valve is not optional.** FFI calls, file I/O and TLS reads
  have no suspension point and will pin a worker —
  `do_yield_on_stream()` only parks plain sockets (`str->is_socket &&
  !str->ssl && !str->is_memory`). With a fixed pool, N workers all
  blocked waiting on a runnable-but-unscheduled thread is a deadlock,
  not a slowdown.
- **Preemption**, or a compute-bound thread hogs a worker forever. Yield
  points are otherwise only sleep, socket EAGAIN and `yield/0`. The hook
  already exists: `YIELD_INTERVAL` in the main loop at
  `src/query.c:2409` (the macro at `:184`), currently gated on
  `q->yield_at`, which only the embedding API sets.
- **Shared-state locking**, which is where the real bug risk lives:
  - clause resolution reads the db with no reader lock; only writers
    take `pl->guard`. True today for `thread_create/3`, but every task
    would now be exposed. **Update:** a reader lock turned out to be
    the wrong fix for the bug this shape predicted - see the UAF note
    under phase 3, "Implemented". The actual fault was two unlocked
    reads racing a purge (`find_key()` before its own `enter_predicate()`,
    and `commit_frame()` reading a clause after releasing the reference
    that protected it); adding a lock around the read made the race
    *more* certain, not less, because it just made the reader wait out
    the purge it needed to beat. Fixed by ordering, not locking - see
    `commit 592dae04` ("Fix db bug", ported from `main` `0bf14578`).
    Whether Option C exposes a
    *different* unlocked-read shape once there are real concurrent
    workers instead of occasional real threads is still open.
  - **Two more of this shape have since turned up, both fixed by
    unsharing rather than by locking** - which is now the pattern, not
    a coincidence. The index skiplists kept their traversal state
    (`wild_card`, `is_find`) as fields *on the skiplist itself*, so two
    concurrent lookups of the same predicate wrote over each other's
    cursor; moved to a per-lookup `slctx` passed down through `cmpkey`
    (`52a29458`, regression test `tests/slow/index-race.pl`). And
    `pl->goal_expansions`, bumped by every thread, was a plain `int`
    whose `++`/`--` could wrap below zero; now `pl_atomic`
    (`fd09f220`, `src/internal.h:1326`). Neither needed a lock. Both
    were reachable from ordinary `thread_create/3` code, so they were
    option B bugs, not option C ones.
  - streams are per-`prolog` (`src/internal.h:1274`), so two tasks
    writing stdout interleave mid-term.
  - `module_lock()` / `module_unlock()` (`src/module.h:72`) - stale.
    They no longer exist; removed entirely (see the comment at that
    line), not merely unused. `prolog_lock_mod()`/`prolog_unlock_mod()`
    replace what this bullet was asking for, phase 3, "Implemented".
- pool width is a setting with a sensible default, not a number derived
  from a CPU count — the right width depends on how I/O-bound the load
  is, and on which cores you actually land. That was the argument for
  leaving `cpu_count` informational; it turned into the argument for
  removing it (above), since informational was all it could ever be.
  `os_threads` stays: a ceiling you must not exceed is useful even when
  the number you should choose is far below it.

### Phase 4 — actors — done, as a library

It turned out to need no engine work at all. Everything an actor layer
wants was already there once phases 1 and 3 had landed:

- a thread *is* a mailbox, with order-preserving selective receive
- `thread_create/3`'s `alias(A)` gives addressing by name
- `at_exit(Goal)` fires on all three ways a goal can end - success,
  failure and exception - and can send messages
- `thread_self/1` inside an at_exit goal returns the dying thread, so a
  death notice can say who died without knowing its own id in advance

So `library(actors/threads)` is ~60 lines of Prolog: `actor_spawn/2,3`,
`actor_send/2`, `actor_recv/1,2`, `actor_link/1`, `actor_unlink/1`. A
linked actor's death arrives as `exit(Pid, Reason)` where Reason is
`true`, `false` or `exception(E)`.

Two details worth keeping:

- **spawn and link have to be atomic.** A short-lived actor can die
  before the caller installs the link, and the notice is then lost. The
  body waits for a `'$actor_go'` message before running, so the link is
  in place before the actor can do anything - selective receive makes
  that safe regardless of what else is queued.
- **the link registry is a shared dynamic predicate**, written from
  several threads at once. That only works because of the database
  concurrency fix; before it, this library would have been a reliable
  way to crash the system.

A minimal supervisor comes with it: `supervisor_start/2,3` and
`supervisor_stop/1`, one_for_one only, with a restart budget -
`max_restarts(N)` within `period(Seconds)`, default 5 in 5. The budget
is the part worth providing rather than leaving to callers: without it
a child that dies on start spins forever. Exhausting it stops the
supervisor and its remaining children.

The other OTP policies are not included, and want a real use case
before they are.

### Phase 5 — task-addressable send/recv — done

`library(actors/threads)` scales actors to however many OS threads the
platform tolerates, not further - the original motivation for tasks in
the first place. Closing that gap needs tasks to be addressable and
messageable the way threads already are, without becoming threads
themselves.

- **`q->task_id`** is the address, and `task_self/1` returns it. It is
  minted at registration, not at construction, as `(owning thread's
  chan << 40) | that thread's next seq` — so an id names the thread
  whose registry can resolve it, which is what lets the registry be per
  thread (below). `q->qid` still exists and is still a process-wide
  serial assigned to every query, but it is internal identity only now,
  never an address.
- **`t->tasks`** is a lazily-created `seq -> query*` skiplist **per
  thread**, populated lazily too: an entry is added the first time a
  query calls `task_self/1`, not at construction. The only way anything
  ever learns an id is that query handing it out itself, so a query that
  never calls `task_self/1` is unaddressable and rightly never occupies
  a slot - this is what keeps the flood of transient queries (format's
  `~@`, `with_output_to`, goal expansion, every plain directive's own
  query) out of the table without having to special-case them by type.
  One consequence worth being deliberate about: a plain top-level
  directive's query, a thread's root query, and a task are all
  addressable the same way once they call `task_self/1` - send/2 does
  not care which kind of thing it's talking to.

  This was one skiplist per `prolog` instance under `prolog_lock()`
  until the scaling pass below, which is where the reasoning for the id
  layout comes from. `retire_thread()` drops a thread's registry, so an
  id naming a retired chan misses rather than reaching whatever
  registers next on the recycled struct - the same answer the global
  registry gave, since chans are monotonic and never reused.
- **The mailbox** is a `task_msg`/`lnode` list per query (`q->mailbox`),
  not the old array-based `send/1`/`recv/1` queue (git history:
  `c5007a4b`, "Gustto phase 1") - the array rotated a skipped message to
  the back on selective receive, which is observable and wrong.
  `list_remove` gives O(1) removal from the middle, so a skipped
  message keeps its position, matching `thread_get_message`'s existing
  behaviour.
- **Locking** puts the mailbox under the target's owning thread's
  `scheduler->guard` (`sched_lock`/`sched_unlock`), the same lock
  `sched_promote` already serialises against, and that whole delivery
  under the owner's `tasks_guard`, so resolving an id and using what it
  resolves to happen under one hold - the owning thread can destroy a
  task in the window between. There is deliberately no
  "find the task for me" helper returning a pointer with the lock
  dropped: that is the bug class fixed on the thread side.
  `sched_get()` (previously single-writer only - a thread lazily
  creating its own scheduler) had to be made safe to call for a
  *foreign* thread's scheduler, since send/2 is the first thing that
  ever needs another thread's scheduler to exist before that thread has
  asked for one itself; it is double-checked now, an atomic read of an
  already-created scheduler with the lock taken only on the one call
  per thread that finds none.
- **Sender tracking** is internal only: `q->cur_task_qid`, set from the
  matched message's `from_qid` on a successful `recv/1`. No public
  `recv/2` yet - deferred until there's a concrete need to expose a
  reply-to address to Prolog.
- Verified at 8 real OS threads × 2000 messages each (16,000 concurrent
  cross-thread sends) to a single receiving task, 20/20 clean under the
  optimized build and 5/5 clean under ASan.

**Both actor styles stay.** Threads-as-actors (`library(actors/threads)`,
renamed from `library(actors)` for the symmetry) and task-actors
(`library(actors/tasks)`, below) are complementary, not a replacement of
one by the other: threads give real parallelism and OS-level isolation
at a ceiling of a few thousand; tasks give cooperative concurrency that
scales to skynet-sized actor counts on a handful of threads.

**`task_create/2`** closes the one gap that stopped `library(actors/tasks)`
being a near-verbatim port: `call_task/N` never told the spawner the new
task's qid, only the task itself could learn it (by calling
`task_self/1`), and only once it had actually run. `thread_create/2`
does not have that problem - a thread id is handed out by the creator,
not self-reported - so `task_create(Goal, Qid)` does the same: the qid
is minted by registering the new query eagerly (unlike `task_self/1`'s
lazy registration) and handed back before the task has executed a
single instruction, so `Qid` is usable with `send/2` immediately.

That also sidesteps the atomicity problem `library(actors/threads)`
solves with a `'$actor_go'` handshake: tasks on one OS thread run
cooperatively, so a freshly `task_create/2`'d child provably has not run
a single instruction by the time `task_create/2` returns - nothing has
yielded yet. `library(actors/tasks)` installs a link straight after
spawning, no handshake message required.

**`recv/2`** closed the blocking-receive gap. `recv/1` still does not
block - never has, does not now - but `recv(Msg, [timeout(Seconds)])`
does, with no timeout meaning block indefinitely. It parks rather than
polls: `do_yield()` (`bif_tasks.c`), the same mechanism
`thread_get_message/3`'s blocking form already used for a task, via
`do_wait_message()`. That existing function turned out to be the right
template down to a subtlety easy to miss - the deadline has to live on
the query (`q->msg_deadline`), not a local, because a parked task is
retried from the top of the builtin on its next entry, and a deadline
recomputed there resets the clock every time; `q->retry` - already
true on that re-entry - is what a fresh call cannot fake.

The one place the template did not transfer directly: `do_yield()` is
a correct no-op (`true`, immediately) for any query where
`q->is_task` is false - which includes a plain top-level directive and
a thread's own root query, both valid `recv/2` callers, not just
tasks. Missing that produced a real bug on the first pass: a non-task
caller's `do_yield()` silently no-opped, so `recv(nope, [timeout(0.2)])`
appeared to succeed instantly against a mailbox that was never checked
at all. Fixed the same way `do_match_message_()` handles it: a dual
path, `do_yield()` when `q->is_task`, a plain C-level sleep-and-rescan
loop (blocking the real OS thread directly, same as
`suspend_thread()`'s role there) otherwise. `library(actors/tasks)`'s
`task_actor_recv/1,2` are now thin wrappers over `recv/2` - the
`yield/0`-spin they used before is gone, verified at 8 real OS threads
× 500 actors each using the blocking path, 10/10 clean.

**`task_cancel/1`** closed the last gap - `library(actors/tasks)` now
has a supervisor too. The interesting part was doing it safely across
threads. `query`'s per-instruction flags (`error`, `yielded`,
`no_recov`, ...) are `bool:1` bitfields packed into a handful of
shared bytes, read-modify-written together - the same class of race
the `prolog`-level flags had (above), except these are mutated on
essentially every instruction `start()` executes, not just
occasionally. Writing `target->error = true` from a foreign thread,
the obvious way to cancel something, would have reintroduced exactly
that bug, worse. Fixed by never touching the bitfields from outside:
`task_cancel/1` sets one new, real, standalone `pl_atomic bool
cancel_requested` and calls the already-cross-thread-safe
`sched_promote()`; `sched_run()`'s own dispatch loop is what turns a
pending request into `error = true`, from inside the task's own owning
thread, right before deciding whether to run it - the one place doing
so is actually safe. Consequence worth being explicit about:
cancellation is cooperative, same as everything else about a task -
it lands at the next scheduling checkpoint, not mid-instruction, same
as `thread_cancel/1`'s own delivery is asynchronous in practice despite
being backed by real preemption.

The supervisor port surfaced one thing not obvious from `library(actors/threads)`'s
version: a thread-based supervisor runs in the background just by
existing, being a real preemptively-scheduled thread; a task-based one
only makes progress while its owning thread drives the scheduler, so
calling `task_supervisor_start/2,3` from a thread that then goes on to
do other things leaves it starved. Documented in
`library/actors/tasks.pl` with the fix - host it on a thread of its
own (`thread_create` wrapping `task_supervisor_start` + `wait`) - since
this generalises to any long-running task tree, not just supervisors.

**Found and fixed - pre-existing, unrelated to this phase:** stress
testing send/2+recv/1 turned up a heap-use-after-free
(`resume_frame`/`retry_choice`/`trim_heap`, `query.c`) that reproduced
under real thread concurrency whenever a selective-receive builtin
(`push_choice`/`import_term`/`unify`/`retry_choice`-or-`drop_choice`)
was used as the condition of `( Cond -> Then ; Else )`. Confirmed
independent of this work at the time: it reproduced identically with
the pre-existing `thread_get_message/3` in the same shape, with none
of the new send/recv code involved - two real threads sending, one
receiving via `( thread_get_message(Me, Msg, [timeout(T)]) -> ... ; ... )`,
100% reproducible in a handful of runs. Confirmed to need genuine OS
thread parallelism, not just interleaving: the identical shape run as
cooperative `call_task/1` tasks on one OS thread, across a spread of
concurrency levels and volumes well beyond what triggered the
threaded version, never reproduced it.

Root cause: `do_if_then_else()`/`do_soft_if_then_else()`
(`bif_control.c`) built their barrier-protected continuation without
marking the calling frame `no_recov`, unlike every other caller that
sets up a barrier this way (`push_succeed_on_retry_with_barrier`, for
one). Without it, `resume_frame()`'s tail-call heap-reclaim fast path
(`query.c`, gated on `q->pl->opt`) could decide - under just the right
concurrent timing - that the frame's heap could be reclaimed while the
continuation still needed it. Fixed on `main` (commit "An if-then-else
use-after-free fix") by setting `f->no_recov = true` before building
the continuation, in both functions, and ported here by
cherry-pick. Regression test: `tests/tests/test0115.pl`, the same
shape that crashed 100% of the time pre-fix, now passing reliably (20
+/20 runs at increased scale under both the optimized build and ASan).


### Phase 5.1 — making tasks scale across real threads — done

Phase 5 made a task addressable from any thread. `samples/skynet_mixed.pl`
then asked the obvious follow-up question - one real thread per core,
tasks inside each, does it go faster? - and for a while the answer was
no. At `size=1000000 div=10` on a 4+6-core Apple M4: one thread 4163ms,
four threads 3569ms (1.14x), ten threads 6213ms, *slower than not
threading at all*. Three separate things, found in the opposite order
to how much they mattered.

**The global lock on the task paths.** `register_task()`,
`unregister_task()`, `find_task_by_qid()` (as it then was) and
`sched_get()` each took
`prolog_lock()` when `is_multithreaded` - three or four acquisitions of
one process-wide mutex per task, against a workload that is 1.11M task
creates, sends and destroys. Fixed in two commits:

- `sched_get()` is double-checked against an atomic `t->sched`; the lock
  only ever guarded a once-per-thread allocation. The scheduler is now
  built complete in a local and published last (`pl_publish_barrier()`),
  because the previous order - assign `t->sched`, then `init_lock()` its
  guard - is only safe while every reader holds the lock.
- `unregister_task()` returns before locking unless the query actually
  registered. `query_destroy()` calls it for every transient sub-query,
  and once any task existed the old `pl->tasks` was non-NULL, so all of
  them took a process-wide lock to perform a lookup that could not hit.
- The registry became per thread, keyed by a per-thread seq, addressed
  through an id carrying its owner's chan (phase 5's bullets, above). A
  send within a thread - nearly all of them - now takes only that
  thread's own lock; only a genuine cross-thread send pays
  `find_thread_by_id()`'s `prolog_lock()`. The small per-thread keys
  also retired a standing note about a process-wide `uint64_t` qid
  truncating where a skiplist key is a 32-bit `uintptr_t`.

Worth 3569 -> 3150ms at four threads. Real, and much less than expected.

**The allocator's accounting was the actual barrier.** Every
`tpl_malloc()`/`tpl_free()` (`src/allocator.c`) hit four process-global
counters: bytes, allocation count, a CAS loop on the peak, and an
unconditional store to the set-allocator lockout flag. Atomic
read-modify-writes on shared cache lines are invisible everywhere
contention is normally looked for - no futex, no sys time, no spinning -
they just make every allocation in every thread queue for the same line.
The tell was four threads burning 10.8s of CPU for 3.2s of wall with
only 4.7% more instructions than one thread; the confirmation was that
four separate *processes* running the same work took 492ms each against
420ms alone, so the machine was fine and the sharing was not.

Fixed by striping those counters 64 ways, cache-line padded, summed by
`pl_get_allocator_stats()`. Which stripe an allocation was counted
against is packed into the top 6 bits of the size word in the allocation
header - `long double` is 8 bytes on arm64, so a separate field would
have taken the header from 8 to 16 bytes - and it has to be per
allocation, not per thread, because a block allocated on one thread is
routinely freed on another. Striping is 64-bit only: 6 stolen bits would
cap a single allocation at 64MB on a 32-bit target, which is reachable,
so those builds keep the single counter they always had. They are the
freestanding and embedded ones, single threaded, with nothing to
contend. `current_bytes`, `allocation_count` and `failure_count` stay
exact; `peak_bytes` becomes the sum of per-stripe peaks - an upper
bound, and exact whenever one stripe is in use, which is every caller
this has (`samples/embed.c`, `samples/allocator.c`,
`samples/freestanding.c`, `ports/arduino-nano-esp32`), all single
threaded. Worth 3150 -> 2216ms at four threads, and it is what stops ten
threads losing to one.

**What is left is the machine, not the engine.** Four threads are 1.9x,
and ten are 2185ms - level with four, never better - because six of this
box's ten cores are efficiency cores running interpreter work 6.0x
slower than the performance ones, and an even static split hands them an
equal share and then waits. Fixing that means sizing by measured
throughput or work-stealing, not another lock. It is also what retired
the `cpu_count` flag (above): the number's only obvious use was the one
that makes things slower.

Verified throughout at 406/406 in both suites, and under ASan for the
task and thread-actor tests plus `skynet_mixed.pl` at ten threads and
`skynet_threads.pl`'s thread churn. `samples/allocator` reporting
`current_bytes == 0` after `pl_destroy` is the check that striped
accounting still balances exactly across cross-thread frees.

## Before any of it: tests — done, and it paid off

Coverage for this whole area was two files when this was written —
`tests/sundry/tasks_scheduler.pl` and `tests/sundry/task_args.pl` — and
phase 0 is a refactor with nothing to check itself against. The plan was
to thicken first, not after:

- task ownership: nested `call_task`, a task spawning a task, `wait/0`
  from more than one place
- `end_wait/0` across the new ownership rules
- mailbox behaviour before it moves: send/receive ordering, selective
  receive, timeouts, a receiver that never gets its message
- `thread_join/2` and mutex handoff, which phase 1 rewrites

That happened, and the list is now:

| Where | Files |
|---|---|
| `tests/sundry/` | `tasks_scheduler`, `task_args`, `task_ownership`, `task_messaging`, `task_recv_blocking`, `task_timers`, `task_cancel`, `task_actors` |
| `tests/misc/` | `thread_mailbox`, `thread_actors`, `task_cancel`, `task_write_yield`, `db_concurrency`, `timeout`, `stream_timeout`, `put_chars_backpressure`, `stream_write_backpressure` |
| `tests/slow/` | `index-race` |
| `tests/tests/` | `test0115` (the if-then-else UAF regression) |

Worth recording that the premise held. Tests written against the
*current* semantics were the specification for the refactor, and the
bugs this phase actually caught were the ones nothing had pinned down:
the timeout bug that fell out of two places checking a deadline
(phase 1), the parked-task deadline that reset itself on re-entry
(phase 1 and again in phase 5's `recv/2`), the non-task `do_yield()`
no-op that made `recv/2` succeed against a mailbox it never read
(phase 5), and two use-after-frees that only real OS parallelism could
reach (phase 3's purge race, phase 5's if-then-else barrier). None of
those were visible to a single-threaded run.


## Open questions

**Settled:**

- *Does `thread_create/3` keep preemptive semantics between phases 2 and
  3?* No, and that is fine — phase 2 is a checkpoint to run it and see,
  not a release. Preemption arrives with the pool.
- *Is `MAX_THREADS 2048` still the right shape?* No. The fixed table
  goes, replaced by a skiplist, as part of phase 2. See there. Done -
  `pl->threads` is a skiplist (`src/internal.h:1282`). The `#define`
  itself survives at `src/internal.h:120` and will show up in a grep:
  it is no longer a table size, only the fallback the `os_threads` flag
  reports when nothing better is known.

**Still open, revisit when we reach them:**

- **What happens to the embedding API?** `pl_yield_at()` and
  `q->yield_at` assume the host drives the scheduler. A pool changes who
  is in charge. Not urgent before phase 3.
- **Logical update view under parallel workers.** `dbgen` is atomic, but
  whether a task's snapshot semantics survive two workers asserting
  concurrently has not been checked. Only matters once there is more
  than one worker — so, phase 3.

Both are option C problems specifically, not phase 3 as a whole - phase
3 was reached, and stopped at option B, which has no workers to raise
either question. They stay open exactly as long as option C does.

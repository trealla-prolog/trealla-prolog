push_succeed_on_retry_with_barrier():  f->no_recov = true;  // FIXME: memory waste
RESOLVED. The pin is now removed outright, with the ones in
do_if_then_else() and do_soft_if_then_else(): test0338, the suite and
Logtalk's threads/primes all pass without it. See the end of section 3 of
tco-then-branch-report.md. What follows is the earlier attempt, which was
withdrawn, kept so it is not retried the same way.

WHAT IT IS
  no_recov pins a frame: trim_frame() will not reclaim it and
  trim_trail() retains its entries. Set permanently on the CALLING
  frame by \+/1, ignore/1, \=/2, countall and succeed_on_retry/2, so
  one \+ in a clause body pins that frame for the rest of its life.
  Nothing to do with clause indexing - separate subsystem.

THE PRIZE, measured
  logtalk examples/threads/primes, primes(1,100000), maxRSS
    permanent flag        74344 kB
    pin removed outright  65892 kB    ceiling, ~11.5%

WHAT I TRIED
  A depth counter on the frame, held for the choicepoint's lifetime:
  f->no_recov_depth++ at push, released when the choicepoint is popped.
  Counter rather than save/restore so it nests and cannot clobber a
  no_recov set by tabling or set_var(). Both fields fitted existing
  padding - sizeof(frame) 72, sizeof(choice) 176, unchanged.

  Releasing at all three pop sites broke tests/issues/test0338 (clpb
  attributed variables lose bindings, third solution becomes
  instantiation_error) and shifted variable numbering in test0104.
  retry_choice() and any_outstanding_choices() RESUME execution rather
  than discard it, so releasing there is too early.

  Releasing only from drop_choice() passed everything I ran - suite
  335/1, test0338, test0104, chess, db-stress, iso_639 11/11, ASan clean
  - and captured the whole saving, 65764 kB against the 65892 ceiling.

WHY IT IS STILL WRONG
  It corrupts results under THREADS. logtalk examples/threads/primes
  goes 4/4 to 1/4:

    [2,3,5,7,11,13,17,19,23,29,31,37,41,43,47|c(user,user,r(user,user,[],[]))]

  A partial list whose tail holds a Logtalk execution-context term -
  the frame was recycled while another thread still held a reference
  into it. threaded_once/threaded_exit share difference-list tails
  across queries, and nothing at drop_choice() time knows another
  thread holds a reference. The permanent pin was covering that.

  Reverting the scoping restores 4/4. Confirmed both directions.

  I missed it because I benchmarked primes(1) - the SINGLE-threaded
  path - rather than running the tester, which exercises 2, 4 and 8
  threads. The single-thread path shows the memory saving and none of
  the breakage.

IF REVISITED
  The hold has to account for cross-query references, not just
  choicepoint lifetime. A thread-aware condition would have to be
  exactly right and the failure mode is silent wrong answers, so for
  ~11% on one benchmark I would leave this alone unless the frame
  ownership story is reworked generally.

  Whatever is tried: run logtalk examples/threads/primes AND
  tests/issues/test0338. Neither trealla's own suite nor ASan caught
  this.

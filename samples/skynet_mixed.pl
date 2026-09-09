% Skynet - https://github.com/atemerev/skynet
%
%     tpl samples/skynet_mixed.pl -g "run(1000000,10),halt"
%     tpl samples/skynet_mixed.pl -g "run(1000000,10,10),halt"
%
% Same benchmark as samples/skynet_threads.pl and samples/skynet_tasks.pl,
% run as a handful of real threads with tasks inside each. The other two
% take an extreme apiece: skynet_threads.pl is one OS thread per actor,
% which stops at the OS thread ceiling long before size=1000000;
% skynet_tasks.pl is all tasks on the one thread, which gets past that
% ceiling but only ever uses one core, because tasks on a thread are
% cooperatively scheduled. This is the middle: a few threads, each with
% its own scheduler (a task inherits the thread of whoever spawned it,
% so the queues never cross), and everything under a thread is tasks.
% run/3 takes the thread count; run/2 fixes it at 4, for the reason in
% the second half of this header.
%
% It pays off now, and what it took to get there is worth more than the
% sample. At size=1000000 div=10 on an Apple M4 (4 performance + 6
% efficiency cores), best of three each:
%
%                                          before      now
%     skynet_tasks.pl (main thread only)   4080ms    4168ms
%     this, run(1000000,10,1)              4163ms    4190ms
%     this, run(1000000,10,2)              3653ms    3014ms
%     this, run(1000000,10,4)              3569ms    2216ms
%     this, run(1000000,10,10)             6213ms    2185ms
%
% "Before" is where this file started: 1.14x from four threads, and one
% thread per CPU 1.5x *slower* than not threading at all. Now four
% threads are 1.9x and ten no longer lose to one. Three things were in
% the way, and the order they were found in is the opposite of the order
% they mattered.
%
% Task bookkeeping took prolog_lock(), a process-wide mutex, three or
% four times per task: register_task(), unregister_task(),
% find_task_by_qid() and sched_get() in src/bif_tasks.c all took it, and
% this benchmark's whole workload is 1.11M task creates, sends and
% destroys. sched_get() only ever needed it to allocate one scheduler
% per thread, so it is double-checked now; the registry became one
% skiplist per thread, addressed through an id that carries its owning
% thread, so a send within a thread - which is nearly all of them - now
% takes only that thread's own lock. Worth 3569 -> 3150ms at four
% threads: real, and much less than expected.
%
% The allocator's accounting was the actual barrier. Every tpl_malloc()
% and tpl_free() in src/allocator.c hit four process-global counters -
% bytes, allocation count, a CAS loop on the peak, and an unconditional
% store to the set-allocator lockout flag. Atomic read-modify-writes on
% shared cache lines are invisible where you look for contention: no
% futex, no sys time, no spinning, just every allocation in every thread
% queueing for the same cache line. Four threads were burning 10.8s of
% CPU for 3.2s of wall with only 4.7% more instructions than one thread.
% What gave it away was that four *separate processes* running this
% workload took 492ms each against 420ms alone - the machine was fine,
% the sharing was not. Striping those counters is the 3150 -> 2216ms.
%
% A logical-CPU count is the wrong width on a hybrid machine, which is
% what retired the cpu_count flag - it said 10 here, and taking it at
% its word is the 6213ms row above. An efficiency core is 6.0x slower
% than a performance one - a bare arithmetic loop, no tasks and no shared
% state, 20M iterations: 1378ms on a P core, 8225ms on an E core under
% background QoS. So a plain thread pool scales to 4 (1406ms on one
% thread, 2112ms on four) and then falls off a cliff as threads land on
% E cores: 6495ms at five, 16728ms at ten. An even static split makes
% every run wait for the slowest share. This is the one still standing:
% ten threads now match four rather than losing to one, but they do not
% beat them, and they never will while six of them run on cores several
% times slower and are handed an equal share of the work anyway. Sizing
% by measured throughput, or work-stealing, is what would fix it. Four
% is hardcoded rather than probed because there is no portable way to
% ask for the performance-core count: sysctlbyname
% hw.perflevel0.logicalcpu on macOS, /sys/devices/cpu_core/cpus or
% cpu_capacity on Linux depending on the vendor, EfficiencyClass from
% GetSystemCpuSetInformation on Windows.
%
% The split is by subtree, at the shallowest level of the tree with at
% least one subtree per thread - level ceil(log_Div(NT)), so div=10 on
% 4 threads splits into 10 subtrees of size/10 and div=2 into 4 of
% size/4. Those are handed out evenly, the first M mod NT threads
% taking one extra.
%
% A thread runs its own subtrees one after another rather than spawning
% them all at once. Nothing is lost: its tasks are cooperative on one
% core either way, so concurrency within a thread buys no parallelism,
% and one at a time keeps peak live tasks at O(depth) instead of
% O(subtree size) - the memory blowup samples/skynet_tasks.pl's header
% describes. All the parallelism is meant to come from the threads.
%
% library(actors/tasks) is kept rather than raw task_create/2 and
% send/2 so the only variable against skynet_tasks.pl is the threading.
% It is not free - the actor layer's link bookkeeping is two dynamic
% database operations per actor, on a database lock every thread
% shares, and was worth 6213ms against a raw-task variant's 5264ms at
% ten threads when the locks above still dominated - but dropping it
% here would flatter this file against the one it is compared to.
%
% Every actor reports to its parent exactly once, either result/1 or
% failed/1, and so does every worker thread - same reasoning as the
% other two: one that died without reporting would leave the level
% above it blocked forever rather than failing. Thread creation is not
% retried, unlike samples/skynet_threads.pl, because four threads is
% nowhere near a limit worth riding out.

:- use_module(library(lists)).
:- use_module(library(actors/tasks)).

% One actor per node, as tasks, entirely within the calling thread.

skynet(Parent, Num, 1, _) :-
	!,
	task_actor_send(Parent, result(Num)).

skynet(Parent, Num, Size, Div) :-
	catch(spawn_and_sum(Num, Size, Div, Tot), E,
		( task_actor_send(Parent, failed(E)), throw(E) )),
	task_actor_send(Parent, result(Tot)).

spawn_and_sum(Num, Size, Div, Tot) :-
	NewSize is Size div Div,
	task_actor_self(Me),
	findall(X, (
		between(1, Div, Idx),
		NewNum is ((Idx - 1) * NewSize) + Num,
		task_actor_spawn(skynet(Me, NewNum, NewSize, Div), _),
		'$skynet_recv_result'(X)
	), Xs),
	sum_list(Xs, Tot).

'$skynet_recv_result'(X) :-
	task_actor_recv(Msg),
	(	Msg = result(X) -> true
	;	Msg = failed(E) -> throw(E)
	;	'$skynet_recv_result'(X)
	).

% A worker thread: the subtrees Lo..Hi-1, summed and reported once.

worker(Main, Lo, Hi, SubSize, Div) :-
	catch(run_slice(Lo, Hi, SubSize, Div, 0, Tot), E,
		( thread_send_message(Main, failed(E)), throw(E) )),
	thread_send_message(Main, result(Tot)).

run_slice(Idx, Hi, _, _, Tot, Tot) :-
	Idx >= Hi,
	!.

run_slice(Idx, Hi, SubSize, Div, Acc, Tot) :-
	Num is Idx * SubSize,
	task_actor_self(Me),
	task_actor_spawn(skynet(Me, Num, SubSize, Div), _),
	wait,
	'$skynet_recv_result'(X),
	Acc1 is Acc + X,
	Idx1 is Idx + 1,
	run_slice(Idx1, Hi, SubSize, Div, Acc1, Tot).

% The shallowest level with at least one subtree per thread, or the
% leaves if the tree runs out first.

chunks(Size, Div, Want, M) :-
	'$chunks'(1, Size, Div, Want, M).

'$chunks'(M0, _, _, Want, M0) :- M0 >= Want, !.
'$chunks'(M0, Size, _, _, M0) :- M0 >= Size, !.
'$chunks'(M0, Size, Div, Want, M) :-
	M1 is M0 * Div,
	'$chunks'(M1, Size, Div, Want, M).

% Worker T of NT, 1-based, gets subtrees Lo..Hi-1.

share(M, NT, T, Lo, Hi) :-
	Base is M div NT,
	Extra is M mod NT,
	I is T - 1,
	Lo is (I * Base) + min(I, Extra),
	( I < Extra -> Cnt is Base + 1 ; Cnt = Base ),
	Hi is Lo + Cnt.

collect(0, Tot, result(Tot)) :- !.
collect(N, Acc, R) :-
	thread_self(Me),
	thread_get_message(Me, Msg),
	(	Msg = result(X)
	->	Acc1 is Acc + X, N1 is N - 1, collect(N1, Acc1, R)
	;	Msg = failed(E)
	->	R = failed(E)
	;	collect(N, Acc, R)
	).

% Reject a Size that is not a power of Div rather than hanging.

exact_levels(1, _, 0) :- !.
exact_levels(Size, Div, N) :-
	Size > 1,
	0 =:= Size mod Div,
	Next is Size div Div,
	exact_levels(Next, Div, N0),
	N is N0 + 1.

run(Size, Div) :-
	run(Size, Div, 4).

run(Size, Div, Want) :-
	(	exact_levels(Size, Div, _)
	->	true
	;	format("size ~w is not a power of ~w~n", [Size,Div]), fail
	),
	chunks(Size, Div, Want, M),
	NT is min(Want, M),
	SubSize is Size div M,
	thread_self(Main),
	get_time(T0),
	forall(between(1, NT, T),
		(	share(M, NT, T, Lo, Hi),
			thread_create(worker(Main, Lo, Hi, SubSize, Div), _, [detached(true)])
		)),
	collect(NT, 0, Msg),
	get_time(T1),
	Ms is round((T1-T0)*1000),
	Expect is (Size * (Size - 1)) // 2,
	(	Msg = result(Tot)
	->	( Tot =:= Expect -> R = ok ; R = wrong(Tot,Expect) )
	;	Msg = failed(E) -> R = failed(E)
	;	R = unexpected(Msg)
	),
	format("size=~w div=~w threads=~w -> ~w in ~wms~n", [Size,Div,NT,R,Ms]).

# What a foreign call costs, and what is left to win

**Done so far:** per-call overhead on this Mac went from about 70 ns to about
33 ns, measured against `5ee513a5`, by not zeroing 6 KB of stack on every call
and by reusing the cif compiled at registration (`5b18da3d`). A native C
builtin costs about 5 ns for comparison, so a foreign call is now about 6x a
builtin rather than 14x. Recorded here so the next round starts from numbers
rather than from guesses.

## How it was measured

`statistics(cputime, T)` deltas around a failure-driven loop of three million
calls, three repeats, minus the cost of the same loop with `true` in it. The
loop and `call/1` dominate at about 99 ns an iteration, which is why the
baseline is subtracted rather than ignored. Wall clock on this machine is
about 2% noisy, and the repeats stayed inside that.

```prolog
:- use_module(library(gsl)).

bench(Label, Goal) :-
	N = 3000000,
	statistics(cputime, T0),
	( between(1, N, _), call(Goal), fail ; true ),
	statistics(cputime, T1),
	Per is (T1 - T0) * 1000000000 / N,
	format("~w~t~20|~1f ns/call~n", [Label, Per]).

main :-
	mat_from_list(M, [[1,2],[3,4]]),
	vec_from_list(V, [1,2]),
	between(1, 3, _),
		bench(empty, true),
		bench(set_4args_void, gsl_matrix_set(M, 0, 0, 1.5)),
		bench(get_3args_double, gsl_matrix_get(M, 0, 0, _)),
		bench(get_2args_double, gsl_vector_get(V, 0, _)),
		bench(builtin_reference, '$gsl_vector_size'(V, _)),
	fail.
main.
```

Pick predicates whose C side does nothing much. `gsl_sf_hyperg_2F1` looks like
a slow four-argument call at 169 ns, but that is a hypergeometric series doing
real work, not marshalling. `gsl_matrix_set` is a store.

| call | before | after |
|---|---|---|
| `gsl_vector_get/3` (2 args) | 71.7 ns | 31.9 ns |
| `gsl_matrix_get/4` (3 args) | 71.5 ns | 38.7 ns |
| `gsl_matrix_set/4` (4 args, void) | 67.7 ns | 33.0 ns |
| `'$gsl_vector_size'/2` (C builtin, reference) | 4.8 ns | 6.2 ns |

The cost barely moves with argument count, before or after. That is the shape
of the problem: it is fixed work per call, not work per argument.

## What was fixed

`wrap_ffi_predicate()` declared five zero-initialised locals:

```c
ffi_type *arg_types[64] = {0};  void *arg_values[64] = {0};
void *s_args[64] = {0};         result cells[64] = {0};
uint8_t bytes[4096] = {0};      // only a struct argument ever reads this
```

Six kilobytes of `memset` for a call that uses three of those sixty-four slots
and, nearly always, no struct at all. This is the same mistake `nested[]` and
`types[]` had, 34 KB of it, fixed earlier and commented in place: 390 ns to
210 ns on a three-integer function. Now the four arrays are zeroed for `arity`
entries and `bytes[]` only when the signature has a struct in it.

The other half is the cif. It is compiled at registration by
`precompile_cif()`, but each call still rebuilt `arg_types[]` to pass to
`ffi_prep_cif()` — and `ffi_type_of()` is a linear scan of a 23-row table, per
argument, per call. With a pre-compiled cif nothing reads `arg_types[]` at
all, so a `prepped` flag now skips filling it, and skips the return-type
lookup too. A struct in the signature means no pre-compiled cif, so that path
is unchanged.

Found while reading the same code: `'$register_function'/4`, the evaluable
form, segfaulted on its first use. It passed `ffi_prep_cif()` a NULL return
type, which libffi dereferences — a fifteen-line C program against the same
libffi confirms the crash is inside libffi, not above it — and it marshalled
`ptr->arity - 1` arguments when for an evaluable `ptr->arity` *is* the
argument count, so the last `arg_values[]` slot was uninitialised stack.
Fixed in `870794d5` with `tests/sundry/ffi_evaluable.pl`, which segfaults a
`v3.14.50` binary and passes after. `precompile_cif()` also computed the
predicate convention's `nargs` for evaluables, which is why its cif could not
simply be reused; that is corrected, and both wrappers now share it.

## What is left, in the order I would do it

About 28 ns of the 33 is still ours, not libffi's.

Items (1) and (3) of the first version of this list are done, and bought no
speed. The wrappers used to walk three if-else chains per argument - 21 tests
to validate the cell, 39 to marshal it, 18 to unify outputs - and those are now
one `switch` that checks and marshals together, `ffi_marshal()`, and one that
reads values back, `ffi_to_term()`, shared by `wrap_ffi_predicate()` and
`wrap_ffi_function()`. The file lost about 500 lines. Counted in instructions
retired over 3M calls, `gsl_matrix_get/4` went from 1069 to 1075 a call and
`gsl_matrix_set/4` from 922 to 923, which is parity: clang had already turned
each chain into a jump table, so the switch only made that visible. Two of the
steps on the way are worth knowing. With the helpers left out of line, the
calls added about 60 instructions a foreign call, so they are forced inline.
With checking and marshalling as two switches, the extra tests added about 23,
so they are one.

1. **Decode the signature at registration.** Store a small op per argument -
   width, signed or not, by value or by pointer - so the per-call loop reads a
   precomputed op instead of switching on the tag. Since the switch is already
   a jump table, expect little from this alone; it matters as the groundwork
   for (2).

2. **A fast path for the common signature:** all scalars, no out-params, no
   struct, which is almost every entry in `library(gsl)`. Skip the generic
   machinery entirely.

3. **Typed thunks for common shapes.** `ffi_call()`'s trampoline is maybe 10
   to 15 ns of what remains. Casting the function pointer to
   `double(*)(double,double)` and friends for a dozen shapes would skip libffi
   on the hot ones. The biggest remaining win and the biggest change; it also
   needs care per ABI, which is exactly what libffi is for. Worth it only if
   someone has a workload that wants it.

Not worth doing: `ffi_err_of()` and `ffi_type_of()` are linear scans of the
type table, but they are off the hot path now; `ffi_cif cif = {0}` is about 56
bytes when a cif is already prepped, which is noise.

## Known gaps

None left from the first version of this note. An evaluable registered at run
time used to be invisible to goals compiled before it - `X is labs(-7)` from a
`-g` goal raised `type_error(evaluable, labs/1)`, because the compiler left no
`bif_ptr` on the cell and nothing looked one up later. `56117054` (v3.14.53)
closed it while fixing #1183: `call_userfun()` now looks an untagged cell's
name and arity up among the evaluables before refusing it, the arithmetic twin
of what `match_head()` does for a goal.

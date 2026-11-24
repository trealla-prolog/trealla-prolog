% Typed foreign handles: what a ptr(Type) return makes, what a ptr(Type) argument takes, and a free(Type) call's
% handle and its copies dead after it. Addresses differ from run to run, so no handle is printed whole.
% Run by ../gsl_handles.sh, which skips it where libgsl or the FFI is missing.

:- use_module(library(gsl)).
:- initialization(main).
:- dynamic(kept/1).

% An error by its formal's name and type and its context, as the culprit holds an address

shape(error(F, C), error(N/T, C)) :- functor(F, N, _), (compound(F), arg(1, F, T), atom(T) -> true ; T = none).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E)
	    ->  format("~w: ~q~n", [Label, Show])
	    ;   shape(E, S), format("~w: raised ~q~n", [Label, S])
	    )
	;   format("~w: failed~n", [Label])
	).

% How a handle prints, up to its address

printed(H, Prefix) :-
	format(atom(A), "~q", [H]),
	sub_atom(A, Before, _, _, '(0x'), sub_atom(A, 0, Before, _, P),
	atom_codes(P, Prefix).

main :-
	gsl_matrix_alloc(2, 3, M),
	gsl_vector_alloc(3, V),
	gsl_permutation_alloc(3, P),
	t(matrix_type, foreign_handle(M, T1), T1),
	t(vector_type, foreign_handle(V, T2), T2),
	t(permutation_type, foreign_handle(P, T3), T3),
	t(not_handle_atom, \+ foreign_handle(foo, _), yes),
	t(not_handle_int, \+ foreign_handle(42, _), yes),
	t(not_handle_type, \+ foreign_handle(M, gsl_vector), yes),
	t(printed, (printed(M, Pr), atom_codes(Shown, Pr), Shown == '\'$gsl_matrix\''), yes),
	t(atomic, (atomic(M), \+ compound(M), \+ atom(M), \+ integer(M), \+ callable(M)), yes),
	t(univ, (M =.. [F0], F0 == M, functor(M, F1, A0), F1 == M), A0),
	t(set_get, (gsl_matrix_set(M, 1, 2, 2.5), gsl_matrix_get(M, 1, 2, X1)), X1),
	t(size, '$gsl_matrix_size'(M, R1, C1), R1*C1),
	t(copy_equal, (copy_term(M, M2), M2 == M), yes),
	t(assert_equal, (assertz(kept(M)), kept(M3), M3 == M), yes),
	t(findall_equal, (findall(H, H = M, [M4]), M4 == M), yes),
	t(not_equal, \+ M == V, yes),
	t(unify_other, \+ M = V, yes),
	t(order, (msort([f(x), V, "s", M, a, 1], L), L = [1, a, H1, H2, _, _], foreign_handle(H1, A1), foreign_handle(H2, A2)), A1-A2),
	t(compare_self, compare(O1, M, M), O1),
	t(wrong_type, gsl_matrix_get(V, 0, 0, _), unreachable),
	t(wrong_type_vector, gsl_vector_get(M, 0, _), unreachable),
	t(wrong_type_builtin, '$gsl_vector_size'(M, _), unreachable),
	t(atom_arg, gsl_matrix_get(foo, 0, 0, _), unreachable),
	t(var_arg, gsl_matrix_get(_, 0, 0, _), unreachable),
	t(mixed_args, (gsl_vector_set_all(V, 1.0), gsl_matrix_set_all(M, 0.0), gsl:gsl_matrix_set_row(M, 0, V, _), gsl_matrix_get(M, 0, 1, X2)), X2),
	t(permutation_args, (gsl_permutation_init(P), gsl_permutation_swap(P, 0, 2, _), gsl_permutation_get(P, 0, X3)), X3),
	t(view_as_address, (gsl_matrix_row(M, 0, Row), '$struct_to_pointer'(Row, RowPtr), gsl_vector_get(RowPtr, 2, X4), '$free_struct_pointer'(RowPtr)), X4),
	t(helpers, (mat_from_list(A, [[2.0,1.0],[1.0,3.0]]), mat_mul(A, A, B), mat_to_list(B, BL), gsl_matrix_free(A), gsl_matrix_free(B)), BL),
	t(free, gsl_matrix_free(M), done),
	t(use_freed, gsl_matrix_get(M, 0, 0, _), unreachable),
	t(use_freed_copy, gsl_matrix_get(M2, 0, 0, _), unreachable),
	t(use_freed_asserted, (kept(K), gsl_matrix_get(K, 0, 0, _)), unreachable),
	t(free_twice, gsl_matrix_free(M), unreachable),
	t(builtin_freed, '$gsl_matrix_size'(M, _, _), unreachable),
	t(freed_still_handle, foreign_handle(M, T4), T4),
	t(freed_wrong_type, gsl_vector_get(M, 0, _), unreachable),
	t(free_vector, gsl_vector_free(V), done),
	t(free_permutation, gsl_permutation_free(P), done),
	t(use_freed_permutation, gsl_permutation_get(P, 0, _), unreachable),
	t(untyped_ptr_takes_handle, (gsl_vector_alloc(2, W), gsl_vector_set_all(W, 3.0),
		use_foreign_module('libgsl.so', [gsl_vector_sum([ptr], double)]), gsl_vector_sum(W, S1)), S1),
	t(untyped_ptr_freed, (gsl_vector_free(W), gsl_vector_sum(W, _)), unreachable).

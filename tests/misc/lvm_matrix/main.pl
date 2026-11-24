% lvm's Logtalk matrix tests (tests/logtalk/matrix_vector, matrix_binary_file and the graph tests in matrix)
% ported to library(gsl) and library(igraph). Labels are lvm's test names where one maps; the expected
% values are lvm's, LAGraph's own, or the maths where lvm only checks a shape. Not ported: Matrix Market
% input, typed int8..uint64 matrices, sparse semantics and lvm's argument-error tests. Run by
% ../lvm_matrix.sh, which skips it where libgsl, igraph or the FFI is missing.

:- use_module(library(gsl)).
:- use_module(library(igraph)).
:- use_module(library(lists)).
:- use_module(library(yall)).
:- initialization(main).

% fopen/2 for gsl_matrix_fwrite/3 and gsl_matrix_fread/3, from whichever libc opens.

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- (	libc(L), catch('$dlopen'(L, 0, _), _, fail)
	->	use_foreign_module(L, [fopen([cstr,cstr], ptr), fclose([ptr], sint)])
	;	true
	).

% Floats to 6 places, so last-bit differences between BLAS builds don't show.

rnd(X, R) :- float(X), !, R is round(X * 1000000) / 1000000.0.
rnd(X, R) :- compound(X), !, X =.. [F|As], maplist(rnd, As, Rs), R =.. [F|Rs].
rnd(X, X).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E)
	    ->  rnd(Show, R), format("~w: ~q~n", [Label, R])
	    ;   format("~w: raised ~q~n", [Label, E])
	    )
	;   format("~w: failed~n", [Label])
	).

main :-
	construction,
	properties,
	elements,
	simple_ops,
	sums,
	eigen,
	writing,
	binary_file,
	connected_components,
	degrees,
	betweenness.

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% matrix_vector: lvm's as_matrix([1,2,3]) is a 1x3 matrix, written [[1,2,3]] here

construction :-
	t(as_matrix_1_function_succeeds_integers, (mat_from_list(M, [[1,0,0],[0,2,0],[0,0,3]]), mat_to_list(M, L)), L),
	t(as_matrix_1_function_succeeds_floats, (mat_from_list(F, [[3.14159265,0.0,0.0],[0.0,2.0,0.0],[0.0,0.0,1.0]]),
		mat_to_list(F, LF)), LF),

	% GSL has no empty matrix or vector, so these fail where lvm makes one.

	t(as_matrix_1_function_succeeds_empty, mat_from_list(_, [[]]), made),
	t(as_vector_1_function_succeeds_empty, vec_from_list(_, []), made),
	t(as_vector_1_function_succeeds_dimension, (vec_from_list(V, [13,13,13,13,13]), '$gsl_vector_size'(V, N)), N),
	t(as_vector_2_predicate_succeeds_dimension, (vec_from_list(V2, [0,1,2,3,4,5,6,7,8,9]), '$gsl_vector_size'(V2, N2)), N2).

properties :-
	t(matrix_property_2_dimension_property_1x3, (mat_from_list(M, [[1,2,3]]), '$gsl_matrix_size'(M, R, C)), R-C),
	mat_from_list(M2, [[1,2,3],[4,5,6]]),
	t(matrix_property_2_dimension_property_2x3, '$gsl_matrix_size'(M2, R2, C2), R2-C2),
	t(matrix_property_2_dimension_property_fails, '$gsl_matrix_size'(M2, 3, _), yes).

elements :-
	t(matrix_element_4_succeeds_1x3, (mat_from_list(M, [[1,2,3]]), gsl_matrix_get(M, 0, 0, E)), E),
	mat_from_list(M2, [[1,2,3],[4,5,6]]),
	t(matrix_element_4_succeeds_3x3, gsl_matrix_get(M2, 1, 1, E2), E2),
	t(matrix_element_4_enumerates_row, (gsl_vector_alloc(3, Row), gsl_matrix_get_row(Row, M2, 0, _),
		vec_to_list(Row, LR), gsl_vector_free(Row), findall(I-X, nth0(I, LR, X), Ps)), Ps),
	t(matrix_element_4_enumerates_all, findall(e(I,J,X), (between(0, 1, I), between(0, 2, J), gsl_matrix_get(M2, I, J, X)), Es), Es),
	vec_from_list(V, [1,2,3]),
	t(vector_element_3_succeeds, gsl_vector_get(V, 0, VE), VE),
	t(vector_element_3_enumerates, findall(P-X, (between(0, 2, P), gsl_vector_get(V, P, X)), VPs), VPs),

	% GSL has no elementwise square root, so it is taken in Prolog; GSL sums it and squares it back.

	t(matrix_element_sqrt_2_sum, (mat_from_list(A, [[0,1,2,3,4]]), mat_to_list(A, [LA]),
		maplist([X1,Y1]>>(Y1 is sqrt(X1)), LA, LS), mat_from_list(S, [LS]), mat_sum(S, Sum),
		simple_op(S, S, *, Sq), mat_to_list(Sq, LSq)), Sum-LSq).

% matrix_simple_op/4 makes a new matrix, where GSL works in place, so on a copy of the first. A matrix
% is a pointer, an integer, so a scalar second argument is k(N).

simple_op(A, B, Op, C) :-
	'$gsl_matrix_size'(A, Rows, Cols),
	gsl_matrix_alloc(Rows, Cols, C),
	gsl_matrix_memcpy(C, A, _),
	op_(Op, C, B, St),
	St =:= 0.

op_(+, C, k(N), St) :- !, K is float(N), gsl_matrix_add_constant(C, K, St).
op_(-, C, k(N), St) :- !, K is -float(N), gsl_matrix_add_constant(C, K, St).
op_(*, C, k(N), St) :- !, K is float(N), gsl_matrix_scale(C, K, St).
op_(/, C, k(N), St) :- !, K is 1 / N, gsl_matrix_scale(C, K, St).
op_(+, C, B, St) :- gsl_matrix_add(C, B, St).
op_(-, C, B, St) :- gsl_matrix_sub(C, B, St).
op_(*, C, B, St) :- gsl_matrix_mul_elements(C, B, St).
op_(/, C, B, St) :- gsl_matrix_div_elements(C, B, St).

% lvm's tests only check the result is a matrix of the right size, so the values here are the maths.

simple_ops :-
	mat_from_list(A, [[1,2,3]]),
	mat_from_list(B, [[4,5,6]]),
	t(matrix_simple_op_4_succeeds, (simple_op(A, B, +, C), mat_to_list(C, L)), L),
	t(matrix_simple_op_4_dimension_preserved_1, (simple_op(A, k(2), +, C1), '$gsl_matrix_size'(C1, R1, Cs1),
		mat_to_list(C1, L1)), R1-Cs1-L1),
	mat_from_list(M1, [[1,2,3],[4,5,6]]),
	mat_from_list(M2, [[4,5,6],[7,8,9]]),
	t(matrix_simple_op_4_dimension_preserved_2, (simple_op(M1, M2, *, M3), simple_op(M1, M2, -, M4),
		simple_op(M3, M4, +, M), '$gsl_matrix_size'(M, R, Cs), mat_to_list(M, LM)), R-Cs-LM),
	t(matrix_simple_op_4_divide, (simple_op(M1, M2, /, D), mat_to_list(D, LD)), LD),
	t(matrix_simple_op_4_scalars, findall(Op-LK, (member(Op, [-,*,/]), simple_op(M1, k(2), Op, K), mat_to_list(K, LK)), Ks), Ks),
	t(matrix_simple_op_4_inputs_intact, (mat_to_list(M1, I1), mat_to_list(M2, I2)), I1/I2).

% M or its transpose times a vector of ones: the row or the column sums

ones_product(M, Trans, Y) :-
	'$gsl_matrix_size'(M, Rows, Cols),
	(Trans =:= 111 -> In = Cols, Out = Rows ; In = Rows, Out = Cols),
	length(Ones, In),
	maplist(=(1), Ones),
	vec_from_list(X, Ones),
	gsl_vector_alloc(Out, Y),
	gsl_blas_dgemv(Trans, 1.0, M, X, 0.0, Y, _),
	gsl_vector_free(X).

mat_sum(M, S) :- ones_product(M, 111, Y), gsl_vector_sum(Y, S), gsl_vector_free(Y).
row_sums(M, L) :- ones_product(M, 111, Y), vec_to_list(Y, L), gsl_vector_free(Y).
col_sums(M, L) :- ones_product(M, 112, Y), vec_to_list(Y, L), gsl_vector_free(Y).

% GSL is double only, so lvm's integer 10 is 10.0 here.

sums :-
	t(matrix_sum_2_integer_elements_succeeds, (mat_from_list(M, [[1,2],[3,4]]), mat_sum(M, S)), S),
	t(matrix_sum_2_float_elements_succeeds, (mat_from_list(F, [[1.0,2.0],[3.0,4.0]]), mat_sum(F, SF)), SF).

% lvm skips both eigenvalue tests, and its 3x3 one reads index 1 twice: the values are the maths, as
% Real-Imaginary pairs in increasing magnitude. The 3x3 [[1,2,3],[4,5,6],[7,8,9]] of lvm's eigen error
% tests is singular, so has a zero eigenvalue. Each eigenvector v of A has A v = lambda v.

eigen :-
	mat_from_list(A2, [[5,4],[1,2]]),
	t(eigen_values_2_succeeds_2x2, (mat_eigen_nonsymm(A2, V2, _), length(V2, N2)), N2-V2),
	t(eigen_vectors_3_succeeds_2x2, (mat_eigen_nonsymm(A2, _, Vs2), maplist(real_up_to_sign, Vs2, Rs2)), Rs2),
	mat_from_list(A3, [[3,1,1],[2,4,2],[1,1,3]]),
	t(eigen_values_2_succeeds_3x3, (mat_eigen_nonsymm(A3, V3, _), length(V3, N3)), N3-V3),
	mat_from_list(S3, [[1,2,3],[4,5,6],[7,8,9]]),
	t(eigen_values_2_singular_3x3, mat_eigen_nonsymm(S3, VS, _), VS),
	t(eigen_vectors_3_residuals, (forall(member(A, [A2,A3,S3]), (mat_eigen_nonsymm(A, Vals, Vecs),
		eigen_residual(A, Vals, Vecs, Max), Max < 1.0e-9))), yes).

% An eigenvector's real parts, the sign chosen so its first nonzero one is positive

real_up_to_sign(Col, Rs) :-
	findall(Re, member(Re-_, Col), Rs0),
	(member(X, Rs0), X =\= 0.0 -> true ; X = 1.0),
	(X < 0.0 -> maplist([P,Q]>>(Q is -P), Rs0, Rs) ; Rs = Rs0).

% The largest |A v - lambda v| over the eigenpairs, all real here

eigen_residual(A, Vals, Vecs, Max) :-
	'$gsl_matrix_size'(A, N, _),
	findall(D,
		(	nth0(I, Vals, Lambda-_), nth0(I, Vecs, Col),
			findall(Re, member(Re-_, Col), V),
			vec_from_list(X, V), gsl_vector_alloc(N, Y),
			gsl_blas_dgemv(111, 1.0, A, X, 0.0, Y, _), vec_to_list(Y, AV),
			gsl_vector_free(X), gsl_vector_free(Y),
			foldl([P,Q,M0,M1]>>(M1 is max(M0, abs(P - Lambda*Q))), AV, V, 0.0, D)
		), Ds),
	max_list(Ds, Max).

% matrix_write/1 and vector_write/1 to the current output, which lvm checks is not empty

writing :-
	forall(
		member(Label-Rows, [
			matrix_write_1_succeeds_non_zeros-[[1,2,3],[4,5,6],[7,8,9]],
			matrix_write_1_succeeds_all_zeros-[[0,0,0],[0,0,0],[0,0,0]],
			matrix_write_1_succeeds_floats-[[1.1,2.2,3.3],[4.4,5.5,6.6],[7.7,8.8,9.9]]]),
		(	format("~w:~n", [Label]), mat_from_list(M, Rows), mat_write(M, user_output), gsl_matrix_free(M)
		)),
	forall(
		member(Label-L, [vector_write_1_succeeds_non_zeros-[1,2,3], vector_write_1_succeeds_all_zeros-[0,0,0]]),
		(	format("~w:~n", [Label]), vec_from_list(V, L), vec_write(V, user_output), gsl_vector_free(V)
		)),

	% matrix_write/2 and vector_write/2 to a stream of their own: mat_write/2 and vec_write/2 used to
	% write to the current output instead, and keep six digits, so here nothing must reach the current
	% output and the file must read back exactly.

	F = 'lvm_matrix.tmp',
	forall(
		member(Label-Rows, [
			matrix_write_2_succeeds_non_zeros-[[1,2,3],[4,5,6],[7,8,9]],
			matrix_write_2_succeeds_all_zeros-[[0,0,0],[0,0,0],[0,0,0]],
			matrix_write_2_succeeds_floats-[[1.1,2.2,3.3],[4.4,5.5,6.6],[7.7,8.8,9.9]]]),
		t(Label, (mat_from_list(M, Rows), write_file(mat_write(M), F), open(F, read, R), mat_read(M2, R, _, _),
			close(R), mat_to_list(M2, L), mat_to_list(M, L)), L)),
	forall(
		member(Label-L0, [
			vector_write_2_succeeds_non_zeros-[1,2,3],
			vector_write_2_succeeds_all_zeros-[0,0,0],
			vector_write_2_succeeds_floats-[1.1,2.2,3.3]]),
		t(Label, (vec_from_list(V, L0), write_file(vec_write(V), F), open(F, read, R), vec_read(V2, R, _),
			close(R), vec_to_list(V2, L), vec_to_list(V, L)), L)),
	t(matrix_write_2_exact, (mat_from_list(E, [[0.1,-2.5e-300],[1.0e300,3.14159265358979]]), write_file(mat_write(E), F),
		open(F, read, R), mat_read(E2, R, _, _), close(R), mat_to_list(E, LE), mat_to_list(E2, LE)), yes),
	catch(delete_file(F), _, true).

% call(Goal, S) on a file opened for writing, nothing of it reaching the current output

write_file(Goal, F) :-
	with_output_to(string(Out), setup_call_cleanup(open(F, write, S), call(Goal, S), close(S))),
	Out == [].

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% matrix_binary_file: lvm saves a GraphBLAS file and loads it back; here GSL's raw doubles

binary_file :-
	F = 'lvm_matrix.tmp',
	mat_from_list(M, [[1,2,3],[4,5,6]]),
	t(matrix_save_binary_file_succeeds, (save_(M, F), exists_file(F), size_file(F, Size)), Size),
	t(load_save_matrix, (gsl_matrix_alloc(2, 3, M2), load_(M2, F), mat_to_list(M2, L)), L),
	Fs = [[0.1,-2.5e-300],[1.0e300,3.14159265358979]],
	t(load_save_matrix_floats_exact, (mat_from_list(F1, Fs), save_(F1, F), gsl_matrix_alloc(2, 2, F2),
		load_(F2, F), mat_to_list(F2, Fs)), yes),
	catch(delete_file(F), _, true).

save_(M, F) :- fopen(F, wb, Fp), Fp =\= 0, gsl_matrix_fwrite(Fp, M, St), fclose(Fp, _), St =:= 0.
load_(M, F) :- fopen(F, rb, Fp), Fp =\= 0, gsl_matrix_fread(Fp, M, St), fclose(Fp, _), St =:= 0.

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% matrix graph tests: the edges of lvm's data/*.mtx files, numbered from 1 as there, a symmetric
% file's stored triangle once and its diagonal left out

% test_connected_components.pl: the number of components lvm's matrix_info/2 gives for each file

connected_components :-
	lfat5(Lfat5),
	findall(A-B, (member(A0-B0, Lfat5), A is A0 + 14, B is B0 + 14), Second),
	append(Lfat5, Second, Lfat5Two),
	forall(
		member(Name-N-Es, [
			'ldbc-undirected-example'-9-[1-2, 1-3, 2-3, 2-4, 2-7, 4-5, 4-7, 5-6, 5-7, 5-8, 5-9, 6-8],
			'ldbc-wcc-example'-10-[1-3, 1-5, 1-8, 2-4, 2-5, 2-10, 3-5, 3-6, 3-8, 3-10, 4-5, 4-6, 4-7, 4-9, 5-8],
			'LFAT5'-14-Lfat5,
			'LFAT5_two'-28-Lfat5Two,
			'tree-example'-6-[1-3, 2-4, 3-5, 4-5, 4-6]]),
		t(connected_components(Name), (numlist(1, N, Vs), igraph_new(Es, G, [vertices(Vs)]),
			igraph_components(G, Cs), igraph_free(G), length(Cs, K)), K-Cs)).

% LFAT5_two.mtx is LFAT5.mtx twice over, the second copy's vertices 14 on.

lfat5([1-4, 1-5, 2-6, 3-7, 4-8, 4-9, 5-8, 5-9, 6-10, 7-11, 8-12, 8-13, 9-12, 9-13, 12-14, 13-14]).

% test_cached_degree.pl: LAGraph's out- and in-degrees, from its test_Cached_Degree.c, which lvm lists
% for matrix_int32.mtx and comments out for the others. GSL gives them as the row and column sums of
% the adjacency matrix; igraph_degree/2 counts both directions, so gives their sum.

degrees :-
	karate(Karate),
	findall(B-A, member(A-B, Karate), Back),
	append(Karate, Back, KarateBoth),
	forall(
		member(Name-N-Directed-Es, [
			matrix_int32-7-true-[4-1, 1-2, 4-3, 6-3, 7-3, 1-4, 7-4, 2-5, 7-5, 3-6, 5-6, 2-7],
			'ldbc-directed-example'-10-true-[1-3, 1-5, 2-4, 2-5, 2-10, 3-1, 3-5, 3-8, 3-10, 5-3, 5-4, 5-8, 6-3, 6-4, 7-4, 8-1, 9-4],
			karate-34-false-KarateBoth]),
		(	t(cached_degree(Name), (adjacency(N, Es, M), row_sums(M, Out), col_sums(M, In), gsl_matrix_free(M)), Out/In),
			(Directed == true -> Gs = Es ; Gs = Karate),
			t(cached_degree_igraph(Name), (numlist(1, N, Vs), igraph_new(Gs, G, [directed(Directed), vertices(Vs)]),
				igraph_degree(G, Ps), igraph_free(G), pairs_values(Ps, Ds)), Ds)
		)),

	% test_SampleDegree.pl samples rows with LAGraph's generator, which only every row having the same
	% degree makes predictable: its row and column means and medians for full.mtx and full_symmetric.mtx.

	forall(
		member(Name-N, [full-3, full_symmetric-4]),
		t(sample_degree(Name), (length(Row, N), maplist(=(1), Row), length(Rows, N), maplist(=(Row), Rows),
			mat_from_list(M, Rows), row_sums(M, R), col_sums(M, C), gsl_matrix_free(M),
			vec_from_list(RV, R), vec_from_list(CV, C), vec_mean(RV, RMean), vec_median(RV, RMedian),
			vec_mean(CV, CMean), vec_median(CV, CMedian)), [RMean,RMedian,CMean,CMedian])).

% The adjacency matrix of an edge list, vertices numbered from 1

adjacency(N, Es, M) :-
	gsl_matrix_calloc(N, N, M),
	forall(member(A-B, Es), (I is A - 1, J is B - 1, gsl_matrix_set(M, I, J, 1.0))).

pairs_values(Ps, Vs) :- findall(V, member(_-V, Ps), Vs).

karate([1-2, 1-3, 1-4, 1-5, 1-6, 1-7, 1-8, 1-9, 1-11, 1-12, 1-13, 1-14, 1-18, 1-20, 1-22, 1-32, 2-3, 2-4,
	2-8, 2-14, 2-18, 2-20, 2-22, 2-31, 3-4, 3-8, 3-9, 3-10, 3-14, 3-28, 3-29, 3-33, 4-8, 4-13, 4-14, 5-7,
	5-11, 6-7, 6-11, 6-17, 7-17, 9-31, 9-33, 9-34, 10-34, 14-34, 15-33, 15-34, 16-33, 16-34, 19-33, 19-34,
	20-34, 21-33, 21-34, 23-33, 23-34, 24-26, 24-28, 24-30, 24-33, 24-34, 25-26, 25-28, 25-32, 26-32,
	27-30, 27-34, 28-34, 29-32, 29-34, 30-33, 30-34, 31-33, 31-34, 32-33, 32-34, 33-34]).

% data/betweenness_matrix.txt, a dense symmetric 0/1 adjacency matrix, vertices numbered from 0. Its
% degrees from GSL's row sums and from igraph agree. lvm's lagraph_betweenness_centrality(M, all, V)
% counts each pair both ways, so gives twice igraph's figure: 17.98845443342347 for vertex 0 and
% 46.761796201734285 for vertex 44, the largest.

betweenness :-
	betweenness_matrix(Atoms),
	maplist([A,R]>>(atom_chars(A, Cs), maplist([C,D]>>atom_number(C, D), Cs, R)), Atoms, Rows),
	findall(I-J, (nth0(I, Rows, Row), nth0(J, Row, 1), I < J), Es),
	length(Rows, N),
	length(Es, NEs),
	t(betweenness_matrix_symmetric, (mat_from_list(M, Rows), gsl_matrix_alloc(N, N, T),
		gsl_matrix_transpose_memcpy(T, M, _), mat_to_list(M, L), mat_to_list(T, L)), N-NEs),
	t(betweenness_degrees_agree, (mat_from_list(M1, Rows), row_sums(M1, Rs), igraph_degree(Es, Ps),
		pairs_values(Ps, Ds), maplist([X,Y]>>(X =:= Y), Rs, Ds), sum_list(Ds, Total)), Total),
	t(betweenness, (igraph_betweenness(Es, Bs), pairs_values(Bs, Vs)), Vs),
	t(betweenness_twice_lagraph, (igraph_betweenness(Es, Bs2), memberchk(0-B0, Bs2), memberchk(44-B44, Bs2),
		abs(2*B0 - 17.98845443342347) < 1.0e-9, abs(2*B44 - 46.761796201734285) < 1.0e-9,
		pairs_values(Bs2, Vs2), max_list(Vs2, B44)), yes).

betweenness_matrix([
	'00010011110101100011110010000010111000101000111000',
	'00001100010011000000101100111110010010000000011110',
	'00001100000101110010111010011001000011101010101110',
	'10000101110011111010111001100000000101111110011111',
	'01100010011110011100100000001000110010001100000110',
	'01110011000000000001110000110011011000110000101011',
	'10001100111110000010100101111111000000011011100000',
	'10010100010000000100001110000111010110100111101011',
	'10010010000010110011110111011110111110011011010111',
	'11011011001001111001001000101011011010100000101111',
	'00001010010110010101111010100001100111110100101001',
	'10101010001011101100001111111001001001010111010100',
	'01011010101101010000001101001001010110100111111001',
	'11110000010110000001001110110010110011011010111010',
	'10110000110100001100101010001011110000110100101111',
	'00111000111010000101010011110100111000110110100000',
	'00011000010100100010011001100001000101011010001110',
	'00001001001100110001101110110001101111001110010111',
	'10110010100000001001011010011010110011110101110110',
	'10000100111001010110001111001010010010110110101001',
	'11111110101000100100011111111011001001110000001001',
	'10110100101000011010100101101001011111100011110110',
	'01110001011111101111100111100110001001001010110100',
	'01000011100111000101111001101100010111011111001001',
	'10100001101101110111101000101110100010000010011100',
	'00010010100110011001111100110001111110100000100000',
	'01010110011101011100111111011111100100111110101001',
	'01100110100101010110100001100110010001100000110101',
	'01101010110110100011110110100110111100101000001111',
	'01000011100000010000001110111000010000011011100111',
	'11000111110001100011101010111000010011011000111100',
	'00100111011110101100110001100000000011110010101001',
	'10001000101001110110000011101000001110010101010100',
	'11001101110011110011010101011110001001101010110110',
	'10000100110100010100111001001000110001010000001010',
	'00010001101010001100010101101000100000100111111001',
	'01101001111011000111010111000011100000000110111000',
	'00110000001101001110111100010011011000010010101111',
	'10110101011010110011110001111001010100011010101001',
	'00010110101101111011100100100111101001100000110011',
	'10111010100001001100001100101110010000100111001001',
	'00011001001110110111000100100000100110001001100010',
	'00110011100111011101011110100101010111101000101111',
	'00000011100110000010010100000100100100001100101110',
	'10100111011011110011011001110111010111110111001101',
	'11010000100111000110011010010010110110010000000000',
	'11110101011011101001100110101011001111101011100001',
	'01111000110100101110011010011110110001000011100010',
	'01111101110001101110010000001100011001010111000101',
	'00010101111010100101100100111101000101111010101010']).

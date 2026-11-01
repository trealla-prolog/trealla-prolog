% Bigint arithmetic at imath digit boundaries, where 32- and 64-bit digits differ.
%
%   Native builds use 64-bit digits and wasm32 still uses 32-bit ones, so CI runs this
%   file at both widths. The answers come from Python and agree with SWI.
%
%   With 64-bit digits a one-digit divisor in [2^63, 2^64) handed its remainder back
%   through a signed mp_small, so a remainder >= 2^63 came out as 2^64 - r; and a
%   bigint just above 2^64 rounded to float twice.
%
%   Older bugs the same sweep found: rem and div were built on mp_int_mod, so rem took
%   the divisor's sign and div rounded the wrong way for a negative divisor; mod by a
%   negative divisor that divides exactly gave the divisor; a bigint mod, rem or div by
%   zero gave the dividend; AND dropped the high digits a negative operand sign-extends
%   into; OR of two negatives came out positive; lsb gave 0 at a digit boundary; gcd
%   could be negative; abs, negation and div -1 of INT64_MIN wrapped; and a small-int div
%   overflowed near INT64_MAX by subtracting the floor-mod first.

:- initialization(main).

val(E, V) :- ( catch(V is E, error(Err,_), V = err(Err)) -> true ; V = failed ).

t(1287938064824998671889116781442 rem 12814412096765332567, 11259086703122017256).
t(1287938064824998671889116781442 mod 12814412096765332567, 11259086703122017256).
t((-1287938064824998671889116781442) rem 12814412096765332567, -11259086703122017256).
t((-1287938064824998671889116781442) mod 12814412096765332567, 1555325393643315311).
t(2^100 mod (2^64-59), 4054449127424).
t((2^128-1) rem (2^64-1), 0).
t(-(2^80) rem -5, -1).
t(-(2^70) rem 2^80, -1180591620717411303424).
t(2^70 rem -(2^80), 1180591620717411303424).
t(2^70 div -(2^80), -1).
t(-(2^70) div -(2^80), 0).
t(-(2^80) div -(2^70), 1024).
t((2^80+1) div -(2^70), -1025).
t(9223372036854775807 div -7487893059873143224, -2).
t(9223372036854775807 div -1218379537, -7570196115).
t((-9223372036854775807) div 133, -69348661931238916).
t(2^95 mod -(2^63), 0).
t(-(2^95) mod -(2^63), 0).
t(2^95 mod -(2^63+1), -4294967296).
t(2^80 mod 0, err(evaluation_error(zero_divisor))).
t(2^80 rem 0, err(evaluation_error(zero_divisor))).
t(2^80 div 0, err(evaluation_error(zero_divisor))).
t(2^80 // 0, err(evaluation_error(zero_divisor))).
t((-1) /\ 2^200, 1606938044258990275541962092341162602522202993782792835301376).
t((-(2^64)) /\ (2^200+5), 1606938044258990275541962092341162602522202993782792835301376).
t((-(2^64)) /\ (-(2^130)), -1361129467683753853853498429727072845824).
t((-(2^64)) \/ (-(2^70)), -18446744073709551616).
t((-(2^70)) \/ 5, -1180591620717411303419).
t(xor(-(2^64), 2^70), -1199038364791120855040).
t(lsb(2^64), 64).
t(lsb(3*2^64), 64).
t(lsb(2^96), 96).
t(lsb(2^128), 128).
t(msb(2^128), 128).
t(float(27670116110564333567), 2.767011611056433e19).
t(float(18446744073709551617), 1.8446744073709552e19).
t(float(27670116110564329472), 2.7670116110564327e19).
t(float(36893488147419103231), 3.6893488147419103e19).
t(gcd(4, -6), 2).
t(gcd(-4, 6), 2).
t(gcd(-4, -6), 2).
t(gcd(-(2^62)*2, 0), 9223372036854775808).
t(gcd(-(2^62)*2, -1), 1).
t(abs(-(2^62)*2), 9223372036854775808).
t(-(-(2^62)*2), 9223372036854775808).
t((-(2^62)*2) div -1, 9223372036854775808).
t((-(2^62)*2) // -1, 9223372036854775808).
t((-(2^62)*2) rem -1, 0).
t((-(2^62)*2) mod -1, 0).

main :-
	findall(E-W-V, (t(E, W), val(E, V), V \== W), Bad),
	(	Bad == [], 27670116110564333567 =:= 2.767011611056433e19
	->	format("bigint_digit_edges: ok~n")
	;	forall(member(E-W-V, Bad), format("~q: got ~q wanted ~q~n", [E, V, W]))
	).

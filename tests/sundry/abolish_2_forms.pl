% abolish/2 takes DEC-10's abolish(Name, Arity), as SWI and XSB do, beside
% SICStus's abolish(PI, Options), where force(true) takes static predicates
% too. Name//Arity is Name/Arity+2.

:- initialization(main).

:- dynamic(dy/1).
dy(1).
:- dynamic(dz/3).
dz(1,2,3).
st(1).
st(2).
:- dynamic(dg/2).
dg(a,b).

main :-
	check(dec10, (abolish(dy, 1), \+ catch(dy(_), _, fail))),
	throws(dec10_static, abolish(st, 1)),
	throws(dec10_builtin, abolish(atom_length, 2)),
	throws(dec10_var_arity, abolish(dy, _)),
	throws(dec10_bad_arity, abolish(dy, a)),
	throws(dec10_bad_name, abolish(3, 1)),
	check(options_none, abolish(nothing/1, [])),
	throws(options_static, abolish(st/1, [])),
	check(force, (abolish(st/1, [force(true)]), \+ catch(st(_), _, fail))),
	throws(force_builtin, abolish(atom_length/2, [force(true)])),
	throws(force_library, abolish(writeln/1, [force(true)])),
	throws(force_bad_value, abolish(nothing/1, [force(maybe)])),
	throws(tree_unsupported, abolish(nothing/1, [tree(true)])),
	throws(options_not_list, abolish(nothing/1, foo)),
	check(dcg_indicator, (abolish(dg//0), \+ catch(dg(_,_), _, fail))),
	check(dcg_indicator2, (abolish(dz//1, []), \+ catch(dz(_,_,_), _, fail))),
	throws(dcg_negative, abolish(dg//(-1))).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), format("~w threw ~q~n", [Name,E])).

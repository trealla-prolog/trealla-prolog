#!/usr/bin/env python3
# Checks tnot/1 against XSB's own negation tests (wfs_tests, neg_tests,
# delay_tests): exit status 1 on any answer different from XSB's. An
# atom XSB finds false but tnot/1 leaves undefined is counted apart, as
# incomplete rather than wrong: answer completion, stage C of
# DESIGN-tabling-phase2.md item 6, is what decides those.
#
#   util/xsb_tnot_check.py [XSBTESTS] [--tpl PATH] [-v]
#
# XSBTESTS defaults to ~/xsb-code/xsbtests. The programs are translated
# from XSB's dialect on the way in: :- import/export/index dropped,
# :- auto_table expanded into a :- table per defined predicate,
# sk_not/1 read as tnot/1 (both existential) and assert/1 as assertz/1.
# Programs inspecting residuals (get_residual/2 and the like) need stage
# D and end as other. Where XSB itself disagrees with SWI, the reference, KNOWN below names
# the exact lines, so any other change still counts as wrong.

import argparse
import os
import re
import subprocess
import sys
import tempfile

# wfs_tests: query(Name, Goal, Subgoals, Trues, Undefineds) gives each
# subgoal's expected value. Checked from fresh tables and cumulatively.
QUERY_HARNESS = r"""
:- use_module(library(tabling)).
:- use_module(library(lists)).
:- op(900, fy, tnot).

run(File) :-
	consult(File),
	query(Name, _, Subgoals, Trues, Undefs),
	forall(member(S, Subgoals), check(fresh, Name, S, Trues, Undefs)),
	abolish_all_tables,
	forall(member(S, Subgoals), check(cumulative, Name, S, Trues, Undefs)).

check(Mode, Name, S, Trues, Undefs) :-
	(  Mode == fresh -> abolish_all_tables ; true ),
	(  member(T, Trues), T =@= S -> E = true
	;  member(U, Undefs), U =@= S -> E = undefined
	;  E = false
	),
	catch((call_delays(S, D) -> (D == true -> G = true ; G = undefined) ; G = false), Err, G = error(Err)),
	classify(E, G, C),
	write(C), write(' '), write(Name), write(' '), write(Mode), write(' '),
	writeq(S), write(' expected='), write(E), write(' got='), writeq(G), nl.

classify(E, E, ok) :- !.
classify(false, undefined, incomplete) :- !.
classify(_, error(error(permission_error(tnot, incomplete_table, _), _)), refused) :- !.
classify(_, error(_), other) :- !.
classify(_, _, wrong).
"""

# neg_tests, delay_tests: test/0 (tabsimp_seq: test(20000)) prints
# lines compared, sorted, with the program's _old file.
TEST_HARNESS = r"""
:- use_module(library(tabling)).
:- use_module(library(lists)).
:- op(900, fy, tnot).

go(File) :-
	consult(File),
	(  current_predicate(test/0) -> Goal = test ; Goal = test(20000) ),
	catch(Goal, E, true),
	(  var(E) -> true
	;  E = error(permission_error(tnot, incomplete_table, _), _) ->
	   write('@@REFUSED'), nl
	;  print_message(error, E), write('@@OTHER'), nl
	).
"""

AUTOTABLE = r"""
:- op(900, fy, tnot).

pis(File) :-
	open(File, read, S),
	read_term(S, T, []),
	pis_(T, S, [], PIs0),
	close(S),
	sort(PIs0, PIs),
	forall((member(N/A, PIs), N/A \== test/0), format(":- table ~q.~n", [N/A])).

pis_(end_of_file, _, Acc, Acc) :- !.
pis_(T, S, Acc0, Acc) :-
	(  T = (:- _) -> Acc1 = Acc0
	;  (T = (H :- _) -> true ; H = T), functor(H, N, A), Acc1 = [N/A|Acc0]
	),
	read_term(S, T1, []),
	pis_(T1, S, Acc1, Acc).
"""


def tpl_run(args, tpl, timeout=10):
	try:
		p = subprocess.run([tpl, '-q'] + args, capture_output=True, text=True, errors='replace', timeout=timeout)
		return p.returncode, p.stdout, p.stderr
	except subprocess.TimeoutExpired:
		return None, '', ''


def translate(src, dst, tmp, tpl):
	out = []
	auto = False

	with open(src, errors='replace') as f:
		for line in f:
			if re.match(r':-\s*(import|export|index)\b', line):
				continue

			if re.match(r':-\s*auto_table\b', line):
				auto = True
				continue

			line = re.sub(r'\bsk_not\(', 'tnot(', line)
			line = re.sub(r'\bassert\(', 'assertz(', line)
			out.append(line)

	body = ''.join(out)

	if auto:
		bodyfile = os.path.join(tmp, 'body.pl')

		with open(bodyfile, 'w') as f:
			f.write(body)

		_, decls, _ = tpl_run([os.path.join(tmp, 'autotable.pl'), '-g', f"pis('{bodyfile}'),halt"], tpl)
		body = decls + body

	with open(dst, 'w') as f:
		f.write(body)


def check_wfs(suite, tmp, tpl, verbose, bad):
	tally = {}

	# Only the generated pNN.P tests: wfsexs.P, their source, holds every
	# query/5 too but its programs only as pgm/2 data.
	for name in sorted(os.listdir(suite)):
		src = os.path.join(suite, name)

		if not re.fullmatch(r'p\d+[a-z]?\.P', name):
			continue

		dst = os.path.join(tmp, name[:-2] + '.pl')
		translate(src, dst, tmp, tpl)
		rc, out, _ = tpl_run([os.path.join(tmp, 'query.pl'), '-g', f"run('{dst}'),halt"], tpl)

		if rc is None:
			tally['timeout'] = tally.get('timeout', 0) + 1
			bad.append(f'timeout wfs_tests/{name}')
			continue

		checks = 0

		for line in out.splitlines():
			kind = line.split(' ', 1)[0]

			if kind not in ('ok', 'refused', 'wrong', 'other', 'incomplete'):
				continue

			checks += 1
			tally[kind] = tally.get(kind, 0) + 1

			if kind in ('wrong', 'other', 'incomplete') or verbose:
				bad.append(f'{line}')

		# A program that never reached its checks must not just vanish.

		if not checks:
			tally['silent'] = tally.get('silent', 0) + 1
			bad.append(f'silent wfs_tests/{name}')

	return tally


# Programs where XSB's output differs from SWI's, which tnot/1 follows:
# the lines tnot/1 adds and drops, variables written _.
KNOWN = {
	# t(4) is undefined like t(5) and t(6), which XSB does list; SWI and
	# the program's own comment have all three.
	'neg_tests/ullman3.P': ({'t(4)'}, set()),
	# s(X, dallas) with X unbound: tnot/1 is existential there, as in SWI,
	# so s(_, dallas) is undefined; XSB leaves it out.
	'delay_tests/pot_pouri.P': ({'s(_,dallas)'}, set()),
}

# XSB's _old files have CRLF endings. A line saying an atom is undefined
# where XSB's says false is incomplete, not wrong (see the top).

def compare_output(name, out, old):
	unvar = lambda l: re.sub(r'\b_\w*', '_', l.rstrip('\r').rstrip())
	got = sorted(unvar(l) for l in out.splitlines())
	want = sorted(unvar(l) for l in old.splitlines())

	if got == want:
		return 'ok'

	if name in KNOWN:
		extra, missing = KNOWN[name]
		g, w = set(got), set(want)

		if (g - w == extra) and (w - g == missing):
			return 'xsb-differs'

	norm = lambda ls: sorted(re.sub(r' is (undefined|false)\b.*$', ' is false', l) for l in ls)

	if norm(got) == norm(want) and len(got) == len(want):
		return 'incomplete'

	return 'DIFF'


# Some programs define between/3 or reverse/2 themselves; Trealla warns.

def benign_removed(err):
	return '\n'.join(l for l in err.splitlines() if not l.startswith('Warning: overwriting')).strip()


def check_tests(suite, tmp, tpl, verbose, bad):
	tally = {}
	label = os.path.basename(suite.rstrip('/'))

	for name in sorted(os.listdir(suite)):
		base = name[:-2]
		old = os.path.join(suite, base + '_old')

		if not name.endswith('.P') or not os.path.exists(old):
			continue

		dst = os.path.join(tmp, base + '.pl')
		translate(os.path.join(suite, name), dst, tmp, tpl)
		rc, out, err = tpl_run([os.path.join(tmp, 'test.pl'), '-g', f"go('{dst}'),halt"], tpl)

		if rc is None:
			kind = 'timeout'
		elif '@@REFUSED' in out:
			kind = 'refused'
		elif '@@OTHER' in out or benign_removed(err):
			kind = 'other'
		else:
			kind = compare_output(f'{label}/{name}', out, open(old, errors='replace').read())

		tally[kind] = tally.get(kind, 0) + 1

		if kind in ('DIFF', 'timeout', 'other', 'incomplete') or verbose:
			bad.append(f'{kind} {label}/{name}')

	return tally


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument('xsbtests', nargs='?', default=os.path.expanduser('~/xsb-code/xsbtests'))
	ap.add_argument('--tpl', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tpl'))
	ap.add_argument('-v', '--verbose', action='store_true')
	args = ap.parse_args()

	with tempfile.TemporaryDirectory() as tmp:
		for fname, text in (('query.pl', QUERY_HARNESS), ('test.pl', TEST_HARNESS), ('autotable.pl', AUTOTABLE)):
			with open(os.path.join(tmp, fname), 'w') as f:
				f.write(text)

		bad = []
		results = [
			('wfs_tests (subgoal checks)', check_wfs(os.path.join(args.xsbtests, 'wfs_tests'), tmp, args.tpl, args.verbose, bad)),
			('neg_tests (programs)', check_tests(os.path.join(args.xsbtests, 'neg_tests'), tmp, args.tpl, args.verbose, bad)),
			('delay_tests (programs)', check_tests(os.path.join(args.xsbtests, 'delay_tests'), tmp, args.tpl, args.verbose, bad)),
		]

	for line in bad:
		print(line)

	for label, tally in results:
		print(f'{label}: ' + ', '.join(f'{v} {k}' for k, v in sorted(tally.items())))

	wrong = sum(t.get('wrong', 0) + t.get('DIFF', 0) for _, t in results)
	print(f'wrong answers: {wrong}')
	return 1 if wrong else 0


if __name__ == '__main__':
	sys.exit(main())

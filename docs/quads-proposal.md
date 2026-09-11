# Proposal: Quad support in Trealla (issue #1063)

Quads are `?- Query.` terms embedded in source files, followed by answer-description
terms documenting the expected toplevel answers. Markus proposes two stages:
(1) parse and ignore them correctly, (2) interpret them as embedded tests.

## 1. Current state (HEAD e187304)

`directives()` in `parser.c` recognises `?-`/1 and sets `p->is_quad`:

```c
if (!strcmp(C_STR(p, d), "?-")) {
    p->is_quad = true;
    return true;
}
```

The flag is consumed in the `tokenize()` end-of-term block (~line 3847): when set,
the *next* term is discarded before `process_clause()` runs. Testing this build
shows three problems with the current "ignore" behaviour:

**a. The query term is asserted into the database.** `directives()` returns `true`
for `?-`, but `process_term()` ignores the return value and falls through to
`assertz_to_db()`. Unlike `:-` terms (which are renamed to `$directive` first),
the `?-` term is asserted verbatim, creating a static `?-`/1 predicate:

```
$ tpl consulted-quad-file.pl
?- predicate_property('?-'(_), P).
   P = static.
```

**b. Exactly one following term is skipped, unconditionally.** The skip is not
form-based, so two consecutive queries break — the second query is swallowed as
if it were the first one's answer, and its real answer term is then processed
as a clause:

```
?- true.          % sets is_quad
?- foo(X).        % DISCARDED (consumed by is_quad)
   X = bar.       % processed as a clause:
                  % "Error: permission error modifying user:(=)/2"
```

Conversely a query with a *multi-term* trailing annotation, or a stray query
with no answer at all, mis-aligns everything after it.

**c. Answer terms still get singleton warnings** (e.g. `X = bar.` warns on `X`),
which will be noisy across a large quad suite.

## 2. Stage 1 — ignore quads *correctly*

Model it on `conditionals()`: a dedicated function called first from
`process_term()`, with its state on the **module** (like `if_depth` /
`ifs_blocked`), since nested parsers are created per-term by `expand_term()`
and `p` flags don't survive reliably; the parser-level `is_quad` bit and the
skip block at line 3847 go away.

### 2.1 New module state (`internal.h`)

```c
struct module_ {
    ...
    clause *quad_query;        // copy of pending ?- Query (stage 2)
    unsigned quad_line_num;    // where the query appeared
    bool in_quad:1;            // saw ?- Q., now consuming answer terms
    ...
};
```

### 2.2 Answer-description recogniser

A small recursive shape check over cells, straight from the grammar in the
issue:

```c
static bool is_answer_description(module *m, cell *c)
{
    if (is_var(c)) return false;

    if (!c->arity)
        return !CMP_STRING_TO_CSTR2(m, c, "true")
            || !CMP_STRING_TO_CSTR2(m, c, "false")
            || !CMP_STRING_TO_CSTR2(m, c, "...")
            || !CMP_STRING_TO_CSTR2(m, c, "loops")
            || !CMP_STRING_TO_CSTR2(m, c, "instantiation_error")
            || !CMP_STRING_TO_CSTR2(m, c, "ad_infinitum")
            || !CMP_STRING_TO_CSTR2(m, c, "sto")
            || !CMP_STRING_TO_CSTR2(m, c, "unexpected");

    if (c->arity == 2) {
        if (!CMP_STRING_TO_CSTR2(m, c, "=")) return is_var(FIRST_ARG(c));
        if (!CMP_STRING_TO_CSTR2(m, c, ",")
         || !CMP_STRING_TO_CSTR2(m, c, ";")
         || !CMP_STRING_TO_CSTR2(m, c, "|"))
            return is_answer_description(m, FIRST_ARG(c))
                && is_answer_description(m, NEXT_ARG(FIRST_ARG(c)));
        if (!CMP_STRING_TO_CSTR2(m, c, "error")
         || !CMP_STRING_TO_CSTR2(m, c, "type_error")
         || !CMP_STRING_TO_CSTR2(m, c, "domain_error"))
            return true;
    }

    if (c->arity == 1)
        return !CMP_STRING_TO_CSTR2(m, c, "throw")
            || !CMP_STRING_TO_CSTR2(m, c, "syntax_error")
            || !CMP_STRING_TO_CSTR2(m, c, "representation_error")
            || !CMP_STRING_TO_CSTR2(m, c, "resource_error")
            || !CMP_STRING_TO_CSTR2(m, c, "uninstantiation_error");

    return false;
}
```

### 2.3 `quads()` hook, mirroring `conditionals()`

```c
static bool quads(parser *p, cell *d)
{
    // A new query, whether or not one was pending
    if (is_interned(d) && (d->arity == 1)
        && !strcmp(C_STR(p, d), "?-")) {
        p->m->in_quad = true;
        p->m->quad_line_num = p->line_num;
        // stage 2: stash a copy of FIRST_ARG(d) + vartab names here
        return true;                       // consume: do NOT assert
    }

    // Terms following a query: consume while they look like answers
    if (p->m->in_quad) {
        if (is_answer_description(p->m, d)) {
            // stage 2: run the check here
            return true;                   // consume
        }
        p->m->in_quad = false;             // ordinary term resumes normal load
    }

    return false;
}

static bool process_term(parser *p, cell *p1)
{
    if (conditionals(p, p1))
        return true;

    if (p->m->ifs_blocked[p->m->if_depth])
        return true;

    if (quads(p, p1))                      // NEW
        return true;
    ...
}
```

Then delete the `?-` branch from `directives()`, the `is_quad` bit from
`struct parser_`, and the skip block at ~3847.

This fixes (a) — nothing asserted; (b) — skipping is form-based, so
consecutive queries, missing answers, and multiple answer terms all work; the
`ignore_vars`/singleton noise (c) can be handled by suppressing the singleton
check when `m->in_quad` was active for the term (small extra flag, optional).

One subtlety: the answer term still passes through `check_body_callable()` and
`term_expansion()` before `process_term()` is reached. `,`/`;`/`=` terms are
callable so the check is harmless, but user `term_expansion/2` could rewrite
an answer term. Acceptable for stage 1; stage 2 could move quad detection
earlier if it ever bites.

## 3. Stage 2 — interpret quads as tests

The parser stays purely syntactic and executes **nothing** at load time
(directives in Trealla are not run as goals, and quads shouldn't be either).
Instead the parser *records* each quad as data, and a library predicate runs
the recorded quads on demand, after the file is fully loaded — ordinary Prolog
at run time, not parse time. This also fixes forward references: a quad may
test a predicate defined further down the file.

### 3.1 Recording

In `quads()`, instead of just consuming the terms, pair the query with its
answer description and assert (via the same path `$directive` terms take):

```prolog
'$quad'(Query, QueryVarNames, AnswerDesc, AnswerVarNames, 'file.pl':Line).
```

The var-name lists (`['X'=V1, ...]`) are built from `p->vartab`
(`vartab.pool` / `vartab.off`, the same data the singleton warning uses).
Because the query and the answer description are *separate terms*, their
variables can only be related **by name** — the two name lists are what ties
`X` in `?- member(X,...)` to `X` in `X = 1 ; ...`.

The C-side cost over stage 1 is only: keep a copy of the pending query clause
in `m->quad_query` (as `expand_term()` already does with `dup_cells`), and on
seeing the answer term, assert the combined `'$quad'/5` fact.

### 3.2 Running: `library/quads.pl`

A new embedded library (added to `library.c` like the others). As
shipped it exports:

```prolog
quads:run_quads/0      % run the quads recorded in module user
quads:run_quads/1      % run the quads recorded in the given module
quads:run_quads_halt/0 % as run_quads/0, then halt(1) if any failed
```

(The names first proposed were `run_all/0` and a `run/1` selecting quads
by the predicate indicator their query calls; the latter was not built,
a module being selection enough so far.)

`run_quads/1` iterates the `'$quad'/6` facts and for each:

1. Unifies same-named variables across the two name lists.
2. Normalises the answer description: split on `'|'` into acceptable
   alternatives; split each on `;` into the expected solution sequence;
   expand error shorthands (`instantiation_error` →
   `error(instantiation_error,_)`, etc.); note trailing `...` /
   `ad_infinitum`, `sto`, `unexpected` annotations.
3. Asks for the answer each description describes rather than
   enumerating and rendering all of them, so no solution cap is needed:
   the Nth description is checked against `call_nth(Query, N)`, and
   after the last one a further answer must not exist. `catch/3` turns
   a thrown ball into an outcome to match, and `loops` is approximated
   with `call_with_time_limit/2` (already in `library/iso_ext.pl`); an
   inference-limit builtin can replace it later.
4. Compares actual vs expected — the bindings of the query's named
   variables against those the description gives, as variants (§8) —
   and on mismatch prints

   ```
   quads: FAILED member_2, tests/misc/quads.pl:126
      ?- member(X,[1,2]).
      expected: X=1;X=99
   ```
5. Finally prints `quads: N run, P passed, F failed.`, and records F so
   that `run_quads_halt/0` can exit on it.

Matching semantics can start strict (`==` on rendered bindings modulo variable
renaming) and grow toward the full Flowlog semantics incrementally — the value
is that all of this lives in Prolog and never touches the parser again.
`library(quads)` now matches solutions with the same `ball_matches/3` walk
used for error balls, so `...` as an unspecified subterm works in bindings
as well (issue #1088; see §8).

### 3.3 Invoking

- Interactively, any time after loading (the `'$quad'/6` facts persist):
  `?- use_module(library(quads)), run_quads.`
- Batch, exiting non-zero if any quad failed:
  `tpl file.pl -g 'use_module(library(quads)), run_quads_halt'`.
  A dedicated `tpl --quads file.pl` option was considered and not added:
  the goal above needs no wrapper either, and keeping the runner out of
  the executable's option table keeps quads a library concern.
- Recording is always on; a `'$quad'/6` fact per test is cheap, and it
  means the REPL can run any consulted file's quads without reloading.

### 3.4 Suggested split into PRs

1. **PR 1 (stage 1):** `quads()` + `is_answer_description()`, remove
   `p->is_quad`, module state, tests for the mis-alignment cases above.
   Small, no behaviour change for non-quad files, unblocks writing quads today.
2. **PR 2:** record `'$quad'/5` facts (query copy + var-name lists).
3. **PR 3:** `library/quads.pl` runner/matcher, `--quads` exit-code, then run
   Flowlog's `tests/ulrich/*_quad.pl` and the Prologue quads as the
   acceptance suite (`make quad-tpl` from the Flowlog repo already exists as
   a cross-check).

## 4. Test cases for stage 1

```prolog
% consecutive queries — second must not be swallowed
?- true.
?- foo(X).
   X = bar.
foo(bar).                 % must load; '?-'/1 must NOT exist

% multi-solution answer, one term
?- member(X,[1,2,3]).
   X = 1
;  X = 2
;  X = 3.

% error description
?- atom_length(A, L).
   error(instantiation_error, _).

% quad followed by directive — directive must run
?- foo(bar).
   true.
:- initialization(main).
```

## 5. Open questions

- Should a pending `in_quad` at `end_of_file` warn (query with no answer
  description)? Yes, and it does: `Warning: quad query without answer
  description, file.pl:2`.
- `X = 1, Y = 2` answers use `,`/2 which is also a valid clause body — the
  form-based skip therefore only treats it as an answer when directly
  following a query, which the design above already does; a bare `true.`
  fact after a quad would also be eaten. In practice quad files don't do
  this, and the issue's spec explicitly accepts it.
- Whether quad queries under `:- if(false)` should be skipped: yes for free,
  since `process_term()` checks `ifs_blocked` before `quads()` — but note the
  answer terms too must be skipped while blocked (they are, same check).
- `sto`/`unexpected`/`ad_infinitum` handling can be stubbed (treated as
  "any outcome accepted") in the first matcher version. `unexpected` is
  now interpreted (§3.2); `sto` is checked (§16); `ad_infinitum` accepts any
  further answers, exactly as `...` does, without checking that there
  are infinitely many (§12).

## 6. Answer *substitutions* (issue #1074)

A toplevel answer reports an answer substitution, which the shape check
above did not enforce: any `=`/2 was accepted, so

```prolog
?- X = 1.
   1 = X.               % not an answer substitution
```

was consumed as a description, and — since the matcher applied the
equation with plain unification — bound `X` to 1 and *passed*. The same
hole admits `X = 1, X = 2`, which binds one variable twice.

Two properties are therefore required of every equation in a
description: the left side is a variable, and no variable is bound twice
within one answer. Alternatives separated by `;` or `|` are separate
answers, so each starts with a fresh set of bound variables.

Rejecting such a term by simply returning "not an answer description"
would let it fall through to ordinary clause loading, where it surfaces
as `permission error modifying user:(=)/2` — accurate but unhelpful.
Nothing else could be meant by an equation directly after a query, so
the recogniser distinguishes *not an answer description* from
*malformed*, and the loader reports the latter the way it reports a
syntax error:

```
Error: malformed answer description, file.pl:9
```

`library/quads.pl` repeats the check, since a `'$quad'` fact can also be
asserted by hand, and reports a malformed description as such rather
than running it as a test that would quietly pass.

## 7. Labelled quads (issue #1071)

A quad may be identified by a ground term, which requires `?-` to be an
infix operator as well as a prefix one:

```prolog
member_1 ?- member(X, [1,2,3]).
   X = 1
;  X = 2
;  X = 3.
```

`{"?-", OP_XFX, 1200}` joins the default table next to the existing
prefix entry, exactly as `:-` is already both `xfx` and `fx`. Being an
operator does not by itself make the term a quad: `quads()` accepts
`?-`/2 alongside `?-`/1, taking the first argument as the label and the
second as the query, so nothing is added to the database and the
following answer description is still consumed rather than loaded as a
clause.

The label identifies a query, so it has to be ground; a variable there
is reported the way a malformed answer description is:

```
Error: quad identifier is not ground, file.pl:4
```

Because the label is ground it shares no variables with the answer
description, and the `VarNames` machinery is unaffected.

### 7.1 Recording

The recorded fact gains the label as its first argument:

```prolog
'$quad'(Id, Query, VarNames, AnswerDescription, File, Line).
```

`Id` is an unbound variable for a quad written with the prefix operator,
so "this quad has no label" needs no reserved atom, and a report can
test it with `var/1`. Reports name the quad when it has a label:

```
quads: FAILED member_2, tests/misc/quads.pl:126
```

One consequence of recording quads as data: the first quad in a file
used to make `'$quad'/6` static, so a program could not add one of its
own. `quad_record()` now declares it dynamic, which is what lets the
library's own shape checking be tested on hand-written facts.

## 8. `...` in answer substitutions (issue #1088)

Trailing `...` already means “further answers are accepted”
(`check_solutions/7`, §12). The same atom also stands for an
*unspecified subterm* inside a binding or an error ball — the English
“…” of ISO answer descriptions:

```prolog
?- X = 1.
   X = ... .

?- length(L, 999).
   L = [_A,_B,_C|...].
```

No parser change is required: `answer_description` accepts any right-hand
side of `=`/2 once the left side is a variable (§6), so `X = ...` and
`L = [_A,_B,_C|...]` are already well-formed descriptions. The gap was
only in the matcher, which compared solution witnesses with `variant/2`
and therefore demanded a literal `...` in the actual answer.

`attempt_match/5` for `solution(Items)` now uses `ball_matches/3` — the
same predicate that already treats `P == ...` as matching any subterm
when checking thrown balls — so a description binding may leave structure
unspecified without weakening the one-to-one variable correspondence that
`variant/2` enforced (issue #1080 / #1067). A different functor still
fails (`X = f(...)` does not describe `X = 1`).

Compound walking in `ball_match/5` uses `functor/3` and `arg/3` rather
than `(=..)/2`: univ on a list whose elements share variables did not
decompose reliably in this recursive match, which made complete
descriptions such as `X = f(Y,Y), Z = Y` fail after the switch.

## 9. `...` needs no quotes (issue #1086)

The answer-description atom `...` is a graphic atom, not an operator.
Quoting it as `'...'` in `library/quads.pl` was unnecessary (and
suggestive of a system that had declared `...` as an operator, which
would also break `write_term/2`’s `max_depth` ellipsis). Code and
comments use the bare atom; parentheses are enough if a reader wants
to stress that it is a term.

## 10. Singleton on loading `library(quads)` (issue #1085)

`report/8` still took the module argument after reports stopped printing
`M:Query` (plain `?- Query.`). The unused `M` warned as a singleton on
`use_module(library(quads))`, with a line number that landed near
`link_names/1`. The module argument was dropped; reporting does not need
it.

## 11. Principal functors identify answer descriptions (issue #1087)

An unknown atom alone after a query is not an answer description:

```prolog
?- Y = 2.
   some_unknown_stuff.     % ordinary term; warn, resume loading
```

But once the principal functor is already one that marks a description
— `','/2`, `';'/2`, `'|'/2`, or `'='/2` as in §6 — the whole term *is*
an answer description. An unknown conjunct makes it malformed, not
“not a description”:

```prolog
?- Y = 2.
   Y = 2, some_unknown_stuff.
```

Previously the recogniser returned “no” for the unknown leaf and
propagated that out of `','/2`, which warned `quad query without answer
description` and fell through to `permission error modifying
user:(,)/2`. The fix elevates a non-description subterm to malformed
when it occurs under a description constructor, so the term is consumed
and `library(quads)` reports `not an answer: some_unknown_stuff` like
any other malformed case (§6 / #1078).

## 12. “Further answers” as an annotation

`...` and `ad_infinitum` both say that whatever answers follow the ones
described are accepted, the latter that there are infinitely many of
them; neither is checked beyond that, as §5 allows. `ad_infinitum` used
to be recognised by the parser and by the library's shape check without
`check_solutions/7` interpreting it, so it was compared as if it were an
ordinary answer and any quad using it failed:

```prolog
?- repeat.
   true
;  ad_infinitum.        % failed, where 'true ; ... .' passed
```

Both are now dropped by `drop_more/3` the way `unexpected` and `sto` are
dropped by `drop_annotation/4`, which also means they are recognised
wherever they occur in the conjunction rather than only as the first
conjunct of an alternative of their own:

```prolog
?- member(X, [1,2,3]).
   X = 1, ... .         % X = 1 is checked; further answers accepted
```

The answer described alongside one of them still has to hold, so
`X = 9, ... .` fails.

Relatedly, `run_quads/1` records the failure count on every run, one
over a module with no quads included. Leaving the previous count in the
blackboard made `run_quads_halt/0` exit non-zero after reporting
`quads: nothing to run.`

## 13. `maybe` for pending constraints (issue #1128)

CLP(R) answered `true`, `false`, or, for a non-linear equation it could
not decide, `maybe` — the substitutions found so far still apply, but
something is left unresolved. UWN asked for the same idea here:

```prolog
?- dif(X, Y), X = a.
   X = a, maybe.
```

`maybe` says only that *some* variable the answer describes is still
attributed once the query has answered — a constraint pending on it,
of any attribute module (`dif`, `clpz`, or a user's own), not resolved
into an ordinary binding. It does not say which variable or which
module, matching the request that it work for any attribute, not just
`dif`.

Grammar-wise `maybe` joins the arity-0 atoms in `answer_description()`
(`parser.c`) and `answer_atom/1` (`library/quads.pl`) next to `sto`,
`unexpected`, and the rest — so `maybe.` alone, or `X = a, maybe.` as a
trailing conjunct, are both well-formed descriptions.

Unlike `unexpected`/`sto`/`...`/`ad_infinitum`, `maybe` is not stripped
out by `drop_annotation/4` as a control annotation: it is itself part
of what the answer asserts, checked rather than discarded.

**First cut, and why it wasn't enough.** The check originally sat in
`attempt_match/5`, right after `call_nth/2` binds the query's witness
copy `W1`: `term_variables(W1, Vs)`, then `'$attributed_var'/1` of
each. That covers `dif(X, Y), X = a` — `Y` is one of the query's own
variables — but UWN's follow-up broke it:

```prolog
ffalse :- freeze(_, false).

?- ffalse.
   maybe.
   true, unexpected.
```

`freeze(X, Goal) :- put_atts(Fresh, frozen(Goal)), Fresh = X.` puts the
attribute on `Fresh`, a variable local to `freeze/2` itself — `ffalse`
has no variables at all, so the witness is `[]` and `some_attributed/1`
never had anything to look at. Both alternatives above failed: `maybe`
for the obvious reason, and `true, unexpected` because `solution([true])`
matches vacuously against an empty witness regardless of what is
pending, so asserting `true` as forbidden had nothing to reject either.

**Fix: track everything the query attributed, not just its own
variables.** `library/builtins.pl` already has the right primitive —
`call_residue_vars/2` marks the trail with `'$mark_start'/1`, runs the
goal, and lists what got attributed since with `'$list_attributed'/2`,
which is also how the toplevel itself finds what residual goals to
print. `attempt_match/5` now marks before `call_nth/2` and checks that
list instead of the witness:

```prolog
'$mark_start'(Mark),
call_nth(M:Q1, N),
( memberchk(maybe, Items) -> some_attributed(Mark) ; \+ some_attributed(Mark) ),
```

This finds `freeze/2`'s pending goal regardless of which variable it
sits on, needing no new C code beyond what `call_residue_vars/2`
already wired up.

**The `\+` matters as much as the positive check.** An answer
describes an answer *completely* (§6, issue #1067) — checking only
what a description happens to mention would let `X = f(Y), Y = 1` be
described by `X = f(Y)` alone. The same principle says the *absence*
of `maybe` has to mean something too, or `true` and `maybe` describe
the same answer and the second alternative above could never usefully
assert `unexpected`. So a description without `maybe` now requires
`\+ some_attributed(Mark)`: nothing pending, not merely nothing
mentioned. No existing quad relied on the looser reading — none in the
suite exercises `dif`/`clpz`/`freeze` outside the ones added for this
issue — so tightening it broke nothing.

Coverage: `tests/misc/quads.pl` (`maybe_1`, `maybe_2`, `maybe_4` pass;
`maybe_3` is a deliberately-failing case, like the file's other
negative examples) and `tests/issues/test1128.pl`.

## 14. `unexpected` ends the description (issue #1141)

`unexpected` names *one leaf answer* and says that answer is not the
one the query gives *at that point* — not that the answer is bad in
general. So a bug report can be filed as a transcript truncated at the
moment of surprise:

```prolog
?- member(X, "abc").
   X = a
;  X = c, unexpected.
```

The query does answer `X = c`, but third, not second, so this holds.
`check_solutions/7` used to keep walking after an `unexpected` leaf and
then probe for “no further answers”, which failed here on the real
third answer. Nothing follows an `unexpected` leaf in the description,
so nothing about the answers after it is claimed — not even that there
are none:

```prolog
;	Unexpected == true
->	true					% nothing is claimed past it
```

This also removes an unsound path in `capture` mode: `expect/10` under
`unexpected` is `\+ expect_do(...)`, so its `FullCs` came back unbound
and was passed on as the next answer's `PrevCs`.

Coverage: `tests/misc/quads.pl` (`unexpected_1` passes; `unexpected_2`
is a deliberately-failing case, the answer at that point being the one
described) and `tests/issues/test1141.pl`.

## 15. Approximate float answers (issue #1145)

A float answer can be described to a stated precision:

```prolog
?- X is 71/5.
   X ~~ '14.2000'.
```

`V ~~ Spec` holds when `V` is a float inside the interval `Spec` is
*written* to — half of its last decimal place, either side, inclusive:
`'14.2000'` is `[14.19995, 14.20005]`, `'14.2'` is `[14.15, 14.25]`.
The trailing zeroes are the whole point, which is why `Spec` is an
atom: read as a float, `'14.2000'` and `'14.2'` are the same term and
the precision is gone before the checker ever sees it. An exponent
takes its precision from the mantissa, so `'1.42e1'` is
`[14.15, 14.25]`, and negatives work the same way.

The bounds are exact. `Spec` names a decimal, so the interval ends are
decimals too, kept as a ratio of unbounded integers; the observed float
is decomposed to `N / 2^K` and the comparison is a cross-multiplication.
Rounding the bound to a float first would be wrong in both directions —
the nearest float to a bound can sit on either side of it. `14.19995`
is the case in point: its nearest float is *below* the exact decimal,
so that float is outside `'14.2000'` while `14.20005`, whose nearest
float is also below its own bound, is inside.

A precision finer than a float can carry describes nothing, so it is
malformed rather than merely unsatisfiable — the interval has to hold
three floats, one below the value it names, that value, and one above:

```prolog
?- X = 1.0.
   X ~~ '1.0000000000000000'.     % MALFORMED - no float in there
```

Around 1.0 that admits 15 decimal places and rejects 16. An expectation
whose neighbours are not finite is malformed for the same reason. So is
one that is not an atom, or not a float spelling (`'14'` has no
fraction), or that overflows (`'1.0e400'`). The observed value must be
a float: an integer answer is not an approximate one.

`~~` binds like an equation, so it counts as a binding everywhere the
substitution checks look — `X = 1.0, X ~~ '1.0'` is a rebinding, and
so malformed.

The operator is exported by `library(quads)` as `700 xfx`, the way
`library(lambda)` exports `+\`, rather than being added to the global
table. A file using `~~` therefore has to import `library(quads)` at
load time. There is no `~~/2` predicate, so it stays available as an
ordinary user predicate.

Coverage: `tests/misc/quads.pl` (`approx_1`-`approx_4` pass; `approx_5`
and `approx_6` are deliberately-failing cases) and
`tests/issues/test1145.pl`.

## 16. `sto` is checked (issue #1154)

`sto` used to be skipped: `check_solutions/7` accepted any answer
carrying it, and nothing stopped a description without it from matching
a cyclic answer:

```prolog
?- X = -X.
   X = - - - ..., unexpected.     % failed: - - - ... matched the cycle
   sto, X = - - + ... .           % passed: never looked at
```

`sto` now says the bindings are cyclic, which only unification without
the occurs check can give, and, as with `maybe` (§13), its absence says
they are not. `attempt_match/5` tests `acyclic_term/1` of the witness
copy `W1` next to the `maybe` test. `drop_annotation/4` still takes
`sto` out so that `sto, false` and `sto, loops` classify as before, and
`with_sto/3` puts it back into `solution(Items)`. Those outcomes have no
bindings, so there `sto` is not checked, but it no longer excuses the
outcome itself.

A `sto` description may state its cycle, `sto, X = - - - X`, which the
substitution check already exempted (issue #1081). Applying it builds a
cyclic witness `W2`, and a plain walk of two cyclic terms never ends, so
`ball_matches/3` matches a cyclic description coinductively: a pair of
subterms met again on the way down, compared with `==`, is taken to
match. That makes `- - - X` the same rational tree as `-X`, and `- - + X`
not. An acyclic description keeps the plain walk, which follows the
description and so terminates.

Without `sto`, `X = - - - X` stays malformed: a substitution cannot
state a cycle.

Coverage: `tests/issues/test1154.pl`; `X = f(X), sto` in
`tests/misc/quads.pl` still passes, now checked.

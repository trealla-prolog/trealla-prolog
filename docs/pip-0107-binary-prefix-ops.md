---
PIP: 0107
Title: Binary prefix operators
Author: François Fages; Andrew Davison
Promoter: ECLiPSe
Discussions-To: https://discourse.prolog-lang.org/c/improvements-forum/6
Status: draft
Supported: eclipse
Type: feature
Content-Type: text/markdown
Requires:
Conflicts:
Created: 2024
Replaces:
Superseded-By:
---

# Binary prefix operators

## Abstract

This PIP adds two operator specifiers, `fxx` and `fxy`, to `op/3` and
`current_op/3`. If `f` is declared as a binary prefix operator, the term
`f(X, Y)` can also be written `f X Y`. The specification follows the
existing implementation in ECLiPSe, as observed in ECLiPSe 8.0 and as
documented in the ECLiPSe User Manual (section "Operators"). Behaviour
that might need changing is collected under *Open issues*.

## Motivation and Rationale

Modelling languages such as MiniZinc use quantifier syntax of the form
`forall (generators) body`. With a binary prefix operator,

```prolog
:- op(1140, fxx, forall).
:- op(700, xfx, in).
:- op(500, yfx, ..).
```

the MiniZinc text `forall (I, J in 1..N) something(I, J)` reads directly as
the Prolog term `forall((I, J in 1..N), something(I, J))`. This makes it
easy to embed constraint models in Prolog source and to write parsers for
modelling languages on top of `read_term/2`. Constraint programming is an
important industrial use of Prolog, so a common way to express such
syntax is useful.

Without binary prefix operators the nearest option is a unary prefix
operator applied to an infix term, e.g. `forall (I in 1..N) : Body`.
That changes the surface syntax and so can't read existing
MiniZinc-style text.

## Specification

### Operator declarations

* `op/3` accepts two new specifiers:
  * `fxx`: both arguments have priority at most P−1.
  * `fxy`: the first argument has priority at most P−1 and the second at most P.

  Priorities range over 1..1200 as usual, and priority 0 removes the definition.
* Binary prefix belongs to the **prefix class**. An atom has at most one
  prefix definition, unary (`fx`, `fy`) or binary (`fxx`, `fxy`). Declaring
  either kind replaces the other: after `op(700,fxx,f), op(700,fx,f)` only
  `700-fx` remains. `op(0, fy, f)` also removes an `fxx` definition of `f`.
* A binary prefix definition can coexist with an infix or postfix
  definition of the same atom, just as unary prefix does.
* `current_op/3` reports `fxx` and `fxy`.
* There are no `fyx` or `fyy` specifiers. `op(700, fyx, f)` raises a range
  error, as any unknown specifier does.
* In a module using ECLiPSe's `iso` language, `op/3` raises
  `domain_error(operator_specifier, fxx)` (likewise for `fxy`).
  `current_op/3` in that mode raises the same error for `fxy`, but simply
  fails for `fxx` (see *Open issues*).

### Syntax

The operator term grammar gains two productions:

```
term(N) --> fxx(N) term(N-1) term(N-1)
term(N) --> fxy(N) term(N-1) term(N)
```

The resulting term has priority N, like any other operator term.

Parsing is deterministic:

1. **The first argument is greedy.** It is the longest term of priority
   ≤ N−1 starting at the token after the operator, and it ends at the first
   token that cannot continue it. The parser does not backtrack. In
   particular, a token that could be an infix operator continues the first
   argument:
   * `f a - b c` is `f(a-b, c)`, and so is `f a -b c`.
   * `f 1 -2` and `f a -1` are **syntax errors**. The first argument
     swallows `-` as an infix operator (`1-2`), so the second argument is
     missing.
2. **Signs.** A `-` directly before a number at the start of an argument
   makes a negative number: `f -1 2` is `f(-1, 2)`. With layout it is the
   prefix operator: `f - 1 2` is `f(-(1), 2)`.
3. **The operator as an atom.** Unary prefix operators already work this
   way, and binary prefix operators do the same. The name is read as a
   plain atom when it is followed by an infix operator, a terminator, or
   nothing:
   `f` → `f`, `f = a` → `(f)=a`, `f , a` → `','(f,a)`, `[f]` → `[f]`.
   Functional notation (no layout before `(`) always means an ordinary
   compound: `f(a,b)` → `f(a,b)`, `f(a)` → `f(a)`.
4. **Exactly two operands.** `f (a)` and `f a` are syntax errors when `f`
   has only a binary prefix definition. The operator does not fall back to
   an atom or to `f/1`. `f a b c` is a syntax error.
5. **Bracketed operands.** `f (a) (b)` → `f(a,b)`,
   `f (a,b) c` → `f((a,b), c)`, `f (a:-b) c` → `f((a:-b), c)`.
6. **Priorities.** `f a=b c` with `f` at 700 is an error because
   `a=b` has priority 700, which is not below 700. Nesting follows the
   specifier:
   `g a g b c` → `g(a, g(b,c))` for `fxy`, and the same text is an error
   for `fxx`. `f f a b c` is always an error because the first argument is
   limited to N−1.
7. **A name token followed by layout and `(`** is a syntax error ("space
   between functor and open bracket"), as it is elsewhere in ECLiPSe
   syntax. So when the first operand ends in a name token, the second
   operand cannot be bracketed: `f a (b)`, `f 'a' (b)` and `f a+b (c)` are
   errors. After other tokens it is fine: `f 1 (b)`, `f X (b)`, `f [a] (b)`
   and `f (a) (b)` are all accepted.

### Output

* `write/1`, `print/1` and `writeq/1` write `f(A, B)` as `f A B` when `f`
  has a binary prefix definition, with one space between operator and
  operands. If `f` also has an infix definition, the infix form is used:
  `a f b`. `write_canonical/1` writes `f(A, B)`.
* Operands are written at priority N−1, N−1 (`fxx`) or N−1, N (`fxy`) and
  are bracketed when they exceed it. The whole term is bracketed when the
  context priority is below N.
* An operator atom used as an operand is bracketed: `f(-, a)` is written
  `f (-) a`.
* When both operands are strings, the second is bracketed, because
  adjacent string literals concatenate: `f("s","t")` is written
  `f "s" ("t")`.

The following terms are written in a form that does not read back (see
*Open issues*):

| Term | `writeq/1` output | Reading it back |
|---|---|---|
| `f(1, -2)` | `f 1 -2` | syntax error (rule 1) |
| `f(1, -(2))` | `f 1 - 2` | syntax error (rule 1) |
| `f(a, -(b))` | `f a - b` | syntax error (rule 1) |
| `f(a, -(-(b)))` | `f a - - b` | syntax error (rule 1) |
| `f(a, f(b,c))` | `f a (f b c)` | syntax error (rule 7) |
| `f(a, (b,c))` | `f a (b, c)` | syntax error (rule 7) |
| `f(a, b=c)` | `f a (b = c)` | syntax error (rule 7) |
| `f(a, -)` | `f a (-)` | syntax error (rule 7) |

Every other case tested reads back correctly, including `f (f a b) c`,
`f (-) a`, `f - 1 2`, `(f a b) = c`, `[f a b]`, `p(f a b)`, `{f a b}` and
`f a b :- c`.

## Test cases

With `op(700, fxx, f)`, `op(700, fxy, g)` and `op(200, fxx, h)`.
"error" means a syntax error. All results are from ECLiPSe 8.0.

| Input | Result |
|---|---|
| `f a b` | `f(a,b)` |
| `f(a,b)` | `f(a,b)` |
| `f (a) (b)` | `f(a,b)` |
| `f (a,b) c` | `f((a,b),c)` |
| `f a` / `f (a)` / `f a b c` | error |
| `f(a)` | `f(a)` |
| `f` / `[f]` / `f(f)` | atom / list / `f(f)` |
| `f = a` | `=(f,a)` |
| `f f f` | `f(f,f)` |
| `f a+b c` | `f(a+b,c)` |
| `f a b+c` | `f(a,b+c)` |
| `f a=b c` / `f a b=c` | error (priority) |
| `f a b, c` | `','(f(a,b),c)` |
| `f a b = c` / `x = f a b` | error (priority) |
| `x = h a b` / `- h a b` | `=(x,h(a,b))` / `-(h(a,b))` |
| `h a b ^ c` | error (priority) |
| `p(h a b, c)` / `[h a b, c]` | `p(h(a,b),c)` / `[h(a,b),c]` |
| `g a g b c` | `g(a,g(b,c))` |
| `f f a b c` / `f a f b c` / `g g a b c` | error |
| `f -1 2` | `f(-1,2)` |
| `f - 1 2` | `f(-(1),2)` |
| `f 1 -2` / `f 1 - 2` / `f a -1` | error (rule 1) |
| `f a - b c` / `f a -b c` | `f(a-b,c)` |
| `f -a b` / `f - - a b` | `f(-(a),b)` / `f(-(-(a)),b)` |
| `f - a` | error |
| `f (-) a` | `f(-,a)` |
| `f 1 (b)` / `f X (b)` / `f [a] (b)` | `f(1,b)` / `f(X,b)` / `f([a],b)` |
| `f a (b)` / `f 'a' (b)` / `f a (-b)` | error (rule 7) |
| `f (a) (-b)` / `f (a) (b=c)` | `f(a,-(b))` / `f(a,b=c)` |
| `f a(b) c` | `f(a(b),c)` |
| `f [a] {b}` | `f([a],{b})` |
| `forall (I, J in 1..N) something(I, J)` | `forall((I,J in 1..N), something(I,J))` |
| `forall (i in 1..n) (x > 0)` | `forall(i in 1..n, x>0)` |

Operator table behaviour:

| Goal | Result |
|---|---|
| `op(700,fxx,f), current_op(P,T,f)` | `P=700, T=fxx` |
| `op(700,fxy,f), op(700,xfx,f)` | both kept; `f a b` → `f(a,b)`, `writeq` uses `a f b` |
| `op(700,fxx,f), op(200,fy,f)` | only `200-fy` remains |
| `op(700,fxx,f), op(0,fy,f)` | no prefix definition remains |
| `op(700,fyx,f)` / `op(700,fyy,f)` | range error |

## Implementation

ECLiPSe implements binary prefix operators in three places:

* **Reader** (`Kernel/src/read.c`). The reader is recursive descent. After
  a binary prefix operator it reads the first operand at priority N−1 with
  a flag (`PREBINFIRST`). The flag makes any token that cannot continue
  the operand end it successfully, where it would otherwise be a syntax
  error. The reader then reads the second operand at N−1 (`fxx`) or N
  (`fxy`).
* **Operator tables** (`Kernel/src/operator.c`). Binary prefix definitions
  share the prefix property slot with unary prefix ones, which gives the
  replacement behaviour above. When looking up an operator for a
  two-argument term, infix definitions are tried before binary prefix
  ones.
* **Writer** (`Kernel/src/write.c`). The writer outputs the operator, the
  first operand, a space and the second operand, and brackets the second
  operand to avoid consecutive string literals.

## Open issues

1. **Round-tripping.** `writeq/1` output does not always read back (see
   *Output*). Two changes would fix it. First, bracket the second operand
   when it is a negative number or its principal functor is a prefix
   operator that is also infix. Second, whenever the second operand is
   bracketed, bracket the first as well, since `f (a) (-b)` and
   `f (a) (b = c)` already read back correctly.
2. **Rule 7.** Accepting `f a (b)` as `f(a, b)` would remove the need to
   bracket the first operand. The layout means `a (b)` is not functional
   notation, so the grammar allows only this reading.
3. **Unary and binary prefix together.** Allowing both (`f a` vs `f a b`)
   would need lookahead and would make rule 1 ambiguous. This PIP keeps
   one prefix definition per atom.
4. **`fyx` / `fyy`.** Left-associative first operands (`f f a b c` →
   `f(f(a,b),c)`) are not proposed.
5. **`current_op/3` in ISO mode.** It fails for `fxx` but raises a
   domain error for `fxy`, because `current_op/3` checks `> FXX` where
   `op/3` checks `>= FXX`. Both should probably raise the error.
6. **Reserved atoms.** Outside ISO mode, `op(700, fxx, ',')` and similar
   declarations on `'|'`, `[]` and `{}` are accepted. Should the
   restrictions that apply to other operator types apply here too?

## Acknowledgements

The original proposal is by François Fages (Inria Saclay), from the 2024
PIP workshop.

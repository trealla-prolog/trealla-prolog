% Each declaration below names an ISO builtin, which may not be declared, so
% each is refused with a permission error. The load must go on past them all.

before(1).
:- dynamic(findall/3).
:- discontiguous(atom_length/2).
:- multifile(findall/3).
:- dynamic([findall/3, listed/1]).
:- dynamic findall/3, commaed/1.
after(1).

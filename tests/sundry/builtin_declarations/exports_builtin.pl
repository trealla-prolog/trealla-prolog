% Exports two ISO builtins it does not define, as SWI allows, and one of its own.

:- module(exports_builtin, [findall/3, atom_length/2, own/1]).

own(1).

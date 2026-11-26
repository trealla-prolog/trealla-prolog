:- module(gensym, [gensym/2,
		   reset_gensym/1]).

:- use_module(library(error)).
:- use_module(library(lists)).
:- use_module(library(iso_ext)).
:- use_module(library(si)).

gensym_key(Base, BaseKey) :-
    atom_concat('gensym_', Base, BaseKey).

append_id(Base, UniqueID, Unique) :-
    atom_chars(Base, BaseChars),
    number_chars(UniqueID, IDChars),
    append(BaseChars, IDChars, AtomChars),
    atom_chars(Unique, AtomChars).

% sig_atomic/1 keeps a task_time_slice slice from handing two tasks one number.

gensym(Base, Unique) :-
    must_be(var, Unique),
    atom_si(Base),
    gensym_key(Base, BaseKey),
    sig_atomic((
        (  bb_get(BaseKey, UniqueID0) -> true
        ;  UniqueID0 = 0
        ),
        UniqueID is UniqueID0 + 1,
        bb_put(BaseKey, UniqueID)
    )),
    append_id(Base, UniqueID, Unique).

reset_gensym(Base) :-
    atom_si(Base),
    gensym_key(Base, BaseKey),
    bb_put(BaseKey, 0).

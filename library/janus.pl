% Janus Prolog-Python interface - phases 0 and 1.
%
% See docs/janus-design.md. Phase 0 is the build wiring, finding
% libpython, the shutdown hook and py_version/0. Phase 1 is the
% marshaller: the bi-translation table of the design's section 3, both
% directions, recursive. Calling and iteration arrive in phases 2-3.
%
% REFERENCE OWNERSHIP, which is the rule everything here obeys:
%
%   - pl_to_py/2 returns a NEW reference. The caller owns it and must
%     Py_DecRef it.
%   - py_to_pl/2 BORROWS its object. It never releases what it was
%     given; the caller keeps ownership. The one exception is the
%     fallback to '$py_obj'/1, which increfs so the handle owns a
%     reference of its own, released by py_free/1 in phase 4.
%   - Whether an entry point returns a new or a borrowed reference is a
%     property of the function, not of the value, so it is recorded in
%     py_sig/3 below and never decided at the call site.
%   - PyList_SetItem and PyTuple_SetItem STEAL their reference; the
%     other containers do not, so what goes into a dict or a set is
%     released afterwards and what goes into a list or tuple is not.
%
% Nothing here is reachable from a default build: the module is only
% embedded when built with `make janus`, so a stock `make` carries no
% reference to Python of any kind.

:- module(janus, [
	py_call/2, py_call/3,
	py_func/3, py_func/4,
	py_dot/3,  py_dot/4,
	py_iter/2, py_iter/3,
	py_setattr/3,
	py_free/1, py_is_object/1,
	keys/2, key/2, values/3, items/2,
	py_add_lib_dir/1, py_add_lib_dir/2,
	py_lib_dirs/1,
	py_type/2, py_pp/1,
	add_py_lib_dir/1, obj_dir/2, obj_dict/2, value/3,
	janus_python_version/1, py_next/2,
	py_version/0,
	py_lib/1
	]).

:- use_module(library(lists)).

% The library is found by dlopen at run time, never at build time -
% configure-time detection would put a Python dependency back into the
% build. Three platforms, three shapes:
%
%   macOS    a framework path carrying no .so suffix at all
%   Linux    libpython3.X.so, with an 'm' ABI suffix before 3.8
%   Windows  python3XX.dll - no 'lib' prefix, version in the filename
%
% Newest first, so a machine with several installs gets the newest.
% PROLOG_PYTHON_LIB overrides the search entirely, which is the escape
% hatch for an install in none of these places.

py_minor_version(V) :- between(9, 20, N), V is 29 - N.

py_candidate(Lib) :-
	getenv('PROLOG_PYTHON_LIB', Lib).
py_candidate(Lib) :-
	py_minor_version(V),
	(   % macOS: Homebrew, python.org, and a MacPorts-style prefix
	    member(Dir, ['/opt/homebrew/Frameworks', '/usr/local/Frameworks',
	                 '/Library/Frameworks']),
	    format(atom(Lib), "~w/Python.framework/Versions/3.~w/Python", [Dir, V])
	;   % Linux, and anything else with a conventional soname
	    format(atom(Lib), "libpython3.~w.so", [V])
	;   format(atom(Lib), "libpython3.~w.so.1.0", [V])
	;   % Windows: neither the prefix nor the suffix mapping reaches this
	    format(atom(Lib), "python3~w.dll", [V])
	).

:- dynamic(py_lib_/1).

%   py_lib(-Path) is semidet.
%
%   The libpython this session is bound to.

py_lib(Lib) :- py_lib_(Lib).

py_open(Handle, Lib) :-
	py_candidate(Lib),
	catch('$dlopen'(Lib, 0, Handle), _, fail),
	!.

% Every entry point phase 0 needs. Two of them take no arguments at all,
% which is why this phase also carries the '$register_predicate'/4 fix.

% py_sig(Name, ArgTypes, ReturnType, Reference).
%
% Reference is what the call HANDS BACK, and it is data rather than a
% comment so that the tests can check it:
%
%   new       a reference this side owns and must release
%   borrowed  a reference owned elsewhere, which must NOT be released
%   foreign   a pointer that is not a PyObject at all, so refcounting
%             does not apply - PyEval_SaveThread's PyThreadState* is the
%             only one, and giving it a name of its own is what keeps
%             "every ptr return is classified" a checkable claim
%   none      the call does not return a pointer
%
% Whether a result is new or borrowed is a property of the function,
% never of the value, so it is settled here and never at the call site.
%
% A borrowed char* must be ccstr and never cstr - Trealla's cstr return
% type calls TPL_free on what it is given, which would hand CPython's own
% buffer to Trealla's allocator.
%
% py_steals/2 records the other direction: an argument the callee takes
% ownership of, so the caller must NOT release it.

py_sig('Py_InitializeEx', [sint32], void, none).
py_sig('Py_FinalizeEx', [],       sint32, none).
py_sig('Py_IsInitialized', [],       sint32, none).
py_sig('Py_GetVersion', [],       ccstr, borrowed).
py_sig('PyRun_SimpleString', [cstr],  sint32, none).
py_sig('Py_IncRef', [ptr],    void, none).
py_sig('Py_DecRef', [ptr],    void, none).

% Errors. Phase 5 turns these into Prolog exceptions properly; phase 1
% only needs to notice a NULL and not marshal it.
py_sig('PyErr_Occurred', [],       ptr, borrowed).
py_sig('PyErr_Clear', [],       void, none).

% Prolog -> Python
py_sig('PyLong_FromLongLong', [sint64],           ptr, new).
py_sig('PyLong_FromString', [cstr, ptr, sint32], ptr, new).
py_sig('PyFloat_FromDouble', [double],           ptr, new).
py_sig('PyUnicode_FromString', [cstr],             ptr, new).
py_sig('PyBool_FromLong', [sint32],           ptr, new).
py_sig('PyList_New', [sint64],           ptr, new).
py_sig('PyList_SetItem', [ptr, sint64, ptr], sint32, none).
py_sig('PyTuple_New', [sint64],           ptr, new).
py_sig('PyTuple_SetItem', [ptr, sint64, ptr], sint32, none).
py_sig('PyDict_New', [],                 ptr, new).
py_sig('PyDict_SetItem', [ptr, ptr, ptr],    sint32, none).
py_sig('PySet_New', [ptr],              ptr, new).
py_sig('PySet_Add', [ptr, ptr],         sint32, none).

% Python -> Prolog
py_sig('PyObject_Str', [ptr],              ptr, new).
py_sig('PyObject_Type', [ptr],              ptr, new).
py_sig('PyObject_IsInstance', [ptr, ptr],         sint32, none).
py_sig('PyObject_IsTrue', [ptr],              sint32, none).
py_sig('PyLong_AsLongLongAndOverflow', [ptr, -sint32], sint64, none).
py_sig('PyNumber_ToBase', [ptr, sint32],      ptr, new).
py_sig('PyFloat_AsDouble', [ptr],              double, none).
py_sig('PyUnicode_AsUTF8', [ptr], ccstr, borrowed).
py_sig('PySequence_Size', [ptr],              sint64, none).
py_sig('PySequence_GetItem', [ptr, sint64],      ptr, new).
py_sig('PyDict_Keys', [ptr],              ptr, new).
py_sig('PyObject_GetItem', [ptr, ptr],         ptr, new).
py_sig('PyObject_GetIter', [ptr],              ptr, new).
py_sig('PyIter_Next', [ptr],              ptr, new).

% Building the type-object cache, and reaching fractions.Fraction
py_sig('PyImport_ImportModule', [cstr],          ptr, new).
py_sig('PyObject_GetAttrString', [ptr, cstr],     ptr, new).
py_sig('PyObject_CallObject', [ptr, ptr],      ptr, new).

% Phase 2: calling, and the GIL that has to wrap it
py_sig('PyObject_Call', [ptr, ptr, ptr],  ptr, new).
py_sig('PyObject_SetAttrString', [ptr, cstr, ptr], sint32, none).
py_sig('PyGILState_Ensure', [],               sint32, none).
py_sig('PyGILState_Release', [sint32],         void, none).
py_sig('PyEval_SaveThread', [],               ptr, foreign).
py_sig('PyEval_RestoreThread', [ptr],            void, none).

% Argument position (1-based) whose reference the callee consumes, so the
% caller must NOT release it. Both of these are why a list or tuple is
% filled without a matching Py_DecRef, while a dict or set is not.

py_steals('PyList_SetItem',  3).
py_steals('PyTuple_SetItem', 3).

% Entry points that may not exist. CPython 3.12 replaced the
% PyErr_Fetch triple with PyErr_GetRaisedException; the old one is still
% exported in 3.14 but soft-deprecated, and the new one is absent before
% 3.12. Rather than guess from Py_GetVersion, each is registered if the
% symbol resolves and remembered if it did - '$register_predicate'/4
% simply fails when dlsym misses, which is exactly the test needed.

py_sig_opt('PyErr_GetRaisedException', [], ptr, new).           % 3.12+
py_sig_opt('PyErr_Fetch', [-ptr, -ptr, -ptr], void, none).      % <= 3.11
py_sig_opt('PyType_GetName', [ptr], ptr, new).                  % 3.11+
py_sig_opt('PyObject_Repr', [ptr], ptr, new).

:- dynamic(py_have_/1).

% A registered foreign predicate is reachable only from an unqualified
% call inside the module that registered it: '$register_predicate'/4 puts
% it on the prolog instance rather than in the module's own table, and
% neither janus:'Py_GetVersion'(V) from outside nor a bare call from user
% finds it. Every C-API call therefore has to live in this file, which is
% the design in any case - but it also means tests cannot reach the API
% directly, and have to go through a Prolog wrapper such as py_run_/1.

% This has to be a plain directive: it runs *during* load, so the clauses
% below compile against predicates that already exist. initialization/1
% runs after the file is loaded, by which time the callers have been
% compiled without them and raise existence_error at run time.

:- (   py_open(H, Lib)
   ->  assertz(py_lib_(Lib)),
       forall(py_sig(N, A, R, _), '$register_predicate'(H, N, A, R)),
       forall(py_sig_opt(N, A, R, _),
              (   catch('$register_predicate'(H, N, A, R), _, fail)
              ->  assertz(py_have_(N))
              ;   true
              ))
   ;   throw(error(existence_error(foreign_library, libpython),
                   'library(janus)':py_open/2))
   ).

% Start the interpreter, once. Py_InitializeEx(0) rather than
% Py_Initialize() so Python does not install its own signal handlers over
% Trealla's - SIGINT has to keep reaching the Prolog toplevel.

% Py_IsInitialized returns *nonzero* when the interpreter is up, not
% specifically 1, so the result has to be compared rather than matched.
% Matching on 1 makes both guards below fail silently, and the failure is
% invisible: py_init just initialises twice (harmless), while py_finalize
% quietly does nothing and Python's buffered output is lost at exit.

py_initialized :-
	'Py_IsInitialized'(R),
	R =\= 0.

py_init :-
	py_types_ready, !.
py_init :-
	(   py_initialized -> true ;   'Py_InitializeEx'(0)   ),
	py_learn_types,

	% Py_InitializeEx leaves the GIL HELD by the calling thread, and
	% nothing ever drops it. Any other Trealla thread then blocks in
	% PyGILState_Ensure forever - not a crash or a race, a silent
	% deadlock, and the first symptom is a test that never returns.
	% PyEval_SaveThread releases it and hands back the main thread's
	% state. Every entry point from here on acquires the GIL through
	% py_gil/1 - but the saved state is kept, because shutdown has to
	% put it back.
	'PyEval_SaveThread'(TState),
	assertz(py_tstate_(TState)).

% Shutdown. Registered by asserting a clause for atexit/0, which halt/0,1
% run through ignore/1 before '$halt'.
%
% Two properties this clause has to have, both of them easy to get wrong:
% it ends in fail, because ignore/1 takes the first solution and a clause
% that succeeds would stop any other library's hook from running; and it
% never throws, because an exception here aborts the goal before '$halt'
% is reached and the exit status is lost.

% Py_FinalizeEx needs the GIL held, which after py_init/0 nobody holds.
%
% And it must run only once, on the thread that started the interpreter.
% Trealla runs the GLOBAL atexit/0 hook when any thread exits, not only
% at process exit, so without this guard the first worker thread to
% finish tears CPython down underneath the ones still inside it. That
% surfaces as a CPython fatal error - "gilstate_tss_set: failed to set
% current tstate (TSS)", runtime state "preinitialized" - from a thread
% that did nothing wrong, which is about as far from the cause as a
% symptom can get.

:- ( thread_self(Me) -> assertz(py_main_thread_(Me)) ; true ).

:- dynamic(py_main_thread_/1).
:- dynamic(py_tstate_/1).

py_on_main_thread :-
	py_main_thread_(Main),
	thread_self(Main).

% Shutdown pairs with PyEval_SaveThread above: the main thread's state
% is restored, which reacquires the GIL and makes this thread current
% again. PyGILState_Ensure is NOT the way to do it - it takes the GIL
% through the auto-state machinery, which is still holding a reference
% when Py_FinalizeEx tears the runtime down, and the process segfaults
% on the way out with every test having already passed.

py_finalize :-
	py_on_main_thread,
	py_initialized,
	!,
	py_forget_types,
	(   retract(py_tstate_(TState))
	->  'PyEval_RestoreThread'(TState)
	;   true
	),
	'Py_FinalizeEx'(_).
py_finalize.

% Qualified to user: halt/0,1 call ignore(atexit) against the global
% atexit/0, and a bare assertz/1 inside a module creates janus:atexit/0
% instead, which nothing ever calls. The failure is silent - the hook
% simply never runs, and the first symptom is Python's buffered output
% vanishing at exit.

:- assertz(user:(atexit :- catch(janus:py_finalize, _, true), fail)).

% Not part of the Janus interface. Phase 0 needs some way to reach the
% interpreter before phase 1's marshalling exists, and the shutdown test
% uses it to register a Python-side atexit handler - which is how we know
% Py_FinalizeEx really ran rather than merely being called.

py_run_(Source) :-
	py_gil('PyRun_SimpleString'(Source, _)).

%   py_version is det.
%
%   Print the Python version this session is bound to, and the library it
%   came from. The phase 0 smoke test: it exercises dlopen, a zero-argument
%   registration, a borrowed string return, and startup.

py_version :-
	py_gil(('Py_GetVersion'(Version), py_lib_(Lib))),
	format("Python ~w~nlibrary ~w~n", [Version, Lib]).


		 /*******************************
		 *   PHASE 1: THE TYPE CACHE    *
		 *******************************/

% PyLong_Check and its friends are C macros, so there is no symbol to
% dlsym, and the type objects they test against - PyLong_Type and the
% rest - are exported as data, which '$register_predicate'/4 cannot use
% either because it registers a symbol as something to CALL.
%
% PyObject_Type of any exemplar is the type object, though, and that is
% an ordinary exported function. So build one value of each type at
% startup and keep its type. Type objects are immortal, so the reference
% kept here is deliberate and never released.

:- dynamic(py_type_/2).           % Name, type object
:- dynamic(py_const_/2).          % Name, singleton object

py_types_ready :-
	py_type_(int, _).

py_forget_types :-
	retractall(py_type_(_, _)),
	retractall(py_const_(_, _)).

py_learn(Name, Exemplar) :-
	py_check(Exemplar),
	'PyObject_Type'(Exemplar, Type),
	py_check(Type),
	assertz(py_type_(Name, Type)),
	'Py_DecRef'(Exemplar).

py_import_attr(Module, Name, Obj) :-
	'PyImport_ImportModule'(Module, M),
	py_check(M),
	'PyObject_GetAttrString'(M, Name, Obj),
	'Py_DecRef'(M),
	py_check(Obj).

py_learn_types :-
	% None is a singleton, compared by identity rather than by type
	py_import_attr(builtins, 'None', NoneObj),
	assertz(py_const_(none, NoneObj)),

	'PyLong_FromLongLong'(1, I),    py_learn(int, I),
	'PyBool_FromLong'(1, B),        py_learn(bool, B),
	'PyFloat_FromDouble'(1.0, F),   py_learn(float, F),
	'PyUnicode_FromString'(x, U),   py_learn(str, U),
	'PyTuple_New'(0, T),            py_learn(tuple, T),
	'PyDict_New'(D),                py_learn(dict, D),
	'PyList_New'(0, L),             py_learn(list, L),
	'PySet_New'(0, S),              py_learn(set, S),

	% fractions.Fraction is a class, so it is already the "type"
	py_import_attr(fractions, 'Fraction', FC),
	assertz(py_type_(fraction, FC)).

%   py_check(+Obj) is det.
%
%   A failed CPython call returns NULL, and the FFI will happily marshal
%   that onwards - a NULL ccstr becomes a nonsense atom and the error
%   surfaces several steps later, naming the wrong predicate. So every
%   fallible result is tested where it is produced. Phase 5 replaces this
%   with the real Python exception.

py_check(Obj) :-
	Obj =:= 0,
	!,
	py_raise.
py_check(_).

%   py_raise is det.
%
%   Turn the pending Python exception into a Prolog one and throw it.
%   Always throws: a NULL with no exception set is a bug in the shim
%   rather than in the program, and saying so is more use than failing.

py_raise :-
	(   py_error_(Type, Message)
	->  throw(error(python_error(Type, Message), janus))
	;   throw(error(system_error(python_call_failed), janus))
	).

%   py_error_(-Type, -Message) is semidet.
%
%   Type is the exception class name as an atom, Message its str(). The
%   pending exception is consumed either way.
%
%   SWI's second argument is the exception OBJECT. An object out of an
%   error path is a handle the catcher has to remember to py_free/1,
%   which is a poor bargain on the one code path nobody tests, so this
%   translates it instead. The functor and arity match SWI so a catch of
%   python_error(Type, _) is portable; only the second argument differs.

%   py_error_api(-Api) is det.
%
%   Which of the two exception APIs is in use: raised_exception for
%   CPython 3.12 and later, fetch for the triple that preceded it, none
%   if neither symbol resolved. py_use_error_api_/1 overrides the choice,
%   which is how the older path gets exercised on a newer interpreter -
%   and the only way to test it without an older Python to hand.

:- dynamic(py_error_api_/1).

py_error_api(Api) :-
	(   py_error_api_(Forced)
	->  Api = Forced
	;   py_have_('PyErr_GetRaisedException')
	->  Api = raised_exception
	;   py_have_('PyErr_Fetch')
	->  Api = fetch
	;   Api = none
	).

py_use_error_api_(default) :-
	!,
	retractall(py_error_api_(_)).
py_use_error_api_(Api) :-
	must_be(oneof([raised_exception, fetch]), Api),
	retractall(py_error_api_(_)),
	assertz(py_error_api_(Api)).

py_error_(Type, Message) :-
	py_error_api(raised_exception),
	!,
	'PyErr_GetRaisedException'(Exc),
	Exc =\= 0,
	setup_call_cleanup(true, py_exception_parts(Exc, Type, Message), 'Py_DecRef'(Exc)).
py_error_(Type, Message) :-
	py_error_api(fetch),
	!,
	% The pre-3.12 triple. Not normalised: without normalisation the
	% value may be the raw argument rather than an instance, so the type
	% object is the reliable source for the name and the value is only
	% used for the message when it is present.
	'PyErr_Fetch'(TypeObj, ValueObj, TB),
	TypeObj =\= 0,
	setup_call_cleanup(true,
		(   py_type_name(TypeObj, Type),
		    (   ValueObj =\= 0
		    ->  py_str_of(ValueObj, Message)
		    ;   Message = ''
		    )
		),
		(   'Py_DecRef'(TypeObj),
		    (   ValueObj =\= 0 -> 'Py_DecRef'(ValueObj) ;   true   ),
		    (   TB =\= 0 -> 'Py_DecRef'(TB) ;   true   )
		)).

py_exception_parts(Exc, Type, Message) :-
	'PyObject_Type'(Exc, T),
	setup_call_cleanup(true, py_type_name(T, Type), 'Py_DecRef'(T)),
	py_str_of(Exc, Message).

% PyType_GetName arrived in 3.11. Without it the name comes from repr of
% the type object, which reads <class 'ValueError'> - trimmed here so the
% error term carries the same atom on every version.

py_type_name(TypeObj, Name) :-
	py_have_('PyType_GetName'),
	!,
	'PyType_GetName'(TypeObj, NameObj),
	setup_call_cleanup(true, py_utf8_of(NameObj, Name), 'Py_DecRef'(NameObj)).
py_type_name(TypeObj, Name) :-
	'PyObject_Repr'(TypeObj, R),
	setup_call_cleanup(true, py_utf8_of(R, Repr), 'Py_DecRef'(R)),
	(   sub_atom(Repr, B, _, _, ''''),
	    Start is B + 1,
	    sub_atom(Repr, Start, _, 1, Inner),
	    sub_atom(Inner, _, 1, 0, '''')
	->  sub_atom(Inner, 0, _, 1, Name)
	;   Name = Repr
	).

py_str_of(Obj, Text) :-
	'PyObject_Str'(Obj, S),
	(   S =:= 0
	->  'PyErr_Clear',
	    Text = ''
	;   setup_call_cleanup(true, py_utf8_of(S, Text), 'Py_DecRef'(S))
	).

py_utf8_of(Obj, Text) :-
	'PyUnicode_AsUTF8'(Obj, Raw),
	(   Raw == []
	->  'PyErr_Clear',
	    Text = ''
	;   Text = Raw
	).

py_isa(Obj, Name) :-
	py_type_(Name, Type),
	'PyObject_IsInstance'(Obj, Type, 1).


		 /*******************************
		 *      PYTHON -> PROLOG        *
		 *******************************/

%   py_to_pl(+Obj, -Term) is det.
%
%   Borrows Obj. The order below is load-bearing, not a lookup table:
%   bool is a subclass of int in Python, so an int-first test turns True
%   into 1 and never reaches @true. bytes is a sequence, so list is
%   tested by type rather than by PySequence_Check, which keeps bytes on
%   the opaque path where the spec leaves it.

py_to_pl(Obj, Term) :-
	py_to_pl(Obj, full, Term).

%   py_to_pl(+Obj, +Mode, -Term) is det.
%
%   Mode is full or exact. exact is what py_object(true) selects: it
%   swaps the subclass-aware chain below for a short exact-type one, so
%   int, float, str and tuple still translate and everything else comes
%   back as a handle. None and bool are decided before the split,
%   because SWI decides them before opening its chain at all and the
%   option is not meant to change them.

py_to_pl(Obj, Mode, Term) :-
	py_check(Obj),
	(   py_is_none(Obj)       ->  Term = @none
	;   py_isa(Obj, bool)     ->  py_bool_to_pl(Obj, Term)
	;   Mode == exact         ->  py_to_pl_exact(Obj, Term)
	;   py_isa(Obj, int)      ->  py_int_to_pl(Obj, Term)
	;   py_isa(Obj, float)    ->  'PyFloat_AsDouble'(Obj, Term)
	;   py_isa(Obj, str)      ->  'PyUnicode_AsUTF8'(Obj, Term)
	;   py_isa(Obj, tuple)    ->  py_tuple_to_pl(Obj, Mode, Term)
	;   py_isa(Obj, dict)     ->  py_dict_to_pl(Obj, Mode, Term)
	;   py_isa(Obj, fraction) ->  py_fraction_to_pl(Obj, Term)
	;   py_isa(Obj, list)     ->  py_seq_to_pl(Obj, Mode, Term)
	;   py_isa(Obj, set)      ->  py_set_to_pl(Obj, Mode, Term)
	;   py_opaque(Obj, Term)
	).

py_to_pl_exact(Obj, Term) :-
	(   py_is_exact(Obj, int)   ->  py_int_to_pl(Obj, Term)
	;   py_is_exact(Obj, float) ->  'PyFloat_AsDouble'(Obj, Term)
	;   py_is_exact(Obj, str)   ->  'PyUnicode_AsUTF8'(Obj, Term)
	;   py_is_exact(Obj, tuple) ->  py_tuple_to_pl(Obj, exact, Term)
	;   py_opaque(Obj, Term)
	).

% Exact type identity, the equivalent of CPython's *_CheckExact macros.
% The type object is immortal, so releasing the reference PyObject_Type
% hands back before comparing it is safe.

py_is_exact(Obj, Name) :-
	py_type_(Name, Type),
	'PyObject_Type'(Obj, T),
	'Py_DecRef'(T),
	T =:= Type.

py_is_none(Obj) :-
	py_const_(none, None),
	Obj =:= None.

py_bool_to_pl(Obj, Term) :-
	'PyObject_IsTrue'(Obj, R),
	(   R =:= 0 -> Term = @false ;   Term = @true   ).

% The int64 fast path, with the base-16 slow path behind it. Base 16 and
% not base 10: since 3.11 CPython caps DECIMAL integer conversion at
% sys.get_int_max_str_digits(), 4300 digits by default, and raises above
% it. Hex is exempt from that limit, and is faster besides.

py_int_to_pl(Obj, N) :-
	'PyLong_AsLongLongAndOverflow'(Obj, Overflow, V),
	(   Overflow =:= 0
	->  N = V
	;   'PyNumber_ToBase'(Obj, 16, S),
	    py_check(S),
	    'PyUnicode_AsUTF8'(S, Text),
	    atom_number(Text, N),
	    'Py_DecRef'(S)
	).

py_fraction_to_pl(Obj, Rational) :-
	py_attr_int(Obj, numerator, N),
	py_attr_int(Obj, denominator, D),
	Rational is N rdiv D.

py_attr_int(Obj, Name, N) :-
	'PyObject_GetAttrString'(Obj, Name, A),
	py_check(A),
	py_int_to_pl(A, N),
	'Py_DecRef'(A).

py_seq_items(_, I, N, _, []) :-
	I >= N,
	!.
py_seq_items(Obj, I, N, Mode, [H|T]) :-
	'PySequence_GetItem'(Obj, I, Item),
	py_check(Item),
	py_to_pl(Item, Mode, H),
	'Py_DecRef'(Item),
	J is I + 1,
	py_seq_items(Obj, J, N, Mode, T).

py_seq_to_pl(Obj, Mode, List) :-
	'PySequence_Size'(Obj, N),
	py_seq_items(Obj, 0, N, Mode, List).

% A tuple of arity N is a compound -/N. The empty tuple has nowhere else
% to go than the atom '-', since a compound of arity zero is not a term.

py_tuple_to_pl(Obj, Mode, Term) :-
	py_seq_to_pl(Obj, Mode, Items),
	Term =.. [-|Items].

% A dict is a curly term wrapping a comma-list of :/2 - except the empty
% one, which is the ATOM {} and not a compound at all.

py_dict_to_pl(Obj, Mode, Term) :-
	'PyDict_Keys'(Obj, Keys),
	py_check(Keys),
	'PySequence_Size'(Keys, N),
	(   N =:= 0
	->  Term = {}
	;   py_dict_pairs(Obj, Keys, 0, N, Mode, Pairs),
	    py_comma_list(Pairs, Inner),
	    Term = {Inner}
	),
	'Py_DecRef'(Keys).

py_dict_pairs(_, _, I, N, _, []) :-
	I >= N,
	!.
py_dict_pairs(Obj, Keys, I, N, Mode, [K:V|T]) :-
	'PySequence_GetItem'(Keys, I, KeyObj),
	py_check(KeyObj),
	py_to_pl(KeyObj, Mode, K),
	'PyObject_GetItem'(Obj, KeyObj, ValObj),
	'Py_DecRef'(KeyObj),
	py_check(ValObj),
	py_to_pl(ValObj, Mode, V),
	'Py_DecRef'(ValObj),
	J is I + 1,
	py_dict_pairs(Obj, Keys, J, N, Mode, T).

py_comma_list([X], X) :-
	!.
py_comma_list([X|Xs], (X,Rest)) :-
	py_comma_list(Xs, Rest).

% And the same structure taken apart. This cannot be py_comma_list/2 run
% backwards: its first clause cuts, so (a:1,b:2) comes back as the single
% element [(a:1,b:2)] and every dict with more than one key is rejected.

py_uncomma((A,B), [A|T]) :-
	!,
	py_uncomma(B, T).
py_uncomma(X, [X]).

% A set is not a sequence, so it goes through the iterator protocol.
% Element order is not preserved, which the spec says explicitly.

py_set_to_pl(Obj, Mode, py_set(List)) :-
	'PyObject_GetIter'(Obj, Iter),
	py_check(Iter),
	py_iter_items(Iter, Mode, List),
	'Py_DecRef'(Iter).

py_iter_items(Iter, Mode, List) :-
	'PyIter_Next'(Iter, Item),
	(   Item =:= 0
	->  List = []                  % exhausted, or an error phase 5 owns
	;   py_to_pl(Item, Mode, H),
	    'Py_DecRef'(Item),
	    List = [H|T],
	    py_iter_items(Iter, Mode, T)
	).

% Everything the table does not name - bytes, complex, a module, a class,
% an arbitrary instance - becomes an opaque handle. It increfs, so the
% term owns a reference of its own; py_free/1 in phase 4 releases it.

py_opaque(Obj, '$py_obj'(Obj)) :-
	'Py_IncRef'(Obj).


		 /*******************************
		 *      PROLOG -> PYTHON        *
		 *******************************/

%   pl_to_py(+Term, -Obj) is det.
%
%   Returns a new reference; the caller releases it. The clause order
%   matters here too, for the mirror-image reason: every integer is a
%   rational in Trealla, so integer/1 has to be tested before rational/1
%   or every integer leaves as a Fraction.

pl_to_py(Term, _) :-
	var(Term),
	!,
	throw(error(instantiation_error, janus)).
pl_to_py(@(Const), Obj) :-
	!,
	py_const_to_py(Const, Obj).
pl_to_py('$py_obj'(Ptr), Obj) :-
	!,
	Obj = Ptr,
	'Py_IncRef'(Ptr).
pl_to_py(Term, Obj) :-
	integer(Term),
	!,
	py_int_to_py(Term, Obj).
pl_to_py(Term, Obj) :-
	rational(Term),
	!,
	py_rational_to_py(Term, Obj).
pl_to_py(Term, Obj) :-
	float(Term),
	!,
	'PyFloat_FromDouble'(Term, Obj),
	py_check(Obj).
pl_to_py([], Obj) :-
	!,
	'PyList_New'(0, Obj),
	py_check(Obj).
pl_to_py({}, Obj) :-
	!,
	'PyDict_New'(Obj),
	py_check(Obj).
pl_to_py(Term, Obj) :-
	atom(Term),
	!,
	'PyUnicode_FromString'(Term, Obj),
	py_check(Obj).
pl_to_py(Term, Obj) :-
	'$string'(Term),
	!,
	atom_string(A, Term),
	'PyUnicode_FromString'(A, Obj),
	py_check(Obj).
pl_to_py({Inner}, Obj) :-
	!,
	py_dict_to_py(Inner, Obj).
pl_to_py(py_set(List), Obj) :-
	is_list(List),
	!,
	py_set_to_py(List, Obj).
pl_to_py(Term, Obj) :-
	is_list(Term),
	!,
	py_list_to_py(Term, Obj).
pl_to_py(Term, Obj) :-
	compound(Term),
	functor(Term, -, _),
	!,
	py_tuple_to_py(Term, Obj).
pl_to_py(Term, _) :-
	throw(error(type_error(python_term, Term), janus)).

py_const_to_py(none, Obj) :-
	!,
	py_const_(none, Obj),
	'Py_IncRef'(Obj).
py_const_to_py(true, Obj) :-
	!,
	'PyBool_FromLong'(1, Obj),
	py_check(Obj).
py_const_to_py(false, Obj) :-
	!,
	'PyBool_FromLong'(0, Obj),
	py_check(Obj).
py_const_to_py(Other, _) :-
	throw(error(domain_error(py_constant, Other), janus)).

% The mirror of py_int_to_pl: int64 where it fits, base-16 text where it
% does not. A bignum cannot cross the FFI at all - every integer argument
% is guarded by is_smallint - so this is not an optimisation.

% The lower bound is INT64_MIN + 1, not INT64_MIN. Trealla's smallint
% range is asymmetric - is_smallint rejects -9223372036854775808 exactly,
% while -9223372036854775807 and 9223372036854775807 both pass - so the
% one value C would call representable is the one the FFI will not carry.
% It takes the hex path instead, which handles it correctly.

py_int_to_py(N, Obj) :-
	N >= -9223372036854775807,
	N =< 9223372036854775807,
	!,
	'PyLong_FromLongLong'(N, Obj),
	py_check(Obj).
py_int_to_py(N, Obj) :-
	format(atom(Hex), "~16r", [N]),
	'PyLong_FromString'(Hex, 0, 16, Obj),
	py_check(Obj).

% Fraction(Numerator, Denominator), built from two integer OBJECTS and
% never from text: Fraction("<huge>/3") goes through int(str) and hits
% the same decimal digit cap, which is a live limitation in SWI's Janus.

py_rational_to_py(R, Obj) :-
	N is numerator(R),
	D is denominator(R),
	py_int_to_py(N, NO),
	py_int_to_py(D, DO),
	'PyTuple_New'(2, Args),
	py_check(Args),
	'PyTuple_SetItem'(Args, 0, NO, _),        % steals NO
	'PyTuple_SetItem'(Args, 1, DO, _),        % steals DO
	py_type_(fraction, Class),
	'PyObject_CallObject'(Class, Args, Obj),
	'Py_DecRef'(Args),
	py_check(Obj).

py_list_to_py(List, Obj) :-
	length(List, N),
	'PyList_New'(N, Obj),
	py_check(Obj),
	py_fill_seq(List, 0, Obj, list).

py_tuple_to_py(Term, Obj) :-
	Term =.. [-|Items],
	length(Items, N),
	'PyTuple_New'(N, Obj),
	py_check(Obj),
	py_fill_seq(Items, 0, Obj, tuple).

% Both setters STEAL the reference, so nothing is released here.

py_fill_seq([], _, _, _).
py_fill_seq([H|T], I, Obj, Kind) :-
	pl_to_py(H, Item),
	(   Kind == list
	->  'PyList_SetItem'(Obj, I, Item, _)
	;   'PyTuple_SetItem'(Obj, I, Item, _)
	),
	J is I + 1,
	py_fill_seq(T, J, Obj, Kind).

% PyDict_SetItem and PySet_Add do NOT steal, so both sides are released
% after they are stored.

py_dict_to_py(Inner, Obj) :-
	'PyDict_New'(Obj),
	py_check(Obj),
	py_uncomma(Inner, Pairs),
	py_fill_dict(Pairs, Obj).

py_fill_dict([], _).
py_fill_dict([K:V|T], Obj) :-
	!,
	pl_to_py(K, KO),
	pl_to_py(V, VO),
	'PyDict_SetItem'(Obj, KO, VO, _),
	'Py_DecRef'(KO),
	'Py_DecRef'(VO),
	py_fill_dict(T, Obj).
py_fill_dict([Bad|_], _) :-
	throw(error(type_error(py_dict_entry, Bad), janus)).

py_set_to_py(List, Obj) :-
	'PySet_New'(0, Obj),
	py_check(Obj),
	py_fill_set(List, Obj).

py_fill_set([], _).
py_fill_set([H|T], Obj) :-
	pl_to_py(H, Item),
	'PySet_Add'(Obj, Item, _),
	'Py_DecRef'(Item),
	py_fill_set(T, Obj).

%   py_round_trip(+Term, -Back) is det.
%
%   Not part of the interface: the marshaller has no caller until phase
%   2, and this is what phase 1's tests exercise it through.

py_round_trip(Term, Back) :-
	py_gil(( pl_to_py(Term, Obj),
	         py_to_pl(Obj, Back),
	         'Py_DecRef'(Obj) )).


		 /*******************************
		 *      PHASE 2: CALLING        *
		 *******************************/

% The GIL is taken here rather than in phase 4, because this is where
% the one wrapper every Python call passes through gets written, and
% adding it later would mean a sweep through finished code. Trealla's
% threads are real pthreads and the FFI is reentrant across them, so two
% threads entering CPython at once is reachable in ordinary code.
%
% PyGILState_Ensure/Release nest, so an outer py_gil/1 costs an inner
% one nothing. setup_call_cleanup/3 releases on failure and on exception
% as well as on success.

py_gil(Goal) :-
	py_init,
	setup_call_cleanup(
		'PyGILState_Ensure'(State),
		once(Goal),
		'PyGILState_Release'(State)).

% Imported modules are cached. PyImport_ImportModule consults
% sys.modules itself, so this saves the lookup rather than the import.

:- dynamic(py_module_/2).

py_module(Name, Obj) :-
	py_module_(Name, Obj),
	!.
% A failed import raises whatever Python raised - ModuleNotFoundError for
% a module that is not there, but ImportError or SyntaxError for one that
% is there and broken. Turning all three into
% existence_error(python_module, Name) was wrong twice over: it discarded
% the reason, and it reported "no such module" for a module that exists.

py_module(Name, Obj) :-
	'PyImport_ImportModule'(Name, Obj),
	py_check(Obj),
	assertz(py_module_(Name, Obj)).

%   py_resolve(+Expr, -Obj) is det.
%
%   Walk a `:` chain to an object, returning a new reference. `:` is
%   600 xfy, so a:b:c parses as a:(b:c) and the recursion has to handle
%   a chain on the right of the first step, not only on the left.
%
%   The leftmost element decides the starting point: an atom is a module
%   name, anything else is marshalled - which is what makes
%   py_dot('hello':upper(), X) work as well as an object reference.

py_resolve(Expr, _) :-
	var(Expr),
	!,
	throw(error(instantiation_error, janus)).
py_resolve(Target:Rest, Obj) :-
	!,
	py_resolve(Target, Base),
	setup_call_cleanup(true, py_apply(Base, Rest, Obj), 'Py_DecRef'(Base)).
py_resolve(Name, Obj) :-
	atom(Name),
	\+ Name == {},
	!,
	py_module(Name, Obj),
	'Py_IncRef'(Obj).            % the cache keeps its own reference
py_resolve(Term, Obj) :-
	pl_to_py(Term, Obj).

%   py_apply(+Target, +Spec, -Obj) is det.
%
%   Spec is an attribute name, a method call, or another `:` chain.

py_apply(_, Spec, _) :-
	var(Spec),
	!,
	throw(error(instantiation_error, janus)).
py_apply(Target, A:B, Obj) :-
	!,
	py_apply(Target, A, Mid),
	setup_call_cleanup(true, py_apply(Mid, B, Obj), 'Py_DecRef'(Mid)).
% A zero-argument method call. Both reference systems spell this
% `close()`, and both parse it: SWI and XSB each accept a zero-arity
% compound, and SWI gives it a term type of its own - functor/3 there
% rejects it with domain_error(compound_non_zero_arity, close()).
% Trealla has no such term: functor(X, close, 0) yields the ATOM close,
% so the spelling cannot be parsed OR represented here.
%
% A bare atom cannot stand in for it either, because `Obj:name` has to
% keep meaning attribute access - that is what math:pi and sys:maxsize
% are. So a zero-argument call is written '()'(Name). The functor is not
% a possible Python identifier, so nothing can collide with it.
%
% This is the one place the interface cannot be spelled as the spec
% writes it, and phase 7's conformance suites use `f()` throughout. See
% docs/janus-design.md.

py_apply(Target, '()'(Name), Obj) :-
	!,
	must_be(atom, Name),
	'PyObject_GetAttrString'(Target, Name, Fn),
	py_check(Fn),
	setup_call_cleanup(true, py_invoke(Fn, [], Obj), 'Py_DecRef'(Fn)).
py_apply(Target, Name, Obj) :-
	atom(Name),
	!,
	'PyObject_GetAttrString'(Target, Name, Obj),
	py_check(Obj).
py_apply(Target, Call, Obj) :-
	compound(Call),
	!,
	Call =.. [Name|RawArgs],
	'PyObject_GetAttrString'(Target, Name, Fn),
	py_check(Fn),
	setup_call_cleanup(true,
		py_invoke(Fn, RawArgs, Obj),
		'Py_DecRef'(Fn)).
py_apply(_, Spec, _) :-
	throw(error(type_error(py_attribute, Spec), janus)).

% f(a, b, kw=v): positional first, keyword after. A positional argument
% following a keyword one is an error rather than a silent reordering,
% which is what Python itself does with the same syntax.

py_invoke(Fn, RawArgs, Obj) :-
	py_split_args(RawArgs, Positional, Keyword),
	length(Positional, N),
	'PyTuple_New'(N, Args),
	py_check(Args),
	setup_call_cleanup(true,
		(   py_fill_seq(Positional, 0, Args, tuple),
		    py_call_with(Fn, Args, Keyword, Obj)
		),
		'Py_DecRef'(Args)),
	py_check(Obj).

py_call_with(Fn, Args, [], Obj) :-
	!,
	'PyObject_CallObject'(Fn, Args, Obj).
py_call_with(Fn, Args, Keyword, Obj) :-
	'PyDict_New'(Kwargs),
	py_check(Kwargs),
	setup_call_cleanup(true,
		(   py_fill_kwargs(Keyword, Kwargs),
		    'PyObject_Call'(Fn, Args, Kwargs, Obj)
		),
		'Py_DecRef'(Kwargs)).

py_fill_kwargs([], _).
py_fill_kwargs([Name=Value|T], Kwargs) :-
	pl_to_py(Name, KO),
	pl_to_py(Value, VO),
	'PyDict_SetItem'(Kwargs, KO, VO, _),      % does not steal
	'Py_DecRef'(KO),
	'Py_DecRef'(VO),
	py_fill_kwargs(T, Kwargs).

py_split_args([], [], []) :-
	!.
py_split_args([A|As], Positional, Keyword) :-
	(   py_is_keyword(A)
	->  Positional = [],
	    py_all_keywords([A|As], Keyword)
	;   Positional = [A|More],
	    py_split_args(As, More, Keyword)
	).

py_is_keyword(A) :-
	nonvar(A),
	A = (Name=_),
	atom(Name).

py_all_keywords([], []).
py_all_keywords([A|As], [A|Rest]) :-
	(   py_is_keyword(A)
	->  py_all_keywords(As, Rest)
	;   throw(error(domain_error(py_keyword_argument, A), janus))
	).

% Options. py_object(true) is the only one with behaviour and the only
% one both reference systems implement; sizecheck/1 and iter/1 are XSB
% spellings with nothing to do here. An unrecognised option is a domain
% error, which is XSB's behaviour - SWI accepts anything silently, and
% the PIP does not settle it.

py_mode(Options, _) :-
	var(Options),
	!,
	throw(error(instantiation_error, janus)).
py_mode(Options, Mode) :-
	\+ is_list(Options),
	!,
	throw(error(type_error(list, Options), janus)).
py_mode(Options, Mode) :-
	py_check_options(Options),
	(   memberchk(py_object(true), Options)
	->  Mode = exact
	;   Mode = full
	).

py_check_options([]).
py_check_options([O|Os]) :-
	(   py_known_option(O)
	->  py_check_options(Os)
	;   throw(error(domain_error(py_option, O), janus))
	).

py_known_option(py_object(B))  :- py_boolean(B).
py_known_option(sizecheck(B))  :- py_boolean(B).   % XSB, no effect here
py_known_option(iter(B))       :- py_boolean(B).   % XSB, no effect here

py_boolean(true).
py_boolean(false).

%   py_call(+Call, -Return) is det.
%   py_call(+Call, -Return, +Options) is det.
%
%   Call is a `:` chain: a module or object on the left, attributes and
%   method calls to its right.

py_call(Call, Return) :-
	py_call(Call, Return, []).

py_call(Call, Return, Options) :-
	py_mode(Options, Mode),
	py_gil(py_call_(Call, Mode, Return)).

py_call_(Call, Mode, Return) :-
	py_resolve(Call, Obj),
	setup_call_cleanup(true, py_to_pl(Obj, Mode, Return), 'Py_DecRef'(Obj)).

%   py_func(+Module, +Function, -Return) is det.
%   py_dot(+ObjRef, +MethAttr, -Return) is det.
%
%   The two primitives the chain is built from, kept as their own
%   entry points because both reference systems export them.

py_func(Module, Function, Return) :-
	py_func(Module, Function, Return, []).

py_func(Module, Function, Return, Options) :-
	py_call(Module:Function, Return, Options).

py_dot(ObjRef, MethAttr, Return) :-
	py_dot(ObjRef, MethAttr, Return, []).

py_dot(ObjRef, MethAttr, Return, Options) :-
	py_call(ObjRef:MethAttr, Return, Options).

%   py_setattr(+On, +Name, +Value) is det.

py_setattr(On, Name, Value) :-
	must_be(atom, Name),
	py_gil(py_setattr_(On, Name, Value)).

py_setattr_(On, Name, Value) :-
	py_resolve(On, Obj),
	setup_call_cleanup(true,
		(   pl_to_py(Value, VO),
		    setup_call_cleanup(true,
			    'PyObject_SetAttrString'(Obj, Name, VO, R),
			    'Py_DecRef'(VO)),
		    (   R =:= 0
		    ->  true
		    ;   'PyErr_Clear',
			throw(error(permission_error(modify, py_attribute, Name), janus))
		    )
		),
		'Py_DecRef'(Obj)).


		 /*******************************
		 *   PHASE 3: ITERATION         *
		 *******************************/

%   py_iter(+Call, -Value) is nondet.
%   py_iter(+Call, -Value, +Options) is nondet.
%
%   Backtrack over a Python iterable, one PyIter_Next at a time. No
%   non-deterministic foreign predicate is involved: the generator is
%   ordinary Prolog over an iterator handle.
%
%   The handle is the reason for setup_call_cleanup/3. It lives across
%   choice points, so a once/1, a cut or an exception would otherwise
%   abandon it with nothing to release it - the likeliest leak in
%   ordinary code, ahead of a forgotten py_free/1. Trealla runs the
%   cleanup on exhaustion, on cut, on failure and on exception.

py_iter(Call, Value) :-
	py_iter(Call, Value, []).

py_iter(Call, Value, Options) :-
	py_mode(Options, Mode),
	setup_call_cleanup(
		py_iter_open(Call, Iter),
		py_iter_value(Iter, Mode, Value),
		py_iter_close(Iter)).

py_iter_open(Call, Iter) :-
	py_gil(py_iter_open_(Call, Iter)).

py_iter_open_(Call, Iter) :-
	py_resolve(Call, Obj),
	setup_call_cleanup(true,
		( 'PyObject_GetIter'(Obj, Iter), py_check(Iter) ),
		'Py_DecRef'(Obj)).

py_iter_close(Iter) :-
	py_gil('Py_DecRef'(Iter)).

py_iter_value(Iter, Mode, Value) :-
	repeat,
	  py_gil(py_iter_step(Iter, Mode, Step)),
	  (   Step == end
	  ->  !,
	      fail
	  ;   Step = item(Value)      % may fail, which backtracks for the next
	  ).

py_iter_step(Iter, Mode, Step) :-
	'PyIter_Next'(Iter, Item),
	(   Item =:= 0
	->  Step = end
	;   setup_call_cleanup(true, py_to_pl(Item, Mode, Value), 'Py_DecRef'(Item)),
	    Step = item(Value)
	).


		 /*******************************
		 *   PHASE 3: DICT ACCESS       *
		 *******************************/

% Pure term manipulation on the curly form of section 3 - no Python is
% involved, and none of these starts the interpreter. The empty dict is
% the ATOM {}, so it needs its own clause everywhere: it does not unify
% with {Comma}, which is {}/1.

%   keys(+Dict, -Keys) is det.

keys({}, Keys) :-
	!,
	Keys = [].
keys({Comma}, Keys) :-
	!,
	py_comma_keys(Comma, Keys).
keys(Dict, _) :-
	throw(error(type_error(py_dict, Dict), janus)).

py_comma_keys((K:_, T), [K|Ks]) :-
	!,
	py_comma_keys(T, Ks).
py_comma_keys(K:_, [K]).

%   key(+Dict, ?Key) is nondet.

key({}, _) :-
	!,
	fail.
key({Comma}, Key) :-
	!,
	py_comma_pair(Comma, Key, _).
key(Dict, _) :-
	throw(error(type_error(py_dict, Dict), janus)).

%   items(+Dict, -Items) is det.
%
%   Items is a list of Key:Value, the same shape the dict carries.

items({}, Items) :-
	!,
	Items = [].
items({Comma}, Items) :-
	!,
	py_comma_items(Comma, Items).
items(Dict, _) :-
	throw(error(type_error(py_dict, Dict), janus)).

py_comma_items((K:V, T), [K:V|Is]) :-
	!,
	py_comma_items(T, Is).
py_comma_items(K:V, [K:V]).

%   values(+Dict, +KeyOrPath, ?Val) is nondet.
%
%   A list navigates nested dicts. A bound key is a lookup and commits
%   to the first match; an unbound one enumerates, which is XSB's
%   behaviour and more useful than SWI's, where an unbound key is not
%   handled at all.

values(Dict, Path, Val) :-
	is_list(Path),
	!,
	py_values_path(Path, Dict, Val).
values(Dict, Key, Val) :-
	py_values_key(Dict, Key, Val).

py_values_path([], Val, Val).
py_values_path([K|Ks], Dict, Val) :-
	py_values_key(Dict, K, Mid),
	py_values_path(Ks, Mid, Val).

py_values_key({}, _, _) :-
	!,
	fail.
py_values_key({Comma}, Key, Val) :-
	!,
	(   nonvar(Key)
	->  once(py_comma_pair(Comma, Key, Val))
	;   py_comma_pair(Comma, Key, Val)
	).
py_values_key(Dict, _, _) :-
	throw(error(type_error(py_dict, Dict), janus)).

py_comma_pair((K:V, _), K, V).
py_comma_pair((_, T), K, V) :-
	py_comma_pair(T, K, V).
py_comma_pair(K:V, K, V).


		 /*******************************
		 *   PHASE 3: LIBRARY PATHS     *
		 *******************************/

%   py_lib_dirs(-Dirs) is det.
%
%   sys.path, as a list of atoms.

py_lib_dirs(Dirs) :-
	py_call(sys:path, Dirs).

%   py_add_lib_dir(+Dir) is det.
%   py_add_lib_dir(+Dir, +Where) is det.
%
%   Where is first or last; last is the default, matching SWI.

py_add_lib_dir(Dir) :-
	py_add_lib_dir(Dir, last).

py_add_lib_dir(Dir0, Where) :-
	'$must_be_text'(Dir0, Dir),
	(   Where == last
	->  py_call(sys:path:append(Dir), _)
	;   Where == first
	->  py_call(sys:path:insert(0, Dir), _)
	;   throw(error(domain_error(py_lib_dir_position, Where), janus))
	).


		 /*******************************
		 *   PHASE 4: LIFETIME          *
		 *******************************/

%   py_is_object(@Term) is semidet.
%
%   True when Term is an object reference. A type test and nothing more:
%   it says the term IS a handle, not that the handle is still live.
%
%   SWI can do better here - its blob is cleared by py_free/1, so
%   py_is_object/1 goes false afterwards. A Prolog term cannot be cleared
%   from under its copies, so this one cannot. See the ownership rule in
%   section 3 of docs/janus-design.md: using a handle after freeing it is
%   a program error the same way use-after-free is, and detecting it by
%   scanning the store is not attempted.

py_is_object(Term) :-
	nonvar(Term),
	Term = '$py_obj'(Ptr),
	integer(Ptr).

%   py_free(+Obj) is det.
%
%   Release the reference a handle owns. Exactly one reference: the
%   marshaller decrefs everything it translates, so only a handle - an
%   untranslatable object, or one asked for with py_object(true) - ever
%   needs this.
%
%   Freeing twice, or using the handle afterwards, is undefined in the
%   same way free() twice is. That is the price of a term the runtime
%   copies and backtracks freely; py_free/1 being part of the agreed
%   interface is what makes explicit release legitimate in the first
%   place.

py_free(Term) :-
	var(Term),
	!,
	throw(error(instantiation_error, janus)).
py_free('$py_obj'(Ptr)) :-
	integer(Ptr),
	!,
	py_gil('Py_DecRef'(Ptr)).
py_free(Term) :-
	throw(error(type_error(py_object, Term), janus)).

%   py_refcount_(+Obj, -Count) is det.
%
%   Not part of the interface, and not exported: the phase 4 tests need
%   to see the count they are reasoning about, and sys.getrefcount is the
%   only way to. The number is inflated by the reference the call itself
%   holds, so only differences are meaningful.

py_refcount_(Term, Count) :-
	py_call(sys:getrefcount(Term), Count).


		 /*******************************
		 *   PHASE 5a: COMPATIBILITY    *
		 *******************************/

% The six XSB spellings, and the two SWI conveniences section 2 keeps in
% scope. Thin by design: every one of them is a line or two over what
% phases 1-5 already do, and phase 7 runs a suite that calls them by
% these names.

%   py_type(+Term, -Type) is det.
%
%   The Python type name, as an atom. SWI's takes an object reference;
%   this takes anything the marshaller can send, so py_type([1,2], T)
%   answers list rather than erroring.

%   NOTE the helper is py_type_of_/2, not py_type_/2: py_type_/2 is the
%   dynamic fact holding the type-object cache, and adding clauses to it
%   made py_isa/2 match them instead - which crashed every phase, not
%   just this one.

py_type(Term, Type) :-
	py_gil(py_type_of_(Term, Type)).

py_type_of_(Term, Type) :-
	pl_to_py(Term, Obj),
	setup_call_cleanup(true,
		(   'PyObject_Type'(Obj, T),
		    py_check(T),
		    setup_call_cleanup(true, py_type_name(T, Type), 'Py_DecRef'(T))
		),
		'Py_DecRef'(Obj)).

%   py_pp(+Term) is det.
%
%   Pretty-print Term the way Python would show it.
%
%   pformat rather than pprint: the formatted text comes back as an atom
%   and is written by Prolog, so there is no interleaving to get wrong.
%   Printing from the Python side would land in CPython's stdio buffer
%   and appear out of order against Prolog's - the trap in section 8 of
%   the design, avoided rather than worked around.

py_pp(Term) :-
	py_call(pprint:pformat(Term), Text),
	write(Text),
	nl.

%   add_py_lib_dir(+Dir) is det.
%
%   XSB's spelling of py_add_lib_dir/1.

add_py_lib_dir(Dir) :-
	py_add_lib_dir(Dir).

%   obj_dir(+Obj, -Dir) is det.
%   obj_dict(+Obj, -Dict) is det.
%
%   XSB defines both as one py_dot each, and so do these.

obj_dir(Obj, Dir) :-
	py_dot(Obj, '()'('__dir__'), Dir).

obj_dict(Obj, Dict) :-
	py_dot(Obj, '__dict__', Dict).

%   value(+Dict, +Key, -Val) is semidet.
%
%   The deterministic singular of values/3. XSB exports value/3 but ships
%   no clause for it anywhere in the tree, so there is no behaviour to
%   copy - committing to the first match is the only reading the name
%   supports next to values/3, which enumerates.

value(Dict, Key, Val) :-
	once(values(Dict, Key, Val)).

%   janus_python_version(-Version) is det.
%
%   The interpreter version, as an atom: '3.14.7'.
%
%   XSB answers something else entirely - PYTHON_BIN_QUOTED, the path of
%   the python binary that configure found. There is no analogue here,
%   since libpython is opened by dlopen and no binary is involved, and
%   the name plainly describes a version. py_lib/1 is where the path
%   lives if that is what was wanted.

janus_python_version(Version) :-
	py_gil('Py_GetVersion'(Full)),
	(   sub_atom(Full, Before, 1, _, ' ')
	->  sub_atom(Full, 0, Before, _, Version)
	;   Version = Full
	).

%   py_next(+Iterator, -Value) is semidet.
%
%   One step of an iterator handle, failing when it is exhausted. The
%   manual form of py_iter/2, which is what XSB means by "py_next is
%   subsumed by py_iter": use py_iter/2 unless the stepping has to be
%   driven from outside.

py_next(Handle, Value) :-
	(   py_is_object(Handle)
	->  true
	;   var(Handle)
	->  throw(error(instantiation_error, janus))
	;   throw(error(type_error(py_object, Handle), janus))
	),
	Handle = '$py_obj'(Iter),
	py_gil(py_iter_step(Iter, full, Step)),
	Step = item(Value).

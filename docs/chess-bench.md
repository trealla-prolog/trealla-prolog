# samples/chess.pl across Prolog systems

Measured 2026-09-26 on an Apple Silicon Mac (macOS, darwin27), with Trealla re-measured 2026-10-13.
The clause-matching changes since v3.11.4 took Trealla from 4.82s at 10ceae67 to 4.17s at v3.11.5,
4.09s at v3.11.6, 3.81s at v3.11.7 and 3.67s at v3.11.8. v3.13.5 ran 3.72s, and v3.13.6, which
compiles `member/2` calls in clause bodies to a builtin, 2.36s in the same interleaved runs.

Shumway was added 2026-11-01, on a machine that was in use at the time. Trealla v3.14.32 ran
2.50s in those same interleaved runs, so read Shumway against 2.50s, not against the 2.36s below.

The commands below are run from `samples/`.

`main` plays three scripted white moves (e2e4, d2d3, c1e3) and the program
replies with a depth-3 alpha-beta search. Every system gives the same replies:

    e7e5 (book)   g8f6 (100)   h7h5 (100)

Times are the best wall-clock of 3 runs and include startup and loading/compiling
the file, not just the search.

| System | Version | Time | Source changes | Extra options |
|---|---|---|---|---|
| ECLiPSe | 8.0.0 | **0.71s** | none | `-L iso -l 262144` (or larger) |
| BinProlog | #12.08 (C-ified, 64-bit) | 0.84s | none | `-h1048576 -s65536 -t262144` |
| Ciao | 1.25.0 [DARWINaarch64] | 1.04s | none | run via `ciaosh`, not `ciao run` |
| XSB | 5.0.0 (Green Tea) | 1.07s | replace `use_module` with `import` | call the darwin25.6.0 binary directly |
| GNU Prolog | 1.6.0 (git 5976ac5) | 1.61s | none | `LOCALSZ=200000` (or larger) |
| GNU Prolog | 1.5.0 (Homebrew) | 1.69s | none | `LOCALSZ=1000000 GLOBALSZ=1000000` |
| Shumway | 0.9.3 (.NET 10) | 1.84s | none | `DOTNET_ROOT` must point at the SDK |
| SWI-Prolog | 10.1.16 | 1.99s | none | none |
| Trealla | 3.13.6, compiled `member/2` | 2.36s | none | none |
| Scryer | 0.10.0 (git 7dad74d) | 2.53s | none | none |
| CxProlog | 0.98.5 | 3.68s | DCG pre-expanded, `member/2` added, `use_module` removed | none |
| Trealla | 3.13.5, original `member/2` | 3.72s | none | none |

## Commands and what each system needed

### Trealla

    tpl chess.pl -g 'main,halt'

Runs unchanged.

### SWI-Prolog

    swipl -g 'main,halt' chess.pl

Runs unchanged.

### Scryer

    scryer-prolog -g 'main,halt' chess.pl

Runs unchanged.

### Shumway

    shumway chess.pl -g 'main,halt'

Runs unchanged. Needs `DOTNET_ROOT` set when the .NET SDK is not in
`/usr/local/share/dotnet`, or the apphost refuses to start.

- Its user time, 2.16s, exceeds its wall time, because the JIT compiles on
  other threads. On one core it would be slower than the wall clock suggests.
- `shumway-link --exe` would not link this program: `library(lists)` is built
  into the top level and the linker finds no `lists` module on its library
  path, so there is no link-time-optimized figure here.

### BinProlog

    echo "['chess.pl'], main, halt." | bp -h1048576 -s65536 -t262144 -q5

- The default 2 MB choice/local stack (`-s`) overflows on the first search:
  `*** choice overflow by 8 bytes ... culprit=>unify`.
- Raising only `-s` (to 16 MB) gets past that, but then the heap garbage collector
  breaks (`different number of forwarded than space allocated`,
  `more marked then forwarded=-16728`, `Not enough memory recovered during GC`)
  and the program prints hundreds of junk moves. A 1 GB heap (`-h1048576`)
  means the collector never runs, and the output is correct.
- `use_module(library(lists))` is reported as `undefined_predicate_in_metacall`,
  but that is harmless because `member/2` is built in.
- Singleton-variable warnings for the `book/4` clauses are also harmless.

### GNU Prolog

1.6.0, built from `~/gprolog` master (5976ac5) and installed to `~/.local/gprolog-1.6.0`,
linked from `~/.local/bin`:

    cd ~/gprolog/src
    ./configure --prefix=$HOME/.local --with-install-dir=$HOME/.local/gprolog-1.6.0 \
        --with-links-dir=$HOME/.local/bin --without-doc-dir --without-html-dir --without-examples-dir
    make            # not -j: the build runs its own freshly built gplc, and parallel make races it
    make check
    make install

    LOCALSZ=200000 gprolog --consult-file chess.pl --query-goal 'main,halt'

- The default local stack (50 MB in 1.6.0) overflows after the first move. 100 MB
  still overflows on the second search, and 200 MB is enough.
- The default global stack is now large enough.

1.5.0 (Homebrew build, since uninstalled):

    LOCALSZ=1000000 GLOBALSZ=1000000 gprolog --consult-file chess.pl --query-goal 'main,halt'

- With default sizes: `local stack overflow (size: 16384 Kb)` after the first move.
- With a bigger local stack only: `global stack overflow (size: 32768 Kb)` after the
  second move.
- 1 GB for both works. `GLOBALSZ=4000000` fails at startup, before any output.

### ECLiPSe

    eclipse -L iso -l 262144 -f chess.pl -e main

- The default language has no `atom_codes/2`, so `-L iso` is required.
- With the default stack it overflows on the second search
  (`Overflow of the local/control stack`, peak local 83 MB, control 48 MB).
  `-l 262144` (256 MB) is enough.

### Ciao

    echo "ensure_loaded('chess.pl'), main." | ciaosh

- `ciao run chess.pl` expects `main/1`, and the sample defines `main/0`.
- Loading writes `chess.itf` and `chess.po` next to the source, so delete them afterwards.
- Warnings about `play/0` vs `play/1` and singleton variables are harmless.

### XSB

    sed 's/^:- use_module(library(lists))\./:- import member\/2 from basics./' chess.pl > chess_xsb.pl
    echo "[chess_xsb], main, halt." | ~/xsb-code/XSB/config/aarch64-apple-darwin25.6.0/bin/xsb

- The `xsb` wrapper script looks for a build that matches the current OS version
  (`aarch64-apple-darwin27.0.0`) and only darwin25.x builds exist, so call a
  config binary directly.
- `:- use_module(library(lists)).` aborts loading. Deleting the line alone
  isn't enough either (`No predicate usermod : member / 2`), so import `member/2`
  from `basics` instead.
- Loading writes `chess_xsb.xwam` next to the source.

### CxProlog

    echo "consult('chess_cx.pl'), main, halt." | cxprolog

CxProlog has no DCG translation, no `member/2` and no `use_module/1`, so it
gets a pre-processed copy:

1. Remove the `use_module(library(lists))` line.
2. Expand the DCG rule (`test_play -->`) with SWI:

       x :- open('chess_nomod.pl',read,I), open('chess_cx.pl',write,O),
         repeat, read_term(I,T,[]),
         ( T == end_of_file -> !
         ; expand_term(T,E),
           ( is_list(E) -> forall(member(C,E),portray_clause(O,C)) ; portray_clause(O,E) ),
           fail ),
         close(I), close(O).

   Then delete the `non_terminal/1` directive that SWI emits.
3. Add `member/2`:

       member(X,[X|_]).
       member(X,[_|T]) :- member(X,T).

## Notes

- Several systems needed stacks far above their defaults (BinProlog, GNU Prolog,
  ECLiPSe). Trealla, SWI, Scryer, Ciao, XSB, CxProlog and Shumway manage with their defaults.
- Trealla 3.13.6 sits between SWI and Scryer: about 2.8-3.3× BinProlog and ECLiPSe, 1.2× SWI, and 7%
  ahead of Scryer. Before compiling `member/2` it was level with CxProlog at the bottom.
- Shumway is 1.36× ahead of Trealla on the 2026-11-01 interleaved runs. Peak RSS in those
  runs separates the systems more than time does: Scryer 225 MB, Shumway 334 MB, SWI 542 MB,
  Trealla 845 MB. This is a deep alpha-beta search that reclaims nothing on cut, so it is
  the kind of load where a compacting heap collector tells.

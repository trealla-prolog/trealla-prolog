% Draws four PLplot plots on one page into samples/plplot.svg.
%
%     tpl -g run,halt samples/test_plplot.pl
%
% With Homebrew on macOS, prefix that with DYLD_LIBRARY_PATH=/opt/homebrew/lib

:- use_module(library(plplot)).
:- use_module(library(lists)).

run :-
	plsdev(svg),
	plsfnam('samples/plplot.svg'),
	plssub(2, 2),
	plinit,
	lines,
	error_bars,
	histogram,
	surface,
	plend,
	write('Wrote samples/plplot.svg'), nl.

% 600 points: plline splits these across calls, which the curves shouldn't show.

lines :-
	plcol0(1),
	plenv(0, 6.3, -1.2, 1.2, 0, 0),
	pllab(x, y, 'sin(x) and cos(x)'),
	findall(X, (between(0, 599, I), X is I * 6.3 / 599), Xs),
	findall(Y, (member(X, Xs), Y is sin(X)), Sin),
	findall(Y, (member(X, Xs), Y is cos(X)), Cos),
	plwidth(2),
	plcol0(2), plline(Xs, Sin),
	plcol0(3), plline(Xs, Cos),
	plwidth(1).

error_bars :-
	plcol0(1),
	plenv(0, 11, 0, 30, 0, 0),
	pllab(x, y, 'Points with error bars'),
	findall(X, between(1, 10, X), Xs),
	findall(Y, (member(X, Xs), Y is 2.5 * X + 2 * sin(X)), Ys),
	findall(Lo, (member(Y, Ys), Lo is Y - 2), Los),
	findall(Hi, (member(Y, Ys), Hi is Y + 2), His),
	plcol0(9), plerry(Xs, Los, His),
	plcol0(2), plpoin(Xs, Ys, 17).

% plseed makes the samples, and so the plot, the same on every run.

histogram :-
	plseed(42),
	findall(V, (between(1, 250, _), gaussian(V)), Data),
	plcol0(1),
	plhist(Data, -3, 3, 24, 0),
	pllab(value, count, '250 normal samples').

gaussian(V) :-
	plrandd(U1),
	plrandd(U2),
	V is sqrt(-2 * log(1 - U1)) * cos(2 * pi * U2).

surface :-
	pladv(0),
	plvpor(0, 1, 0, 0.9),
	plwind(-1, 1, -0.9, 1.1),
	plcol0(1),
	plw3d(1, 1, 1, -1.5, 1.5, -1.5, 1.5, -1, 1, 33, 24),
	plbox3(bnstu, x, 0, 0, bnstu, y, 0, 0, bcdmnstuv, z, 0, 0),
	findall(X, (between(0, 39, I), X is -1.5 + 3 * I / 39), Xs),
	findall(Row, (member(X, Xs), findall(Z, (member(Y, Xs), ripple(X, Y, Z)), Row)), Zs),
	plscmap1l(0, [0,1], [240,0], [0.6,0.6], [0.8,0.8], []),
	plplot_const('MAG_COLOR', Mag),
	plplot_const('FACETED', Faceted),
	Opt is Mag \/ Faceted,
	plsurf3d(Xs, Xs, Zs, Opt, []),
	plcol0(2),
	plmtex(t, 1, 0.5, 0.5, 'exp(-r^2) cos(2 pi r)').

ripple(X, Y, Z) :-
	R is sqrt(X * X + Y * Y),
	Z is exp(-R * R) * cos(2 * pi * R).

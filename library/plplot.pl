:- module(plplot, [
	% Setup and teardown
	plsdev/1,
	plsfnam/1,
	plsetopt/2,
	plinit/0,
	plstar/2,
	plstart/3,
	plend/0,
	plend1/0,
	plsstrm/1,
	plgstrm/1,
	plmkstrm/1,
	plsori/1,
	plspage/6,
	plgpage/6,
	plsdiori/1,
	plsdidev/4,
	plsdiplt/4,
	plsdiplz/4,
	plscompression/1,
	plsfam/3,
	plspause/1,
	plseed/1,
	plrandd/1,

	% Pages
	plssub/2,
	pladv/1,
	plbop/0,
	pleop/0,
	plclear/0,
	plflush/0,
	plreplot/0,
	plgra/0,
	pltext/0,

	% Viewports and windows
	plenv/6,
	plenv0/6,
	plvpor/4,
	plsvpa/4,
	plvpas/5,
	plvasp/1,
	plvsta/0,
	plwind/4,
	plgvpd/4,
	plgvpw/4,
	plgspa/4,
	plw3d/11,

	% Axes, labels and text
	plbox/6,
	plaxes/8,
	plbox3/12,
	pllab/3,
	plmtex/5,
	plmtex3/5,
	plptex/6,
	plptex3/11,
	plschr/2,
	plgchr/2,
	plsmaj/2,
	plsmin/2,
	plssym/2,
	plsxax/2,
	plsyax/2,
	plszax/2,
	plgxax/2,
	plgyax/2,
	plgzax/2,
	plprec/2,
	plfont/1,
	plfontld/1,
	plsfont/3,
	plgfont/3,
	plsesc/1,
	pltimefmt/1,

	% Colour
	plcol0/1,
	plcol1/1,
	plscolor/1,
	plscol0/4,
	plscol0a/5,
	plgcol0/4,
	plscolbg/3,
	plscolbga/4,
	plgcolbg/3,
	plscmap0n/1,
	plscmap1n/1,
	plscmap0/3,
	plscmap1/3,
	plscmap1l/6,
	plscmap1_range/2,
	plgcmap1_range/2,
	plspal0/1,
	plspal1/2,

	% Line and fill styles
	pllsty/1,
	plwidth/1,
	plpsty/1,
	plpat/2,

	% 2D drawing
	plline/2,
	pljoin/4,
	plpoin/3,
	plsym/3,
	plstring/3,
	plfill/2,
	plgradient/3,
	plarc/8,
	plerrx/3,
	plerry/3,
	plhist/5,
	plbin/3,

	% 3D drawing
	plline3/3,
	plpoin3/4,
	plstring3/4,
	plfill3/3,
	plmesh/4,
	plot3d/5,
	plsurf3d/5,

	plplot_const/2
	]).

% PLplot bindings. Written against PLplot 5.15.
%
% MACOS: brew install plplot, then run with DYLD_LIBRARY_PATH=/opt/homebrew/lib
% UBUNTU: sudo apt install libplplot-dev
%
% REF: https://plplot.sourceforge.net/docbook-manual/
%
% The pl* predicates follow the C API with the array lengths dropped: arrays
% are lists, matrices are lists of rows, and a PLFLT may be given as an
% integer. Text may be an atom or a double-quoted string.
%
%     plsdev(svg), plsfnam('sine.svg'), plinit,
%     plenv(0, 6.3, -1, 1, 0, 0), pllab(x, 'sin(x)', 'Sine'),
%     findall(X, (between(0, 63, I), X is I / 10), Xs),
%     findall(Y, (member(X, Xs), Y is sin(X)), Ys),
%     plline(Xs, Ys), plend.
%
% Arrays reach C through '$struct_to_pointer'/2, which packs at most 255
% elements, and are freed as soon as the call returns. plline, plpoin, plsym,
% plstring, plerrx, plerry and their 3D forms split longer lists across
% calls; the rest raise a representation_error past 255, so a mesh or
% surface is at most 255 x 255.
%
% Not bound: anything taking a callback (plcont, plshade, plshades,
% plimagefr, plmap, plslabelfunc, plstransform), anything filling a
% caller's buffer (plgver, plgdev, plgfnam), pllegend and plcolorbar.

:- use_module(library(iso_ext)).

% 255 fields is MAX_FFI_STRUCT_FIELDS, so these are the longest arrays.

:- foreign_struct(plflt_vector, [
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double, double,double,double,double,double,
	double,double,double,double,double
	]).

:- foreign_struct(plint_vector, [
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint, sint,sint,sint,sint,sint,
	sint,sint,sint,sint,sint
	]).

:- foreign_struct(plptr_vector, [
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr, ptr,ptr,ptr,ptr,ptr,
	ptr,ptr,ptr,ptr,ptr
	]).

:- use_foreign_module('libplplot.so', [
	c_plsdev([cstr], void),
	c_plsfnam([cstr], void),
	c_plsetopt([cstr,cstr], sint),
	c_plinit([], void),
	c_plstar([sint,sint], void),
	c_plstart([cstr,sint,sint], void),
	c_plend([], void),
	c_plend1([], void),
	c_plsstrm([sint], void),
	c_plgstrm([-sint], void),
	c_plmkstrm([-sint], void),
	c_plsori([sint], void),
	c_plspage([double,double,sint,sint,sint,sint], void),
	c_plgpage([-double,-double,-sint,-sint,-sint,-sint], void),
	c_plsdiori([double], void),
	c_plsdidev([double,double,double,double], void),
	c_plsdiplt([double,double,double,double], void),
	c_plsdiplz([double,double,double,double], void),
	c_plscompression([sint], void),
	c_plsfam([sint,sint,sint], void),
	c_plspause([sint], void),
	c_plseed([uint], void),
	c_plrandd([], double),

	c_plssub([sint,sint], void),
	c_pladv([sint], void),
	c_plbop([], void),
	c_pleop([], void),
	c_plclear([], void),
	c_plflush([], void),
	c_plreplot([], void),
	c_plgra([], void),
	c_pltext([], void),

	c_plenv([double,double,double,double,sint,sint], void),
	c_plenv0([double,double,double,double,sint,sint], void),
	c_plvpor([double,double,double,double], void),
	c_plsvpa([double,double,double,double], void),
	c_plvpas([double,double,double,double,double], void),
	c_plvasp([double], void),
	c_plvsta([], void),
	c_plwind([double,double,double,double], void),
	c_plgvpd([-double,-double,-double,-double], void),
	c_plgvpw([-double,-double,-double,-double], void),
	c_plgspa([-double,-double,-double,-double], void),
	c_plw3d([double,double,double,double,double,double,double,double,double,double,double], void),

	c_plbox([cstr,double,sint,cstr,double,sint], void),
	c_plaxes([double,double,cstr,double,sint,cstr,double,sint], void),
	c_plbox3([cstr,cstr,double,sint,cstr,cstr,double,sint,cstr,cstr,double,sint], void),
	c_pllab([cstr,cstr,cstr], void),
	c_plmtex([cstr,double,double,double,cstr], void),
	c_plmtex3([cstr,double,double,double,cstr], void),
	c_plptex([double,double,double,double,double,cstr], void),
	c_plptex3([double,double,double,double,double,double,double,double,double,double,cstr], void),
	c_plschr([double,double], void),
	c_plgchr([-double,-double], void),
	c_plsmaj([double,double], void),
	c_plsmin([double,double], void),
	c_plssym([double,double], void),
	c_plsxax([sint,sint], void),
	c_plsyax([sint,sint], void),
	c_plszax([sint,sint], void),
	c_plgxax([-sint,-sint], void),
	c_plgyax([-sint,-sint], void),
	c_plgzax([-sint,-sint], void),
	c_plprec([sint,sint], void),
	c_plfont([sint], void),
	c_plfontld([sint], void),
	c_plsfont([sint,sint,sint], void),
	c_plgfont([-sint,-sint,-sint], void),
	c_plsesc([schar], void),
	c_pltimefmt([cstr], void),

	c_plcol0([sint], void),
	c_plcol1([double], void),
	c_plscolor([sint], void),
	c_plscol0([sint,sint,sint,sint], void),
	c_plscol0a([sint,sint,sint,sint,double], void),
	c_plgcol0([sint,-sint,-sint,-sint], void),
	c_plscolbg([sint,sint,sint], void),
	c_plscolbga([sint,sint,sint,double], void),
	c_plgcolbg([-sint,-sint,-sint], void),
	c_plscmap0n([sint], void),
	c_plscmap1n([sint], void),
	c_plscmap0([ptr,ptr,ptr,sint], void),
	c_plscmap1([ptr,ptr,ptr,sint], void),
	c_plscmap1l([sint,sint,ptr,ptr,ptr,ptr,ptr], void),
	c_plscmap1_range([double,double], void),
	c_plgcmap1_range([-double,-double], void),
	c_plspal0([cstr], void),
	c_plspal1([cstr,sint], void),

	c_pllsty([sint], void),
	c_plwidth([double], void),
	c_plpsty([sint], void),
	c_plpat([sint,ptr,ptr], void),

	c_plline([sint,ptr,ptr], void),
	c_pljoin([double,double,double,double], void),
	c_plpoin([sint,ptr,ptr,sint], void),
	c_plsym([sint,ptr,ptr,sint], void),
	c_plstring([sint,ptr,ptr,cstr], void),
	c_plfill([sint,ptr,ptr], void),
	c_plgradient([sint,ptr,ptr,double], void),
	c_plarc([double,double,double,double,double,double,double,sint], void),
	c_plerrx([sint,ptr,ptr,ptr], void),
	c_plerry([sint,ptr,ptr,ptr], void),
	c_plhist([sint,ptr,double,double,sint,sint], void),
	c_plbin([sint,ptr,ptr,sint], void),

	c_plline3([sint,ptr,ptr,ptr], void),
	c_plpoin3([sint,ptr,ptr,ptr,sint], void),
	c_plstring3([sint,ptr,ptr,ptr,cstr], void),
	c_plfill3([sint,ptr,ptr,ptr], void),
	c_plmesh([ptr,ptr,ptr,sint,sint,sint], void),
	c_plot3d([ptr,ptr,ptr,sint,sint,sint,sint], void),
	c_plsurf3d([ptr,ptr,ptr,sint,sint,sint,ptr,sint], void)
	]).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

plplot_const('PL_X_AXIS', 1).
plplot_const('PL_Y_AXIS', 2).
plplot_const('PL_Z_AXIS', 3).

plplot_const('PL_BIN_DEFAULT', 0x0).
plplot_const('PL_BIN_CENTRED', 0x1).
plplot_const('PL_BIN_NOEXPAND', 0x2).
plplot_const('PL_BIN_NOEMPTY', 0x4).

plplot_const('PL_HIST_DEFAULT', 0x00).
plplot_const('PL_HIST_NOSCALING', 0x01).
plplot_const('PL_HIST_IGNORE_OUTLIERS', 0x02).
plplot_const('PL_HIST_NOEXPAND', 0x08).
plplot_const('PL_HIST_NOEMPTY', 0x10).

plplot_const('PL_POSITION_NULL', 0x0).
plplot_const('PL_POSITION_LEFT', 0x1).
plplot_const('PL_POSITION_RIGHT', 0x2).
plplot_const('PL_POSITION_TOP', 0x4).
plplot_const('PL_POSITION_BOTTOM', 0x8).
plplot_const('PL_POSITION_INSIDE', 0x10).
plplot_const('PL_POSITION_OUTSIDE', 0x20).
plplot_const('PL_POSITION_VIEWPORT', 0x40).
plplot_const('PL_POSITION_SUBPAGE', 0x80).

plplot_const('PL_DRAWMODE_UNKNOWN', 0x0).
plplot_const('PL_DRAWMODE_DEFAULT', 0x1).
plplot_const('PL_DRAWMODE_REPLACE', 0x2).
plplot_const('PL_DRAWMODE_XOR', 0x4).

plplot_const('PL_FCI_MARK', 0x80000000).
plplot_const('PL_FCI_FAMILY', 0x0).
plplot_const('PL_FCI_STYLE', 0x1).
plplot_const('PL_FCI_WEIGHT', 0x2).
plplot_const('PL_FCI_SANS', 0x0).
plplot_const('PL_FCI_SERIF', 0x1).
plplot_const('PL_FCI_MONO', 0x2).
plplot_const('PL_FCI_SCRIPT', 0x3).
plplot_const('PL_FCI_SYMBOL', 0x4).
plplot_const('PL_FCI_UPRIGHT', 0x0).
plplot_const('PL_FCI_ITALIC', 0x1).
plplot_const('PL_FCI_OBLIQUE', 0x2).
plplot_const('PL_FCI_MEDIUM', 0x0).
plplot_const('PL_FCI_BOLD', 0x1).

plplot_const('DRAW_LINEX', 0x001).
plplot_const('DRAW_LINEY', 0x002).
plplot_const('DRAW_LINEXY', 0x003).
plplot_const('MAG_COLOR', 0x004).
plplot_const('BASE_CONT', 0x008).
plplot_const('TOP_CONT', 0x010).
plplot_const('SURF_CONT', 0x020).
plplot_const('DRAW_SIDES', 0x040).
plplot_const('FACETED', 0x080).
plplot_const('MESH', 0x100).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Setup and teardown

plsdev(Dev) :- text_(Dev, D), c_plsdev(D).
plsfnam(File) :- text_(File, F), c_plsfnam(F).
plsetopt(Opt, Arg) :- text_(Opt, O), text_(Arg, A), c_plsetopt(O, A, 0).
plinit :- c_plinit.
plstar(Nx, Ny) :- c_plstar(Nx, Ny).
plstart(Dev, Nx, Ny) :- text_(Dev, D), c_plstart(D, Nx, Ny).
plend :- c_plend.
plend1 :- c_plend1.
plsstrm(Strm) :- c_plsstrm(Strm).
plgstrm(Strm) :- c_plgstrm(Strm).
plmkstrm(Strm) :- c_plmkstrm(Strm).
plsori(Ori) :- c_plsori(Ori).

plspage(Xp, Yp, Xleng, Yleng, Xoff, Yoff) :-
	floats_([Xp,Yp], [Xp1,Yp1]),
	c_plspage(Xp1, Yp1, Xleng, Yleng, Xoff, Yoff).

plgpage(Xp, Yp, Xleng, Yleng, Xoff, Yoff) :- c_plgpage(Xp, Yp, Xleng, Yleng, Xoff, Yoff).
plsdiori(Rot) :- R is float(Rot), c_plsdiori(R).

plsdidev(Mar, Aspect, Jx, Jy) :-
	floats_([Mar,Aspect,Jx,Jy], [A,B,C,D]),
	c_plsdidev(A, B, C, D).

plsdiplt(Xmin, Ymin, Xmax, Ymax) :-
	floats_([Xmin,Ymin,Xmax,Ymax], [A,B,C,D]),
	c_plsdiplt(A, B, C, D).

plsdiplz(Xmin, Ymin, Xmax, Ymax) :-
	floats_([Xmin,Ymin,Xmax,Ymax], [A,B,C,D]),
	c_plsdiplz(A, B, C, D).

plscompression(Compression) :- c_plscompression(Compression).
plsfam(Fam, Num, Bmax) :- c_plsfam(Fam, Num, Bmax).
plspause(Pause) :- c_plspause(Pause).
plseed(Seed) :- c_plseed(Seed).
plrandd(R) :- c_plrandd(R).

% Pages

plssub(Nx, Ny) :- c_plssub(Nx, Ny).
pladv(Page) :- c_pladv(Page).
plbop :- c_plbop.
pleop :- c_pleop.
plclear :- c_plclear.
plflush :- c_plflush.
plreplot :- c_plreplot.
plgra :- c_plgra.
pltext :- c_pltext.

% Viewports and windows

plenv(Xmin, Xmax, Ymin, Ymax, Just, Axis) :-
	floats_([Xmin,Xmax,Ymin,Ymax], [A,B,C,D]),
	c_plenv(A, B, C, D, Just, Axis).

plenv0(Xmin, Xmax, Ymin, Ymax, Just, Axis) :-
	floats_([Xmin,Xmax,Ymin,Ymax], [A,B,C,D]),
	c_plenv0(A, B, C, D, Just, Axis).

plvpor(Xmin, Xmax, Ymin, Ymax) :-
	floats_([Xmin,Xmax,Ymin,Ymax], [A,B,C,D]),
	c_plvpor(A, B, C, D).

plsvpa(Xmin, Xmax, Ymin, Ymax) :-
	floats_([Xmin,Xmax,Ymin,Ymax], [A,B,C,D]),
	c_plsvpa(A, B, C, D).

plvpas(Xmin, Xmax, Ymin, Ymax, Aspect) :-
	floats_([Xmin,Xmax,Ymin,Ymax,Aspect], [A,B,C,D,E]),
	c_plvpas(A, B, C, D, E).

plvasp(Aspect) :- A is float(Aspect), c_plvasp(A).
plvsta :- c_plvsta.

plwind(Xmin, Xmax, Ymin, Ymax) :-
	floats_([Xmin,Xmax,Ymin,Ymax], [A,B,C,D]),
	c_plwind(A, B, C, D).

plgvpd(Xmin, Xmax, Ymin, Ymax) :- c_plgvpd(Xmin, Xmax, Ymin, Ymax).
plgvpw(Xmin, Xmax, Ymin, Ymax) :- c_plgvpw(Xmin, Xmax, Ymin, Ymax).
plgspa(Xmin, Xmax, Ymin, Ymax) :- c_plgspa(Xmin, Xmax, Ymin, Ymax).

plw3d(Basex, Basey, Height, Xmin, Xmax, Ymin, Ymax, Zmin, Zmax, Alt, Az) :-
	floats_([Basex,Basey,Height,Xmin,Xmax,Ymin,Ymax,Zmin,Zmax,Alt,Az], [A,B,C,D,E,F,G,H,I,J,K]),
	c_plw3d(A, B, C, D, E, F, G, H, I, J, K).

% Axes, labels and text

plbox(Xopt, Xtick, Nxsub, Yopt, Ytick, Nysub) :-
	text_(Xopt, XO), text_(Yopt, YO),
	floats_([Xtick,Ytick], [XT,YT]),
	c_plbox(XO, XT, Nxsub, YO, YT, Nysub).

plaxes(X0, Y0, Xopt, Xtick, Nxsub, Yopt, Ytick, Nysub) :-
	text_(Xopt, XO), text_(Yopt, YO),
	floats_([X0,Y0,Xtick,Ytick], [A,B,XT,YT]),
	c_plaxes(A, B, XO, XT, Nxsub, YO, YT, Nysub).

plbox3(Xopt, Xlabel, Xtick, Nxsub, Yopt, Ylabel, Ytick, Nysub, Zopt, Zlabel, Ztick, Nzsub) :-
	text_(Xopt, XO), text_(Xlabel, XL),
	text_(Yopt, YO), text_(Ylabel, YL),
	text_(Zopt, ZO), text_(Zlabel, ZL),
	floats_([Xtick,Ytick,Ztick], [XT,YT,ZT]),
	c_plbox3(XO, XL, XT, Nxsub, YO, YL, YT, Nysub, ZO, ZL, ZT, Nzsub).

pllab(Xlabel, Ylabel, Tlabel) :-
	text_(Xlabel, X), text_(Ylabel, Y), text_(Tlabel, T),
	c_pllab(X, Y, T).

plmtex(Side, Disp, Pos, Just, Text) :-
	text_(Side, S), text_(Text, T),
	floats_([Disp,Pos,Just], [D,P,J]),
	c_plmtex(S, D, P, J, T).

plmtex3(Side, Disp, Pos, Just, Text) :-
	text_(Side, S), text_(Text, T),
	floats_([Disp,Pos,Just], [D,P,J]),
	c_plmtex3(S, D, P, J, T).

plptex(X, Y, Dx, Dy, Just, Text) :-
	text_(Text, T),
	floats_([X,Y,Dx,Dy,Just], [A,B,C,D,E]),
	c_plptex(A, B, C, D, E, T).

plptex3(Wx, Wy, Wz, Dx, Dy, Dz, Sx, Sy, Sz, Just, Text) :-
	text_(Text, T),
	floats_([Wx,Wy,Wz,Dx,Dy,Dz,Sx,Sy,Sz,Just], [A,B,C,D,E,F,G,H,I,J]),
	c_plptex3(A, B, C, D, E, F, G, H, I, J, T).

plschr(Def, Scale) :- floats_([Def,Scale], [D,S]), c_plschr(D, S).
plgchr(Def, Ht) :- c_plgchr(Def, Ht).
plsmaj(Def, Scale) :- floats_([Def,Scale], [D,S]), c_plsmaj(D, S).
plsmin(Def, Scale) :- floats_([Def,Scale], [D,S]), c_plsmin(D, S).
plssym(Def, Scale) :- floats_([Def,Scale], [D,S]), c_plssym(D, S).
plsxax(Digmax, Digits) :- c_plsxax(Digmax, Digits).
plsyax(Digmax, Digits) :- c_plsyax(Digmax, Digits).
plszax(Digmax, Digits) :- c_plszax(Digmax, Digits).
plgxax(Digmax, Digits) :- c_plgxax(Digmax, Digits).
plgyax(Digmax, Digits) :- c_plgyax(Digmax, Digits).
plgzax(Digmax, Digits) :- c_plgzax(Digmax, Digits).
plprec(Setp, Prec) :- c_plprec(Setp, Prec).
plfont(Font) :- c_plfont(Font).
plfontld(Fnt) :- c_plfontld(Fnt).
plsfont(Family, Style, Weight) :- c_plsfont(Family, Style, Weight).
plgfont(Family, Style, Weight) :- c_plgfont(Family, Style, Weight).
plsesc(Esc) :- char_code(Esc, C), c_plsesc(C).
pltimefmt(Fmt) :- text_(Fmt, F), c_pltimefmt(F).

% Colour

plcol0(Icol0) :- c_plcol0(Icol0).
plcol1(Col1) :- C is float(Col1), c_plcol1(C).
plscolor(Color) :- c_plscolor(Color).
plscol0(Icol0, R, G, B) :- c_plscol0(Icol0, R, G, B).
plscol0a(Icol0, R, G, B, Alpha) :- A is float(Alpha), c_plscol0a(Icol0, R, G, B, A).
plgcol0(Icol0, R, G, B) :- c_plgcol0(Icol0, R, G, B).
plscolbg(R, G, B) :- c_plscolbg(R, G, B).
plscolbga(R, G, B, Alpha) :- A is float(Alpha), c_plscolbga(R, G, B, A).
plgcolbg(R, G, B) :- c_plgcolbg(R, G, B).
plscmap0n(Ncol0) :- c_plscmap0n(Ncol0).
plscmap1n(Ncol1) :- c_plscmap1n(Ncol1).

plscmap0(Rs, Gs, Bs) :-
	bounded_(plscmap0/3, [Rs,Gs,Bs], N),
	with_arrays_([i(Rs,R), i(Gs,G), i(Bs,B)], c_plscmap0(R, G, B, N)).

plscmap1(Rs, Gs, Bs) :-
	bounded_(plscmap1/3, [Rs,Gs,Bs], N),
	with_arrays_([i(Rs,R), i(Gs,G), i(Bs,B)], c_plscmap1(R, G, B, N)).

% An empty AltHuePath passes NULL, as PLplot allows.

plscmap1l(Itype, Intensity, Coord1, Coord2, Coord3, AltHuePath) :-
	bounded_(plscmap1l/6, [Intensity,Coord1,Coord2,Coord3], N),
	Arrays = [f(Intensity,I), f(Coord1,C1), f(Coord2,C2), f(Coord3,C3)],
	(	AltHuePath == []
	->	Alt = 0, Arrays1 = Arrays
	;	bounded_(plscmap1l/6, [Intensity,AltHuePath], _),
		Arrays1 = [i(AltHuePath,Alt)|Arrays]
	),
	with_arrays_(Arrays1, c_plscmap1l(Itype, N, I, C1, C2, C3, Alt)).

plscmap1_range(Min, Max) :- floats_([Min,Max], [A,B]), c_plscmap1_range(A, B).
plgcmap1_range(Min, Max) :- c_plgcmap1_range(Min, Max).
plspal0(File) :- text_(File, F), c_plspal0(F).
plspal1(File, Interpolate) :- text_(File, F), c_plspal1(F, Interpolate).

% Line and fill styles

pllsty(Lin) :- c_pllsty(Lin).
plwidth(Width) :- W is float(Width), c_plwidth(W).
plpsty(Patt) :- c_plpsty(Patt).

plpat(Incs, Dels) :-
	bounded_(plpat/2, [Incs,Dels], N),
	with_arrays_([i(Incs,I), i(Dels,D)], c_plpat(N, I, D)).

% 2D drawing

plline(Xs, Ys) :- chunked_(plline/2, [Xs,Ys], 1, line_).

line_(N, [Xs,Ys]) :- with_arrays_([f(Xs,X), f(Ys,Y)], c_plline(N, X, Y)).

pljoin(X1, Y1, X2, Y2) :-
	floats_([X1,Y1,X2,Y2], [A,B,C,D]),
	c_pljoin(A, B, C, D).

plpoin(Xs, Ys, Code) :- chunked_(plpoin/3, [Xs,Ys], 0, poin_(Code)).

poin_(Code, N, [Xs,Ys]) :- with_arrays_([f(Xs,X), f(Ys,Y)], c_plpoin(N, X, Y, Code)).

plsym(Xs, Ys, Code) :- chunked_(plsym/3, [Xs,Ys], 0, sym_(Code)).

sym_(Code, N, [Xs,Ys]) :- with_arrays_([f(Xs,X), f(Ys,Y)], c_plsym(N, X, Y, Code)).

plstring(Xs, Ys, Text) :-
	text_(Text, T),
	chunked_(plstring/3, [Xs,Ys], 0, string_(T)).

string_(T, N, [Xs,Ys]) :- with_arrays_([f(Xs,X), f(Ys,Y)], c_plstring(N, X, Y, T)).

plfill(Xs, Ys) :-
	bounded_(plfill/2, [Xs,Ys], N),
	with_arrays_([f(Xs,X), f(Ys,Y)], c_plfill(N, X, Y)).

plgradient(Xs, Ys, Angle) :-
	bounded_(plgradient/3, [Xs,Ys], N),
	A is float(Angle),
	with_arrays_([f(Xs,X), f(Ys,Y)], c_plgradient(N, X, Y, A)).

plarc(X, Y, A, B, Angle1, Angle2, Rotate, Fill) :-
	floats_([X,Y,A,B,Angle1,Angle2,Rotate], [X1,Y1,A1,B1,An1,An2,R]),
	c_plarc(X1, Y1, A1, B1, An1, An2, R, Fill).

plerrx(Xmins, Xmaxs, Ys) :- chunked_(plerrx/3, [Xmins,Xmaxs,Ys], 0, errx_).

errx_(N, [Xmins,Xmaxs,Ys]) :-
	with_arrays_([f(Xmins,Xmin), f(Xmaxs,Xmax), f(Ys,Y)], c_plerrx(N, Xmin, Xmax, Y)).

plerry(Xs, Ymins, Ymaxs) :- chunked_(plerry/3, [Xs,Ymins,Ymaxs], 0, erry_).

erry_(N, [Xs,Ymins,Ymaxs]) :-
	with_arrays_([f(Xs,X), f(Ymins,Ymin), f(Ymaxs,Ymax)], c_plerry(N, X, Ymin, Ymax)).

plhist(Data, Datmin, Datmax, Nbin, Opt) :-
	bounded_(plhist/5, [Data], N),
	floats_([Datmin,Datmax], [A,B]),
	with_arrays_([f(Data,D)], c_plhist(N, D, A, B, Nbin, Opt)).

plbin(Xs, Ys, Opt) :-
	bounded_(plbin/3, [Xs,Ys], N),
	with_arrays_([f(Xs,X), f(Ys,Y)], c_plbin(N, X, Y, Opt)).

% 3D drawing

plline3(Xs, Ys, Zs) :- chunked_(plline3/3, [Xs,Ys,Zs], 1, line3_).

line3_(N, [Xs,Ys,Zs]) :- with_arrays_([f(Xs,X), f(Ys,Y), f(Zs,Z)], c_plline3(N, X, Y, Z)).

plpoin3(Xs, Ys, Zs, Code) :- chunked_(plpoin3/4, [Xs,Ys,Zs], 0, poin3_(Code)).

poin3_(Code, N, [Xs,Ys,Zs]) :-
	with_arrays_([f(Xs,X), f(Ys,Y), f(Zs,Z)], c_plpoin3(N, X, Y, Z, Code)).

plstring3(Xs, Ys, Zs, Text) :-
	text_(Text, T),
	chunked_(plstring3/4, [Xs,Ys,Zs], 0, string3_(T)).

string3_(T, N, [Xs,Ys,Zs]) :-
	with_arrays_([f(Xs,X), f(Ys,Y), f(Zs,Z)], c_plstring3(N, X, Y, Z, T)).

plfill3(Xs, Ys, Zs) :-
	bounded_(plfill3/3, [Xs,Ys,Zs], N),
	with_arrays_([f(Xs,X), f(Ys,Y), f(Zs,Z)], c_plfill3(N, X, Y, Z)).

% Z is a list of rows, one per element of Xs, each as long as Ys.

plmesh(Xs, Ys, Z, Opt) :-
	grid_(plmesh/4, Xs, Ys, Z, Nx, Ny),
	with_arrays_([f(Xs,X), f(Ys,Y), m(Z,Zp)], c_plmesh(X, Y, Zp, Nx, Ny, Opt)).

plot3d(Xs, Ys, Z, Opt, Side) :-
	grid_(plot3d/5, Xs, Ys, Z, Nx, Ny),
	with_arrays_([f(Xs,X), f(Ys,Y), m(Z,Zp)], c_plot3d(X, Y, Zp, Nx, Ny, Opt, Side)).

% Empty Clevels passes NULL, as PLplot allows.

plsurf3d(Xs, Ys, Z, Opt, Clevels) :-
	grid_(plsurf3d/5, Xs, Ys, Z, Nx, Ny),
	Arrays = [f(Xs,X), f(Ys,Y), m(Z,Zp)],
	(	Clevels == []
	->	C = 0, Nlevel = 0, Arrays1 = Arrays
	;	bounded_(plsurf3d/5, [Clevels], Nlevel),
		Arrays1 = [f(Clevels,C)|Arrays]
	),
	with_arrays_(Arrays1, c_plsurf3d(X, Y, Zp, Nx, Ny, Opt, C, Nlevel)).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

vector_max_(255).

text_(T, A) :- atom(T), !, A = T.
text_(T, A) :- atom_chars(A, T).

floats_([], []).
floats_([X|Xs], [F|Fs]) :- F is float(X), floats_(Xs, Fs).

ints_([], []).
ints_([X|Xs], [I|Is]) :- I is integer(X), ints_(Xs, Is).

% Arrays is a list of f(Floats,Ptr), i(Ints,Ptr) or m(Rows,Ptr): each Ptr is
% bound to a C array for the duration of Goal, and freed however it exits.

with_arrays_(Arrays, Goal) :-
	setup_call_cleanup(arrays_(Arrays, Ptrs), Goal, free_all_(Ptrs)).

% Everything is converted before anything is allocated, so a bad element
% throws with nothing to free. '$struct_to_pointer' reads list cells
% without dereferencing, so it needs the fresh copies findall/3 makes.

arrays_(Arrays, Ptrs) :-
	findall(Ss, structs_(Arrays, Ss), [Structs]),
	pointers_(Arrays, Structs, [], Ptrs).

structs_([], []).
structs_([A|As], [S|Ss]) :- struct_(A, S), structs_(As, Ss).

struct_(f(Xs, _), [plflt_vector|Fs]) :- floats_(Xs, Fs).
struct_(i(Xs, _), [plint_vector|Is]) :- ints_(Xs, Is).
struct_(m(Rows, _), Vs) :- rows_(Rows, Vs).

rows_([], []).
rows_([R|Rs], [[plflt_vector|Fs]|Vs]) :- floats_(R, Fs), rows_(Rs, Vs).

pointers_([], [], Ptrs, Ptrs).
pointers_([A|As], [S|Ss], Ptrs0, Ptrs) :-
	pointer_(A, S, Ptrs0, Ptrs1),
	pointers_(As, Ss, Ptrs1, Ptrs).

pointer_(m(_, Ptr), Vs, Ptrs0, [Ptr|Ptrs]) :-
	!,
	row_pointers_(Vs, Ps, Ptrs0, Ptrs),
	findall([plptr_vector|Ps], true, [V]),
	'$struct_to_pointer'(V, Ptr).
pointer_(A, S, Ptrs, [Ptr|Ptrs]) :-
	arg(2, A, Ptr),
	'$struct_to_pointer'(S, Ptr).

row_pointers_([], [], Ptrs, Ptrs).
row_pointers_([V|Vs], [P|Ps], Ptrs0, Ptrs) :-
	'$struct_to_pointer'(V, P),
	row_pointers_(Vs, Ps, [P|Ptrs0], Ptrs).

free_all_([]).
free_all_([P|Ps]) :- '$free_struct_pointer'(P), free_all_(Ps).

lengths_(Pred, [L|Ls], N) :-
	length(L, N),
	(	same_lengths_(Ls, N)
	->	true
	;	throw(error(domain_error(equal_length_lists, N), Pred))
	).

same_lengths_([], _).
same_lengths_([L|Ls], N) :- length(L, N), same_lengths_(Ls, N).

bounded_(Pred, Lists, N) :-
	lengths_(Pred, Lists, N),
	vector_max_(Max),
	(	N =< Max
	->	true
	;	throw(error(representation_error(max_array_length), Pred))
	).

grid_(Pred, Xs, Ys, Rows, Nx, Ny) :-
	bounded_(Pred, [Xs,Rows], Nx),
	bounded_(Pred, [Ys|Rows], Ny).

% Call Goal on successive chunks of the parallel Lists, each repeating the
% last Overlap elements of the one before so that polylines join up.

chunked_(Pred, Lists, Overlap, Goal) :-
	lengths_(Pred, Lists, N),
	chunks_(N, Lists, Overlap, Goal).

chunks_(0, _, _, _) :- !.
chunks_(N, Lists, _, Goal) :-
	vector_max_(Max),
	N =< Max,
	!,
	call(Goal, N, Lists).
chunks_(N, Lists, Overlap, Goal) :-
	vector_max_(Max),
	splits_(Max, Lists, Fronts, _),
	call(Goal, Max, Fronts),
	Skip is Max - Overlap,
	splits_(Skip, Lists, _, Rests),
	N1 is N - Skip,
	chunks_(N1, Rests, Overlap, Goal).

splits_(_, [], [], []).
splits_(K, [L|Ls], [F|Fs], [B|Bs]) :-
	split_(K, L, F, B),
	splits_(K, Ls, Fs, Bs).

split_(0, L, [], L) :- !.
split_(K, [X|Xs], [X|Fs], Bs) :-
	K1 is K - 1,
	split_(K1, Xs, Fs, Bs).

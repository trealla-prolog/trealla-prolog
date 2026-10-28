/*  library(syslog) - the SWI-compatible interface to syslog(3).

	Emulates https://www.swi-prolog.org/pldoc/man?section=syslog on top of
	'$openlog'/3, '$syslog'/2 and '$closelog'/0, which map the symbolic
	names onto the platform's LOG_* values.

	Differences from SWI, all forced by what Trealla provides:

	  * POSIX only. On Windows and WASI there is no syslog(3), and every
	    predicate here raises resource_error(syslog_unavailable) rather
	    than silently discarding messages.

	  * SWI's syslog/3 falls back to print_message/2 when no connection is
	    open. Trealla's print_message/2 is minimal, with no debug kind, so
	    the message is written to user_error instead, prefixed with the
	    priority.

	  * SWI re-routes debug/3 through syslog via prolog:debug_print_hook/3.
	    Trealla's library(debug) is the Scryer-style goal wrappers instead
	    - there is no debug/3 topic system to hook, so this has no
	    equivalent and is not provided.

	The connection flag is a plain dynamic predicate, so as with
	library(socket) the bookkeeping is correct for a single thread only.
	The underlying syslog(3) is itself process-wide.
*/

:- module(syslog, [
	openlog/3,
	syslog/2,
	syslog/3,
	closelog/0
	]).

:- use_module(library(error)).

% Records whether a connection has been established. syslog(3) opens one
% implicitly on first use, so this tracks what *Prolog* has asked for,
% which is what syslog/3 branches on.

:- dynamic('$syslog_connected'/0).

%% openlog(+Ident, +Options, +Facility).
%
% Ident is prepended to every message, conventionally the program name.
%
% Options is a list drawn from `cons`, `ndelay`, `nowait`, `odelay`,
% `perror` and `pid` - the LOG_* option names lowercased. `perror` also
% copies each message to stderr, which is the only way to observe the
% output without a running log daemon.
%
% Facility is one of `auth`, `authpriv`, `cron`, `daemon`, `ftp`,
% `kern`, `local0` .. `local7`, `lpr`, `mail`, `news`, `syslog`, `user`
% or `uucp`. A name the platform does not define raises
% domain_error(syslog_facility, Facility).

openlog(Ident, Options, Facility) :-
	must_be(atom, Ident),
	must_be(list, Options),
	must_be(atom, Facility),
	'$openlog'(Ident, Options, Facility),
	'$syslog_mark_open'.

'$syslog_mark_open' :-
	(  '$syslog_connected'
	-> true
	;  assertz('$syslog_connected')
	).

%% syslog(+Priority, +Message).
%
% Priority is one of `emerg`, `alert`, `crit`, `err`, `warning`,
% `notice`, `info` or `debug`. Message may be an atom, a string, or a
% list of chars or codes.
%
% A connection is opened implicitly if openlog/3 was not called.

syslog(Priority, Message) :-
	must_be(atom, Priority),
	'$syslog'(Priority, Message),
	'$syslog_mark_open'.

%% syslog(+Priority, +Format, +Args).
%
% format/3 followed by syslog/2. With no connection open the message
% goes to user_error instead - see the note at the top of this file.

syslog(Priority, Format, Args) :-
	must_be(atom, Priority),
	must_be(list, Args),
	(  '$syslog_connected'
	-> format(string(Text), Format, Args),
	   '$syslog'(Priority, Text)
	;  format(user_error, "~w: ", [Priority]),
	   format(user_error, Format, Args),
	   nl(user_error)
	).

%% closelog.
%
% Closes the connection. Logging afterwards reopens one implicitly, as
% syslog(3) does.

closelog :-
	'$closelog',
	retractall('$syslog_connected').

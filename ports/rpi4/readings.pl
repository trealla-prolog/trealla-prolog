/*  A Pi 4 that answers questions over TFTP.

		make rpi4-app main=ports/rpi4/readings.pl RPI4_NET=1

		$ tftp 192.168.50.2
		tftp> get status/index
		tftp> quit
		$ cat index

	The names are readings rather than files - the board has no filesystem
	to serve - and each one is a Prolog term, so a Prolog client can
	read_term/2 the answer while tftp and cat still work for anyone else.
	This is samples/tftp_sensors.pl moved onto the metal: the same
	library(tftp), over the freestanding library(socket) a network image
	embeds.
*/

:- use_module(library(tftp)).
:- initialization(main).

main :-
	format("TREALLA READINGS on port 69~n", []),
	tftp_serve('/', 69, [virtual(reading)]).

% The namespace is a predicate: adding a reading is adding a clause.

reading('net/stats', Codes) :-
	net_stats(Rx, Dropped, Tx, Arp, Icmp),
	term_codes(net_stats([rx(Rx), dropped(Dropped), tx(Tx),
		arp_requests(Arp), icmp_echoes(Icmp)]), Codes).
reading('net/link', Codes) :-
	net_link(State),
	term_codes(link(State), Codes).
reading('screen/size', Codes) :-
	% No monitor means no framebuffer, which is an answer, not a missing name.
	(	catch(fb_size(Width, Height), error(existence_error(framebuffer, _), _), fail)
	->	Term = screen(Width, Height)
	;	Term = screen(none)
	),
	term_codes(Term, Codes).
reading('status/pattern', Codes) :-
	% Longer than one 512-byte block, to show a transfer spanning several.
	numlist(0, 1499, Ns),
	maplist(pattern_byte, Ns, Codes).
reading('status/index', Codes) :-
	findall(Name-About, about(Name, About), Pairs),
	term_codes(readings(Pairs), Codes).

about('net/stats', 'frame and protocol counters').
about('net/link', 'carrier, up or down').
about('screen/size', 'framebuffer size in pixels').
about('status/pattern', '1500 bytes of a-z, for testing').
about('status/index', 'this list').

pattern_byte(N, B) :-
	B is 0'a + N mod 26.

term_codes(Term, Codes) :-
	format(atom(A), "~q.~n", [Term]),
	atom_codes(A, Codes).

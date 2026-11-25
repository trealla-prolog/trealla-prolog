% library(socket) - the SWI-compatible interface. Phases 2 to 5 of
% docs/socket-swi-design.md: address conversion, the handle lifecycle,
% the TCP client and server paths, unix domain sockets, and UDP.
%
% The client is checked against library(sockets)'s server rather than
% against itself, so a bug in the handle layer cannot cancel out. The
% server path is then checked against this library's own client, which
% by then has been independently verified.
%
% Ports are fixed and in the 34xx range; if the suite ever runs
% concurrently with itself these will collide.

:- use_module(library(socket)).
:- use_module(library(sockets)).
:- initialization(main).

:- dynamic(saw_failure/0).

t(L, G) :-
	(  catch(G, E, (R = err(E)))
	-> (var(R) -> R = ok ; true)
	;  R = failed
	),
	(  R == ok
	-> true
	;  format("SOCKET-FAIL ~w: ~q~n", [L, R]),
	   (  saw_failure -> true ; assertz(saw_failure) )
	).

main :-
    % address conversion
    t(ip4_name,   (ip_name(ip(127,0,0,1), N1), N1 == '127.0.0.1')),
    t(name_to_ip, (ip_name(I2, '10.0.0.7'), I2 == ip(10,0,0,7))),
    t(gethostname,(gethostname(H), atom(H))),
    t(host_to_addr,(tcp_host_to_address(localhost, A), nonvar(A))),
    % names given as strings, as SWI takes any text
    t(str_name_to_ip, (ip_name(I3, "10.0.0.7"), I3 == ip(10,0,0,7))),
    t(str_host_to_addr,(tcp_host_to_address("localhost", A2), nonvar(A2))),
    t(str_host_address,(once(host_address("localhost", A3, [])), nonvar(A3))),

    % handle lifecycle without any I/O
    t(socket_create, (tcp_socket(S1), S1 = '$socket'(_))),
    t(close_fresh,   (tcp_socket(S2), tcp_close_socket(S2))),
    t(open_fresh_err,(tcp_socket(S3), catch(tcp_open_socket(S3,_), error(permission_error(_,_,_),_), true))),
    t(bad_socket,    catch(tcp_open_socket(not_a_socket,_), error(type_error(socket,_),_), true)),
    t(setopt_ok,     (tcp_socket(S4), tcp_setopt(S4, reuseaddr), tcp_setopt(S4, nodelay), tcp_close_socket(S4))),
    t(setopt_refused,(tcp_socket(S5), catch(tcp_setopt(S5, broadcast), error(domain_error(socket_option,_),_), true), tcp_close_socket(S5))),

    % real connect to a sockets.pl server
    socket_server_open(3401, Srv, []),
    t(connect,   (tcp_socket(C), tcp_connect(C, '127.0.0.1':3401),
                  tcp_open_socket(C, Str), format(Str, "hi~n", []), flush_output(Str),
                  socket_server_accept(Srv, _Client, In, []),
                  getline(In, Line), Line == "hi",
                  tcp_getopt(C, file_no(FD)), integer(FD),
                  tcp_close_socket(C), close(In))),
    t(connect3,  (tcp_connect('127.0.0.1':3401, P, []),
                  format(P, "yo~n", []), flush_output(P),
                  socket_server_accept(Srv, _C2, In2, []),
                  getline(In2, L2), L2 == "yo", close(P), close(In2))),
    socket_server_close(Srv),

    % server path, this library on both ends
    t(roundtrip, (tcp_socket(Sv), tcp_bind(Sv, '127.0.0.1':3402), tcp_listen(Sv, 5),
                  tcp_socket(Cl), tcp_connect(Cl, '127.0.0.1':3402),
                  tcp_open_socket(Cl, CS), format(CS, "ping~n", []), flush_output(CS),
                  tcp_accept(Sv, Slave, Peer), Peer == ip(127,0,0,1),
                  tcp_open_socket(Slave, SS), getline(SS, L3), L3 == "ping",
                  format(SS, "pong~n", []), flush_output(SS),
                  getline(CS, L4), L4 == "pong",
                  tcp_close_socket(Slave), tcp_close_socket(Cl), tcp_close_socket(Sv))),

    % an unbound port is reported back by the bind, as SWI does
    t(ephemeral, (tcp_socket(Se), tcp_bind(Se, '127.0.0.1':Port),
                  integer(Port), Port > 0,
                  tcp_socket(Ce), tcp_connect(Ce, '127.0.0.1':Port),
                  tcp_accept(Se, Sl2, _), tcp_close_socket(Sl2),
                  tcp_close_socket(Ce), tcp_close_socket(Se))),

    % failures name their cause and surface at the right predicate
    t(bind_in_use, (tcp_socket(B1), tcp_bind(B1, '127.0.0.1':3403),
                    tcp_socket(B2),
                    catch(tcp_bind(B2, '127.0.0.1':3403),
                          error(socket_error(eaddrinuse,_), tcp_bind/2), true),
                    tcp_close_socket(B2), tcp_close_socket(B1))),
    t(connect_refused, (tcp_socket(R),
                    catch(tcp_connect(R, '127.0.0.1':3499),
                          error(socket_error(econnrefused,_), tcp_connect/2), true),
                    tcp_close_socket(R))),

    % the phase machine refuses out-of-order use
    t(listen_unbound, (tcp_socket(P1),
                    catch(tcp_listen(P1, 5), error(permission_error(listen,_,_),_), true),
                    tcp_close_socket(P1))),
    t(accept_unbound, (tcp_socket(P2),
                    catch(tcp_accept(P2,_,_), error(permission_error(accept,_,_),_), true),
                    tcp_close_socket(P2))),
    t(rebind, (tcp_socket(P3), tcp_bind(P3, '127.0.0.1':3404),
                    catch(tcp_bind(P3, '127.0.0.1':3405), error(permission_error(bind,_,_),_), true),
                    tcp_close_socket(P3))),

    % udp
    t(udp_roundtrip, (udp_socket(Dsv), tcp_bind(Dsv, '127.0.0.1':DP),
                  udp_socket(Dcl),
                  udp_send(Dcl, 'hello', '127.0.0.1':DP, []),
                  udp_receive(Dsv, DD, DFrom, []),
                  DD == "hello", DFrom = DIp:_, DIp == ip(127,0,0,1),
                  tcp_close_socket(Dcl), tcp_close_socket(Dsv))),

    udp_socket(Usv), tcp_bind(Usv, '127.0.0.1':3410), udp_socket(Ucl),
    t(udp_as_atom, (udp_send(Ucl, abc, '127.0.0.1':3410, []),
                  udp_receive(Usv, X1, _, [as(atom)]), X1 == abc)),
    t(udp_as_codes, (udp_send(Ucl, hi, '127.0.0.1':3410, []),
                  udp_receive(Usv, X2, _, [as(codes)]), X2 == [104,105])),
    t(udp_as_term, (udp_send(Ucl, foo(bar,[1,2]), '127.0.0.1':3410, [as(term)]),
                  udp_receive(Usv, X3, _, [as(term)]), X3 == foo(bar,[1,2]))),
    t(udp_number, (udp_send(Ucl, 42, '127.0.0.1':3410, []),
                  udp_receive(Usv, X4, _, [as(atom)]), X4 == '42')),
    t(udp_max_size, (udp_send(Ucl, abcdefgh, '127.0.0.1':3410, []),
                  udp_receive(Usv, X5, _, [max_message_size(3)]), X5 == "abc")),

    % byte-exact: the text path is UTF-8, so 255 and 128 would each go out
    % as two bytes without encoding(octet)
    t(udp_octet, (udp_send(Ucl, [0,255,128,7], '127.0.0.1':3410, [encoding(octet)]),
                  udp_receive(Usv, X6, _, [encoding(octet), as(codes)]),
                  X6 == [0,255,128,7])),

    % sending without an explicit bind materialises an ephemeral socket
    t(udp_unbound_send, (udp_socket(Uc2), udp_send(Uc2, x, '127.0.0.1':3410, []),
                  udp_receive(Usv, X7, _, []), X7 == "x", tcp_close_socket(Uc2))),

    t(udp_bad_as, catch(udp_receive(Usv, _, _, [as(bogus)]),
                  error(domain_error(udp_as, bogus), _), true)),
    t(udp_bad_encoding, catch(udp_receive(Usv, _, _, [encoding(iso_latin_1)]),
                  error(domain_error(encoding, _), _), true)),
    t(udp_on_tcp_socket, (tcp_socket(Ut),
                  catch(udp_send(Ut, x, '127.0.0.1':3410, []),
                        error(permission_error(udp, _, _), _), true),
                  tcp_close_socket(Ut))),
    tcp_close_socket(Ucl), tcp_close_socket(Usv),

    % unix domain sockets. The path is fixed, so as with the ports above a
    % concurrent run of this suite would collide.
    catch(delete_file('/tmp/trealla_socket_test.sock'), _, true),
    t(unix_roundtrip, (
        unix_domain_socket(Uv), tcp_bind(Uv, '/tmp/trealla_socket_test.sock'),
        tcp_listen(Uv, 5),
        unix_domain_socket(Uc), tcp_connect(Uc, '/tmp/trealla_socket_test.sock'),
        tcp_open_socket(Uc, UCS), format(UCS, "ping~n", []), flush_output(UCS),
        tcp_accept(Uv, USl, _),
        tcp_open_socket(USl, USS), getline(USS, U1), U1 == "ping",
        format(USS, "pong~n", []), flush_output(USS),
        getline(UCS, U2), U2 == "pong",
        tcp_close_socket(USl), tcp_close_socket(Uc), tcp_close_socket(Uv))),

    % it must be a real AF_UNIX socket, not a TCP one: the bind leaves a
    % socket inode behind. This is what regressed silently before - the
    % round-trip above passes either way.
    t(unix_is_real, (
        unix_domain_socket(Uf), tcp_bind(Uf, '/tmp/trealla_socket_test2.sock'),
        catch(delete_file('/tmp/trealla_socket_test2.sock'), _, fail),
        tcp_close_socket(Uf))),

    t(unix_missing, (unix_domain_socket(Un),
        catch(tcp_connect(Un, '/tmp/no_such_dir_xyzzy/x.sock'),
              error(socket_error(enoent,_), tcp_connect/2), true),
        tcp_close_socket(Un))),
    catch(delete_file('/tmp/trealla_socket_test.sock'), _, true),

    (  saw_failure
    -> format("socket: FAILURES above~n")
    ;  format("socket: all ok~n")
    ).

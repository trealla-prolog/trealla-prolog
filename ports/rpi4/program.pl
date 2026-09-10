% Smoke program for the Raspberry Pi 4. It defines everything
% samples/freestanding.c drives, plus the GPIO checks only this target has.

freestanding_answer(42).

freestanding_failure :- fail.

freestanding_platform_probe :-
    write('TREALLA PROLOG OK'), nl,
    gpio_probe,
    write('TREALLA GPIO OK'), nl,
    fb_probe,
    write('TREALLA FB OK'), nl.

freestanding_oom_probe :-
    catch(length(_, 100000), error(resource_error(memory), _), true).

% GPIO21 is a plain header pin with no boot-time function of its own.
gpio_probe :-
    gpio_mode(21, output),
    gpio_write(21, 1),
    gpio_write(21, 0),
    gpio_mode(21, input),
    gpio_pull(21, up),
    gpio_read(21, Level),
    ( Level == 0 ; Level == 1 ),
    !,
    gpio_rejects.

% The level read back from an unwired pin proves nothing under QEMU, but the
% argument checking is real behaviour and worth asserting. Goal has to throw:
% succeeding quietly must fail the test, not pass it.
gpio_throws(Goal, Error) :-
    catch((Goal, fail), Error, true).

gpio_rejects :-
    gpio_throws(gpio_mode(99, output),
        error(domain_error(gpio_pin, 99), _)),
    gpio_throws(gpio_mode(14, output),
        error(permission_error(modify, gpio_pin, 14), _)),
    gpio_throws(gpio_write(21, 2),
        error(domain_error(gpio_level, 2), _)),
    gpio_throws(gpio_mode(21, wibble),
        error(domain_error(gpio_mode, wibble), _)).

% QEMU always hands us a framebuffer, so the drawing predicates can be called
% for real. The corner pixel is put back afterwards: the console shares this
% screen, and util/rpi4_screen.py reads every cell of it and fails on one no
% glyph explains. What the pixels look like is checked by drawing colours and
% reading them out of a screendump; what is checked here is the argument
% handling, which is real behaviour either way.
fb_probe :-
    fb_size(W, H),
    W > 0, H > 0,
    X is W - 1, Y is H - 1,
    fb_pixel(X, Y, 0xffffff),
    fb_pixel(X, Y, 0x000000),
    fb_rect(W, H, 10, 10, 0x00ff00),
    !,
    fb_rejects.

% Off-screen is clipped rather than refused; a negative coordinate or a
% colour outside 24 bits is a mistake and says so.
fb_rejects :-
    gpio_throws(fb_pixel(-1, 0, 0),
        error(domain_error(fb_coord, -1), _)),
    gpio_throws(fb_pixel(0, 0, -1),
        error(domain_error(fb_colour, -1), _)),
    gpio_throws(fb_pixel(0, 0, 0x1000000),
        error(domain_error(fb_colour, 16777216), _)),
    gpio_throws(fb_rect(0, 0, -1, 1, 0),
        error(domain_error(fb_extent, -1), _)).

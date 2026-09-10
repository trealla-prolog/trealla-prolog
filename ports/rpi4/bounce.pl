% A box with a label, bouncing around the screen. Something to look at with a
% monitor plugged into the Pi and nothing else attached.
%
% Nothing is written to the console: it shares this screen and scrolls, which
% would drag the picture up. There is no double buffering either, so the box
% is erased and redrawn each frame - at this size and speed that is fine.

:- initialization(main).

box_width(224).
box_height(56).

main :-
    fb_size(W, H),
    fb_clear(0x000018),
    fb_text(16, 8, 'trealla prolog - bare metal', 0x4080c0),
    move(W, H, 40, 120, 5, 3).

move(W, H, X, Y, DX, DY) :-
    draw(X, Y),
    delay_ms(30),
    erase(X, Y),
    box_width(BW), box_height(BH),
    step(X, DX, W, BW, X1, DX1),
    step(Y, DY, H, BH, Y1, DY1),
    move(W, H, X1, Y1, DX1, DY1).

% Reflects off the far edge rather than sticking to it, so the box keeps its
% speed and the bounce lands where it would have gone.
step(V, D, Limit, Size, V1, D1) :-
    Next is V + D,
    Max is Limit - Size,
    (   Next < 0
    ->  V1 is -Next, D1 is -D
    ;   Next > Max
    ->  V1 is 2 * Max - Next, D1 is -D
    ;   V1 = Next, D1 = D
    ).

draw(X, Y) :-
    box_width(BW), box_height(BH),
    fb_rect(X, Y, BW, BH, 0xd8d000),
    TX is X + 24,
    TY is Y + 24,
    fb_text(TX, TY, 'TREALLA', 0x000000).

erase(X, Y) :-
    box_width(BW), box_height(BH),
    fb_rect(X, Y, BW, BH, 0x000018).

:- use_module(library(raylib)).

run :-
	ScreenWidth = 800,
	ScreenHeight = 450,
	'InitWindow'(ScreenWidth, ScreenHeight, "Trealla Prolog Raylib"),

	X is ScreenWidth / 2.0,
	Y is ScreenHeight / 2.0,

	'SetTargetFPS'(60),
	loop(X, Y, 5.0, 4.0, 20, 0).

loop(PosX, PosY, SpeedX, SpeedY, BallRadius, Paused) :-
	'WindowShouldClose' -> 'CloseWindow' ;
	draw(PosX, PosY, SpeedX, SpeedY, BallRadius, Paused, NewPosX, NewPosY, NewSpeedX, NewSpeedY, Paused2),
	loop(NewPosX, NewPosY, NewSpeedX, NewSpeedY, BallRadius, Paused2).

draw(PosX, PosY, SpeedX, SpeedY, BallRadius, Paused, X, Y, NewSpeedX, NewSpeedY, Paused2) :-
	'GetScreenWidth'(ScreenWidth),
	'GetScreenHeight'(ScreenHeight),

	KEY_SPACE = 32,
	( 'IsKeyPressed'(KEY_SPACE) -> Paused2 is \Paused ; Paused2 is Paused ),

	(
		Paused2 =\= 0
		-> ( X = PosX, Y = PosY, NewSpeedX = SpeedX, NewSpeedY = SpeedY )
		; (
			X is PosX + SpeedX,
			Y is PosY + SpeedY,

			% Check walls collision for bouncing

			(
				X >= (ScreenWidth - BallRadius) -> NewSpeedX is SpeedX * -1.0 ;
				X =< BallRadius -> NewSpeedX is SpeedX * -1.0 ;
				NewSpeedX is SpeedX
			),

			(
				Y >= (ScreenHeight - BallRadius) -> NewSpeedY is SpeedY * -1.0 ;
				Y =< BallRadius -> NewSpeedY is SpeedY * -1.0 ;
				NewSpeedY is SpeedY
			)
		)
	),

	BallRadius2 is float(BallRadius),
	Height is ScreenHeight - 25,

	LIGHTGRAY = [color,200,200,200,255],
	GRAY = [color,130,130,130,255],
	RAYWHITE = [color,245,245,245,245],
	MAROON = [color,190,33,55,255],

	'BeginDrawing',
	'ClearBackground'(RAYWHITE),

	'DrawCircleV'([vector2,X,Y], BallRadius2, MAROON),
	'DrawText'("PRESS SPACE to PAUSE BALL MOVEMENT", 10, Height, 20, LIGHTGRAY),

	 (Paused2 =\= 0 -> 'DrawText'("PAUSED", 350, 200, 30, GRAY) ; true),

	'DrawFPS'(10, 10),
	'EndDrawing'.

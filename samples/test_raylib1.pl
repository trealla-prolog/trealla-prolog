:- use_module(library(raylib)).

run :-
	'InitWindow'(800, 450, "Trealla Prolog Raylib"),
	'SetTargetFPS'(60),
	loop.

loop :-
	(
		'WindowShouldClose' ->
		'CloseWindow'
	;	(
		'BeginDrawing',
		'ClearBackground'([color,255,255,255,255]),
		'DrawText'("Congrats! You created your first window!", 198, 200, 20, [color,200,200,200,255]),
		'EndDrawing',
		loop
		)
	).

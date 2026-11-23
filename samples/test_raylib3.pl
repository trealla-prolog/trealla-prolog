% A tour of the wider raylib binding: window flags, a 2D camera, mouse
% input, shapes, textures, text measurement, and a 3D model.
%
% Run it with:
%
%     tpl samples/test_raylib3.pl -g "run,halt"
%
% On macOS raylib lives outside the default loader path, so it may need:
%
%     DYLD_LIBRARY_PATH=/opt/homebrew/lib tpl samples/test_raylib3.pl -g "run,halt"

:- use_module(library(raylib)).

width(900).
height(560).

run :-
	width(W), height(H),

	% Constants come from the library rather than being hardcoded.
	raylib_const('FLAG_WINDOW_RESIZABLE', Resizable),
	raylib_const('FLAG_VSYNC_HINT', Vsync),
	Flags is Resizable \/ Vsync,
	'SetConfigFlags'(Flags),

	'InitWindow'(W, H, "Trealla Prolog - raylib tour"),
	'SetTargetFPS'(60),

	% A 4x4 red image promoted to a texture, drawn scaled up later.
	raylib_color('MAROON', Maroon),
	'GenImageColor'(4, 4, Maroon, Image),
	'LoadTextureFromImage'(Image, Texture),
	'UnloadImage'(Image),

	% A cube mesh promoted to a model. Mesh is 120 bytes and Model is
	% 136, so both of these calls pass more struct than the FFI used to
	% allow in one call at all.
	'GenMeshCube'(2.0, 2.0, 2.0, Mesh),
	'LoadModelFromMesh'(Mesh, Model),

	loop(Texture, Model, 1.0, 0.0),

	'UnloadModel'(Model),
	'UnloadTexture'(Texture),
	'CloseWindow'.

loop(Texture, Model, Zoom, Angle) :-
	(	'WindowShouldClose'
	->	true
	;	step(Zoom, Angle, Zoom2, Angle2),
		draw(Texture, Model, Zoom2, Angle2),
		loop(Texture, Model, Zoom2, Angle2)
	).

% Mouse wheel drives the camera zoom; the polygon spins on its own.
step(Zoom, Angle, Zoom2, Angle2) :-
	'GetMouseWheelMove'(Wheel),
	Z is Zoom + (Wheel * 0.1),
	(	Z < 0.3 -> Zoom2 = 0.3
	;	Z > 3.0 -> Zoom2 = 3.0
	;	Zoom2 = Z
	),
	'GetFrameTime'(Delta),
	Angle2 is Angle + (Delta * 45.0).

draw(Texture, Model, Zoom, Angle) :-
	'GetScreenWidth'(W),
	'GetScreenHeight'(H),
	'GetMousePosition'(Mouse),
	Mouse = [vector2, MouseX, MouseY],

	raylib_color('RAYWHITE', RayWhite),
	raylib_color('MAROON', Maroon),
	raylib_color('DARKBLUE', DarkBlue),
	raylib_color('LIGHTGRAY', LightGray),
	raylib_color('GOLD', Gold),

	'BeginDrawing',
	'ClearBackground'(RayWhite),

	% --- 3D, behind everything else ---------------------------------
	'BeginMode3D'([camera3d, 6.0,4.0,6.0, 0.0,0.0,0.0, 0.0,1.0,0.0, 45.0, 0]),
	'DrawModelEx'(Model, [vector3,0.0,0.0,0.0], [vector3,0.0,1.0,0.0],
		Angle, [vector3,1.0,1.0,1.0], Gold),
	'DrawModelWires'(Model, [vector3,0.0,0.0,0.0], 1.001, DarkBlue),
	'DrawGrid'(10, 1.0),
	'EndMode3D',

	% --- world space, under the 2D camera ---------------------------
	HalfW is W / 2.0,
	HalfH is H / 2.0,
	'BeginMode2D'([camera2d, HalfW, HalfH, 0.0, 0.0, 0.0, Zoom]),

	'DrawRectangleRounded'([rectangle,-160.0,-100.0,320.0,200.0], 0.15, 16, LightGray),
	'DrawPoly'([vector2,0.0,0.0], 6, 70.0, Angle, Maroon),
	'DrawRing'([vector2,0.0,0.0], 90.0, 100.0, 0.0, 270.0, 48, Gold),
	'DrawCircleV'([vector2,0.0,0.0], 8.0, DarkBlue),

	'EndMode2D',

	% --- screen space -----------------------------------------------
	% The texture is 4x4, scaled 8x on the way to the screen.
	'DrawTextureEx'(Texture, [vector2,20.0,20.0], 0.0, 8.0, Maroon),

	% A dot tracking the mouse, and a readout of where it is.
	'DrawCircleV'(Mouse, 6.0, DarkBlue),
	format(atom(Readout), "mouse ~2f, ~2f   zoom ~2f", [MouseX, MouseY, Zoom]),
	'DrawText'(Readout, 20, 70, 20, DarkBlue),

	% MeasureText lets us centre a caption without guessing.
	Caption = "scroll to zoom",
	'MeasureText'(Caption, 30, TextWidth),
	CaptionX is (W - TextWidth) // 2,
	CaptionY is H - 60,
	'DrawText'(Caption, CaptionX, CaptionY, 30, LightGray),

	'DrawFPS'(20, 20),
	'EndDrawing'.

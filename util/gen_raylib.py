#!/usr/bin/env python3
# Regenerates library/raylib.pl from raylib.h.
#
#   ./util/gen_raylib.py > /tmp/raylib.pl     # print
#   ./util/gen_raylib.py --in-place           # write library/raylib.pl
#   ./util/gen_raylib.py --verify             # check layouts against cc
#
# The header is found via RAYLIB_H, else the first of HEADERS that exists.
#
# Every RLAPI prototype becomes a use_foreign_module/2 entry, keeping
# raylib's own name and its trailing comment. What cannot be expressed is
# listed, with a reason, at the end of the generated file.
#
# STRUCTS ARE FLATTENED, and that is the whole reason this is a script
# rather than a hand-written file. src/bif_ffi.c cannot marshal a nested
# struct: a struct-typed field is packed at the wrong offset going in, and
# the struct-return decoder gives up on one. So a Camera2D has to be
# declared as six floats rather than two vector2s, and anything with
# mixed field widths needs explicit padding entries at exactly the offsets
# the C compiler pads at. That is mechanical, unreadable, and very easy to
# get quietly wrong by hand - STRUCTS below is the readable form, and
# layout() derives the padding.
#
# --verify emits a C program of offsetof/sizeof assertions for every
# struct and runs it, so a raylib upgrade that moves a field fails loudly
# instead of silently corrupting arguments. It has already earned that:
# raylib 6.0 dropped FilePathList.capacity, which 5.x had.

import re, os, sys, subprocess, tempfile

HEADERS = [
	'/opt/homebrew/include/raylib.h',
	'/usr/local/include/raylib.h',
	'/usr/include/raylib.h',
]

OUT = 'library/raylib.pl'

# Sizes and alignments are the LP64 ones every platform trealla's FFI
# builds on shares; --verify is what proves that for the host in hand.

SCALARS = {
	# name  -> (size, align, foreign_struct tag)
	'float':  (4, 4, 'float'),
	'double': (8, 8, 'double'),
	'int':    (4, 4, 'sint'),
	'uint':   (4, 4, 'uint'),
	'bool':   (1, 1, 'bool'),
	'char':   (1, 1, 'schar'),
	'uchar':  (1, 1, 'uint8'),
	'ptr':    (8, 8, 'ptr'),
}

# (field, type, array length or None), in declaration order. Types are
# either a SCALARS key or another entry here.

STRUCTS = {
 'vector2':      [('x','float',None),('y','float',None)],
 'vector3':      [('x','float',None),('y','float',None),('z','float',None)],
 'vector4':      [('x','float',None),('y','float',None),('z','float',None),('w','float',None)],
 'quaternion':   [('x','float',None),('y','float',None),('z','float',None),('w','float',None)],
 # raylib declares Matrix a row at a time but names the fields column-major.
 'matrix':       [('m%d'%i,'float',None) for i in (0,4,8,12, 1,5,9,13, 2,6,10,14, 3,7,11,15)],
 'color':        [('r','uchar',None),('g','uchar',None),('b','uchar',None),('a','uchar',None)],
 'rectangle':    [('x','float',None),('y','float',None),('width','float',None),('height','float',None)],
 'image':        [('data','ptr',None),('width','int',None),('height','int',None),('mipmaps','int',None),('format','int',None)],
 'texture':      [('id','uint',None),('width','int',None),('height','int',None),('mipmaps','int',None),('format','int',None)],
 'rendertexture':[('id','uint',None),('texture','texture',None),('depth','texture',None)],
 'npatchinfo':   [('source','rectangle',None),('left','int',None),('top','int',None),('right','int',None),('bottom','int',None),('layout','int',None)],
 'glyphinfo':    [('value','int',None),('offsetX','int',None),('offsetY','int',None),('advanceX','int',None),('image','image',None)],
 'font':         [('baseSize','int',None),('glyphCount','int',None),('glyphPadding','int',None),('texture','texture',None),('recs','ptr',None),('glyphs','ptr',None)],
 'camera3d':     [('position','vector3',None),('target','vector3',None),('up','vector3',None),('fovy','float',None),('projection','int',None)],
 'camera2d':     [('offset','vector2',None),('target','vector2',None),('rotation','float',None),('zoom','float',None)],
 'mesh':         [('vertexCount','int',None),('triangleCount','int',None),
                  ('vertices','ptr',None),('texcoords','ptr',None),('texcoords2','ptr',None),('normals','ptr',None),
                  ('tangents','ptr',None),('colors','ptr',None),('indices','ptr',None),
                  ('boneCount','int',None),('boneIndices','ptr',None),('boneWeights','ptr',None),
                  ('animVertices','ptr',None),('animNormals','ptr',None),
                  ('vaoId','uint',None),('vboId','ptr',None)],
 'shader':       [('id','uint',None),('locs','ptr',None)],
 'materialmap':  [('texture','texture',None),('color','color',None),('value','float',None)],
 'material':     [('shader','shader',None),('maps','ptr',None),('params','float',4)],
 'transform':    [('translation','vector3',None),('rotation','quaternion',None),('scale','vector3',None)],
 'boneinfo':     [('name','char',32),('parent','int',None)],
 'modelskeleton':[('boneCount','int',None),('bones','ptr',None),('bindPose','ptr',None)],
 'model':        [('transform','matrix',None),('meshCount','int',None),('materialCount','int',None),
                  ('meshes','ptr',None),('materials','ptr',None),('meshMaterial','ptr',None),
                  ('skeleton','modelskeleton',None),('currentPose','ptr',None),('boneMatrices','ptr',None)],
 'modelanimation':[('name','char',32),('boneCount','int',None),('keyframeCount','int',None),('keyframePoses','ptr',None)],
 'ray':          [('position','vector3',None),('direction','vector3',None)],
 'raycollision': [('hit','bool',None),('distance','float',None),('point','vector3',None),('normal','vector3',None)],
 'boundingbox':  [('min','vector3',None),('max','vector3',None)],
 'wave':         [('frameCount','uint',None),('sampleRate','uint',None),('sampleSize','uint',None),('channels','uint',None),('data','ptr',None)],
 'audiostream':  [('buffer','ptr',None),('processor','ptr',None),('sampleRate','uint',None),('sampleSize','uint',None),('channels','uint',None)],
 'sound':        [('stream','audiostream',None),('frameCount','uint',None)],
 'music':        [('stream','audiostream',None),('frameCount','uint',None),('looping','bool',None),('ctxType','int',None),('ctxData','ptr',None)],
 'vrdeviceinfo': [('hResolution','int',None),('vResolution','int',None),('hScreenSize','float',None),('vScreenSize','float',None),
                  ('eyeToScreenDistance','float',None),('lensSeparationDistance','float',None),('interpupillaryDistance','float',None),
                  ('lensDistortionValues','float',4),('chromaAbCorrection','float',4)],
 'vrstereoconfig':[('projection','matrix',2),('viewOffset','matrix',2),('leftLensCenter','float',2),('rightLensCenter','float',2),
                  ('leftScreenCenter','float',2),('rightScreenCenter','float',2),('scale','float',2),('scaleIn','float',2)],
 'filepathlist': [('count','uint',None),('paths','ptr',None)],
 'automationevent':[('frame','uint',None),('type','uint',None),('params','int',4)],
 'automationeventlist':[('capacity','uint',None),('count','uint',None),('events','ptr',None)],
 # raymath.h only, for MatrixToFloatV / Vector3ToFloatV.
 'float3':       [('v','float',3)],
 'float16':      [('v','float',16)],
}

CNAME = {
 'vector2':'Vector2','vector3':'Vector3','vector4':'Vector4','quaternion':'Quaternion','matrix':'Matrix',
 'color':'Color','rectangle':'Rectangle','image':'Image','texture':'Texture','rendertexture':'RenderTexture',
 'npatchinfo':'NPatchInfo','glyphinfo':'GlyphInfo','font':'Font','camera3d':'Camera3D','camera2d':'Camera2D',
 'mesh':'Mesh','shader':'Shader','materialmap':'MaterialMap','material':'Material','transform':'Transform',
 'boneinfo':'BoneInfo','modelskeleton':'ModelSkeleton','model':'Model','modelanimation':'ModelAnimation',
 'ray':'Ray','raycollision':'RayCollision','boundingbox':'BoundingBox','wave':'Wave','audiostream':'AudioStream',
 'sound':'Sound','music':'Music','vrdeviceinfo':'VrDeviceInfo','vrstereoconfig':'VrStereoConfig',
 'filepathlist':'FilePathList','automationevent':'AutomationEvent','automationeventlist':'AutomationEventList',
 'float3':'float3','float16':'float16',
}

# raylib typedefs these onto a struct already described above.
ALIASES = {'texture2d':'texture','texturecubemap':'texture','rendertexture2d':'rendertexture','camera':'camera3d'}

SCALAR_MAP = {
	'void':'void', 'bool':'bool', 'int':'sint', 'unsigned int':'uint',
	'char':'schar', 'unsigned char':'uint8', 'short':'sshort',
	'unsigned short':'ushort', 'long':'slong', 'unsigned long':'ulong',
	'float':'float', 'double':'double',
}

CALLBACKS = {'TraceLogCallback','LoadFileDataCallback','SaveFileDataCallback',
             'LoadFileTextCallback','SaveFileTextCallback','AudioCallback'}

# Must track src/bif_ffi.c. Both are bounds checked there, so going over
# is a resource_error rather than a stray write - but a binding that can
# never succeed is still not worth emitting.

MAX_FFI_STRUCT_BYTES = 4096
MAX_FFI_RET_BYTES = 4096

DOC_COLUMN = 56


def header_path():
	p = os.environ.get('RAYLIB_H')

	if p:
		return p

	for p in HEADERS:
		if os.path.exists(p):
			return p

	sys.exit("no raylib.h found; set RAYLIB_H")


def raymath_path(raylib_h):
	"""raymath.h, which ships beside raylib.h."""
	p = os.environ.get('RAYMATH_H') or os.path.join(os.path.dirname(raylib_h), 'raymath.h')

	if not os.path.exists(p):
		sys.exit("no raymath.h beside %s; set RAYMATH_H" % raylib_h)

	return p


# ---------------------------------------------------------------- layout

def align_of(t):
	if t in SCALARS:
		return SCALARS[t][1]

	return max(align_of(ft) for _, ft, _ in STRUCTS[t])


def size_of(t):
	if t in SCALARS:
		return SCALARS[t][0]

	return layout(t)[1]


def fields(t):
	"""Every scalar in t, recursively, as (dotted path, type, offset)."""
	off = 0

	for fname, ftype, alen in STRUCTS[t]:
		a = align_of(ftype)
		off = (off + a - 1) // a * a

		for i in range(1 if alen is None else alen):
			idx = '' if alen is None else '[%d]' % i

			if ftype in SCALARS:
				yield (fname + idx, ftype, off)
				off += SCALARS[ftype][0]
			else:
				for sub, st, so in fields(ftype):
					yield (fname + idx + '.' + sub, st, off + so)

				off += size_of(ftype)


def layout(t):
	"""(entries, size), entries being (tag, path) with padding as (tag, None)."""
	out, off = [], 0

	for path, st, foff in fields(t):
		while off < foff:
			gap = foff - off

			for psz, ptag in ((4,'uint32'), (2,'uint16'), (1,'uint8')):
				if gap >= psz and off % psz == 0:
					out.append((ptag, None))
					off += psz
					break
			else:
				out.append(('uint8', None))
				off += 1

		out.append((SCALARS[st][2], path))
		off += SCALARS[st][0]

	a = align_of(t)
	return out, (off + a - 1) // a * a


# ------------------------------------------------------------ headers

def prototypes(path, marker='RLAPI '):
	"""(module banner, sub-heading, prototype, trailing comment) per prototype.

	raylib.h groups under '... (Module: core)' banners with '... functions'
	sub-headings; raymath.h has only 'Module Functions Definition - X'.
	"""
	out, heading, banner = [], None, None

	for raw in open(path):
		line = raw.rstrip('\n')

		if re.match(r'^\s*//-{10,}', line):
			continue

		m = re.match(r'^\s*//\s*(.+?)\s*$', line)

		if m and not line.lstrip().startswith('// NOTE'):
			txt = m.group(1)

			if '(Module:' in txt:
				banner, heading = txt, None
			elif txt.startswith('Module Functions Definition - '):
				banner = txt.split(' - ', 1)[1]
				heading = None
			elif re.match(r'^[A-Z].*[Ff]unctions\s*$', txt):
				heading = txt

			continue

		if line.startswith(marker):
			body = line[len(marker):]
			doc = ''

			if '//' in body:
				body, doc = body.split('//', 1)
				doc = doc.strip()

			out.append((banner, heading, body.strip().rstrip(';').strip(), doc))

	return out


def enums(path):
	src = open(path).read()
	src = re.sub(r'//.*', '', src)
	src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
	out = []

	for body, name in re.findall(r'typedef\s+enum\s*\{(.*?)\}\s*([A-Za-z_][A-Za-z0-9_]*)\s*;', src, flags=re.S):
		members, nxt = [], 0

		for part in body.split(','):
			part = part.strip()

			if not part:
				continue

			if '=' in part:
				k, v = part.split('=', 1)
				k, val = k.strip(), int(v.strip(), 0)
			else:
				k, val = part, nxt

			members.append((k, val))
			nxt = val + 1

		out.append((name, members))

	return out


def colors(path):
	out = []

	for line in open(path):
		m = re.match(r'^#define\s+([A-Z][A-Z0-9_]*)\s+CLITERAL\(Color\)\{\s*'
		             r'([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\}', line)

		if m:
			out.append((m.group(1), tuple(int(m.group(i)) for i in (2,3,4,5))))

	return out


# ------------------------------------------------------------- bindings

class Unsupported(Exception):
	pass


def map_type(t, is_ret):
	"""C type -> (foreign type, bytes of struct it costs)."""
	t = t.strip()

	if t == '...':
		raise Unsupported('varargs')

	base = t.replace('const', '').strip()

	if base.endswith('*'):
		inner = base[:-1].strip()

		# A string. Returns are never 'cstr': that tag makes bif_ffi.c
		# TPL_free() the pointer, which for raylib is a static buffer or
		# memory raylib owns. 'ccstr' reads it without freeing.

		if inner == 'char' and base.count('*') == 1:
			return ('ccstr' if is_ret else 'cstr'), 0

		return 'ptr', 0

	if base in CALLBACKS:
		raise Unsupported('callback')

	if base in SCALAR_MAP:
		return SCALAR_MAP[base], 0

	k = ALIASES.get(base.lower(), base.lower())

	if k in STRUCTS:
		return k, layout(k)[1]

	raise Unsupported('unknown type %r' % t)


def parse(proto):
	m = re.match(r'^(.*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)$', proto.strip())
	ret, name, params = m.group(1).strip(), m.group(2), m.group(3).strip()
	args = []

	if params not in ('void', ''):
		for p in params.split(','):
			p = p.strip()

			if p == '...':
				args.append('...')
				continue

			pm = re.match(r'^(.*?)\b([A-Za-z_][A-Za-z0-9_]*)(\[\])?$', p)
			args.append(pm.group(1).strip() + (' *' if pm.group(3) else ''))

	return ret, name, args


def convert(proto):
	ret, name, cargs = parse(proto)
	ffi_args, total = [], 0

	for a in cargs:
		t, n = map_type(a, False)
		ffi_args.append(t)
		total += n

	if total > MAX_FFI_STRUCT_BYTES:
		raise Unsupported('struct args total %d bytes > %d' % (total, MAX_FFI_STRUCT_BYTES))

	rt, rn = map_type(ret, True)

	if rn > MAX_FFI_RET_BYTES:
		raise Unsupported('struct return %d bytes > %d' % (rn, MAX_FFI_RET_BYTES))

	return name, ffi_args, rt, len(ffi_args) + (0 if rt == 'void' else 1)


# --------------------------------------------------------------- output

PREAMBLE = """\
% raylib and raymath bindings for Trealla Prolog.
%
% GENERATED by util/gen_raylib.py from raylib.h and raymath.h - edit that,
% not this. Written against raylib 6.0.
%
% raymath is in here rather than in a module of its own because it is
% header-only, ships with raylib, resolves out of the same libraylib.so,
% shares the vector and matrix structs, and shares no function name with
% raylib.h. Note that its symbols exist only because raylib was built
% with RAYMATH_IMPLEMENTATION and default visibility; a raylib built
% otherwise exports none of them, and because do_use_foreign_module
% ignores a failed dlsym, that shows up as existence_error per call
% rather than as a load failure.
%
% CALLING CONVENTION
%
% Bindings keep raylib's own CamelCase names, so they need quoting:
%
%     'InitWindow'(800, 450, "demo")
%
% A function returning non-void takes one extra argument, last, to
% receive the result. C bools arrive as 0 or 1:
%
%     'GetScreenWidth'(W)
%     'IsKeyDown'(Key, Down)
%
% A C string return arrives as an ATOM, not a Prolog string:
%
%     ?- 'GetFileName'("/tmp/a/b.png", N).
%     N = 'b.png'.
%
% String returns are typed 'ccstr' throughout. The other string tag,
% 'cstr', makes bif_ffi.c TPL_free() the returned pointer - which for
% raylib is either a static internal buffer or memory raylib owns, so it
% must not be used here. The cost is that the few functions returning
% freshly allocated text (LoadFileText, TextReplace, TextInsert) leak it;
% their Unload counterparts need a pointer this binding no longer has.
%
% A C float or double argument must be given a Prolog float, never an
% integer - bif_ffi.c type-checks the cell tag, so 'DrawCircleV'(P, 20, C)
% raises a type_error where 20.0 works.
%
% STRUCTS
%
% Structs pass and return as a list whose head is the struct name:
%
%     [color, 255, 255, 255, 255]
%     [vector2, 3.0, 4.0]
%
% The lists are FLAT, never nested. A camera2d is six floats in one list,
% not two vector2 sublists. This is forced by bif_ffi.c: a struct-typed
% field is packed at the wrong offset on the way in, and the struct-return
% decoder bails out on one entirely. So each struct below is declared as
% its scalar fields in C layout order, with explicit padding fields where
% the C compiler inserts padding. Every layout is checked field-by-field
% against offsetof/sizeof by `./util/gen_raylib.py --verify`.
%
% Padding fields read as junk and are ignored on the way in; they are
% marked '_' in the layout comment on each declaration.
%
% Struct field values are NOT type-checked, unlike scalar arguments, and
% are read straight through the cell. An integer at or above 2^63 is a
% bignum in trealla, and passes its pointer rather than its value - keep
% struct fields inside int64 range.
%
% POINTERS
%
% A C pointer argument is 'ptr' and shows up as a plain integer address -
% whatever a previous call returned. Nothing here allocates one for you,
% so the Image*/Mesh*/Wave* mutators are only callable on a pointer raylib
% itself handed back.
%
% CONSTANTS
%
% raylib_const/2 and raylib_color/2 carry the enum values and predefined
% colours, so callers need no magic numbers:
%
%     raylib_const('KEY_SPACE', K),
%     raylib_color('RAYWHITE', C)
%
% LOADING
%
% Where raylib sits outside the loader's default search path - Homebrew on
% macOS, for one - a bare run reports
%
%     Error: foreign module creation failed: libraylib.so, raylib
%
% which means dlopen, not a missing binding. Point the loader at it:
%
%     DYLD_LIBRARY_PATH=/opt/homebrew/lib tpl ...
%"""

EPILOGUE = """\
% ---------------------------------------------------------------
% Not bound. Everything raylib passes or returns by value is
% reachable; what is left needs FFI machinery that does not exist.
%
%   'callback'
%       takes a C function pointer. The FFI cannot build a closure
%       that calls back into Prolog.
%
%   'varargs'
%       TextFormat and TraceLog are printf-style, which needs
%       ffi_prep_cif_var and a per-call signature.
%
% Struct size is no longer a reason. MAX_FFI_STRUCT_BYTES and
% MAX_FFI_RET_BYTES in src/bif_ffi.c allow 4096 bytes of struct
% arguments per call and 4096 bytes of returned struct, both bounds
% checked; the largest thing raylib passes by value is Model, at 136
% bytes. They were 64 and 256, which put the whole model and mesh
% API out of reach.
% ---------------------------------------------------------------
"""


def generate(path, mpath):
	# raymath.h is folded into this same module; PREAMBLE says why.
	protos = prototypes(path) + [('raymath: ' + (b or 'misc'), h, p, d)
	                             for b, h, p, d in prototypes(mpath, 'RMAPI ')]
	bound, skipped = [], []

	for banner, heading, proto, doc in protos:
		try:
			bound.append((banner, heading, doc) + convert(proto))
		except Unsupported as e:
			skipped.append((parse(proto)[1], str(e)))

	used = set()

	for _, _, _, _, args, rt, _ in bound:
		for a in list(args) + [rt]:
			if a in STRUCTS:
				used.add(a)

	L = []
	A = L.append

	A(PREAMBLE)
	A("% NOT BOUND")
	A("%")
	A("%% %d of the %d functions across both headers are left out - the %d"
	  % (len(skipped), len(protos), sum(1 for _, w in skipped if w == 'callback')))
	A("%% that take a C callback and the %d that are varargs, all of them in"
	  % sum(1 for _, w in skipped if w == 'varargs'))
	A("%% raylib.h. All %d of raymath.h is bound. See the end of this file."
	  % sum(1 for b, _, _, _, _, _, _ in bound if str(b).startswith('raymath')))
	A("")

	# module + exports, grouped by the headers' module banners
	A(":- module(raylib, [")
	groups, order = {}, []

	for banner, _, _, name, _, _, arity in bound:
		if banner not in groups:
			groups[banner] = []
			order.append(banner)

		groups[banner].append("'%s'/%d" % (name, arity))

	for b in order:
		A("\t%% %s" % (b or 'misc'))

		for it in groups[b]:
			A("\t%s," % it)

		A("")

	A("\t% constants")
	A("\traylib_const/2,")
	A("\traylib_color/2")
	A("\t]).")
	A("")

	# struct declarations
	A("% Struct layouts, flattened. The comment on each line names the")
	A("% fields in order; '_' is compiler padding.")
	A("")

	for name in STRUCTS:
		if name not in used:
			continue

		flat, total = layout(name)
		A("%% %s (%d bytes): %s"
		  % (CNAME[name], total, ', '.join(p if p else '_' for _, p in flat)))
		A(":- foreign_struct(%s, [%s])." % (name, ','.join(t for t, _ in flat)))

	A("")
	aliased = [(a, t) for a, t in ALIASES.items() if t in used]

	if aliased:
		A("% raylib typedefs these to structs already declared above; the")
		A("% bindings below just use the underlying name.")

		for a, t in aliased:
			A("%%   %s -> %s" % (a, t))

		A("")

	# the bindings
	A(":- use_foreign_module('libraylib.so', [")
	cur_b = cur_h = object()

	for i, (banner, heading, doc, name, args, rt, _) in enumerate(bound):
		if banner != cur_b:
			if i:
				A("")

			A("\t%% ==== %s ====" % (banner or 'misc'))
			cur_b, cur_h = banner, object()

		if heading != cur_h:
			A("\t%% %s" % (heading or ''))
			cur_h = heading

		sig = "\t'%s'([%s], %s)%s" % (name, ','.join(args), rt,
		                             '' if i == len(bound)-1 else ',')
		A(sig.ljust(DOC_COLUMN) + ' % ' + doc if doc else sig)

	A("\t]).")
	A("")

	# constants
	A("% ---------------------------------------------------------------")
	A("% Constants, transcribed from the raylib enums and #defines so")
	A("% callers need not hardcode magic numbers.")
	A("% ---------------------------------------------------------------")
	A("")
	A("%% raylib_const(?Name, ?Value) is nondet.")
	A("%")
	A("% Enum constants, under their C names:")
	A("%")
	A("%     ?- raylib_const('KEY_SPACE', K).")
	A("%     K = 32.")
	A("")

	for ename, members in enums(path):
		A("%% %s" % ename)

		for k, v in members:
			A("raylib_const('%s', %d)." % (k, v))

		A("")

	A("%% raylib_color(?Name, ?Color) is nondet.")
	A("%")
	A("% The predefined colours, as ready-to-pass color structs:")
	A("%")
	A("%     ?- raylib_color('RAYWHITE', C).")
	A("%     C = [color,245,245,245,255].")
	A("")

	for k, (r, g, b, a) in colors(path):
		A("raylib_color('%s', [color,%d,%d,%d,%d])." % (k, r, g, b, a))

	A("")
	A(EPILOGUE)

	for name, why in sorted(skipped):
		A("%%   %-28s %s" % (name, why))

	A("")
	return '\n'.join(L) + '\n'


# --------------------------------------------------------------- verify

def verify(path):
	"""Assert every computed offset and size against the C compiler."""
	src = ['#include <stdio.h>', '#include <stddef.h>',
	       '#include "%s"' % path, '#include "%s"' % raymath_path(path),
	       'int fails = 0;',
	       '#define CK(S,F,E) do{ if(offsetof(S,F)!=(size_t)(E)){'
	       'printf("OFFSET %s.%s: cc=%zu here=%d\\n",#S,#F,offsetof(S,F),(int)(E));fails++;} }while(0)',
	       '#define CKSZ(S,E) do{ if(sizeof(S)!=(size_t)(E)){'
	       'printf("SIZE %s: cc=%zu here=%d\\n",#S,sizeof(S),(int)(E));fails++;} }while(0)',
	       'int main(void) {']

	n = 0

	for name in STRUCTS:
		src.append('\tCKSZ(%s, %d);' % (CNAME[name], layout(name)[1]))

		for fpath, _, off in fields(name):
			src.append('\tCK(%s, %s, %d);' % (CNAME[name], fpath, off))
			n += 1

	src.append('\tif (!fails) printf("%%d struct layouts OK\\n", %d);' % len(STRUCTS))
	src.append('\treturn fails ? 1 : 0;\n}')

	with tempfile.TemporaryDirectory() as d:
		c, exe = os.path.join(d, 'v.c'), os.path.join(d, 'v')
		open(c, 'w').write('\n'.join(src))
		cc = os.environ.get('CC', 'cc')
		r = subprocess.run([cc, '-I' + os.path.dirname(path), '-o', exe, c],
		                   capture_output=True, text=True)

		if r.returncode:
			print(r.stderr, file=sys.stderr)
			return 1

		r = subprocess.run([exe], capture_output=True, text=True)
		sys.stdout.write(r.stdout)
		print("%d fields checked" % n, file=sys.stderr)
		return r.returncode


if __name__ == '__main__':
	path = header_path()

	if '--verify' in sys.argv:
		sys.exit(verify(path))

	body = generate(path, raymath_path(path))

	if '--in-place' in sys.argv:
		i = sys.argv.index('--in-place')
		target = sys.argv[i+1] if len(sys.argv) > i+1 else OUT
		old = open(target).read() if os.path.exists(target) else None
		# Unchanged output must not touch the file: library/tmp/%.c depends on
		# it, so a rewrite forces a rebuild of the embedded library.
		how = 'unchanged' if old == body else ('written' if old else 'created')

		if how != 'unchanged':
			open(target, 'w').write(body)

		print("%s: %s" % (target, how), file=sys.stderr)
	else:
		sys.stdout.write(body)

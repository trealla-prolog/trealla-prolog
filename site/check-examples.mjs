// Runs every program the site offers to run in the playground through public/tpl.wasm, the way
// the playground's worker does, and fails if an answer is an error, anything reaches stderr, or a
// run crashes or hangs. These are the doc examples that get a "Run in the playground" link and
// the programs in src/gallery/. `npm run check` splits README.md first, as the build does.
//
//   node check-examples.mjs          check them all
//   node check-examples.mjs -v       and print each one's output

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = fileURLToPath(new URL('.', import.meta.url));
const TIMEOUT_S = 30;

if (process.argv[2] === '--one') await runOne();
else await main(process.argv.includes('-v'));

async function main(verbose) {
	const runs = [...(await docRuns()), ...(await galleryRuns())];
	let failed = 0;
	for (const r of runs) {
		const res = spawnSync(process.execPath, [fileURLToPath(import.meta.url), '--one'], {
			input: JSON.stringify(r),
			encoding: 'utf8',
			timeout: TIMEOUT_S * 1000,
		});
		const result = res.status === 0 ? JSON.parse(res.stdout) : undefined;
		const problem = res.error
			? `no answer within ${TIMEOUT_S} s`
			: res.status !== 0
				? `the checker failed: ${res.stderr.trim()}`
				: judge(result);
		if (problem) failed++;
		console.log(`${problem ? 'FAIL' : 'ok  '} ${r.name}`);
		if (problem) console.log(indent(problem));
		if (verbose && result) console.log(indent(result.out));
	}
	console.log(`\n${runs.length - failed} of ${runs.length} playground programs ran cleanly.`);
	if (!runs.length || failed) process.exit(1);
}

function judge({ out, err, code }) {
	if (err.trim()) return `wrote to stderr:\n${err.trimEnd()}`;
	if (code !== 0) return `exited with ${code}:\n${out.trimEnd()}`;
	const errors = out.split('\n').filter((l) => /^ {3}error\(/.test(l));
	if (errors.length) return `answered with an error:\n${errors.join('\n')}`;
	return undefined;
}

function indent(s) {
	return s.trimEnd().replace(/^/gm, '    ');
}

// The doc examples, from the pages split out of README.md.
async function docRuns() {
	const { fromMarkdown } = await import('mdast-util-from-markdown');
	const guessLang = (await import('./src/remark-guess-lang.mjs')).default();
	const { runnableBlocks } = await import('./src/remark-try-it.mjs');
	const root = join(HERE, 'src/content/docs');
	const runs = [];
	for (const f of walk(root).filter((f) => f.endsWith('.md'))) {
		const tree = fromMarkdown(readFileSync(f, 'utf8').replace(/^---\n[\s\S]*?\n---\n/, ''));
		guessLang(tree);
		for (const { node, run } of runnableBlocks(tree, relative(root, f))) {
			runs.push({ name: `${relative(root, f)}:${node.position.start.line}`, program: run.program, query: run.query });
		}
	}
	return runs;
}

async function galleryRuns() {
	const { parseExample } = await import('./src/gallery.mjs');
	const dir = join(HERE, 'src/gallery');
	return readdirSync(dir)
		.filter((f) => f.endsWith('.pl'))
		.sort()
		.map((f) => {
			const { program, queries } = parseExample(readFileSync(join(dir, f), 'utf8'));
			return { name: `gallery/${f}`, program, query: queries.join('\n') };
		});
}

function walk(dir) {
	return readdirSync(dir).flatMap((f) => {
		const p = join(dir, f);
		return statSync(p).isDirectory() ? walk(p) : [p];
	});
}

// One run, as src/repl/tpl-worker.ts does it: the same shim, in-memory files and arguments.
async function runOne() {
	// The shim logs to the console now and then; stdout carries only the result here.
	console.log = console.info = (...a) => process.stderr.write(a.join(' ') + '\n');
	const { WASI, File, OpenFile, ConsoleStdout, PreopenDirectory } = await import('@bjorn3/browser_wasi_shim');
	const { program, query } = JSON.parse(readFileSync(0, 'utf8'));
	let q = query.trim();
	if (!q.endsWith('.')) q += '.';
	const enc = new TextEncoder();
	const text = { out: '', err: '' };
	const stream = (k) => {
		const dec = new TextDecoder();
		return new ConsoleStdout((buf) => (text[k] += dec.decode(buf, { stream: true })));
	};
	const files = new Map([
		['runner.pl', new File(readFileSync(join(HERE, 'src/repl/runner.pl')))],
		['program.pl', new File(enc.encode(program))],
		['query.pl', new File(enc.encode(q + '\n'))],
	]);
	const fds = [new OpenFile(new File(new Uint8Array())), stream('out'), stream('err'), new PreopenDirectory('.', files)];
	const wasi = new WASI(['tpl', '-q', '-f', 'runner.pl', 'program.pl', '-g', 'web_repl_main'], ['LANG=C.UTF-8'], fds);
	const readlink = wasi.wasiImport.path_readlink;
	wasi.wasiImport.path_readlink = (...a) => {
		const r = readlink(...a);
		return r === 58 ? 28 : r;
	};
	let code;
	try {
		const module = await WebAssembly.compile(readFileSync(join(HERE, 'public/tpl.wasm')));
		const inst = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi.wasiImport });
		code = wasi.start(inst);
	} catch (e) {
		text.err += String(e);
		code = -1;
	}
	process.stdout.write(JSON.stringify({ out: text.out.replace(/\x01/g, ''), err: text.err, code }));
}

// Runs one tpl.wasm process per message, with the program and query as files in an in-memory directory.
import { WASI, File, OpenFile, ConsoleStdout, PreopenDirectory } from '@bjorn3/browser_wasi_shim';
import runner from './runner.pl?raw';

type Run = { module: WebAssembly.Module; program?: string; query?: string; args?: string[] };

const enc = new TextEncoder();
const MAX_OUTPUT = 256 * 1024;
let sent = 0;

function stream(type: 'out' | 'err') {
	const dec = new TextDecoder();
	return new ConsoleStdout((buf) => {
		if (sent > MAX_OUTPUT) return;
		const text = dec.decode(buf, { stream: true });
		if (!text) return;
		const room = MAX_OUTPUT - sent;
		sent += text.length;
		self.postMessage({ type, text: text.length > room ? text.slice(0, room) + '\n… output cut off at 256 KB\n' : text });
	});
}

self.onmessage = async (e: MessageEvent<Run>) => {
	const { module, program = '', query = '', args } = e.data;
	const files = new Map([
		['runner.pl', new File(enc.encode(runner))],
		['program.pl', new File(enc.encode(program))],
		['query.pl', new File(enc.encode(query))],
	]);
	const fds = [new OpenFile(new File(new Uint8Array())), stream('out'), stream('err'), new PreopenDirectory('.', files)];
	const argv = args ?? ['tpl', '-q', '-f', 'runner.pl', 'program.pl', '-g', 'web_repl_main'];
	const wasi = new WASI(argv, ['LANG=C.UTF-8'], fds);
	// realpath() asks readlink about every file; WASI says EINVAL for "not a symlink", the shim says ENOTSUP.
	const readlink = wasi.wasiImport.path_readlink as (...a: number[]) => number;
	wasi.wasiImport.path_readlink = (...a: number[]) => {
		const r = readlink(...a);
		return r === 58 ? 28 : r;
	};
	sent = 0;
	const t0 = performance.now();
	try {
		const inst = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi.wasiImport });
		const code = wasi.start(inst as unknown as { exports: { memory: WebAssembly.Memory; _start: () => unknown } });
		self.postMessage({ type: 'done', code, ms: performance.now() - t0 });
	} catch (err) {
		self.postMessage({ type: 'crash', text: String(err), ms: performance.now() - t0 });
	}
};

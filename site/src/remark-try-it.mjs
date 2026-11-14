// Puts a "Run in the playground" link under each Prolog block that can run in a browser tab:
// the lines before its first ?- become the program, and its ?- lines the queries, run in turn.
// A block of queries alone runs against the program of the last linked block on its page.

// What tpl.wasm in a browser tab cannot do: foreign libraries, the network, processes,
// threads, hardware, files that are not there, and halting before the queries run.
const UNRUNNABLE = new RegExp(
	[
		String.raw`\$dlopen|\$register_|use_foreign_module|foreign_callback|janus`,
		String.raw`\b(py|pg|llm|igraph|re|sqlite3?|http|thread|process|task)_\w+\(`,
		String.raw`\b(lp_solve|geturl|popen|shell|system|future|tasklist|gpio_\w+|delay_ms)\(`,
		String.raw`_in_file\(|parse_csv_file\(|samples/|\bhalt\b`,
	].join('|'),
);

const SHELL = /^\s*(\$|✗)(\s|$)/;

// The transcripts were made with these libraries loaded from ~/.tplrc; the playground has none.
const LIBRARIES = [
	['freeze', /\bfreeze\(/],
	['dif', /\bdif\(/],
	['when', /\bwhen\(/],
];

// The program and queries in a block, or undefined when there is nothing to run here.
export function tryIt(code, inherited = '') {
	const program = [];
	const queries = [];
	let pending;
	let started = false;
	for (const line of code.split('\n')) {
		const m = line.match(/^\s*\?-\s?(.*)$/);
		if (m) {
			started = true;
			pending = m[1].trim() ? [m[1]] : undefined;
		} else if (pending) {
			pending.push(line);
		} else {
			if (!started && !SHELL.test(line)) program.push(line);
			continue;
		}
		if (!pending) continue;
		const text = pending.join('\n').replace(/\s+%[^'"]*$/, '').trimEnd();
		if (text.endsWith('.')) {
			queries.push(text);
			pending = undefined;
		}
	}
	// A query the browser cannot run ends the list, since later ones may lean on it.
	const cut = queries.findIndex((q) => UNRUNNABLE.test(q));
	if (cut >= 0) queries.length = cut;
	if (queries.every((q) => /^\s*(use_module|ensure_loaded)\(/.test(q))) return undefined;
	const own = dedent(program).join('\n').replace(/^\s*\n/, '').trimEnd();
	if (UNRUNNABLE.test(own)) return undefined;
	let p = own ? own + '\n' : inherited;
	const query = queries.join('\n');
	const uses = LIBRARIES.filter(([lib, re]) => re.test(p + query) && !p.includes(`library(${lib})`));
	p = uses.map(([lib]) => `:- use_module(library(${lib})).\n`).join('') + p;
	return { program: p, query, own: own !== '' };
}

// Drops the indent the program lines share, which the README's nesting leaves behind.
function dedent(lines) {
	const indents = lines.filter((l) => l.trim()).map((l) => l.match(/^[\t ]*/)[0].length);
	const n = indents.length ? Math.min(...indents) : 0;
	return lines.map((l) => l.slice(Math.min(n, l.match(/^[\t ]*/)[0].length)));
}

const share = ({ program, query }) => Buffer.from(JSON.stringify({ p: program, q: query })).toString('base64url');

// page.program is the program of the last linked block, for a block of queries alone to inherit.
function walk(node, href, page) {
	const kids = node.children;
	if (!kids) return;
	for (let i = 0; i < kids.length; i++) {
		const k = kids[i];
		const run = k.type === 'code' && k.lang === 'prolog' && tryIt(k.value, page.program);
		if (run) {
			if (run.own) page.program = run.program;
			kids.splice(++i, 0, {
				type: 'html',
				value: `<p class="tr-try"><a href="${href}#code=${share(run)}&amp;run">Run in the playground</a></p>`,
			});
		} else walk(k, href, page);
	}
}

export default function remarkTryIt({ base = '/' } = {}) {
	const href = `${base.replace(/\/?$/, '/')}playground/`;
	return (tree) => walk(tree, href, { program: '' });
}

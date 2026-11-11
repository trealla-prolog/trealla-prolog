// The README's tab-indented code blocks carry no language; guess one so they get highlighted.

const SHELL = /^(\$ |make|tpl|git|brew|cd|sudo|apt(-get)?|dnf|pacman|pkg|wasmtime|export|gcc|cc|clang|python3?|pip3?|curl|wget|cmake|idf\.py|arduino-cli|picocom|qemu-\S+|\.\/\S+|mkdir|cp|mv|ls|cat|echo|LD_|TPL_|CC=)/;

function guess(code) {
	const first = (code.split('\n').find((l) => l.trim()) ?? '').trim();
	if (/^\$ /m.test(code)) return 'console';
	if (/^\s*\?-/m.test(code)) return 'prolog';
	if (/#include|^\s*(int|void|static|bool|char|pl_\w+)\s+\**\w+\s*\(/m.test(code)) return 'c';
	if (/^(import \w|from \w+ import )/m.test(code)) return 'python';
	if (SHELL.test(first)) return 'sh';
	if (/:-|-->/.test(code) || /^[a-z]\w*(\(.*\))?\s*\.\s*$/m.test(code)) return 'prolog';
	return 'text';
}

function walk(node) {
	if (node.type === 'code' && !node.lang) node.lang = guess(node.value);
	// The README marks toplevel transcripts as console; without a shell prompt they are Prolog.
	if (node.type === 'code' && node.lang === 'console' && !/^\$ /m.test(node.value)) node.lang = 'prolog';
	for (const child of node.children ?? []) walk(child);
}

export default function remarkGuessLang() {
	return (tree) => walk(tree);
}

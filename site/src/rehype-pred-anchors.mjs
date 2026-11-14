// Gives each table row that starts with a `name/arity` an anchor of that name, so a link such as
// reference/predicates/#msort/2 lands on it. The anchor is a small heading round the code, since
// Pagefind only makes search sub-results from headings: searching msort then jumps to the row.
// The first row for a name wins on a page. The predicate reference also gets a filter box, and
// a script that opens the collapsed section an anchor sits in.

const INDICATOR = /^\S+\/\d+$/;

const text = (node) => (node.type === 'text' ? node.value : (node.children ?? []).map(text).join(''));

function walk(node, seen) {
	if (node.type === 'element' && node.tagName === 'tr') {
		const cell = node.children.find((c) => c.type === 'element' && c.tagName === 'td');
		const i = cell ? cell.children.findIndex((c) => c.type === 'element' && c.tagName === 'code') : -1;
		const pi = i >= 0 && text(cell.children[i]).trim();
		if (pi && INDICATOR.test(pi) && !seen.has(pi)) {
			seen.add(pi);
			cell.children[i] = {
				type: 'element',
				tagName: 'h4',
				properties: { id: pi, className: ['tr-pi'] },
				children: [cell.children[i]],
			};
		}
	}
	for (const c of node.children ?? []) walk(c, seen);
}

const FILTER = `<div class="tr-pred-filter"><input type="search" placeholder="Filter predicates, e.g. sort or atom_" aria-label="Filter predicates" autocomplete="off" spellcheck="false"><span aria-live="polite"></span></div>`;

const SCRIPT = `<script>
(() => {
	const show = () => {
		const id = decodeURIComponent(location.hash.slice(1));
		const el = id && document.getElementById(id);
		if (!el || !el.closest('details')) return;
		for (let d = el.closest('details'); d; d = d.parentElement && d.parentElement.closest('details')) d.open = true;
		el.scrollIntoView({ block: 'center' });
	};
	addEventListener('hashchange', show);
	show();
	const box = document.querySelector('.tr-pred-filter input');
	const count = document.querySelector('.tr-pred-filter span');
	const rows = [...document.querySelectorAll('.sl-markdown-content details tbody tr')];
	const sections = [...document.querySelectorAll('.sl-markdown-content details')];
	box && box.addEventListener('input', () => {
		const q = box.value.trim().toLowerCase();
		let n = 0;
		for (const tr of rows) {
			tr.hidden = q !== '' && !tr.cells[0].textContent.toLowerCase().includes(q);
			if (!tr.hidden) n++;
		}
		for (const d of sections) {
			const any = [...d.querySelectorAll('tbody tr')].some((tr) => !tr.hidden);
			d.open = q !== '' && any;
			d.hidden = q !== '' && !any;
			const head = d.previousElementSibling;
			if (head) head.hidden = d.hidden;
		}
		count.textContent = q ? (n === 1 ? '1 match' : n + ' matches') : '';
	});
})();
</script>`;

const raw = (value) => ({ type: 'raw', value });

export default function rehypePredAnchors() {
	return (tree, file) => {
		walk(tree, new Set());
		if (!/reference[\\/]predicates\.md$/.test(file.path ?? '')) return;
		const first = tree.children.findIndex((c) => c.type === 'element' && /^h[1-6]$/.test(c.tagName));
		tree.children.splice(first < 0 ? 0 : first, 0, raw(FILTER));
		tree.children.push(raw(SCRIPT));
	};
}

// A program in src/gallery/ is plain Prolog whose %?- lines are its queries.
// Example.astro shows it and links to it; check-examples.mjs runs it.
export function parseExample(src) {
	const lines = src.trimEnd().split('\n');
	const queries = lines.filter((l) => l.startsWith('%?- ')).map((l) => l.slice(4));
	const program = lines.filter((l) => !l.startsWith('%?- ')).join('\n').trimEnd() + '\n';
	return { program, queries };
}

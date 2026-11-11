#!/usr/bin/env python3
# Splits README.md into the website's doc pages, following site/pages.json.
#
#   ./util/split_readme.py            (run from the repo root, or via npm run build in site/)
#
# README.md stays the single source: the pages under site/src/content/docs/<group>/
# are regenerated on every build and are not committed. A top-level README
# heading that pages.json neither maps nor skips stops the build, so a new
# section cannot go missing from the site.

import json, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, 'site')
DOCS = os.path.join(SITE, 'src', 'content', 'docs')
GEN = os.path.join(SITE, 'src', 'generated')
REPO = 'https://github.com/trealla-prolog/trealla-prolog'
EXPERIMENTAL = '##EXPERIMENTAL##'

def clean_title(t):
	return t.replace(EXPERIMENTAL, '').strip()

def plain(t):
	t = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', t)
	return re.sub(r'[`*]', '', t).strip()

def gh_slug(t):
	# github-slugger: lowercase, drop all but letters, digits, '_', '-' and space, spaces to '-'
	return re.sub(r'[^\w\- ]', '', plain(t).lower()).replace(' ', '-')

def loose(t):
	return re.sub(r'[^a-z0-9]', '', t.lower())

class Slugger:
	def __init__(self): self.seen = {}
	def slug(self, t):
		s = base = gh_slug(t)
		while s in self.seen:
			self.seen[base] += 1
			s = f'{base}-{self.seen[base]}'
		self.seen[s] = 0
		return s

def parse(lines):
	# Returns [(level, title, line_index, underline)] for headings outside code.
	heads, fence, prev_blank = [], False, True
	for i, l in enumerate(lines):
		if re.match(r'^\s*```', l):
			fence = not fence
		elif not fence and not l.startswith(('\t', '    ')):
			m = re.match(r'^(#{1,6})\s+(.*?)\s*#*\s*$', l)
			nxt = lines[i+1] if i+1 < len(lines) else ''
			if m:
				heads.append((len(m.group(1)), m.group(2), i, False))
			elif l.strip() and not l.startswith('|') and re.match(r'^(=+|-+)\s*$', nxt):
				heads.append((1 if nxt[0] == '=' else 2, l.strip(), i, True))
	return heads

def main():
	cfg = json.load(open(os.path.join(SITE, 'pages.json')))
	lines = open(os.path.join(ROOT, 'README.md'), encoding='utf-8').read().split('\n')
	heads = parse(lines)

	# Cut into top-level sections.
	tops = [h for h in heads if h[0] == 1]
	sections = {}
	for n, (lvl, title, i, ul) in enumerate(tops):
		end = tops[n+1][2] if n+1 < len(tops) else len(lines)
		sections[clean_title(title)] = {
			'raw': title, 'start': i + (2 if ul else 1), 'end': end,
			'experimental': EXPERIMENTAL in title}

	where = {}
	for g in cfg['groups']:
		for order, p in enumerate(g['pages']):
			for s in p['sections']:
				if s not in sections: sys.exit(f'split_readme: pages.json names "{s}", which README.md lacks')
				where[s] = (g, p, order)
	missing = [s for s in sections if s not in where and s not in cfg['skip']]
	if missing:
		sys.exit('split_readme: README.md sections not in site/pages.json: ' + ', '.join(missing))

	# Every heading's README slug -> (page url, slug on that page).
	readme_slugger, anchors, by_name = Slugger(), {}, {}
	page_sluggers = {}
	sec_of = None
	for lvl, title, i, ul in heads:
		if lvl == 1: sec_of = clean_title(title)
		rs = readme_slugger.slug(title)
		if sec_of not in where: continue
		g, p, _ = where[sec_of]
		url = f"{g['dir']}/{p['slug']}/"
		ps = page_sluggers.setdefault(url, Slugger())
		single = len(p['sections']) == 1
		local = '' if (lvl == 1 and single) else ps.slug(clean_title(title))
		anchors.setdefault(rs, (url, local))
		anchors.setdefault('~' + loose(rs), (url, local))
		name = clean_title(title) if lvl == 1 else f'{sec_of} > {clean_title(title)}'
		by_name.setdefault(name, (url, local))

	def rewrite_links(text, here):
		def anchor(m):
			label, target = m.group(1), m.group(2)
			if target.startswith('#'):
				hit = anchors.get(target[1:]) or anchors.get('~' + loose(target[1:]))
				if not hit: return m.group(0)
				url, local = hit
				frag = f'#{local}' if local else ''
				if url == here: return f'[{label}]({frag or "#"})'
				return f'[{label}](../../{url}{frag})'
			if re.match(r'^[a-z]+:', target): return m.group(0)
			img = m.group(0).startswith('!')
			return f"{'!' if img else ''}[{label}]({REPO}/{'raw' if img else 'blob'}/main/{target})"
		return re.sub(r'!?\[([^\]]*)\]\(([^)\s]+)\)', anchor, text)

	# Write pages.
	os.makedirs(DOCS, exist_ok=True)
	for g in cfg['groups']:
		d = os.path.join(DOCS, g['dir'])
		shutil.rmtree(d, ignore_errors=True)
		os.makedirs(d)
		for order, p in enumerate(g['pages']):
			here = f"{g['dir']}/{p['slug']}/"
			single = len(p['sections']) == 1
			first = sections[p['sections'][0]]
			title = p.get('title') or p['sections'][0]
			out, desc = [], None
			for sname in p['sections']:
				sec = sections[sname]
				body = lines[sec['start']:sec['end']]
				shift = 1 if not single else 0
				# Lift the shallowest sub-heading to h2 (h3 under a merged section's h2).
				subs = [h for h in heads if sec['start'] <= h[2] < sec['end']]
				if subs:
					shift = (3 if not single else 2) - min(h[0] for h in subs)
				if not single:
					out += ['', f'## {sname}', '']
				fence, skip = False, False
				for k, l in enumerate(body):
					idx = sec['start'] + k
					if skip: skip = False; continue
					if re.match(r'^\s*```', l): fence = not fence
					h = next((h for h in subs if h[2] == idx), None)
					if h and not fence:
						lvl = min(6, h[0] + shift)
						out.append(f"{'#' * lvl} {clean_title(h[1])}")
						skip = h[3]
						continue
					if not fence and not l.startswith(('\t', '    ')):
						l = rewrite_links(l, here)
					out.append(l)
				if desc is None:
					para = []
					for l in body:
						if not l.strip():
							if para: break
							continue
						if l.startswith(('\t', '    ', '|', '<', '#', '```', '-', '*')): break
						para.append(l.strip())
					if para:
						desc = plain(' '.join(para))
						m = re.match(r'^(.{40,200}?[.!?])(\s|$)', desc)
						desc = m.group(1) if m else desc[:200]
			fm = ['---', f'title: {json.dumps(title)}']
			if desc: fm.append(f'description: {json.dumps(desc)}')
			fm += [f'editUrl: {json.dumps(REPO + "/edit/main/README.md")}', 'sidebar:', f'  order: {order}']
			if any(sections[s]['experimental'] for s in p['sections']):
				fm += ['  badge:', '    text: Experimental', '    variant: caution']
			fm.append('---')
			text = '\n'.join(fm + out).rstrip() + '\n'
			text = re.sub(r'\n{3,}', '\n\n', text)
			open(os.path.join(d, p['slug'] + '.md'), 'w', encoding='utf-8').write(text)

	# Old single-page anchors -> new pages, for the landing page's redirect.
	old = json.load(open(os.path.join(SITE, 'old-anchors.json')))
	redirects = {}
	for oid, name in old.items():
		if oid.startswith('_'): continue
		hit = by_name.get(name)
		if hit: redirects[oid] = hit[0] + (f'#{hit[1]}' if hit[1] else '')
	os.makedirs(GEN, exist_ok=True)
	json.dump(redirects, open(os.path.join(GEN, 'old-anchors.json'), 'w'), indent=1, sort_keys=True)
	lost = [k for k in old if not k.startswith('_') and k not in redirects]
	if lost: print('split_readme: old anchors with no new home: ' + ', '.join(lost), file=sys.stderr)

if __name__ == '__main__':
	main()

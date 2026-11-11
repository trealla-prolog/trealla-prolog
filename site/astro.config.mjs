import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import pages from './pages.json' with { type: 'json' };
import remarkGuessLang from './src/remark-guess-lang.mjs';

const repo = 'https://github.com/trealla-prolog/trealla-prolog';

export default defineConfig({
	site: 'https://trealla-prolog.github.io',
	base: '/trealla-prolog',
	markdown: {
		smartypants: false,
		remarkPlugins: [remarkGuessLang],
	},
	integrations: [
		starlight({
			title: 'Trealla Prolog',
			description: 'A compact, efficient ISO Prolog interpreter written in plain C.',
			favicon: '/favicon.svg',
			logo: { src: './src/assets/trealla-mark.svg', alt: '' },
			head: [
				{ tag: 'meta', attrs: { property: 'og:image', content: 'https://trealla-prolog.github.io/trealla-prolog/social.png' } },
				{ tag: 'meta', attrs: { name: 'twitter:card', content: 'summary_large_image' } },
			],
			social: [{ icon: 'github', label: 'GitHub', href: repo }],
			editLink: { baseUrl: `${repo}/edit/main/site/` },
			customCss: ['./src/styles/theme.css'],
			sidebar: [
				{ label: 'Playground', slug: 'playground' },
				...pages.groups.map((g) => ({
					label: g.label,
					items: [{ autogenerate: { directory: g.dir } }],
				})),
			],
		}),
	],
});

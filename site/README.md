# Website

The site at https://trealla-prolog.github.io/trealla-prolog/, built with [Starlight](https://starlight.astro.build/).

The docs pages are generated from the top-level `README.md` by `util/split_readme.py` on every build, following `pages.json`. Edit the README, not the generated pages. A new top-level README section has to be added to `pages.json` (or its `skip` list) or the build stops.

Hand-written pages are `src/content/docs/index.mdx` (the landing page) and `playground.mdx`. The REPL is `src/components/Repl.astro`; it runs `tpl.wasm` in a worker (`src/repl/tpl-worker.ts`) with `src/repl/runner.pl` printing answers the way the toplevel does.

To work on it locally:

    cd site
    npm ci
    unzip -j path/to/tpl-wasm-wasi.zip '*/tpl.wasm' -d public    # from a release, or `make wasm`
    npm run dev

`.github/workflows/site.yaml` builds and deploys it on pushes to `main` that touch the README or the site, and after each tagged release, taking `tpl.wasm` from the latest release.

# MAKO website

This directory owns MAKO's public product website. It has a Vinext preview/build for local Sites-compatible development and a static Vite build for GitHub Pages.

Run `npm ci`, then `npm run dev` for the local preview. Run `npm run build` to validate the Vinext production build and `npm run build:pages` to create the GitHub Pages artifact under `dist-pages/`. Both builds validate the website's GPL declaration, deployed third-party notices, and visible notices link through `npm run check:legal`. The Pages build also runs `npm run check:seo`, which requires canonical and social metadata, structured data, crawler files, and server-rendered page content in the static artifact.

`app/site-metadata.ts` owns the canonical URL, search title, description, keywords, social image, and structured data shared by the Vinext and GitHub Pages builds. The Pages Vite plugin injects those values and pre-renders the React page before browser hydration. Keep crawler-owned files in `public/robots.txt` and `public/sitemap.xml`; never patch generated `dist-pages/` output.

Current MAKO Decky and MAKO Renderer versions, release pages, and direct asset URLs are derived from the canonical release metadata in `plugin/package.json`; do not duplicate them in page copy. Both production builds run `npm run check:release-contract`, which rejects inconsistent renderer pins or a hardcoded semantic release version in `app/page.tsx`.

Public website copy does not use em dashes. Use commas, full stops, parentheses, or plain hyphens for compact numeric ranges instead.

The repository workflow at `.github/workflows/pages.yml` publishes `dist-pages/` after an approved website change or canonical release-metadata change reaches `main`. Local builds do not publish anything.

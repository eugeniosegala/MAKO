# MAKO website

This directory owns MAKO's public product website. It has a Vinext preview/build for local Sites-compatible development and a static Vite build for GitHub Pages.

Run `npm ci`, then `npm run dev` for the local preview. Run `npm run build` to validate the Vinext production build and `npm run build:pages` to create the GitHub Pages artifact under `dist-pages/`. Both builds validate the website's GPL declaration, deployed third-party notices, and visible notices link through `npm run check:legal`.

Current MAKO Decky and MAKO Renderer versions, release pages, and direct asset URLs are derived from the canonical release metadata in `plugin/package.json`; do not duplicate them in page copy. Both production builds run `npm run check:release-contract`, which rejects inconsistent renderer pins or a hardcoded semantic release version in `app/page.tsx`.

Public website copy does not use em dashes. Use commas, full stops, parentheses, or plain hyphens for compact numeric ranges instead.

The repository workflow at `.github/workflows/pages.yml` publishes `dist-pages/` after an approved website change or canonical release-metadata change reaches `main`. Local builds do not publish anything.

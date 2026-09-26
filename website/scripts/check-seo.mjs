#!/usr/bin/env node

import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const websiteRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '..',
);
const outputRoot = path.join(websiteRoot, 'dist-pages');
const index = readFileSync(path.join(outputRoot, 'index.html'), 'utf8');
const robots = readFileSync(path.join(outputRoot, 'robots.txt'), 'utf8');
const sitemap = readFileSync(path.join(outputRoot, 'sitemap.xml'), 'utf8');
const siteUrl = 'https://eugeniosegala.github.io/MAKO/';

const requiredIndexFragments = [
  '<html lang="en">',
  '<meta name="description"',
  '<meta name="robots" content="index, follow, max-image-preview:large, max-snippet:-1, max-video-preview:-1">',
  '<link rel="canonical" href="https://eugeniosegala.github.io/MAKO/">',
  '<link rel="sitemap" type="application/xml" href="https://eugeniosegala.github.io/MAKO/sitemap.xml">',
  '<meta property="og:site_name" content="MAKO">',
  '<meta property="og:image:alt"',
  '<meta name="twitter:image:alt"',
  '<script type="application/ld+json">',
  '"@type":"SoftwareApplication"',
  '"@type":"FAQPage"',
  '<main>',
  '<h1>',
  'Steam Deck',
  'MAKO Renderer',
];

for (const fragment of requiredIndexFragments) {
  if (!index.includes(fragment)) {
    throw new Error(
      `The static website is missing required SEO output: ${fragment}`,
    );
  }
}

if (index.includes('<div id="root"></div>')) {
  throw new Error(
    'The static website must pre-render its page content instead of shipping an empty root',
  );
}
if (index.includes('content="[') || index.includes('href="[')) {
  throw new Error(
    'The static website contains a Markdown-formatted URL inside HTML metadata',
  );
}
if (
  !robots.includes('User-agent: *') ||
  !robots.includes(`Sitemap: ${siteUrl}sitemap.xml`)
) {
  throw new Error(
    'robots.txt must allow crawling and advertise the canonical sitemap',
  );
}
if (!sitemap.includes(`<loc>${siteUrl}</loc>`)) {
  throw new Error('sitemap.xml must include the canonical MAKO homepage');
}

console.log(
  'Validated canonical metadata, social cards, structured data, crawler files, and pre-rendered page content',
);

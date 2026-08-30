import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const websiteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const packageManifest = JSON.parse(readFileSync(path.join(websiteRoot, 'package.json'), 'utf8'));
const page = readFileSync(path.join(websiteRoot, 'app/page.tsx'), 'utf8');
const notices = readFileSync(path.join(websiteRoot, 'public/third-party-notices.txt'), 'utf8');

if (packageManifest.license !== 'GPL-3.0-or-later') {
  throw new Error('The website package must declare GPL-3.0-or-later');
}
if (!page.includes('href="third-party-notices.txt"')) {
  throw new Error('The website footer must link to the deployed third-party notices');
}
for (const requiredNotice of ['Meta Platforms', 'Vercel', 'Cloudflare', 'Tailwind Labs', 'MIT License', 'GitHub', 'Discord']) {
  if (!notices.includes(requiredNotice)) {
    throw new Error(`The website third-party notices are missing: ${requiredNotice}`);
  }
}

console.log('Validated website license and third-party notices');

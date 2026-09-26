import tailwindcss from '@tailwindcss/postcss';
import react from '@vitejs/plugin-react';
import { createElement } from 'react';
import { renderToString } from 'react-dom/server';
import { defineConfig, type HtmlTagDescriptor, type Plugin } from 'vite';
import Home from './app/page';
import {
  siteDescription,
  siteKeywords,
  siteTitle,
  siteUrl,
  socialImageUrl,
  structuredDataJson,
} from './app/site-metadata';

const socialImageAlt =
  'MAKO frame generation, spatial scaling, and shaders for SteamOS and Linux';

function metaTag(name: string, content: string): HtmlTagDescriptor {
  return { tag: 'meta', attrs: { name, content }, injectTo: 'head' };
}

function propertyTag(property: string, content: string): HtmlTagDescriptor {
  return { tag: 'meta', attrs: { property, content }, injectTo: 'head' };
}

function staticPagesSeo(): Plugin {
  return {
    name: 'mako-static-pages-seo',
    transformIndexHtml(html) {
      const renderedPage = renderToString(createElement(Home));
      const tags: HtmlTagDescriptor[] = [
        { tag: 'title', children: siteTitle, injectTo: 'head' },
        metaTag('description', siteDescription),
        metaTag('keywords', siteKeywords.join(', ')),
        metaTag('author', 'MAKO contributors'),
        metaTag(
          'robots',
          'index, follow, max-image-preview:large, max-snippet:-1, max-video-preview:-1',
        ),
        propertyTag('og:title', siteTitle),
        propertyTag('og:description', siteDescription),
        propertyTag('og:type', 'website'),
        propertyTag('og:url', siteUrl),
        propertyTag('og:site_name', 'MAKO'),
        propertyTag('og:locale', 'en_US'),
        propertyTag('og:image', socialImageUrl),
        propertyTag('og:image:secure_url', socialImageUrl),
        propertyTag('og:image:type', 'image/png'),
        propertyTag('og:image:width', '1200'),
        propertyTag('og:image:height', '630'),
        propertyTag('og:image:alt', socialImageAlt),
        metaTag('twitter:card', 'summary_large_image'),
        metaTag('twitter:title', siteTitle),
        metaTag('twitter:description', siteDescription),
        metaTag('twitter:image', socialImageUrl),
        metaTag('twitter:image:alt', socialImageAlt),
        {
          tag: 'link',
          attrs: { rel: 'canonical', href: siteUrl },
          injectTo: 'head',
        },
        {
          tag: 'link',
          attrs: {
            rel: 'sitemap',
            type: 'application/xml',
            href: `${siteUrl}sitemap.xml`,
          },
          injectTo: 'head',
        },
        {
          tag: 'script',
          attrs: { type: 'application/ld+json' },
          children: structuredDataJson,
          injectTo: 'head',
        },
      ];

      return {
        html: html.replace(
          '<div id="root"></div>',
          `<div id="root">${renderedPage}</div>`,
        ),
        tags,
      };
    },
  };
}

export default defineConfig({
  base: process.env.GITHUB_ACTIONS ? '/MAKO/' : '/',
  css: {
    postcss: {
      plugins: [tailwindcss()],
    },
  },
  plugins: [react(), staticPagesSeo()],
  build: {
    outDir: 'dist-pages',
    emptyOutDir: true,
  },
});

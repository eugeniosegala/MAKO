import type { Metadata } from 'next';
import './globals.css';
import {
  siteDescription,
  siteKeywords,
  siteTitle,
  siteUrl,
  socialImageUrl,
  structuredDataJson,
} from './site-metadata';

export const metadata: Metadata = {
  title: siteTitle,
  description: siteDescription,
  applicationName: 'MAKO',
  authors: [
    { name: 'MAKO contributors', url: 'https://github.com/eugeniosegala/MAKO' },
  ],
  creator: 'MAKO contributors',
  publisher: 'MAKO',
  keywords: siteKeywords,
  category: 'technology',
  metadataBase: new URL(siteUrl),
  alternates: {
    canonical: siteUrl,
  },
  robots: {
    index: true,
    follow: true,
    googleBot: {
      index: true,
      follow: true,
      'max-image-preview': 'large',
      'max-snippet': -1,
      'max-video-preview': -1,
    },
  },
  icons: {
    icon: 'assets/mako-discord-icon.webp',
  },
  openGraph: {
    title: siteTitle,
    description: siteDescription,
    url: siteUrl,
    siteName: 'MAKO',
    type: 'website',
    locale: 'en_US',
    images: [
      {
        url: socialImageUrl,
        width: 1200,
        height: 630,
        alt: 'MAKO frame generation, spatial scaling, and shaders for SteamOS and Linux',
      },
    ],
  },
  twitter: {
    card: 'summary_large_image',
    title: siteTitle,
    description: siteDescription,
    images: [
      {
        url: socialImageUrl,
        alt: 'MAKO frame generation, spatial scaling, and shaders for SteamOS and Linux',
      },
    ],
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <head>
        <meta name="theme-color" content="#020508" />
        <link
          rel="sitemap"
          type="application/xml"
          href={`${siteUrl}sitemap.xml`}
        />
        <script
          type="application/ld+json"
          dangerouslySetInnerHTML={{ __html: structuredDataJson }}
        />
      </head>
      <body>{children}</body>
    </html>
  );
}

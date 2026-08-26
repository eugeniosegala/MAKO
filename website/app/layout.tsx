import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'MAKO - Spatial Scaling & Frame Generation on SteamOS/Linux',
  description: 'MAKO brings proprietary Lossless Scaling LS1, a built-in open Vulkan spatial scaler, and LSFG frame generation to Steam Deck, SteamOS, and Linux.',
  metadataBase: new URL('https://eugeniosegala.github.io/MAKO/'),
  icons: {
    icon: 'assets/mako-logo.webp',
  },
  openGraph: {
    title: 'MAKO - Spatial Scaling & Frame Generation on SteamOS/Linux',
    description: 'MAKO combines proprietary Lossless Scaling LS1, a built-in open Vulkan spatial scaler, and LSFG frame generation on Steam Deck, SteamOS, and Linux.',
    url: 'https://eugeniosegala.github.io/MAKO/',
    siteName: 'MAKO',
    type: 'website',
    images: [
      {
        url: 'og.png',
        width: 1200,
        height: 630,
        alt: 'MAKO - Spatial Scaling and Frame Generation on SteamOS/Linux',
      },
    ],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'MAKO - Spatial Scaling & Frame Generation on SteamOS/Linux',
    description: 'MAKO combines proprietary Lossless Scaling LS1, a built-in open Vulkan spatial scaler, and LSFG frame generation on Steam Deck, SteamOS, and Linux.',
    images: ['og.png'],
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}

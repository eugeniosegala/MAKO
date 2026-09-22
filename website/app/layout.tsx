import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'MAKO - Frame Generation, Scaling & Shaders on SteamOS/Linux',
  description: 'Independent Linux frame generation, scaling, and bundled shaders, unaffiliated with Lossless Scaling. LSFG and LS1 require a separate purchase; MAKO Scaler and shaders do not.',
  metadataBase: new URL('https://eugeniosegala.github.io/MAKO/'),
  icons: {
    icon: 'assets/mako-discord-icon.webp',
  },
  openGraph: {
    title: 'MAKO - Frame Generation, Scaling & Shaders on SteamOS/Linux',
    description: 'Independent Linux frame generation, scaling, and bundled shaders, unaffiliated with Lossless Scaling. LSFG and LS1 require a separate purchase; MAKO Scaler and shaders do not.',
    url: 'https://eugeniosegala.github.io/MAKO/',
    siteName: 'MAKO',
    type: 'website',
    images: [
      {
        url: 'og.png',
        width: 1200,
        height: 630,
        alt: 'MAKO - Frame Generation, Scaling, and Shaders on SteamOS/Linux',
      },
    ],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'MAKO - Frame Generation, Scaling & Shaders on SteamOS/Linux',
    description: 'Independent Linux frame generation, scaling, and bundled shaders, unaffiliated with Lossless Scaling. LSFG and LS1 require a separate purchase; MAKO Scaler and shaders do not.',
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

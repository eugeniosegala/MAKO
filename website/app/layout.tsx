import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'MAKO - Frame Generation & Spatial Scaling on SteamOS/Linux',
  description: 'MAKO brings LSFG frame generation plus Vulkan spatial scaling with proprietary LS1 and the open MAKO Scaler to Steam Deck, Steam Machine, SteamOS, and Linux.',
  metadataBase: new URL('https://eugeniosegala.github.io/MAKO/'),
  icons: {
    icon: 'assets/mako-discord-icon.webp',
  },
  openGraph: {
    title: 'MAKO - Frame Generation & Spatial Scaling on SteamOS/Linux',
    description: 'MAKO brings LSFG frame generation plus Vulkan spatial scaling with proprietary LS1 and the open MAKO Scaler to Steam Deck, Steam Machine, SteamOS, and Linux.',
    url: 'https://eugeniosegala.github.io/MAKO/',
    siteName: 'MAKO',
    type: 'website',
    images: [
      {
        url: 'og.png',
        width: 1200,
        height: 630,
        alt: 'MAKO - Frame Generation and Spatial Scaling on SteamOS/Linux',
      },
    ],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'MAKO - Frame Generation & Spatial Scaling on SteamOS/Linux',
    description: 'MAKO brings LSFG frame generation plus Vulkan spatial scaling with proprietary LS1 and the open MAKO Scaler to Steam Deck, Steam Machine, SteamOS, and Linux.',
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

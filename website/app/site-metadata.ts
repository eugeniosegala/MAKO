import { releaseData } from './release-data';

export const siteUrl = 'https://eugeniosegala.github.io/MAKO/';
export const siteTitle =
  'MAKO: Frame Generation & Scaling for SteamOS and Linux';
export const siteDescription =
  'MAKO brings frame generation, spatial scaling, and per-game shader effects to Steam Deck, SteamOS, and desktop Linux through Decky and Vulkan.';
export const socialImageUrl = `${siteUrl}og.png`;
export const siteKeywords = [
  'MAKO',
  'Steam Deck frame generation',
  'Linux frame generation',
  'SteamOS frame generation',
  'Linux spatial scaling',
  'Steam Deck shaders',
  'Decky Loader plugin',
  'Vulkan frame generation',
  'Lossless Scaling Linux',
];

export const structuredData = {
  '@context': 'https://schema.org',
  '@graph': [
    {
      '@type': 'WebSite',
      '@id': `${siteUrl}#website`,
      url: siteUrl,
      name: 'MAKO',
      alternateName: 'MAKO Decky and MAKO Renderer',
      description: siteDescription,
      inLanguage: 'en',
    },
    {
      '@type': 'SoftwareApplication',
      '@id': `${siteUrl}#software`,
      name: 'MAKO',
      alternateName: ['MAKO Decky', 'MAKO Renderer'],
      url: siteUrl,
      description: siteDescription,
      applicationCategory: 'UtilitiesApplication',
      operatingSystem: 'SteamOS, Linux',
      isAccessibleForFree: true,
      offers: {
        '@type': 'Offer',
        price: '0',
        priceCurrency: 'USD',
        availability: 'https://schema.org/InStock',
      },
      softwareVersion: `MAKO Decky ${releaseData.deckyVersion}; MAKO Renderer ${releaseData.rendererVersion}`,
      downloadUrl: [
        releaseData.links.deckyDownload,
        releaseData.links.rendererDownload,
      ],
      codeRepository: releaseData.links.repository,
      license: `${releaseData.links.repository}/blob/main/LICENSE.md`,
      image: socialImageUrl,
      softwareRequirements:
        'Lossless Scaling is required only for LSFG frame generation and LS1 scaling. MAKO Scaler and bundled shaders work independently.',
      featureList: [
        'Fixed and adaptive frame generation',
        'Spatial scaling with MAKO Scaler or LS1',
        'Per-game Vulkan shader effects',
        'Steam Deck and SteamOS integration through Decky Loader',
        'Standalone MAKO Renderer for desktop Linux',
      ],
      mainEntityOfPage: { '@id': `${siteUrl}#website` },
    },
    {
      '@type': 'FAQPage',
      '@id': `${siteUrl}#faq`,
      mainEntity: [
        {
          '@type': 'Question',
          name: 'Which MAKO version should I install?',
          acceptedAnswer: {
            '@type': 'Answer',
            text: 'Use MAKO Decky for Steam Deck, Steam Machine, and Decky Loader. Use MAKO Renderer for a direct Vulkan-layer installation on desktop Linux.',
          },
        },
        {
          '@type': 'Question',
          name: 'Does MAKO include Lossless Scaling?',
          acceptedAnswer: {
            '@type': 'Answer',
            text: 'No. MAKO is independent and does not distribute Lossless Scaling or its proprietary resources. Frame generation and LS1 scaling require a separate Lossless Scaling purchase. MAKO Scaler and the bundled shaders do not.',
          },
        },
        {
          '@type': 'Question',
          name: 'Can frame generation, scaling, and shaders run together?',
          acceptedAnswer: {
            '@type': 'Answer',
            text: 'Yes. A MAKO profile can combine spatial scaling, fixed or adaptive frame generation, and per-game shader effects.',
          },
        },
        {
          '@type': 'Question',
          name: 'Which operating systems and architectures does MAKO support?',
          acceptedAnswer: {
            '@type': 'Answer',
            text: 'MAKO targets SteamOS and desktop Linux on x86_64 hosts. Published Renderer packages include both 64-bit and 32-bit x86 Vulkan layers.',
          },
        },
      ],
    },
  ],
} as const;

export const structuredDataJson = JSON.stringify(structuredData).replaceAll(
  '<',
  '\\u003c',
);

'use client';

import type { PointerEvent as ReactPointerEvent } from 'react';
import { releaseData } from './release-data';
import { DiscordIcon, GitHubIcon } from './social-icons';

const { deckyVersion, rendererVersion, links } = releaseData;

const newTabProps = { target: '_blank', rel: 'noopener noreferrer' } as const;

function tiltCard(event: ReactPointerEvent<HTMLDivElement>) {
  if (event.pointerType === 'touch' || window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;

  const card = event.currentTarget;
  const bounds = card.getBoundingClientRect();
  const x = Math.min(1, Math.max(0, (event.clientX - bounds.left) / bounds.width));
  const y = Math.min(1, Math.max(0, (event.clientY - bounds.top) / bounds.height));

  card.style.setProperty('--tilt-x', `${((0.5 - y) * 7).toFixed(2)}deg`);
  card.style.setProperty('--tilt-y', `${((x - 0.5) * 8).toFixed(2)}deg`);
  card.style.setProperty('--shine-x', `${(x * 100).toFixed(1)}%`);
  card.style.setProperty('--shine-y', `${(y * 100).toFixed(1)}%`);
}

function resetCardTilt(event: ReactPointerEvent<HTMLDivElement>) {
  const card = event.currentTarget;
  card.style.setProperty('--tilt-x', '0deg');
  card.style.setProperty('--tilt-y', '0deg');
  card.style.setProperty('--shine-x', '50%');
  card.style.setProperty('--shine-y', '50%');
}

const features = [
  {
    index: '01',
    code: 'AF',
    title: 'Adaptive Frame Generation',
    text: 'Target 30–240 FPS while MAKO varies generated frames up to your selected 2×–4× ceiling.',
    stat: '30—240',
    label: 'FPS target',
    tone: 'cyan',
  },
  {
    index: '02',
    code: 'IQ',
    title: 'Full-quality synthesis',
    text: 'Run the full-quality v2 model with fine-grained performance controls and significantly reduced ghosting.',
    stat: 'V2',
    label: 'quality model',
    tone: 'violet',
  },
  {
    index: '03',
    code: 'PF',
    title: 'Per-game profiles',
    text: 'Capture a game once. MAKO selects its isolated renderer and compatibility settings automatically.',
    stat: '1:1',
    label: 'game identity',
    tone: 'orange',
  },
  {
    index: '04',
    code: 'RC',
    title: 'Gamescope recovery',
    text: 'Bounded recovery preserves native presentation, then resumes generation only after cadence stabilizes.',
    stat: 'NATIVE',
    label: 'fallback path',
    tone: 'green',
  },
  {
    index: '05',
    code: 'FX',
    title: 'Flatpak ready',
    text: 'Prepare Heroic and EmuDeck Flatpaks through the same private configuration and renderer pipeline.',
    stat: '23—25',
    label: 'runtime matrix',
    tone: 'blue',
  },
  {
    index: '06',
    code: 'VK',
    title: 'Dual-architecture Vulkan',
    text: 'Architecture-matched 64-bit and 32-bit x86 layers let the Vulkan loader select the correct binary.',
    stat: '32+64',
    label: 'bit layers',
    tone: 'pink',
  },
];

export default function Home() {
  return (
    <main>
      <a className="skip-link" href="#content">Skip to content</a>

      <nav className="site-nav" aria-label="Main navigation">
        <a className="brand" href="#top" aria-label="MAKO home">
          <img src="assets/mako-discord-icon.webp" alt="" width="44" height="44" draggable={false} />
          <span>MAKO</span>
        </a>
        <div className="nav-links">
          <a href="#system">System</a>
          <a href="#features">Features</a>
          <a href={links.docs} {...newTabProps}>Installation Guide</a>
          <a href="#downloads">Downloads</a>
          <a className="social-link" href={links.repository} {...newTabProps}><GitHubIcon /><span>GitHub</span></a>
          <a className="social-link" href={links.discord} {...newTabProps}><DiscordIcon /><span>Discord</span></a>
        </div>
        <a className="nav-download" href={links.deckyDownload} {...newTabProps}>
          Download <span>v{deckyVersion}</span>
        </a>
        <details className="mobile-nav">
          <summary>Menu <span aria-hidden="true">+</span></summary>
          <div className="mobile-nav-panel">
            <a href="#system">System</a>
            <a href="#features">Features</a>
            <a href={links.docs} {...newTabProps}>Installation Guide</a>
            <a href="#downloads">Downloads</a>
            <a className="social-link" href={links.repository} {...newTabProps}><GitHubIcon /><span>GitHub</span></a>
            <a className="social-link" href={links.discord} {...newTabProps}><DiscordIcon /><span>Discord</span></a>
          </div>
        </details>
      </nav>

      <div id="content">
        <section className="hero" id="top">
          <div className="hero-glow hero-glow-cyan" />
          <div className="hero-glow hero-glow-orange" />
          <div className="hero-copy">
            <p className="eyebrow"><span /> Lossless Scaling frame generation / Linux</p>
            <h1>Lossless Scaling.<br /><em>On Linux.</em></h1>
            <p className="hero-intro">MAKO brings Lossless Scaling frame generation to Steam Deck, Steam Machine, SteamOS, and Linux through a focused Decky control layer and a purpose-built Vulkan renderer.</p>
            <div className="hero-actions">
              <a className="button button-primary" href={links.deckyDownload} {...newTabProps}><span className="download-glyph" aria-hidden="true"><i /></span><span>Get MAKO Decky</span></a>
              <a className="button button-primary button-renderer" href={links.rendererDownload} {...newTabProps}><span className="download-glyph" aria-hidden="true"><i /></span><span>Get MAKO Renderer</span></a>
              <a className="button button-secondary" href={links.docs} {...newTabProps}>Installation Guide <span aria-hidden="true">→</span></a>
              <a className="button button-secondary" href="#system">Explore the system <span aria-hidden="true">↓</span></a>
            </div>
            <div className="hero-meta">
              <span><b>2—4×</b> generation ceiling</span>
              <span><b>30—240</b> adaptive FPS target</span>
              <span><b>32 + 64</b> bit Vulkan layers</span>
            </div>
          </div>

          <div className="hero-visual" aria-label="MAKO Decky and Renderer system visualization">
            <div className="orbit orbit-one" />
            <div className="orbit orbit-two" />
            <div className="visual-card visual-card-decky" onPointerMove={tiltCard} onPointerLeave={resetCardTilt}>
              <span className="card-index">01 / CONTROL</span>
              <img src="assets/mako-logo.webp" alt="MAKO Decky shark logo" width="170" height="170" draggable={false} />
              <div><strong>MAKO Decky</strong><span>Per-game orchestration</span></div>
            </div>
            <div className="visual-card visual-card-renderer" onPointerMove={tiltCard} onPointerLeave={resetCardTilt}>
              <span className="card-index">02 / PIPELINE</span>
              <img src="assets/mako-render-logo.webp" alt="MAKO Renderer shark logo" width="154" height="154" draggable={false} />
              <div><strong>MAKO Renderer</strong><span>Vulkan frame synthesis</span></div>
            </div>
          </div>
        </section>

        <div className="signal-strip" aria-hidden="true">
          <div>STEAM DECK</div><span />
          <div>STEAM MACHINE</div><span />
          <div>STEAMOS</div><span />
          <div>LINUX</div><span />
          <div>VULKAN</div><span />
          <div>GAMESCOPE</div><span />
          <div>FLATPAK</div>
        </div>

        <section className="system-section section-shell" id="system">
          <header className="section-heading">
            <p className="section-kicker"><span>01</span> One system. Two precision layers.</p>
            <div>
              <h2>Controls above.<br /><em>Vulkan below.</em></h2>
              <p>MAKO Decky manages the game. MAKO Renderer runs the frame pipeline. Together, they bring Lossless Scaling frame generation into Linux gaming.</p>
            </div>
          </header>

          <div className="system-grid">
            <article className="system-panel decky-panel">
              <div className="panel-number">01</div>
              <div className="panel-topline"><span>CONTROL LAYER</span><i>STEAMOS / DECKY</i></div>
              <img src="assets/mako-logo.webp" alt="MAKO Decky" width="230" height="230" draggable={false} />
              <h3>MAKO Decky</h3>
              <p>Per-game controls, profiles, installation, Flatpak preparation, and launch integration—from one focused interface.</p>
              <ul>
                <li><span /> Per-game frame-generation switch</li>
                <li><span /> Steam, Heroic &amp; EmuDeck profiles</li>
                <li><span /> Fixed &amp; Adaptive modes</li>
              </ul>
              <a href={links.deckyDownload} {...newTabProps}>Download Decky ZIP <span>→</span></a>
            </article>

            <div className="pipeline" aria-label="Configuration flows from MAKO Decky to MAKO Renderer">
              <div className="pipeline-label">REAL-TIME CONFIG</div>
              <div className="pipeline-line"><span /><span /><span /><span /><span /><span /></div>
              <div className="pipeline-chip">MAKO<br />SYNC</div>
              <div className="pipeline-line reverse"><span /><span /><span /><span /><span /><span /></div>
              <div className="pipeline-label">FRAME TELEMETRY</div>
            </div>

            <article className="system-panel renderer-panel">
              <div className="panel-number">02</div>
              <div className="panel-topline"><span>RENDER LAYER</span><i>VULKAN / LINUX</i></div>
              <img src="assets/mako-render-logo.webp" alt="MAKO Renderer" width="230" height="230" draggable={false} />
              <h3>MAKO Renderer</h3>
              <p>A private Vulkan pipeline for frame synthesis, deterministic scheduling, presentation recovery, and future scaling.</p>
              <ul>
                <li><span /> Lossless Scaling frame-generation models</li>
                <li><span /> Adaptive generated-frame planning</li>
                <li><span /> 32-bit &amp; 64-bit layers</li>
              </ul>
              <a href={links.rendererDownload} {...newTabProps}>Download Linux archive <span>→</span></a>
            </article>
          </div>
        </section>

        <section className="features-section" id="features">
          <div className="section-shell">
            <header className="section-heading features-heading">
              <p className="section-kicker"><span>02</span> Engineered for play</p>
              <div>
                <h2>Every frame.<br /><em>Under control.</em></h2>
                <p>MAKO’s scheduler, profiles, and launch boundaries are designed to stay fast, predictable, and out of your way.</p>
              </div>
            </header>

            <div className="feature-grid">
              {features.map((feature) => (
                <article className={`feature-card feature-${feature.tone}`} key={feature.index}>
                  <div className="feature-top"><span>{feature.index}</span><i>{feature.code}</i></div>
                  <h3>{feature.title}</h3>
                  <p>{feature.text}</p>
                  <div className="feature-stat"><strong>{feature.stat}</strong><span>{feature.label}</span></div>
                </article>
              ))}
            </div>
          </div>
        </section>

        <section className="adaptive-section section-shell">
          <div className="adaptive-copy">
            <p className="section-kicker"><span>03</span> Adaptive intelligence</p>
            <h2>It doesn’t just add frames.<br /><em>It reads the rhythm.</em></h2>
            <p>Choose a target and ceiling. MAKO Renderer observes each game interval, plans only the generated frames needed to reach the output rhythm, and backs off cleanly when delivery becomes unstable.</p>
            <div className="adaptive-points">
              <div><span>01</span><p><strong>Observe</strong> game cadence and delivery health</p></div>
              <div><span>02</span><p><strong>Plan</strong> the right generated-frame count</p></div>
              <div><span>03</span><p><strong>Recover</strong> without sacrificing native presentation</p></div>
            </div>
          </div>

          <div className="telemetry-card" aria-label="Adaptive frame generation example: changing native cadence with generated-frame segments filling the output to a 90 FPS target.">
            <div className="telemetry-head"><span>ADAPTIVE TELEMETRY</span></div>
            <div className="telemetry-readout">
              <div><span>TARGET</span><strong>90<small> FPS</small></strong></div>
              <div><span>CEILING</span><strong>3<small> ×</small></strong></div>
              <div><span>STATUS</span><strong className="healthy">STABLE</strong></div>
            </div>
            <div className="chart-grid">
              <div className="chart-y"><span>120</span><span>90</span><span>60</span><span>0</span></div>
              <div className="chart-area">
                <div className="chart-key" aria-hidden="true"><span><i className="native-key" />NATIVE CADENCE</span><span><i className="generated-key" />GENERATED FRAMES</span></div>
                <div className="target-line"><span>90 FPS OUTPUT</span></div>
                <div className="bars" aria-hidden="true">
                  {[31, 40, 34, 47, 38, 45, 32, 49, 42, 36, 46, 39, 50, 33, 44, 37, 48, 41].map((nativeCadence, index) => (
                    <i className="cadence-bar" key={index}>
                      <span className="native-bar" style={{ height: `${nativeCadence}%` }} />
                      <span className="generated-bar" style={{ height: `${75 - nativeCadence}%` }} />
                    </i>
                  ))}
                </div>
              </div>
            </div>
            <div className="telemetry-foot"><span>INPUT CADENCE 37—60 FPS</span><span>OUTPUT RHYTHM 90 FPS</span></div>
          </div>
        </section>

        <section className="start-section">
          <div className="section-shell start-inner">
            <header className="start-heading">
              <p className="section-kicker"><span>04</span> From zero to smoother</p>
              <h2>Three steps.<br /><em>Then dive in.</em></h2>
            </header>
            <ol className="steps">
              <li>
                <span>01</span>
                <div><strong>Bring the model</strong><p>Install Lossless Scaling through Steam. MAKO reads your licensed <code>Lossless.dll</code>; it never bundles or copies it.</p></div>
              </li>
              <li>
                <span>02</span>
                <div><strong>Install MAKO</strong><p>Use MAKO Decky on SteamOS, or extract MAKO Renderer directly on desktop Linux.</p></div>
              </li>
              <li>
                <span>03</span>
                <div><strong>Launch your game</strong><p>Start with Fixed 2×, then tune one setting at a time or move to an Adaptive FPS target.</p></div>
              </li>
            </ol>
            <a className="text-link" href={links.docs} {...newTabProps}>Read the installation guide <span>→</span></a>
          </div>
        </section>

        <section className="downloads-section section-shell" id="downloads">
          <header className="downloads-heading">
            <p className="section-kicker"><span>05</span> Latest release</p>
            <h2>Choose your<br /><em>entry point.</em></h2>
            <p>Both components are open source and independently versioned. Current packages are MAKO Decky v{deckyVersion} and MAKO Renderer v{rendererVersion}.</p>
          </header>

          <div className="download-grid">
            <article className="download-card download-decky">
              <div className="download-badge">RECOMMENDED FOR STEAMOS</div>
              <div className="download-icon"><img src="assets/mako-logo.webp" alt="" width="88" height="88" draggable={false} /></div>
              <p className="download-type">CONTROL + BUNDLED RENDERER</p>
              <h3>MAKO Decky</h3>
              <p className="download-copy">The complete managed experience for Steam Deck, Steam Machine, Decky Loader, Heroic, and EmuDeck.</p>
              <dl>
                <div><dt>VERSION</dt><dd>{deckyVersion}</dd></div>
                <div><dt>FORMAT</dt><dd>ZIP</dd></div>
                <div><dt>INSTALL</dt><dd>DECKY</dd></div>
              </dl>
              <a className="download-button" href={links.deckyDownload} {...newTabProps}><span>Download Decky ZIP</span><i>→</i></a>
              <a className="release-link" href={links.deckyRelease} {...newTabProps}>View release notes <span>→</span></a>
            </article>

            <article className="download-card download-renderer">
              <div className="download-badge">DIRECT LINUX INSTALL</div>
              <div className="download-icon"><img src="assets/mako-render-logo.webp" alt="" width="88" height="88" draggable={false} /></div>
              <p className="download-type">STANDALONE VULKAN LAYER</p>
              <h3>MAKO Renderer</h3>
              <p className="download-copy">The direct host archive for desktop Linux, with the UI, launcher, CLI, and both x86 Vulkan layers.</p>
              <dl>
                <div><dt>VERSION</dt><dd>{rendererVersion}</dd></div>
                <div><dt>FORMAT</dt><dd>TAR.XZ</dd></div>
                <div><dt>ARCH</dt><dd>x86_64</dd></div>
              </dl>
              <a className="download-button" href={links.rendererDownload} {...newTabProps}><span>Download Renderer</span><i>→</i></a>
              <div className="release-links">
                <a className="release-link" href={links.flatpakDownload} {...newTabProps}>Flatpak bundle <span>→</span></a>
                <a className="release-link" href={links.rendererRelease} {...newTabProps}>Release notes <span>→</span></a>
              </div>
            </article>
          </div>

          <div className="requirement-note">
            <span>IMPORTANT / LICENSED INPUT</span>
            <p>MAKO is an independent project, not an official Lossless Scaling release. It requires <code>Lossless.dll</code> from a licensed <a href={links.losslessScaling} {...newTabProps}>Lossless Scaling</a> installation and does not bundle, copy, or modify that proprietary library.</p>
          </div>
        </section>

        <section className="faq-section section-shell">
          <header>
            <p className="section-kicker"><span>06</span> Signal check</p>
            <h2>Before you<br /><em>make the jump.</em></h2>
          </header>
          <div className="faq-list">
            <details>
              <summary><span>01</span> Which version should I install?<i>+</i></summary>
              <p>Use MAKO Decky for Steam Deck, Steam Machine, and Decky Loader. Use the standalone MAKO Renderer archive when you want direct Vulkan-layer installation on desktop Linux.</p>
            </details>
            <details>
              <summary><span>02</span> Does MAKO include Lossless Scaling?<i>+</i></summary>
              <p>No. MAKO is an independent project. You need your own licensed Lossless Scaling installation from Steam, then MAKO discovers or lets you select its <code>Lossless.dll</code>.</p>
            </details>
            <details>
              <summary><span>03</span> Is scaling available too?<i>+</i></summary>
              <p>Frame generation is available today. Scaling is the next planned capability and is not presented as a finished feature in the current release.</p>
            </details>
            <details>
              <summary><span>04</span> What hardware is published today?<i>+</i></summary>
              <p>MAKO Renderer v{rendererVersion} targets x86_64 Linux hosts and includes 64-bit and 32-bit x86 Vulkan layers. Native AArch64 packages are not included in this release.</p>
            </details>
          </div>
        </section>

        <section className="final-cta section-shell">
          <div className="cta-glow" />
          <img src="assets/mako-logo.webp" alt="MAKO shark" width="260" height="260" draggable={false} />
          <div>
            <p className="eyebrow"><span /> Motion-Adaptive Kernel Orchestration</p>
            <h2>Bring Lossless Scaling<br /><em>to Linux.</em></h2>
            <p>Choose MAKO Decky for the managed SteamOS workflow or MAKO Renderer for a direct Linux installation.</p>
            <div className="hero-actions">
              <a className="button button-primary" href={links.deckyDownload} {...newTabProps}>Download MAKO <span>→</span></a>
              <a className="button button-secondary social-button" href={links.repository} {...newTabProps}><span className="social-button-label"><GitHubIcon />View on GitHub</span><span aria-hidden="true">→</span></a>
              <a className="button button-secondary social-button" href={links.discord} {...newTabProps}><span className="social-button-label"><DiscordIcon />Join Discord</span><span aria-hidden="true">→</span></a>
            </div>
          </div>
        </section>
      </div>

      <footer className="site-footer">
        <div className="footer-brand">
          <a className="brand" href="#top"><img src="assets/mako-discord-icon.webp" alt="" width="42" height="42" draggable={false} /><span>MAKO</span></a>
          <p>Lossless Scaling frame generation for Steam Deck, Steam Machine, SteamOS, and Linux.</p>
        </div>
        <div className="footer-links">
          <div><span>PROJECT</span><a className="social-link" href={links.repository} {...newTabProps}><GitHubIcon /><span>GitHub</span></a><a className="social-link" href={links.discord} {...newTabProps}><DiscordIcon /><span>Discord</span></a><a href={links.docs} {...newTabProps}>Installation Guide</a><a href={links.issues} {...newTabProps}>Issues</a></div>
          <div><span>DOWNLOAD</span><a href={links.deckyRelease} {...newTabProps}>MAKO Decky</a><a href={links.rendererRelease} {...newTabProps}>MAKO Renderer</a><a href={links.allReleases} {...newTabProps}>All releases</a></div>
        </div>
        <div className="footer-bottom"><span>GPL-3.0-OR-LATER</span><span>INDEPENDENT COMMUNITY PROJECT</span><span>© 2026 MAKO</span></div>
      </footer>
    </main>
  );
}

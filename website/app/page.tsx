'use client';

import { SharkHero } from './shark-display';
import { releaseData } from './release-data';
import { DiscordIcon, GitHubIcon } from './social-icons';

const { deckyVersion, rendererVersion, links } = releaseData;

const newTabProps = { target: '_blank', rel: 'noopener noreferrer' } as const;

function DonationPanel() {
  return (
    <div className="donate-panel">
      <p><strong>Support MAKO</strong>Help fund development, GPU compute and real-hardware testing.</p>
      <a href="https://ko-fi.com/eugeniosegala" {...newTabProps}>Ko-fi <span aria-hidden="true">↗</span></a>
      <a href="https://github.com/sponsors/eugeniosegala" {...newTabProps}>GitHub Sponsors <span aria-hidden="true">↗</span></a>
    </div>
  );
}

function DonateMenu() {
  return (
    <details className="donate-menu" onKeyDown={(event) => {
      if (event.key === 'Escape') {
        event.currentTarget.open = false;
        event.currentTarget.querySelector('summary')?.focus();
      }
    }}>
      <summary>Donate <span className="donate-chevron" aria-hidden="true" /></summary>
      <DonationPanel />
    </details>
  );
}

const features = [
  {
    index: '01',
    code: 'AF',
    title: 'Adaptive Frame Generation',
    text: 'Target 30–240 FPS while MAKO varies generated frames up to your selected 2×–5× ceiling.',
    stat: '30-240',
    label: 'FPS target',
    tone: 'cyan',
  },
  {
    index: '02',
    code: 'SR',
    title: 'MAKO + LS1 spatial scaling',
    text: 'Use the built-in open MAKO Scaler by default, or optional LS1 Quality or Performance, before Fixed or Adaptive generation.',
    stat: '1-2×',
    label: 'scale factor',
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
    code: 'SH',
    title: 'Per-game shaders',
    text: 'Sharpen, smooth, or stylize each game with lightweight controls that update live while you play.',
    stat: 'LIVE',
    label: 'effect tuning',
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

      <nav id="top" className="site-nav" aria-label="Main navigation">
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
          <DonateMenu />
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
            <section className="mobile-donate" aria-label="Donate">
              <DonationPanel />
            </section>
          </div>
        </details>
      </nav>

      <div id="content">
        <SharkHero>
          <header className="hero-title">
            <p className="section-kicker"><span>MAKO</span> Steam Deck, Steam Machine and Linux</p>
            <h1>Frames. Scaling. <br /><em>Shaders.</em></h1>
          </header>
          <div className="hero-copy">
            <p className="hero-intro">MAKO is an independent Linux project, unaffiliated with Lossless Scaling. MAKO FG, MAKO Scaler and bundled shaders work without it. Only optional LSFG and LS1 require a separate purchase.</p>
            <a className="open-source-sticker" href={`${links.repository}/blob/main/LICENSE.md`} {...newTabProps} aria-label="Fully open source. Read the GPL-3.0-or-later licence.">
              <span className="open-source-sticker-mark" aria-hidden="true">OPEN</span>
              <span className="open-source-sticker-copy"><strong>Fully open source</strong><span>Contribute. Fork it. Change it. Make it yours.</span></span>
              <span className="open-source-sticker-arrow" aria-hidden="true">↗</span>
            </a>
            <div className="hero-downloads">
              <a href={links.deckyDownload} {...newTabProps}>
                <span className="hero-download-copy">Get MAKO Decky<small>Includes MAKO Renderer</small></span>
                <span className="download-glyph" aria-hidden="true"><i /></span>
              </a>
              <a className="renderer-download" href={links.rendererDownload} {...newTabProps}>
                <span className="hero-download-copy">Get MAKO Renderer<small>Standalone for Linux</small></span>
                <span className="download-glyph" aria-hidden="true"><i /></span>
              </a>
            </div>
            <nav className="hero-links" aria-label="MAKO resources">
              <a className="hero-model" href={links.makoFg} {...newTabProps}>
                <img src="assets/hugging-face.svg" width="20" height="20" alt="" />
                <span><strong>MAKO FG</strong> on Hugging Face</span>
              </a>
              <a href={links.docs} {...newTabProps}>Install guide</a>
            </nav>
          </div>
        </SharkHero>

        <dl className="hero-meta section-shell">
          <div><dt>Spatial scaling</dt><dd>1–2×</dd></div>
          <div><dt>Adaptive FPS target</dt><dd>30–240</dd></div>
          <div><dt>Generation ceiling</dt><dd>2–5×</dd></div>
          <div><dt>Per-game shaders</dt><dd>Live</dd></div>
        </dl>

        <section className="system-section section-shell" id="system">
          <header className="section-heading">
            <p className="section-kicker"><span>01</span> Components</p>
            <div>
              <h2>How MAKO <em>works</em></h2>
              <p>MAKO Decky lets you configure frame generation, scaling, and shaders for each game. MAKO Renderer applies those settings in Vulkan and can also run on its own.</p>
            </div>
          </header>

          <div className="system-grid">
            <article className="system-panel decky-panel">
              <div className="panel-number">01</div>
              <div className="panel-topline"><span>CONTROL LAYER</span><i>STEAMOS / DECKY</i></div>
              <img src="assets/mako-logo.webp" alt="MAKO Decky" width="230" height="230" draggable={false} />
              <h3>MAKO Decky</h3>
              <p>Per-game controls, profiles, installation, Flatpak preparation, and launch integration through one focused interface.</p>
              <ul>
                <li><span /> Scaling, generation &amp; shader controls</li>
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
              <p>A private Vulkan pipeline for spatial reconstruction, frame synthesis, deterministic scheduling, and presentation recovery, with optional bundled shader effects.</p>
              <ul>
                <li><span /> Built-in open MAKO Scaler or optional LS1</li>
                <li><span /> MAKO FG or optional Lossless Scaling models</li>
                <li><span /> Adaptive generated-frame planning</li>
              </ul>
              <a href={links.rendererDownload} {...newTabProps}>Download Linux archive <span>→</span></a>
            </article>
          </div>
        </section>

        <section className="features-section" id="features">
          <div className="section-shell">
            <header className="section-heading features-heading">
              <p className="section-kicker"><span>02</span> SteamOS and desktop Linux</p>
              <div>
                <h2>MAKO <br /><em>features</em></h2>
                <p>Set a target frame rate, choose a scaler, and save shader settings for each game. MAKO Renderer supports both 32-bit and 64-bit x86 Vulkan games.</p>
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

        <div className="generation-scaling-stack section-shell">
          <div className="generation-scaling-divider" aria-hidden="true"><span>FRAME GENERATION</span><i /><span>SPATIAL SCALING</span><i /><span>SHADERS</span></div>

        <section className="adaptive-section processing-section">
          <div className="adaptive-copy">
            <p className="section-kicker"><span>03</span> MAKO Renderer</p>
            <h2>Adaptive <br /><em>frame generation</em></h2>
            <p>Set a target frame rate and a maximum multiplier. MAKO Renderer adjusts how many frames it generates as the game’s frame rate changes. A 3× ceiling allows up to two generated frames per game frame.</p>
            <div className="adaptive-points">
              <div><span>01</span><p><strong>Measure</strong> the time between game frames</p></div>
              <div><span>02</span><p><strong>Schedule</strong> generated frames to meet the target</p></div>
              <div><span>03</span><p><strong>Reduce</strong> generation if frame delivery becomes unstable</p></div>
            </div>
          </div>

          <div className="telemetry-card processing-visual" aria-label="Adaptive frame generation example: changing native cadence with generated-frame segments filling the output to a 90 FPS target.">
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
            <div className="telemetry-foot"><span>INPUT 37-60 FPS</span><span>OUTPUT 90 FPS</span></div>
          </div>
        </section>

        <div className="processing-divider" aria-hidden="true" />

        <section className="scaling-section processing-section" id="scaling">
          <div className="scaling-copy">
            <p className="section-kicker"><span>04</span> MAKO Renderer</p>
            <h2>Spatial <br /><em>scaling</em></h2>
            <p>Your game can render at 720p while MAKO Renderer upscales each frame to 1080p for display. You can use scaling on its own or before frame generation.</p>
            <div className="scaling-modes">
              <div><span>NATIVE</span><strong>Fast linear baseline</strong></div>
              <div className="scaling-mode-active"><span>MAKO</span><strong>Open single-pass scaler</strong></div>
              <div><span>LS1</span><strong>Licensed quality or performance</strong></div>
            </div>
          </div>

          <div className="scaling-visual processing-visual" aria-label="Spatial scaling visual showing a 1280 by 720 render target reconstructed for a 1920 by 1080 display target.">
            <div className="scaling-visual-head"><span>SPATIAL RECONSTRUCTION</span><span>INPUT → DISPLAY</span></div>
            <div className="resolution-flow" aria-hidden="true">
              <div className="resolution-surface resolution-source">
                <span>RENDER TARGET</span><strong>1280 × 720</strong>
                <div className="resolution-preview resolution-preview-source"><img src="assets/mako-logo.webp" alt="" width="256" height="256" draggable={false} /></div>
                <small>COARSE INPUT SAMPLE</small>
              </div>
              <div className="scaling-transfer"><span>1.50×</span><i>→</i><small>RECONSTRUCT</small></div>
              <div className="resolution-surface resolution-output">
                <span>DISPLAY TARGET</span><strong>1920 × 1080</strong>
                <div className="resolution-preview resolution-preview-output"><img src="assets/mako-logo.webp" alt="" width="256" height="256" draggable={false} /></div>
                <small>RECONSTRUCTED DETAIL</small>
              </div>
            </div>
            <div className="scaling-status"><span><i /> MAKO Scaler READY</span><span>1.50× SHOWN <b>·</b> UP TO 2.00×</span></div>
          </div>
        </section>

        <div className="processing-divider" aria-hidden="true" />

        <section className="shader-section processing-section" id="shaders">
          <div className="shader-copy">
            <p className="section-kicker"><span>05</span> MAKO Renderer</p>
            <h2>Per-game <br /><em>shaders</em></h2>
            <p>Choose sharpening, anti-aliasing, and colour settings for each game. You can adjust supported effects while the game is running.</p>
            <div className="shader-modes">
              <div><span>DETAIL</span><strong>CAS or DLS sharpening</strong></div>
              <div><span>EDGES</span><strong>FXAA or SMAA smoothing</strong></div>
              <div className="shader-mode-active"><span>STYLE</span><strong>Seven finishing presets</strong></div>
            </div>
          </div>

          <div className="shader-visual processing-visual" aria-label="Per-game shader preview with live sharpening, anti-aliasing, and colour controls.">
            <div className="shader-visual-head"><span>SHADER PIPELINE</span><span>PER-GAME · LIVE</span></div>
            <div className="shader-stage">
              <div className="shader-preview" aria-hidden="true">
                <span className="shader-before-label">BEFORE</span>
                <span className="shader-after-label">SHADER</span>
                <img className="shader-before-image" src="assets/mako-logo.webp" alt="" width="256" height="256" draggable={false} />
                <img className="shader-after-image" src="assets/mako-logo.webp" alt="" width="256" height="256" draggable={false} />
                <i className="shader-split"><b>↔</b></i>
                <small>MOVE FROM NEUTRAL TO CRISP, SMOOTH, OR STYLIZED</small>
              </div>
              <div className="shader-controls" aria-hidden="true">
                <div><span>SHARPENING</span><strong>DLS</strong><i><b style={{ width: '64%' }} /></i></div>
                <div><span>ANTI-ALIASING</span><strong>SMAA</strong><i><b style={{ width: '48%' }} /></i></div>
                <div><span>COLOUR</span><strong>VIBRANCE</strong><i><b style={{ width: '72%' }} /></i></div>
              </div>
            </div>
            <div className="shader-status"><span><i /> EFFECTS ACTIVE</span><span>LIVE TUNING</span></div>
          </div>
        </section>
        </div>

        <section className="start-section">
          <div className="section-shell start-inner">
            <header className="start-heading">
              <p className="section-kicker"><span>06</span> SteamOS and desktop Linux</p>
              <h2>Installation</h2>
            </header>
            <ol className="steps">
              <li>
                <span>01</span>
                <div><strong>Choose your pipeline</strong><p>MAKO FG, the open MAKO Scaler, and bundled shaders work without Lossless Scaling. For the optional Lossless Scaling frame generation model (LSFG) or LS1 scaling, purchase and install Lossless Scaling separately through Steam so MAKO can read your licensed <code>Lossless.dll</code>.</p></div>
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
            <p className="section-kicker"><span>07</span> Latest release</p>
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
              <p className="download-copy">The direct host archive for desktop Linux, with the UI, launcher, CLI, both x86 Vulkan layers, and bundled shader effects.</p>
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
            <span>OPTIONAL / LICENSED INPUT</span>
            <p>MAKO is independent and unaffiliated with Lossless Scaling. MAKO FG, the open MAKO Scaler, and bundled shaders do not need it. Only the optional Lossless Scaling frame generation model (LSFG) and LS1 scaling require your own separately purchased <a href={links.losslessScaling} {...newTabProps}>Lossless Scaling</a> installation. MAKO does not distribute Lossless Scaling or its proprietary resources, alter your DLL, or save extracted resources as standalone files. Your copy’s terms apply. See <a href={`${links.repository}/blob/main/THIRD_PARTY_NOTICES.md`} {...newTabProps}>third-party notices</a>.</p>
          </div>
        </section>

        <section className="faq-section section-shell" id="faq">
          <header>
            <p className="section-kicker"><span>08</span> Setup and compatibility</p>
            <h2>Common <br /><em>questions</em></h2>
          </header>
          <div className="faq-list">
            <details>
              <summary><span>01</span> Which version should I install?<i>+</i></summary>
              <p>Use MAKO Decky for Steam Deck, Steam Machine, and Decky Loader. Use the standalone MAKO Renderer archive when you want direct Vulkan-layer installation on desktop Linux.</p>
            </details>
            <details>
              <summary><span>02</span> Does MAKO include Lossless Scaling?<i>+</i></summary>
              <p>No. MAKO does not contain or distribute Lossless Scaling, <code>Lossless.dll</code>, or extracted proprietary model payloads. MAKO FG, the open MAKO Scaler, and bundled shaders work without it. Only the optional Lossless Scaling frame generation model (LSFG) and LS1 scaling require a separate Lossless Scaling purchase and installation from Steam, then MAKO discovers or lets you select its local DLL.</p>
            </details>
            <details>
              <summary><span>03</span> Can frame generation, scaling, and shaders run together?<i>+</i></summary>
              <p>Yes. Scale with the open MAKO Scaler or optional LS1, feed reconstructed real frames into Fixed or Adaptive frame generation, and apply per-game shader effects in the same profile. The downloads above track MAKO Decky v{deckyVersion} and MAKO Renderer v{rendererVersion} from canonical release metadata.</p>
            </details>
            <details>
              <summary><span>04</span> Does MAKO Renderer support Arch Linux?<i>+</i></summary>
              <p>Yes. On x86_64 Arch Linux, enable <code>multilib</code> and use pacman to install <code>mako-renderer-bin</code> from the <a className="underline" href={links.rendererRelease} {...newTabProps}>MAKO Renderer v{rendererVersion} release</a>. It includes Vulkan layers for 64-bit and 32-bit x86 games. Native ARM64 (AArch64) packages are unavailable for this release.</p>
            </details>
          </div>
        </section>

        <section className="final-cta section-shell">
          <div className="cta-glow" />
          <img src="assets/mako-logo.webp" alt="MAKO shark" width="260" height="260" draggable={false} />
          <div>
            <p className="eyebrow"><span /> Motion-Adaptive Kernel Orchestration</p>
            <h2>Play smoother.<br /><em>Look sharper.</em></h2>
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
          <p>Frame generation, spatial scaling, and bundled shaders for Steam Deck, Steam Machine, SteamOS, and Linux.</p>
        </div>
        <div className="footer-links">
          <div><span>PROJECT</span><a className="social-link" href={links.repository} {...newTabProps}><GitHubIcon /><span>GitHub</span></a><a className="social-link" href={links.discord} {...newTabProps}><DiscordIcon /><span>Discord</span></a><a href={links.docs} {...newTabProps}>Installation Guide</a><a href="third-party-notices.txt">Third-party notices</a><a href={links.issues} {...newTabProps}>Issues</a></div>
          <div><span>DOWNLOAD</span><a href={links.deckyRelease} {...newTabProps}>MAKO Decky</a><a href={links.rendererRelease} {...newTabProps}>MAKO Renderer</a><a href={links.allReleases} {...newTabProps}>All releases</a></div>
        </div>
        <div className="footer-bottom"><span>GPL-3.0-OR-LATER</span><span>INDEPENDENT COMMUNITY PROJECT</span><span>© 2026 MAKO</span></div>
      </footer>
    </main>
  );
}

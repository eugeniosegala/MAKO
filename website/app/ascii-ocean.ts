import { OceanWake } from './ocean-wake';

const tau = 2 * Math.PI;
const glyphs = ".,_-/\\~'`";
const reflections = ['--cyan', undefined, '--orange', '--cyan', undefined, '--magenta'] as const;
type Mark = { column: number; row: number; glyph: string };
type CachedBand = { depth: number; y: number; origin: number; top: number; height: number };

// Project successive wave fronts closer together toward the horizon. Each
// front has a long swell, crossing wind chop and a broken edge of foam.
function oceanBands(width: number, height: number) {
  const count = Math.round(Math.min(32, Math.max(22, height / 25)));
  return Array.from({ length: count }, (_, index) => {
    const depth = (index + .4) / count;
    const distance = depth ** 1.8;
    const y = height * distance * 1.10;
    const pitch = Math.max(4.8, Math.min(8, width / 85)) * (.63 + .65 * depth);
    const columns = Math.min(320, Math.ceil(width / pitch));
    const cellWidth = width / columns;
    const cellHeight = cellWidth * 1.38;
    const amplitude = Math.min(height * .064, width * .075) * (.025 + distance);
    const marks: Mark[] = [];
    const frequency = Math.max(1, Math.round(width / (height * (.12 + .86 * depth))));
    const seed = index * 2.39996;

    let previous = 0;
    for (let col = -1; col < columns; col++) {
      const x = tau * col / columns;
      const phase = x * frequency + seed + .85 * Math.sin(x - seed);
      const long = Math.sin(phase + .42 * Math.sin(phase));
      const cross = Math.sin(x * (frequency + 2) - seed * 1.7);
      const chop = Math.sin(x * (frequency * 4 + 3) + seed * 2.1 + 1.6 * cross);
      const swell = .64 * long + .26 * cross + .10 * chop;
      const ridge = -amplitude / cellHeight * (.6 * swell + .4 * Math.max(0, swell) ** 2);
      const slope = ridge - previous;
      previous = ridge;
      if (col < 0) continue;
      const light = .55 * long + .28 * cross + .17 * chop;
      const grain = Math.sin(col * 78.233 + index * 37.719) * 43758.5453;
      const fleck = grain - Math.floor(grain);
      if (light > .40) {
        marks.push({ column: col, row: ridge, glyph: fleck > .42 ? '~' : '-' });
        if (light + fleck * .4 > .89) {
          marks.push({ column: col + .32, row: ridge - .25 - .5 * fleck, glyph: fleck > .66 ? "'" : '`' });
        }
      } else if (light > -.20 || fleck > .8) {
        const glyph = Math.abs(slope) > .3 ? (slope < 0 ? '/' : '\\') : (fleck > .45 ? '-' : '_');
        marks.push({ column: col, row: ridge, glyph });
      }
      // Short, broken reflections follow the front face into the trough.
      if (light + fleck * .72 > .19) {
        marks.push({ column: col, row: ridge + .4 + .67 * fleck, glyph: fleck > .61 ? ',' : '.' });
        if (light * fleck > .23) {
          marks.push({ column: col - .23, row: ridge + 1 + (1 + depth) * fleck, glyph: '.' });
        }
      }
    }
    const accent = reflections[index % reflections.length];
    return { depth, y, cellWidth, cellHeight, marks, accent };
  });
}

function stillFrame() {
  const columns = 128;
  const rows = 48;
  const grid = Array.from({ length: rows }, () => Array<string>(columns).fill(' '));
  const accents: (typeof reflections)[number][] = [];
  for (const band of oceanBands(1024, 576)) {
    if (band.accent) accents[Math.floor(band.y * rows / 576)] = band.accent;
    for (const mark of band.marks) {
      const x = Math.floor(mark.column * band.cellWidth * columns / 1024);
      const y = Math.floor((band.y + mark.row * band.cellHeight) * rows / 576);
      if (x >= 0 && x < columns && y >= 0 && y < rows) grid[y][x] = mark.glyph;
    }
  }
  return grid.map((row, i) => ({ text: row.join(''), accent: accents[i] }));
}

export const stillOcean = stillFrame();

function drawStrip(ctx: CanvasRenderingContext2D, atlas: HTMLCanvasElement, sourceX: number, band: CachedBand, span: number, x: number, y: number, height: number) {
  const start = sourceX % atlas.width;
  const first = Math.min(span, atlas.width - start);
  ctx.drawImage(atlas, start, band.top, first, band.height, x, y, first, height);
  if (first < span) ctx.drawImage(atlas, 0, band.top, span - first, band.height, x + first, y, span - first, height);
}

export function animateWater(hero: HTMLElement, canvas: HTMLCanvasElement) {
  const ctx = canvas.getContext('2d');
  const atlas = document.createElement('canvas');
  const ink = atlas.getContext('2d');
  const alphabet = document.createElement('canvas');
  const letters = alphabet.getContext('2d');
  if (!ctx || !ink || !letters) return;

  // Rasterize the alphabet once. All sizes of ocean type use this small sheet.
  const em = 24;
  alphabet.width = glyphs.length * em;
  alphabet.height = em;
  const heroStyle = getComputedStyle(hero);
  letters.font = `20px ${heroStyle.getPropertyValue('--font-mono')}`;
  letters.textBaseline = 'middle';
  letters.textAlign = 'center';
  for (let i = 0; i < glyphs.length; i++) {
    const glyph = glyphs[i];
    letters.fillStyle = ".,".includes(glyph) ? '#4586a3' : "~'`".includes(glyph) ? '#b3dce0' : '#68a9bd';
    letters.fillText(glyph, (i + .5) * em, em / 2);
  }

  const motion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const wake = new OceanWake();
  let density = 1;
  let mouseBounds: DOMRect | undefined;
  let bands: CachedBand[] = [];
  let running = false;
  let time = 0;
  let focusX = .5;
  let focusY = .5;
  let strength = 0;
  let request = 0;
  let last = 0;
  let targetX = focusX;
  let targetY = focusY;
  let targetPull = 0;
  let clientX = 0;
  let clientY = 0;
  let pointerMoved = false;

  const paint = () => {
    const { width, height } = canvas;
    ctx.clearRect(0, 0, width, height);
    for (let i = 0; i < bands.length; i++) {
      const band = bands[i];
      const { depth, y, origin } = band;
      const pulse = Math.sin(time * (.47 + .13 * depth) + i * 1.7);
      const reach = (y / height - focusY) / .13;
      const pull = strength * Math.exp(-reach * reach);
      const roll = 1 + .13 * pulse;
      const lift = height * depth * depth * (.012 * pulse - .034 * pull);
      const drift = time * (.004 + .018 * depth * depth) * (i % 4 === 1 ? -.58 : 1);
      const shift = drift - pull * (focusX - .5) * .19;
      const offset = (shift - Math.floor(shift)) * width;
      const destY = y + lift - origin * roll;
      const h = band.height * roll;
      if (!wake.sampleRow(y + lift)) {
        drawStrip(ctx, atlas, offset, band, width, 0, destY, h);
        continue;
      }
      // Bend just the portion of the cached strip crossed by the mouse.
      const first = Math.max(0, wake.first - 1);
      const last = Math.min(wake.dx.length - 1, wake.last + 1);
      const left = first * wake.step;
      if (left > 0) drawStrip(ctx, atlas, offset, band, left, 0, destY, h);
      for (let col = first; col < last; col++) {
        const x = col * wake.step;
        const span = Math.min(wake.step, width - x);
        const stretch = 1 + (wake.dx[col + 1] - wake.dx[col]) / span;
        const slope = (wake.dy[col + 1] - wake.dy[col]) / span;
        ctx.setTransform(stretch, slope, 0, 1, x + wake.dx[col], destY + wake.dy[col]);
        drawStrip(ctx, atlas, offset + x, band, span, 0, 0, h);
      }
      ctx.resetTransform();
      const right = last * wake.step;
      if (right < width) drawStrip(ctx, atlas, offset + right, band, width - right, right, destY, h);
    }
    wake.drawFoam(ctx, alphabet, em, glyphs.indexOf('~'), glyphs.indexOf("'"));
  };

  const resize = () => {
    density = Math.min(devicePixelRatio, 1.5, 1800 / hero.clientWidth, 1100 / hero.clientHeight);
    const width = Math.max(1, Math.round(hero.clientWidth * density));
    const height = Math.max(1, Math.round(hero.clientHeight * density));
    if (bands.length && width === canvas.width && height === canvas.height) return;
    canvas.width = atlas.width = width;
    canvas.height = height;
    wake.resize(width, density);
    mouseBounds = undefined;
    const fronts = oceanBands(hero.clientWidth, hero.clientHeight);
    let atlasHeight = 0;
    bands = fronts.map(band => {
      const top = atlasHeight;
      // Pack only the occupied rows of each swell into the shared bitmap.
      const line = band.cellHeight * density;
      const halfGlyph = band.cellWidth * density * .95;
      let low = 0;
      let high = 0;
      for (const { row } of band.marks) {
        low = Math.min(low, row * line - halfGlyph);
        high = Math.max(high, row * line + halfGlyph);
      }
      const origin = -Math.floor(low);
      const h = Math.ceil(high) + origin;
      atlasHeight += h + 2;
      return { depth: band.depth, y: band.y * density, top, height: h, origin };
    });
    atlas.height = atlasHeight;
    fronts.forEach((band, i) => {
      const cell = band.cellWidth * density;
      const line = band.cellHeight * density;
      const size = cell * 1.90;
      ink.globalAlpha = .38 + .62 * band.depth;
      for (const { column, row, glyph } of band.marks) {
        const x = column * cell + (cell - size) / 2;
        const y = bands[i].top + bands[i].origin + row * line - size / 2;
        ink.drawImage(alphabet, glyphs.indexOf(glyph) * em, 0, em, em, x, y, size, size);
      }
      if (band.accent) {
        // Tint the cached glyphs so the reflection moves and bends with the wave.
        const color = heroStyle.getPropertyValue(band.accent).trim();
        const center = i % 2 ? .67 : .34;
        const light = ink.createLinearGradient(0, 0, width, 0);
        light.addColorStop(0, 'transparent');
        light.addColorStop(center - .22, color);
        light.addColorStop(center + .22, color);
        light.addColorStop(1, 'transparent');
        ink.globalCompositeOperation = 'source-atop';
        ink.globalAlpha = 1;
        ink.fillStyle = light;
        ink.fillRect(0, bands[i].top, width, bands[i].height);
        ink.globalCompositeOperation = 'source-over';
      }
    });
    paint();
    canvas.hidden = false;
  };

  const move = (event: PointerEvent) => {
    if (!event.isPrimary) return;
    if (event.pointerType === 'mouse') {
      if (event.type !== 'pointermove' || !running || motion.matches) return;
      mouseBounds ??= hero.getBoundingClientRect();
      wake.move((event.clientX - mouseBounds.left) * density, (event.clientY - mouseBounds.top) * density, performance.now());
      return;
    }
    clientX = event.clientX;
    clientY = event.clientY;
    pointerMoved = true;
    targetPull = 1;
  };
  const release = (event: PointerEvent) => {
    if (!event.isPrimary) return;
    if (event.pointerType !== 'mouse') targetPull = 0;
    else if (event.type !== 'pointerup') wake.leave();
  };
  const tick = (now: number) => {
    const seconds = Math.min((now - last) / 1000, .1);
    last = now;
    if (pointerMoved) {
      const bounds = hero.getBoundingClientRect();
      targetX = (clientX - bounds.left) / bounds.width;
      targetY = (clientY - bounds.top) / bounds.height;
      pointerMoved = false;
    }
    const follow = 1 - Math.exp(-seconds * 14);
    focusX += (targetX - focusX) * follow;
    focusY += (targetY - focusY) * follow;
    strength += (targetPull - strength) * follow;
    // Run the swell at double speed when idle, easing back to normal during a drag.
    time += seconds * (2 - strength);
    mouseBounds = undefined;
    wake.advance(now);
    paint();
    request = requestAnimationFrame(tick);
  };
  const sync = () => {
    cancelAnimationFrame(request);
    wake.reset();
    mouseBounds = undefined;
    if (running && !motion.matches) {
      last = performance.now();
      request = requestAnimationFrame(tick);
    }
  };

  resize();
  const observer = new ResizeObserver(resize);
  observer.observe(hero);
  motion.addEventListener('change', sync);
  hero.addEventListener('pointermove', move, { passive: true });
  hero.addEventListener('pointerdown', move, { passive: true });
  hero.addEventListener('pointerup', release);
  hero.addEventListener('pointerleave', release);
  hero.addEventListener('pointercancel', release);
  return {
    setRunning(value: boolean) {
      running = value;
      sync();
    },
    dispose() {
      cancelAnimationFrame(request);
      observer.disconnect();
      motion.removeEventListener('change', sync);
      hero.removeEventListener('pointermove', move);
      hero.removeEventListener('pointerdown', move);
      hero.removeEventListener('pointerup', release);
      hero.removeEventListener('pointerleave', release);
      hero.removeEventListener('pointercancel', release);
    },
  };
}

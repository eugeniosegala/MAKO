type Stroke = {
  x: number; y: number; tx: number; ty: number; length: number;
  born: number; force: number; age: number; reach: number; lift: number;
};

const lifetime = 700; // milliseconds

export class OceanWake {
  private strokes: Stroke[] = [];
  private previous: { x: number; y: number; at: number } | undefined;
  private density = 1;
  private width = 0;
  step = 0;
  first = 0;
  last = 0;
  dx = new Float32Array(0);
  dy = new Float32Array(0);
  private weights = new Float32Array(0);

  resize(width: number, density: number) {
    this.reset();
    this.width = width;
    this.density = density;
    this.step = 12 * density;
    const columns = Math.ceil(width / this.step) + 1;
    this.dx = new Float32Array(columns);
    this.dy = new Float32Array(columns);
    this.weights = new Float32Array(columns);
  }

  leave() { this.previous = undefined; }

  reset() {
    this.leave();
    this.strokes.length = 0;
  }

  move(x: number, y: number, now: number) {
    const from = this.previous;
    if (from) {
      const vx = x - from.x, vy = y - from.y;
      const length = Math.hypot(vx, vy);
      if (length < 9 * this.density) return;
      const speed = length / (this.density * Math.max(8, Math.min(100, now - from.at)));
      const force = Math.min(1, speed / 1.5);
      if (this.strokes.length >= 40) this.strokes.shift();
      this.strokes.push({ x: from.x, y: from.y, tx: vx / length, ty: vy / length, length, born: now, force, age: 0, reach: 0, lift: 0 });
    }
    this.previous = { x, y, at: now };
  }

  advance(now: number) {
    while (this.strokes.length && now - this.strokes[0].born >= lifetime) this.strokes.shift();
    for (const s of this.strokes) {
      s.age = Math.max(0, now - s.born) / lifetime;
      s.reach = this.density * (24 + 26 * s.force + 24 * s.age);
      s.lift = this.density * 29 * s.force * (1 - s.age) ** 2;
    }
  }

  // Sample a cross-section through the stroke. Push both banks apart and
  // carry the crest forward. At a turn, use the strongest overlapping stroke.
  sampleRow(y: number) {
    if (!this.strokes.length) return false;
    this.first = this.dx.length;
    this.last = -1;
    this.weights.fill(0);
    this.dx.fill(0);
    this.dy.fill(0);
    for (const s of this.strokes) {
      const endY = s.y + s.ty * s.length;
      if (y < Math.min(s.y, endY) - s.reach || y > Math.max(s.y, endY) + s.reach) continue;
      const endX = s.x + s.tx * s.length;
      const left = Math.max(0, Math.floor((Math.min(s.x, endX) - s.reach) / this.step));
      const right = Math.min(this.dx.length - 1, Math.ceil((Math.max(s.x, endX) + s.reach) / this.step));
      for (let i = left; i <= right; i++) {
        const vx = Math.min(this.width, i * this.step) - s.x, vy = y - s.y;
        const along = Math.max(0, Math.min(s.length, vx * s.tx + vy * s.ty));
        const rx = vx - along * s.tx, ry = vy - along * s.ty;
        const falloff = 1 - (rx * rx + ry * ry) / (s.reach * s.reach);
        if (falloff <= 0) continue;
        const weight = s.lift * falloff * falloff;
        if (weight <= this.weights[i]) continue;
        const bank = 2.5 * (ry * s.tx - rx * s.ty) / s.reach;
        this.weights[i] = weight;
        this.dx[i] = weight * (.42 * s.tx - bank * s.ty);
        this.dy[i] = weight * (.42 * s.ty + bank * s.tx);
        this.first = Math.min(this.first, i);
        this.last = Math.max(this.last, i);
      }
    }
    return this.last >= 0;
  }

  drawFoam(ctx: CanvasRenderingContext2D, alphabet: HTMLCanvasElement, em: number, crestGlyph: number, sprayGlyph: number) {
    if (!this.strokes.length) return;
    for (const s of this.strokes) {
      const spread = s.reach * (.08 + .58 * s.age);
      const size = this.density * (8 + 7 * s.force);
      ctx.globalAlpha = s.force * (1 - s.age) ** 2;
      const count = Math.min(80, Math.ceil(s.length / (14 * this.density)));
      for (let i = 0; i < count; i++) {
        const distance = (i + .5) * s.length / count;
        const x = s.x + s.tx * distance, y = s.y + s.ty * distance;
        const glint = Math.sin((x + y * .73) / this.density);
        const glyph = glint > -.4 ? crestGlyph : sprayGlyph;
        const bank = spread * (1 + .2 * glint);
        ctx.drawImage(alphabet, glyph * em, 0, em, em, x - bank * s.ty - size / 2, y + bank * s.tx - size / 2, size, size);
        if (glint > 0) {
          ctx.drawImage(alphabet, sprayGlyph * em, 0, em, em, x + bank * s.ty - size / 2, y - bank * s.tx - size / 2, size, size);
        }
      }
    }
    ctx.globalAlpha = 1;
  }
}

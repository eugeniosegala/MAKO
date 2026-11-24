import { useEffect, useRef, useState, type ReactNode } from 'react';
import { animateWater, stillOcean } from './ascii-ocean';

export function SharkHero({ children }: { children: ReactNode }) {
  const heroRef = useRef<HTMLElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const water = useRef<ReturnType<typeof animateWater>>(undefined);
  const [visible, setVisible] = useState<boolean | null>(null);
  const [paused, setPaused] = useState(false);

  useEffect(() => {
    const hero = heroRef.current;
    const canvas = canvasRef.current;
    if (!hero || !canvas) return;
    let inView = false;
    const update = () => setVisible(inView && !document.hidden);
    const observer = new IntersectionObserver(([entry]) => {
      inView = entry.isIntersecting;
      update();
    });
    observer.observe(hero);
    document.addEventListener('visibilitychange', update);
    water.current = animateWater(hero, canvas);
    return () => {
      water.current?.dispose();
      water.current = undefined;
      observer.disconnect();
      document.removeEventListener('visibilitychange', update);
    };
  }, []);

  const running = visible === true && !paused;
  useEffect(() => { water.current?.setRunning(running); }, [running]);

  return (
    <section className="hero" ref={heroRef} data-running={running}>
      <div className="ascii-water" aria-hidden="true">
        <canvas ref={canvasRef} hidden />
        <pre>{stillOcean.map(({ text, accent }, row) => (
          <span key={row} style={accent ? { color: `var(${accent})` } : undefined}>{text}{row < stillOcean.length - 1 && '\n'}</span>
        ))}</pre>
      </div>
      <figure className="shark-display">
        <div className="shark-field" aria-hidden="true">
          <div className="shark-art">
            <div className="shark-tail"><span className="shark-glyphs" /></div>
            <div className="shark-body"><span className="shark-glyphs" /></div>
          </div>
        </div>
        <figcaption>
          <span className="sr-only">The MAKO shark swimming through waves drawn with ASCII characters.</span>
          {visible !== null && (
            <button type="button" onClick={() => setPaused(!paused)}>
              <span aria-hidden="true">{paused ? '▶' : 'Ⅱ'}</span> {paused ? 'Play' : 'Pause'} motion
            </button>
          )}
        </figcaption>
      </figure>
      {children}
    </section>
  );
}

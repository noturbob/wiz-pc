"use client";
import { useCallback, useEffect, useRef, useState } from "react";
import { wiz, useBulbs, useConnected, type LiveBulb } from "@/app/lib/wizd";

type Position = { x: number; y: number }; // normalized 0..1 within the floor

const DEFAULT_POSITIONS: Position[] = [
  { x: 0.24, y: 0.3 },
  { x: 0.74, y: 0.24 },
  { x: 0.3, y: 0.76 },
  { x: 0.78, y: 0.72 },
  { x: 0.5, y: 0.5 },
  { x: 0.5, y: 0.15 },
];
const STORAGE_KEY = "wiz.positions";

// Room layout is a per-viewer convenience, not shared state — it lives in
// this browser's storage until the daemon grows its own room store (see the
// plan's store.cpp / Settings room editor).
function loadPositions(count: number): Position[] {
  const fallback = (i: number) => DEFAULT_POSITIONS[i % DEFAULT_POSITIONS.length];
  if (typeof window === "undefined") return Array.from({ length: count }, (_, i) => fallback(i));
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    const saved = raw ? (JSON.parse(raw) as Position[]) : [];
    if (Array.isArray(saved)) {
      return Array.from({ length: count }, (_, i) => saved[i] ?? fallback(i));
    }
  } catch {
    // corrupted/unavailable storage — fall through to defaults
  }
  return Array.from({ length: count }, (_, i) => fallback(i));
}

function savePositions(positions: Position[]) {
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(positions));
  } catch {
    // private browsing / storage disabled — layout just won't persist
  }
}

function glowStyle(p: LiveBulb | undefined): string {
  if (!p) return "";
  const alpha = p.dim / 100;
  return `radial-gradient(circle, rgba(${p.r},${p.g},${p.b},${alpha}) 0%, rgba(${p.r},${p.g},${p.b},0) 70%)`;
}

export function Room({
  selected,
  onSelect,
}: {
  selected: Set<number>;
  onSelect: (next: Set<number>) => void;
}) {
  const bulbs = useBulbs();
  const connected = useConnected();
  const floorRef = useRef<HTMLDivElement>(null);
  const glowRefs = useRef<Map<number, HTMLDivElement>>(new Map());
  const [positions, setPositions] = useState<Position[]>(() => loadPositions(bulbs.length));
  const [loadedCount, setLoadedCount] = useState(bulbs.length);
  const dragging = useRef<{ index: number; moved: boolean } | null>(null);

  // The bulb count is only known after the daemon's first "state" message
  // arrives, so re-derive positions the first time it changes from empty.
  // (React's documented pattern for adjusting state during render, not an effect.)
  if (bulbs.length !== loadedCount) {
    setLoadedCount(bulbs.length);
    setPositions(loadPositions(bulbs.length));
  }

  // Mirror the 30Hz live frame straight into the DOM. Going through React
  // state here would mean a re-render per bulb per frame; this is the one
  // orchestrated, continuous motion in the page, so it gets its own loop.
  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const live = wiz.getLive();
      for (const [index, el] of glowRefs.current) el.style.background = glowStyle(live[index]);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  const movePosition = useCallback((index: number, x: number, y: number) => {
    setPositions((prev) => {
      const next = prev.slice();
      next[index] = { x: Math.min(0.95, Math.max(0.05, x)), y: Math.min(0.9, Math.max(0.1, y)) };
      savePositions(next);
      return next;
    });
  }, []);

  function toggleSelect(index: number) {
    const next = new Set(selected);
    if (next.has(index)) next.delete(index);
    else next.add(index);
    onSelect(next);
  }

  function handlePointerDown(e: React.PointerEvent<HTMLButtonElement>, index: number) {
    dragging.current = { index, moved: false };
    e.currentTarget.setPointerCapture(e.pointerId);
  }

  function handlePointerMove(e: React.PointerEvent<HTMLButtonElement>) {
    if (!dragging.current || !floorRef.current) return;
    dragging.current.moved = true;
    const rect = floorRef.current.getBoundingClientRect();
    movePosition(dragging.current.index, (e.clientX - rect.left) / rect.width, (e.clientY - rect.top) / rect.height);
  }

  function handlePointerUp(index: number) {
    const drag = dragging.current;
    dragging.current = null;
    if (drag && !drag.moved) toggleSelect(index);
  }

  // Double-tapping empty floor (not a bulb) toggles every light at once —
  // the room itself is the all-lights switch.
  function handleFloorDoubleClick(e: React.MouseEvent<HTMLDivElement>) {
    if (e.target !== e.currentTarget) return;
    const anyOn = wiz.getLive().some((p) => p.dim > 0);
    wiz.set([], anyOn ? [0, 0, 0] : [1, 1, 1], anyOn ? 0 : 0.85);
  }

  return (
    <div
      ref={floorRef}
      onDoubleClick={handleFloorDoubleClick}
      className="relative min-h-[50vh] flex-1 overflow-hidden rounded-3xl border border-wall bg-plaster"
    >
      {!connected && (
        <p className="absolute inset-x-0 top-4 text-center text-sm text-dusk">Reconnecting to wizd…</p>
      )}
      {bulbs.length === 0 && connected && (
        <p className="absolute inset-0 flex items-center justify-center text-sm text-dusk">
          No bulbs yet — run wizd with --fake or on a network with WiZ bulbs.
        </p>
      )}
      {bulbs.map((bulb) => {
        const pos = positions[bulb.index] ?? DEFAULT_POSITIONS[bulb.index % DEFAULT_POSITIONS.length];
        return (
          <div
            key={bulb.index}
            className="absolute -translate-x-1/2 -translate-y-1/2"
            style={{ left: `${pos.x * 100}%`, top: `${pos.y * 100}%` }}
          >
            <div
              ref={(el) => {
                if (el) glowRefs.current.set(bulb.index, el);
                else glowRefs.current.delete(bulb.index);
              }}
              className="pointer-events-none absolute -inset-24 rounded-full"
            />
            <button
              onPointerDown={(e) => handlePointerDown(e, bulb.index)}
              onPointerMove={handlePointerMove}
              onPointerUp={() => handlePointerUp(bulb.index)}
              className={`relative h-5 w-5 touch-none rounded-full border-2 bg-wall transition-shadow ${
                // A filament-coloured ring alone can vanish against a warm bulb glow —
                // pairing a light inner border with an outer accent ring reads as
                // "selected" against any colour the bulb happens to be showing.
                selected.has(bulb.index)
                  ? "border-linen shadow-[0_0_0_3px_var(--filament)]"
                  : "border-dusk"
              }`}
              aria-pressed={selected.has(bulb.index)}
              aria-label={`${bulb.mac || `Bulb ${bulb.index + 1}`} — drag to move, click to select`}
            />
          </div>
        );
      })}
    </div>
  );
}

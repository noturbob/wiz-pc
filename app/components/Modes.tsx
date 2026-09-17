"use client";
import { useEffect, useRef, useState } from "react";
import { Card, CardContent } from "@/components/ui/card";
import { Toggle } from "@/components/ui/toggle";
import { wiz, type EffectName } from "@/app/lib/wizd";

type Mode = { id: EffectName; label: string; swatch: string; reducedMotionSafe?: boolean };

const MODES: Mode[] = [
  { id: "none", label: "Off", swatch: "var(--muted)" },
  {
    id: "rainbow",
    label: "Rainbow",
    swatch: "conic-gradient(from 0deg, red, yellow, lime, cyan, blue, magenta, red)",
  },
  { id: "aurora", label: "Aurora", swatch: "linear-gradient(135deg, #2fae6a, #2fa7ae, #6b4fd1)" },
  {
    id: "rainbow-aurora",
    label: "Rainbow → Aurora",
    swatch: "linear-gradient(135deg, #ff4d4d 0%, #ffd23b 25%, #2fae6a 55%, #2fa7ae 75%, #6b4fd1 100%)",
  },
  { id: "candle", label: "Candle", swatch: "linear-gradient(135deg, #ffb661, #a85a1a)" },
  { id: "fireplace", label: "Fireplace", swatch: "linear-gradient(135deg, #ff8a3d, #b3220a)" },
  { id: "ocean", label: "Ocean", swatch: "linear-gradient(135deg, #3ba7c9, #1c4d6b)" },
  { id: "breathe", label: "Breathe", swatch: "linear-gradient(135deg, #fff3d6, #ffb661)" },
  { id: "storm", label: "Storm", swatch: "linear-gradient(135deg, #2a2f3a, #6d7891)" },
  {
    id: "strobe",
    label: "Strobe",
    swatch: "repeating-linear-gradient(45deg, #fff 0 6px, #111 6px 12px)",
    reducedMotionSafe: false,
  },
];

export function Modes({ selected }: { selected: Set<number> }) {
  const [active, setActive] = useState<EffectName>("none");
  const [reducedMotion, setReducedMotion] = useState(
    () => typeof window !== "undefined" && window.matchMedia("(prefers-reduced-motion: reduce)").matches,
  );
  const lastRef = useRef<EffectName>("none");

  useEffect(() => {
    const mq = window.matchMedia("(prefers-reduced-motion: reduce)");
    const onChange = () => setReducedMotion(mq.matches);
    mq.addEventListener("change", onChange);
    return () => mq.removeEventListener("change", onChange);
  }, []);

  // Reflects whichever bulb represents the current selection, so the active
  // mode stays correct even if it was started from another tab or the
  // effect was cancelled by a manual color change (see main.cpp: a "set"
  // command always cancels a running effect on its targets).
  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const live = wiz.getLive();
      const rep = selected.size === 0 ? 0 : Math.min(...selected);
      const next = live[rep]?.effect ?? "none";
      if (next !== lastRef.current) {
        lastRef.current = next;
        setActive(next);
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [selected]);

  const targets = Array.from(selected);

  return (
    <Card>
      <CardContent className="flex gap-2 overflow-x-auto">
        {MODES.filter((mode) => mode.reducedMotionSafe !== false || !reducedMotion).map((mode) => (
          <Toggle
            key={mode.id}
            pressed={active === mode.id}
            onPressedChange={() => wiz.effect(targets, mode.id)}
            className="h-auto shrink-0 flex-col gap-2 px-3 py-2 data-[state=on]:bg-muted"
          >
            <span
              className="h-9 w-9 rounded-full ring-1 ring-border"
              style={{ background: mode.swatch }}
            />
            <span className="text-xs text-muted-foreground">{mode.label}</span>
          </Toggle>
        ))}
      </CardContent>
    </Card>
  );
}

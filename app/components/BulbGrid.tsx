"use client";
import { useEffect, useRef, useState } from "react";
import { Card, CardContent, CardHeader, CardTitle, CardAction } from "@/components/ui/card";
import { Switch } from "@/components/ui/switch";
import { wiz, useBulbs, useConnected, type LiveBulb } from "@/app/lib/wizd";
import { kelvinToRgb } from "@/app/lib/color";

function swatchStyle(p: LiveBulb | undefined): { background: string } {
  if (!p || p.dim === 0) return { background: "var(--muted)" };
  const c = `${p.r}, ${p.g}, ${p.b}`;
  const alpha = (0.35 + (p.dim / 100) * 0.65).toFixed(2);
  return {
    background: `radial-gradient(circle at 32% 28%, rgba(255,255,255,0.9) 0%, rgba(${c},${alpha}) 45%, rgba(${c},${(Number(alpha) * 0.4).toFixed(2)}) 100%)`,
  };
}

function setOn(index: number, on: boolean) {
  const rgb = kelvinToRgb(2700);
  wiz.set([index], on ? [rgb.r, rgb.g, rgb.b] : [0, 0, 0], on ? 0.85 : 0);
}

function BulbCard({
  index,
  label,
  selected,
  onToggleSelect,
}: {
  index: number;
  label: string;
  selected: boolean;
  onToggleSelect: () => void;
}) {
  const swatchRef = useRef<HTMLDivElement>(null);
  const [on, setOnState] = useState(false);
  const lastOnRef = useRef(false);

  // Mirror the 30Hz live frame straight into the DOM for the swatch (no
  // per-frame re-render), and into React state for the switch — but only
  // when on/off actually flips, so this card doesn't re-render 30x/second.
  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const p = wiz.getLive()[index];
      if (swatchRef.current) Object.assign(swatchRef.current.style, swatchStyle(p));
      const isOn = (p?.dim ?? 0) > 0;
      if (isOn !== lastOnRef.current) {
        lastOnRef.current = isOn;
        setOnState(isOn);
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [index]);

  return (
    <Card
      onClick={onToggleSelect}
      className={`cursor-pointer transition-shadow ${selected ? "ring-2 ring-primary" : ""}`}
    >
      <CardHeader>
        <CardTitle className="truncate">{label}</CardTitle>
        <CardAction onClick={(e) => e.stopPropagation()}>
          <Switch aria-label={`Turn ${label} on or off`} checked={on} onCheckedChange={(v) => setOn(index, v)} />
        </CardAction>
      </CardHeader>
      <CardContent>
        <div ref={swatchRef} className="glass h-20 w-full rounded-lg" />
      </CardContent>
    </Card>
  );
}

export function BulbGrid({
  selected,
  onSelect,
}: {
  selected: Set<number>;
  onSelect: (next: Set<number>) => void;
}) {
  const bulbs = useBulbs();
  const connected = useConnected();

  function toggleSelect(index: number) {
    const next = new Set(selected);
    if (next.has(index)) next.delete(index);
    else next.add(index);
    onSelect(next);
  }

  if (bulbs.length === 0) {
    return (
      <Card className="flex-1">
        <CardContent className="flex h-full min-h-64 items-center justify-center text-sm text-muted-foreground">
          {connected ? "No bulbs yet — run wizd with --fake or on a network with WiZ bulbs." : "Connecting to wizd…"}
        </CardContent>
      </Card>
    );
  }

  return (
    <div className="grid flex-1 auto-rows-min grid-cols-2 gap-4 sm:grid-cols-3 xl:grid-cols-4">
      {bulbs.map((bulb) => (
        <BulbCard
          key={bulb.index}
          index={bulb.index}
          label={bulb.mac || `Bulb ${bulb.index + 1}`}
          selected={selected.has(bulb.index)}
          onToggleSelect={() => toggleSelect(bulb.index)}
        />
      ))}
    </div>
  );
}

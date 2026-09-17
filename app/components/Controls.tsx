"use client";
import { useMemo, useState, type CSSProperties } from "react";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Slider } from "@/components/ui/slider";
import { wiz } from "@/app/lib/wizd";
import { hsvToRgb, kelvinToRgb, rgbToCss } from "@/app/lib/color";

const KELVIN_MIN = 1800;
const KELVIN_MAX = 6500;

// A hue *ring* is the design's stated intent, but a horizontal strip is the
// same picker with far less pointer-math to get right — same job, smaller
// diff. Upgrade to a ring if the flat strip feels cramped in practice.
const HUE_STOPS = ["#ff3b3b", "#ffd23b", "#58ff3b", "#3bffd2", "#3b6bff", "#d23bff", "#ff3b3b"];

// The Slider wrapper's type doesn't narrow to number[] from a `value={[n]}`
// call site, so onValueChange comes back typed as `number | readonly number[]`.
function firstValue(v: number | readonly number[]): number {
  return Array.isArray(v) ? v[0] : (v as number);
}

export function Controls({ selected }: { selected: Set<number> }) {
  const [brightness, setBrightness] = useState(0.85);
  const [hue, setHue] = useState(30);
  const [kelvin, setKelvin] = useState<number | null>(2700);

  const targets = Array.from(selected);
  const label =
    selected.size === 0 ? "All lights" : selected.size === 1 ? `Bulb ${targets[0] + 1}` : `${selected.size} lights`;

  function currentRgb(): [number, number, number] {
    const rgb = kelvin != null ? kelvinToRgb(kelvin) : hsvToRgb({ h: hue, s: 0.9, v: 1 });
    return [rgb.r, rgb.g, rgb.b];
  }

  function applyBrightness(next: number) {
    setBrightness(next);
    wiz.set(targets, currentRgb(), next);
  }

  function applyHue(nextHue: number) {
    setHue(nextHue);
    setKelvin(null);
    const rgb = hsvToRgb({ h: nextHue, s: 0.9, v: 1 });
    wiz.set(targets, [rgb.r, rgb.g, rgb.b], brightness);
  }

  function applyKelvin(nextKelvin: number) {
    setKelvin(nextKelvin);
    const rgb = kelvinToRgb(nextKelvin);
    wiz.set(targets, [rgb.r, rgb.g, rgb.b], brightness);
  }

  const brightnessPct = Math.round(brightness * 100);
  const kelvinTrack = useMemo(() => {
    const stops = Array.from({ length: 11 }, (_, i) => {
      const k = KELVIN_MIN + ((KELVIN_MAX - KELVIN_MIN) * i) / 10;
      return `${rgbToCss(kelvinToRgb(k))} ${(i / 10) * 100}%`;
    });
    return `linear-gradient(to right, ${stops.join(", ")})`;
  }, []);
  const hueTrack = `linear-gradient(to right, ${HUE_STOPS.join(", ")})`;

  return (
    <Card className="w-full md:w-80">
      <CardHeader>
        <CardDescription>Selection</CardDescription>
        <CardTitle className="text-xl">{label}</CardTitle>
      </CardHeader>
      <CardContent className="flex flex-col gap-6">
        <div>
          <div className="flex items-baseline justify-between">
            <span className="text-sm text-muted-foreground">Brightness</span>
            <span className="font-mono text-lg tabular-nums">{brightnessPct}</span>
          </div>
          <Slider
            aria-label="Brightness"
            className="mt-3"
            min={0}
            max={100}
            value={[brightnessPct]}
            onValueChange={(v) => applyBrightness(firstValue(v) / 100)}
          />
        </div>

        <div>
          <span className="text-sm text-muted-foreground">Color temperature</span>
          <Slider
            aria-label="Color temperature"
            className="mt-3 [&_[data-slot=slider-range]]:bg-transparent [&_[data-slot=slider-track]]:bg-[image:var(--track-gradient)]"
            style={{ "--track-gradient": kelvinTrack } as CSSProperties}
            min={KELVIN_MIN}
            max={KELVIN_MAX}
            value={[kelvin ?? KELVIN_MIN]}
            onValueChange={(v) => applyKelvin(firstValue(v))}
          />
        </div>

        <div>
          <span className="text-sm text-muted-foreground">Hue</span>
          <Slider
            aria-label="Hue"
            className="mt-3 [&_[data-slot=slider-range]]:bg-transparent [&_[data-slot=slider-track]]:bg-[image:var(--track-gradient)]"
            style={{ "--track-gradient": hueTrack } as CSSProperties}
            min={0}
            max={360}
            value={[hue]}
            onValueChange={(v) => applyHue(firstValue(v))}
          />
        </div>
      </CardContent>
    </Card>
  );
}

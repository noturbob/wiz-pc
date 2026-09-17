"use client";
import { useMemo, useState } from "react";
import { wiz } from "@/app/lib/wizd";
import { hsvToRgb, kelvinToRgb, rgbToCss } from "@/app/lib/color";

const KELVIN_MIN = 1800;
const KELVIN_MAX = 6500;

// A hue *ring* is the design's stated intent, but a horizontal strip is the
// same picker with far less pointer-math to get right — same job, smaller
// diff. Upgrade to a ring if the flat strip feels cramped in practice.
const HUE_TRACK =
  "linear-gradient(to right, #ff3b3b, #ffd23b, #58ff3b, #3bffd2, #3b6bff, #d23bff, #ff3b3b)";

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

  // The brightness readout's own weight tracks the value it's showing —
  // the number looks as heavy as the light it describes.
  const weight = 200 + Math.round(brightness * 600);

  const kelvinTrack = useMemo(() => {
    const stops = Array.from({ length: 11 }, (_, i) => {
      const k = KELVIN_MIN + ((KELVIN_MAX - KELVIN_MIN) * i) / 10;
      return `${rgbToCss(kelvinToRgb(k))} ${(i / 10) * 100}%`;
    });
    return `linear-gradient(to right, ${stops.join(", ")})`;
  }, []);

  return (
    <div className="flex w-full flex-col gap-6 rounded-3xl border border-wall bg-plaster p-6 md:w-80">
      <div>
        <p className="text-sm text-dusk">Selection</p>
        <h2 className="text-2xl text-linen">{label}</h2>
      </div>

      <div>
        <div className="flex items-baseline justify-between">
          <label htmlFor="brightness" className="text-sm text-dusk">
            Brightness
          </label>
          <span className="text-3xl tabular-nums text-linen" style={{ fontVariationSettings: `"wght" ${weight}` }}>
            {Math.round(brightness * 100)}
          </span>
        </div>
        <input
          id="brightness"
          type="range"
          min={0}
          max={100}
          value={Math.round(brightness * 100)}
          onChange={(e) => applyBrightness(Number(e.target.value) / 100)}
          className="mt-2 w-full accent-filament"
        />
      </div>

      <div>
        <p className="text-sm text-dusk">Color temperature</p>
        <input
          type="range"
          min={KELVIN_MIN}
          max={KELVIN_MAX}
          value={kelvin ?? KELVIN_MIN}
          onChange={(e) => applyKelvin(Number(e.target.value))}
          className="mt-2 w-full"
          style={{ background: kelvinTrack }}
        />
      </div>

      <div>
        <p className="text-sm text-dusk">Hue</p>
        <input
          type="range"
          min={0}
          max={360}
          value={hue}
          onChange={(e) => applyHue(Number(e.target.value))}
          className="mt-2 w-full"
          style={{ background: HUE_TRACK }}
        />
      </div>
    </div>
  );
}

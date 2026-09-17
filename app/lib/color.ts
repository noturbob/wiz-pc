// Mirrors engine/src/color.hpp's math in TypeScript. Kept as small pure
// functions rather than shared across languages — not worth a shared package
// for two functions in a two-language project this size.

export type Rgb = { r: number; g: number; b: number }; // each 0..1
export type Hsv = { h: number; s: number; v: number }; // h: 0..360, s/v: 0..1

export function hsvToRgb({ h, s, v }: Hsv): Rgb {
  const c = v * s;
  const hp = h / 60;
  const x = c * (1 - Math.abs((hp % 2) - 1));
  const m = v - c;
  let rgb: Rgb;
  if (hp < 1) rgb = { r: c, g: x, b: 0 };
  else if (hp < 2) rgb = { r: x, g: c, b: 0 };
  else if (hp < 3) rgb = { r: 0, g: c, b: x };
  else if (hp < 4) rgb = { r: 0, g: x, b: c };
  else if (hp < 5) rgb = { r: x, g: 0, b: c };
  else rgb = { r: c, g: 0, b: x };
  return { r: rgb.r + m, g: rgb.g + m, b: rgb.b + m };
}

// Tanner Helland's fit to the Planckian locus — the same approximation the
// daemon uses to mirror CCT mode back as a display colour (see kelvinToRgb
// in engine/src/color.hpp). Used here to *pick* a temperature: sending a
// bulb this RGB is low-saturation enough that the daemon routes it to its
// real colour-temperature channel instead of RGB.
export function kelvinToRgb(kelvin: number): Rgb {
  const temp = Math.min(40000, Math.max(1000, kelvin)) / 100;
  const r = temp <= 66 ? 255 : 329.698727446 * Math.pow(temp - 60, -0.1332047592);
  const g =
    temp <= 66
      ? 99.4708025861 * Math.log(temp) - 161.1195681661
      : 288.1221695283 * Math.pow(temp - 60, -0.0755148492);
  const b = temp >= 66 ? 255 : temp <= 19 ? 0 : 138.5177312231 * Math.log(temp - 10) - 305.0447927307;
  return {
    r: Math.min(255, Math.max(0, r)) / 255,
    g: Math.min(255, Math.max(0, g)) / 255,
    b: Math.min(255, Math.max(0, b)) / 255,
  };
}

export function rgbToCss({ r, g, b }: Rgb, alpha = 1): string {
  return `rgba(${Math.round(r * 255)}, ${Math.round(g * 255)}, ${Math.round(b * 255)}, ${alpha})`;
}

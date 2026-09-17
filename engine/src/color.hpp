#pragma once
// RGB<->HSV conversion and the RGB->Pilot decision (including the
// low-saturation->kelvin rule that real WiZ bulbs need to look right).
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "wiz.hpp"

namespace wiz::color {

// WiZ bulbs render low-saturation RGB (e.g. a pale "white") poorly — the LEDs
// look washed out or tinted. Below this saturation, prefer the dedicated
// colour-temperature channel instead, which drives the bulb's warm/cool white LEDs.
constexpr float kMinSaturationForRgb = 0.15f;
constexpr uint16_t kFallbackKelvin = 4000;

struct Rgb { float r, g, b; }; // each 0..1
struct Hsv { float h, s, v; }; // h: 0..360, s/v: 0..1

inline Hsv rgbToHsv(Rgb c) {
    float mx = std::max({c.r, c.g, c.b});
    float mn = std::min({c.r, c.g, c.b});
    float d = mx - mn;
    float h = 0.f;
    if (d > 1e-6f) {
        if (mx == c.r) h = 60.f * std::fmod((c.g - c.b) / d, 6.f);
        else if (mx == c.g) h = 60.f * ((c.b - c.r) / d + 2.f);
        else h = 60.f * ((c.r - c.g) / d + 4.f);
    }
    if (h < 0.f) h += 360.f;
    float s = mx <= 1e-6f ? 0.f : d / mx;
    return {h, s, mx};
}

inline Rgb hsvToRgb(Hsv c) {
    float C = c.v * c.s;
    float Hp = c.h / 60.f;
    float X = C * (1.f - std::fabs(std::fmod(Hp, 2.f) - 1.f));
    float m = c.v - C;
    Rgb rgb{0, 0, 0};
    switch (static_cast<int>(Hp) % 6) {
        case 0: rgb = {C, X, 0}; break;
        case 1: rgb = {X, C, 0}; break;
        case 2: rgb = {0, C, X}; break;
        case 3: rgb = {0, X, C}; break;
        case 4: rgb = {X, 0, C}; break;
        default: rgb = {C, 0, X}; break;
    }
    return {rgb.r + m, rgb.g + m, rgb.b + m};
}

// Approximates what a given colour temperature looks like as RGB (Tanner
// Helland's fit to the Planckian locus). Used only to mirror a bulb's CCT
// mode back to the dashboard as a glow colour — never sent to the bulb itself.
inline Rgb kelvinToRgb(float kelvin) {
    float temp = std::clamp(kelvin, 1000.f, 40000.f) / 100.f;
    float r = temp <= 66.f ? 255.f : 329.698727446f * std::pow(temp - 60.f, -0.1332047592f);
    float g = temp <= 66.f ? 99.4708025861f * std::log(temp) - 161.1195681661f
                           : 288.1221695283f * std::pow(temp - 60.f, -0.0755148492f);
    float b = temp >= 66.f ? 255.f : (temp <= 19.f ? 0.f : 138.5177312231f * std::log(temp - 10.f) - 305.0447927307f);
    return {
        std::clamp(r, 0.f, 255.f) / 255.f,
        std::clamp(g, 0.f, 255.f) / 255.f,
        std::clamp(b, 0.f, 255.f) / 255.f,
    };
}

// OKLab: a perceptually-even colour space (Björn Ottosson). Used to crossfade
// between two colours without passing through a muddy gray in the middle —
// which is what a naive per-channel RGB lerp does whenever the two colours
// sit far apart on the hue wheel (e.g. rainbow -> aurora).
struct Oklab { float L, a, b; };

inline Oklab rgbToOklab(Rgb c) {
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;
    float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
    };
}

inline Rgb oklabToRgb(Oklab c) {
    float l_ = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    float m_ = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    float s_ = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return {
        std::clamp(+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s, 0.f, 1.f),
        std::clamp(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s, 0.f, 1.f),
        std::clamp(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s, 0.f, 1.f),
    };
}

inline Rgb mixOklab(Rgb a, Rgb b, float t) {
    Oklab la = rgbToOklab(a), lb = rgbToOklab(b);
    return oklabToRgb({
        la.L + (lb.L - la.L) * t,
        la.a + (lb.a - la.a) * t,
        la.b + (lb.b - la.b) * t,
    });
}

// Per-bulb calibration: real LEDs drift from the "ideal" values the protocol
// assumes. Leave at defaults until the settings calibration wizard sets these.
struct Calibration {
    float gamma = 1.0f;                // applied to r,g,b before quantizing
    float whiteBalance[3] = {1, 1, 1}; // per-channel multiplier
};

inline uint8_t applyChannel(float v, float gamma, float balance) {
    v = std::clamp(v * balance, 0.f, 1.f);
    v = std::pow(v, gamma);
    return static_cast<uint8_t>(std::lround(v * 255.f));
}

// Converts a linear 0..1 RGB colour + 0..1 brightness into the Pilot the bulb
// should receive, applying the low-saturation->kelvin rule and calibration.
inline Pilot toPilot(Rgb c, float brightness, const Calibration& cal = {}) {
    Pilot p;
    p.on = brightness > 0.001f;
    if (!p.on) return p;

    p.dimming = static_cast<uint8_t>(std::clamp(brightness, 0.f, 1.f) * 90.f + 10.f);

    Hsv hsv = rgbToHsv(c);
    if (hsv.s < kMinSaturationForRgb) {
        p.useTemp = true;
        p.kelvin = kFallbackKelvin;
        return p;
    }

    p.useTemp = false;
    p.r = applyChannel(c.r, cal.gamma, cal.whiteBalance[0]);
    p.g = applyChannel(c.g, cal.gamma, cal.whiteBalance[1]);
    p.b = applyChannel(c.b, cal.gamma, cal.whiteBalance[2]);
    return p;
}

} // namespace wiz::color

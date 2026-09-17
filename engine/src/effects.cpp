#include "effects.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace wiz::effects {

namespace {

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

// Cheap stand-in for simplex/perlin noise: a handful of incommensurate sine
// waves summed together.
// ponytail: not true gradient noise — can look faintly periodic under close
// inspection at very slow speeds or large scales. Swap in a real simplex
// noise implementation if the aurora ever needs to hold up to that scrutiny.
float pseudoNoise(float x, float y, float z) {
    float n = std::sin(x * 1.7f + y * 0.9f + z * 1.3f) +
              std::sin(x * 0.6f - y * 1.4f + z * 0.5f) * 0.6f +
              std::sin(x * 2.3f + y * 0.3f - z * 0.8f) * 0.4f;
    return n / 2.0f; // approx -1..1
}

color::Rgb rainbow(double t, float pos01) {
    float hue = std::fmod(static_cast<float>(t) * 40.f + pos01 * 360.f, 360.f);
    if (hue < 0.f) hue += 360.f;
    return color::hsvToRgb({hue, 0.9f, 1.f});
}

color::Rgb aurora(double t, float pos01) {
    float tt = static_cast<float>(t) * 0.15f;
    float n = pseudoNoise(pos01 * 3.f, tt, 0.f);
    // Greens through teals into violet — deliberately not the full wheel,
    // that's what keeps this reading as "aurora" rather than "slow rainbow".
    float hue = 150.f + n * 90.f; // ~60..240
    float value = 0.6f + 0.4f * pseudoNoise(pos01 * 2.f, tt * 1.3f, 10.f);
    return color::hsvToRgb({hue, 0.8f, std::clamp(value, 0.2f, 1.f)});
}

color::Rgb candleLike(double t, float pos01, float baseKelvin, float flickerAmount) {
    float tt = static_cast<float>(t) * 6.f;
    float flicker = pseudoNoise(pos01 * 5.f, tt, 3.f);
    color::Rgb base = color::kelvinToRgb(baseKelvin);
    float value = std::clamp(1.f - flickerAmount * 0.5f + flicker * flickerAmount * 0.5f, 0.15f, 1.f);
    return {base.r * value, base.g * value, base.b * value};
}

color::Rgb ocean(double t, float pos01) {
    float tt = static_cast<float>(t) * 0.3f;
    float hue = 190.f + 20.f * std::sin(tt + pos01 * 3.f);
    float value = 0.5f + 0.3f * std::sin(tt * 0.6f - pos01 * 2.f);
    return color::hsvToRgb({hue, 0.7f, std::clamp(value, 0.2f, 1.f)});
}

color::Rgb breathe(double t, float /*pos01*/) {
    float value = 0.3f + 0.7f * (0.5f + 0.5f * std::sin(t * 1.2f));
    return color::hsvToRgb({30.f, 0.4f, value}); // a soft warm white, breathing
}

color::Rgb storm(double t, float pos01, BulbEffectState& state) {
    color::Rgb ambient = color::hsvToRgb({220.f, 0.4f, 0.12f}); // near-dark stormy blue-gray

    if (t >= state.nextStrikeAt) {
        state.strikeEnergy = 1.f;
        std::uniform_real_distribution<double> gap(0.8, 5.0);
        state.nextStrikeAt = t + gap(rng()) + pos01 * 0.15; // strikes ripple slightly across the room
    }
    if (state.strikeEnergy > 0.f) {
        color::Rgb out = {
            ambient.r + (1.f - ambient.r) * state.strikeEnergy,
            ambient.g + (1.f - ambient.g) * state.strikeEnergy,
            ambient.b + (1.f - ambient.b) * state.strikeEnergy,
        };
        state.strikeEnergy *= 0.75f; // exponential decay, tick over tick
        if (state.strikeEnergy < 0.01f) state.strikeEnergy = 0.f;
        return out;
    }
    return ambient;
}

color::Rgb strobe(double t, float pos01) {
    // Capped at 2Hz, well under the 3Hz+ range associated with photosensitive
    // seizure risk (see e.g. W3C WCAG's three-flashes-per-second threshold).
    constexpr double kHz = 2.0;
    float hue = std::fmod(pos01 * 360.f, 360.f);
    bool on = std::fmod(t * kHz, 1.0) < 0.5;
    return on ? color::hsvToRgb({hue, 0.9f, 1.f}) : color::Rgb{0.f, 0.f, 0.f};
}

} // namespace

Id parse(std::string_view n) {
    if (n == "rainbow") return Id::Rainbow;
    if (n == "aurora") return Id::Aurora;
    if (n == "rainbow-aurora") return Id::RainbowAurora;
    if (n == "candle") return Id::Candle;
    if (n == "fireplace") return Id::Fireplace;
    if (n == "ocean") return Id::Ocean;
    if (n == "breathe") return Id::Breathe;
    if (n == "storm") return Id::Storm;
    if (n == "strobe") return Id::Strobe;
    return Id::None;
}

std::string_view name(Id id) {
    switch (id) {
        case Id::Rainbow: return "rainbow";
        case Id::Aurora: return "aurora";
        case Id::RainbowAurora: return "rainbow-aurora";
        case Id::Candle: return "candle";
        case Id::Fireplace: return "fireplace";
        case Id::Ocean: return "ocean";
        case Id::Breathe: return "breathe";
        case Id::Storm: return "storm";
        case Id::Strobe: return "strobe";
        case Id::None: return "none";
    }
    return "none";
}

color::Rgb evaluate(Id id, double t, float pos01, BulbEffectState& state) {
    switch (id) {
        case Id::Rainbow: return rainbow(t, pos01);
        case Id::Aurora: return aurora(t, pos01);
        case Id::RainbowAurora: {
            // Crossfades once, over 8s, then settles into aurora — it
            // "switches into" aurora per the brief, not an endless back-and-forth.
            float morph = std::clamp(static_cast<float>(t) / 8.f, 0.f, 1.f);
            return color::mixOklab(rainbow(t, pos01), aurora(t, pos01), morph);
        }
        case Id::Candle: return candleLike(t, pos01, 1900.f, 0.35f);
        case Id::Fireplace: return candleLike(t, pos01, 1700.f, 0.55f);
        case Id::Ocean: return ocean(t, pos01);
        case Id::Breathe: return breathe(t, pos01);
        case Id::Storm: return storm(t, pos01, state);
        case Id::Strobe: return strobe(t, pos01);
        case Id::None: return {0.f, 0.f, 0.f};
    }
    return {0.f, 0.f, 0.f};
}

} // namespace wiz::effects

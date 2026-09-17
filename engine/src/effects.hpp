#pragma once
// Time- and position-driven colour effects. Pure functions: given a moment
// in time and a bulb's normalized position along the room (0..1), return the
// colour that bulb should show right now. main.cpp evaluates these every
// tick and pushes the result through the same Scheduler manual control uses
// — an effect is just a producer of Pilots, same as a slider drag.
//
// Position is currently proxied by bulb index (i / (count-1)) rather than
// real room coordinates: the dashboard's room layout lives in the browser
// (see app/components/Room.tsx) and isn't sent to the daemon yet. Wire real
// positions through once a "bulb.move" command exists (see the plan's later
// Settings/room-editor step) — evaluate() doesn't need to change, just what
// main.cpp passes as pos01.
#include <string>
#include <string_view>

#include "color.hpp"

namespace wiz::effects {

enum class Id {
    None,
    Rainbow,
    Aurora,
    RainbowAurora, // one-way morph from rainbow into aurora
    Candle,
    Fireplace,
    Ocean,
    Breathe,
    Storm,
    Strobe,
};

Id parse(std::string_view name); // unknown text -> Id::None
std::string_view name(Id id);

// Per-bulb scratch state that must persist between ticks for effects that
// aren't pure functions of time alone (storm's lightning timing). Effects
// that don't need it just ignore it.
struct BulbEffectState {
    double nextStrikeAt = 0.0; // storm: seconds (effect-local clock) until the next strike
    float strikeEnergy = 0.f;  // storm: current flash brightness, decays each tick
};

// `t` is seconds since this bulb's effect (re)started — restarting resets phase.
color::Rgb evaluate(Id id, double t, float pos01, BulbEffectState& state);

} // namespace wiz::effects

# wiz-pc

Control every Philips WiZ bulb on your network from your PC — a low-latency
C++ daemon that talks directly to the bulbs, and a dashboard to drive it.

No cloud, no app-store app, no account. `wizd` runs on your machine and
speaks the bulbs' own local UDP protocol; the dashboard is a page you open
in a browser on the same machine or LAN.

## What's here

- **`engine/`** — `wizd`, the daemon. C++23, non-blocking UDP to the bulbs,
  a latest-wins/rate-limited send scheduler (so a slow bulb never builds up
  a backlog of stale commands), and a small hand-rolled WebSocket server for
  the dashboard. Nine built-in effects (rainbow, aurora, a morph between
  them, candle, fireplace, ocean, breathe, storm, strobe) run as pure
  time/position functions evaluated in the same tick that drives the bulbs.
- **`app/`** — the dashboard. Next.js (App Router), talks straight to
  `wizd` over WebSocket with no API routes in between. Built on
  [shadcn/ui](https://ui.shadcn.com): each bulb is a card with a live color
  swatch and on/off switch, plus shared brightness/temperature/hue controls
  and a mode picker for the effects above.

## Status

Working: manual color/brightness/kelvin control, all nine effects, discovery
of real bulbs, and a `--fake N` mode for developing without hardware.

Not yet built: audio-reactive modes, screen/ambilight sync, scenes and
schedules, and a calibration UI — see the code comments in `engine/src/` for
where these are meant to land.

## Requirements

- Linux, g++ (C++23) or another recent GCC/Clang, CMake ≥ 3.24
- OpenSSL dev headers (used only for the WebSocket handshake)
- Node.js and npm

## Getting started

Build and run the daemon:

```bash
cd engine
cmake -B build -S .
cmake --build build -j"$(nproc)"
ctest --test-dir build          # run the self-checks

./build/wizd --fake 4           # hardware-free: 4 virtual bulbs on loopback
# or, on a network with real WiZ bulbs:
./build/wizd                    # discovers and controls them
```

Run the dashboard, in another terminal:

```bash
npm install
npm run dev
```

Open <http://localhost:3000>.

### `wizd` flags

| Flag | Effect |
|---|---|
| `--fake N` | Run against N virtual bulbs instead of real hardware |
| `--discover` | List discoverable bulbs and exit |
| `--lan` | Bind the WebSocket server to `0.0.0.0` instead of `127.0.0.1`, for phone/LAN access |
| `--port N` | WebSocket port (default `7878`) |

## How it talks to the bulbs

WiZ bulbs accept plain JSON over UDP on port 38899 — no cloud round-trip
needed. `wizd` discovers them with a broadcast, then sends `setPilot`
commands directly. See `engine/src/wiz.cpp` for the wire format and
`engine/src/color.hpp` for how RGB gets mapped to the bulb's RGB or
color-temperature channel (low-saturation colors route to the temperature
channel, since WiZ's white LEDs render a pale "white" far better than its
color LEDs do).

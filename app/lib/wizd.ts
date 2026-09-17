"use client";
// Single WebSocket connection to wizd, plus a store for it. There are no
// Next.js API routes here: the browser talks straight to the C++ daemon on
// 127.0.0.1:7878 (see engine/src/server.cpp for the wire format).
import { useSyncExternalStore } from "react";

export type Bulb = { index: number; ip: string; mac: string };
// One bulb's live state, as mirrored by the daemon's 30Hz binary frame:
// r/g/b/dim are all 0..255 on the wire even though dim is really a 0..100
// percentage server-side — see buildLiveFrame in engine/src/main.cpp.
export type LiveBulb = { r: number; g: number; b: number; dim: number };

type Listener = () => void;

const DEFAULT_URL = "ws://127.0.0.1:7878";

class WizConnection {
  private ws: WebSocket | null = null;
  private bulbs: Bulb[] = [];
  private live: LiveBulb[] = [];
  private connected = false;
  private reconnectDelayMs = 500;
  private stateListeners = new Set<Listener>();
  private frameListeners = new Set<Listener>();

  constructor(private url: string) {
    if (typeof window !== "undefined") this.connect();
  }

  private connect() {
    const ws = new WebSocket(this.url);
    ws.binaryType = "arraybuffer";
    this.ws = ws;

    ws.onopen = () => {
      this.connected = true;
      this.reconnectDelayMs = 500;
      this.emitState();
    };

    ws.onmessage = (ev) => {
      if (typeof ev.data === "string") {
        this.handleJson(ev.data);
      } else {
        this.handleFrame(new Uint8Array(ev.data as ArrayBuffer));
      }
    };

    ws.onclose = () => {
      this.connected = false;
      this.emitState();
      const delay = this.reconnectDelayMs;
      this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, 8000);
      setTimeout(() => this.connect(), delay);
    };

    ws.onerror = () => ws.close();
  }

  private handleJson(text: string) {
    let msg: { type?: string; bulbs?: Bulb[] };
    try {
      msg = JSON.parse(text);
    } catch {
      return;
    }
    if (msg.type === "state" && Array.isArray(msg.bulbs)) {
      this.bulbs = msg.bulbs;
      if (this.live.length !== this.bulbs.length) {
        this.live = this.bulbs.map(() => ({ r: 0, g: 0, b: 0, dim: 0 }));
      }
      this.emitState();
    }
  }

  private handleFrame(bytes: Uint8Array) {
    for (let i = 0; i + 4 < bytes.length; i += 5) {
      const index = bytes[i];
      if (index >= this.live.length) continue;
      this.live[index] = { r: bytes[i + 1], g: bytes[i + 2], b: bytes[i + 3], dim: bytes[i + 4] };
    }
    this.emitFrame();
  }

  private emitState = () => this.stateListeners.forEach((l) => l());
  private emitFrame = () => this.frameListeners.forEach((l) => l());

  subscribeState = (cb: Listener) => {
    this.stateListeners.add(cb);
    return () => this.stateListeners.delete(cb);
  };
  // The live 30Hz frame is read directly via getLive() inside requestAnimationFrame
  // callbacks (see components/Room.tsx) — subscribing here would mean a React
  // re-render on every frame, which is exactly what that pattern avoids.
  subscribeFrame = (cb: Listener) => {
    this.frameListeners.add(cb);
    return () => this.frameListeners.delete(cb);
  };

  getBulbs = () => this.bulbs;
  getConnected = () => this.connected;
  getLive = () => this.live;

  // `targets` empty means "all bulbs". rgb channels are linear 0..1.
  set(targets: number[], rgb: [number, number, number], brightness: number) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(
      JSON.stringify({ type: "set", targets, r: rgb[0], g: rgb[1], b: rgb[2], brightness }),
    );
  }
}

export const wiz = new WizConnection(DEFAULT_URL);

const emptyBulbs: Bulb[] = [];
export function useBulbs(): Bulb[] {
  return useSyncExternalStore(wiz.subscribeState, wiz.getBulbs, () => emptyBulbs);
}

export function useConnected(): boolean {
  return useSyncExternalStore(wiz.subscribeState, wiz.getConnected, () => false);
}

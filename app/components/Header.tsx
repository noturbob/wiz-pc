"use client";
import { useEffect, useRef, useState } from "react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { wiz, useConnected } from "@/app/lib/wizd";

export function Header() {
  const connected = useConnected();
  const [anyOn, setAnyOn] = useState(false);
  const lastRef = useRef(false);

  // Mirrors whether any bulb is lit, for this header's master power button.
  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const next = wiz.getLive().some((p) => p.dim > 0);
      if (next !== lastRef.current) {
        lastRef.current = next;
        setAnyOn(next);
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  return (
    <header className="flex items-center justify-between px-6 py-5 md:px-10">
      <span className="text-lg font-semibold">wiz</span>

      <div className="flex items-center gap-3">
        <Badge variant="outline" className="gap-1.5">
          <span className={`h-1.5 w-1.5 rounded-full ${connected ? "bg-green-500" : "bg-amber-500"}`} />
          {connected ? "Connected" : "Reconnecting…"}
        </Badge>
        <Button
          variant={anyOn ? "default" : "outline"}
          size="icon"
          aria-pressed={anyOn}
          aria-label={anyOn ? "Turn all lights off" : "Turn all lights on"}
          onClick={() => wiz.set([], anyOn ? [0, 0, 0] : [1, 1, 1], anyOn ? 0 : 0.85)}
        >
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden>
            <path
              d="M8 1v6.5M3.5 3.8a5.5 5.5 0 1 0 9 0"
              stroke="currentColor"
              strokeWidth="1.6"
              strokeLinecap="round"
            />
          </svg>
        </Button>
      </div>
    </header>
  );
}

"use client";
import { useState } from "react";
import { Room } from "@/app/components/Room";
import { Controls } from "@/app/components/Controls";

export default function Home() {
  const [selected, setSelected] = useState<Set<number>>(new Set());

  return (
    <main className="flex flex-1 flex-col gap-6 p-6 md:flex-row">
      <Room selected={selected} onSelect={setSelected} />
      <Controls selected={selected} />
    </main>
  );
}

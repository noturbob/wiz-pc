"use client";
import { useState } from "react";
import { Header } from "@/app/components/Header";
import { BulbGrid } from "@/app/components/BulbGrid";
import { Controls } from "@/app/components/Controls";
import { Modes } from "@/app/components/Modes";

export default function Home() {
  const [selected, setSelected] = useState<Set<number>>(new Set());

  return (
    <div className="flex flex-1 flex-col">
      <Header />
      <main className="mx-auto flex w-full max-w-6xl flex-1 flex-col gap-6 px-6 pb-6 md:px-10 md:pb-10">
        <div className="flex flex-1 flex-col gap-6 md:flex-row">
          <BulbGrid selected={selected} onSelect={setSelected} />
          <Controls selected={selected} />
        </div>
        <Modes selected={selected} />
      </main>
    </div>
  );
}

import React from "react";
import Header from "./components/Header";
import StormVisualiser from "./components/StormVisualiser";
import ControlDeck from "./components/ControlDeck";
import Keyboard from "./components/Keyboard";
import { KnobDefs } from "./components/Knob";
import { TooltipLayer } from "./components/Tooltip";

export default function App() {
  return (
    <div className="flex h-full w-full flex-col gap-[var(--gap)] p-[var(--pad)]">
      <KnobDefs />
      <Header />
      <main className="relative min-h-0 flex-1">
        <StormVisualiser />
      </main>
      <ControlDeck />
      <Keyboard />
      <TooltipLayer />
    </div>
  );
}

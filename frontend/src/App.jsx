import React, { useState, useEffect } from 'react';

// Mock JUCE backend for development in browser
const isJuce = true; // Always assume true for this build logic

function App() {
  const [rainIntensity, setRainIntensity] = useState(0.5);
  const [humidity, setHumidity] = useState(0.3);
  const [windSpeed, setWindSpeed] = useState(0.1);
  const [thunderDist, setThunderDist] = useState(0.8);
  const [hammerHardness, setHammerHardness] = useState(0.6);

  useEffect(() => {
    const handleParamUpdate = (e) => {
        const { name, value } = e.detail;
        if (name === 'rain_intensity') setRainIntensity(value);
        if (name === 'humidity') setHumidity(value);
        if (name === 'wind_speed') setWindSpeed(value);
        if (name === 'thunder_distance') setThunderDist(value);
        if (name === 'hammer_hardness') setHammerHardness(value);
    };

    window.addEventListener('parameterUpdate', handleParamUpdate);
    return () => window.removeEventListener('parameterUpdate', handleParamUpdate);
  }, []);

  const updateParam = (name, value) => {
    // Update local state
    if (name === 'rain_intensity') setRainIntensity(value);
    if (name === 'humidity') setHumidity(value);
    if (name === 'wind_speed') setWindSpeed(value);
    if (name === 'thunder_distance') setThunderDist(value);
    if (name === 'hammer_hardness') setHammerHardness(value);

    // IPC: Send to C++ via URL interception
    // petrichor://param?name=rain&value=0.5
    // Note: Use a hidden iframe or standard location assignment that gets intercepted.
    // window.location.href triggers navigation which we intercept and cancel in C++.
    window.location.href = `petrichor://param?name=${name}&value=${value}`;
  };

  return (
    <div className="w-full h-screen bg-gradient-to-b from-slate-900 to-black flex flex-col items-center justify-center p-8">
      <h1 className="text-4xl font-bold mb-8 text-cyan-400 tracking-widest drop-shadow-[0_0_10px_rgba(34,211,238,0.5)]">
        PETRICHOR
      </h1>

      <div className="grid grid-cols-3 gap-8 w-full max-w-4xl glass-panel p-6 rounded-xl border border-slate-700 bg-slate-800/30 backdrop-blur-md">

        {/* Rain Control */}
        <div className="flex flex-col items-center space-y-4">
          <label className="text-cyan-200">RAIN INTENSITY</label>
          <input
            type="range" min="0" max="1" step="0.01"
            value={rainIntensity}
            onChange={(e) => updateParam('rain_intensity', parseFloat(e.target.value))}
            className="w-full h-2 bg-slate-700 rounded-lg appearance-none cursor-pointer accent-cyan-400"
          />
          <span className="text-xs text-slate-400">{Math.round(rainIntensity * 100)}%</span>
        </div>

        {/* Humidity Control */}
        <div className="flex flex-col items-center space-y-4">
          <label className="text-cyan-200">HUMIDITY</label>
          <input
            type="range" min="0" max="1" step="0.01"
            value={humidity}
            onChange={(e) => updateParam('humidity', parseFloat(e.target.value))}
            className="w-full h-2 bg-slate-700 rounded-lg appearance-none cursor-pointer accent-cyan-400"
          />
           <span className="text-xs text-slate-400">{Math.round(humidity * 100)}%</span>
        </div>

        {/* Wind Control */}
        <div className="flex flex-col items-center space-y-4">
          <label className="text-cyan-200">WIND SPEED</label>
          <input
            type="range" min="0" max="1" step="0.01"
            value={windSpeed}
            onChange={(e) => updateParam('wind_speed', parseFloat(e.target.value))}
            className="w-full h-2 bg-slate-700 rounded-lg appearance-none cursor-pointer accent-cyan-400"
          />
           <span className="text-xs text-slate-400">{Math.round(windSpeed * 100)}%</span>
        </div>

         {/* Thunder Control */}
        <div className="flex flex-col items-center space-y-4">
          <label className="text-cyan-200">THUNDER DIST</label>
          <input
            type="range" min="0" max="1" step="0.01"
            value={thunderDist}
            onChange={(e) => updateParam('thunder_distance', parseFloat(e.target.value))}
            className="w-full h-2 bg-slate-700 rounded-lg appearance-none cursor-pointer accent-cyan-400"
          />
           <span className="text-xs text-slate-400">{Math.round(thunderDist * 100)}%</span>
        </div>

         {/* Hammer Control */}
         <div className="flex flex-col items-center space-y-4">
          <label className="text-cyan-200">HAMMER HARDNESS</label>
          <input
            type="range" min="0" max="1" step="0.01"
            value={hammerHardness}
            onChange={(e) => updateParam('hammer_hardness', parseFloat(e.target.value))}
            className="w-full h-2 bg-slate-700 rounded-lg appearance-none cursor-pointer accent-cyan-400"
          />
           <span className="text-xs text-slate-400">{Math.round(hammerHardness * 100)}%</span>
        </div>

        {/* Thunder Trigger Button */}
        <div className="flex flex-col items-center justify-center">
            <button
                className="px-6 py-2 bg-red-900/50 hover:bg-red-700/50 border border-red-500 rounded text-red-200 transition-colors"
                onClick={() => updateParam('thunder_trigger', 1)}
            >
                STRIKE
            </button>
        </div>

      </div>

      <div className="mt-8 w-full h-32 border border-slate-700 rounded bg-slate-900/50 relative overflow-hidden">
        {/* Visualizer Placeholder */}
        <p className="absolute inset-0 flex items-center justify-center text-slate-600 pointer-events-none">
            Rain/Moisture Visualizer
        </p>
        <canvas id="rain-canvas" className="w-full h-full"></canvas>
      </div>
    </div>
  )
}

export default App

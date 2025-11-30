# Building Petrichor

## Prerequisites

1.  **Node.js**: Required to build the React frontend.
2.  **Visual Studio 2022** (Windows) or **Xcode** (macOS).
3.  **CMake**: Required to generate the project files.

## Quick Start (Recommended)

### Windows
1.  Double-click `setup_vs_project.bat`.
    *   This script will install npm dependencies, build the frontend, download JUCE, and generate a Visual Studio Solution (`.sln`).
2.  Open `Build/Petrichor.sln`.
3.  Set the configuration to **Release**.
4.  Right-click the `Petrichor_VST3` target and select **Build**.
5.  The plugin will be output to `Build/Petrichor_artefacts/Release/VST3/Petrichor.vst3`.

### macOS
1.  Run `./setup_mac_project.sh` in a terminal.
2.  Open `Build/Petrichor.xcodeproj`.
3.  Build the `Petrichor_VST3` target.

---

## Projucer (Legacy/Alternative)

A `Petrichor.jucer` file is included for reference. However, to use it, you must:
1.  Have a local copy of JUCE installed.
2.  Update the module paths in Projucer to point to your JUCE installation.
3.  Manually run `cd frontend && npm install && npm run build` BEFORE saving the project in Projucer, as the Jucer file expects `frontend/dist/index.html` to exist.

**We strongly recommend using the CMake workflow (Quick Start) as it handles dependencies automatically.**

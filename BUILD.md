# Building Petrichor

## Prerequisites

1.  **Node.js**: Required to build the React frontend.
2.  **Visual Studio 2022** (Windows) or **Xcode** (macOS).
    *   *Windows Note:* You **must** install the **"Desktop development with C++"** workload in the Visual Studio Installer.
3.  **CMake**: Required to generate the project files.

**Important:** This project relies on **JUCE 8.0.0+** features (`WebBrowserComponent::Options::withResourceProvider`) to function offline.
We strongly recommend using the provided CMake scripts, which automatically fetch the correct version of JUCE. Using an older system-installed JUCE (e.g. via Projucer) will cause build errors.

## Quick Start (Recommended)

### Windows
1.  Double-click `setup_vs_project.bat`.
    *   This script will install npm dependencies, build the frontend, download JUCE 8, and generate a Visual Studio Solution (`.sln`).
2.  Open `Build/Petrichor.sln`.
3.  Set the configuration to **Release**.
4.  Right-click the `Petrichor_VST3` target and select **Build**.
5.  The plugin will be output to `Build/Petrichor_artefacts/Release/VST3/Petrichor.vst3`.

### macOS
1.  Run `./setup_mac_project.sh` in a terminal.
2.  Open `Build/Petrichor.xcodeproj`.
3.  Build the `Petrichor_VST3` target.

## Troubleshooting

### "Could not find any instance of Visual Studio"
1.  Open **Visual Studio Installer**.
2.  Click **Modify**.
3.  Ensure **Desktop development with C++** is checked.

### "'withResourceProvider': is not a member of..." or JUCE Version Errors
This error occurs if you are building against an older version of JUCE (e.g., global install in `C:\Program Files\JUCE`).
*   **Solution**: Do not use Projucer or existing `.jucer` files. Use the `setup_vs_project.bat` script, which guarantees the correct JUCE 8 version is fetched and used locally.

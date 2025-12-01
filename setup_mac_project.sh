#!/bin/bash
set -e

echo "=========================================="
echo "Petrichor VST - macOS/Linux Setup Script"
echo "=========================================="

# 1. Check Node
if ! command -v node &> /dev/null; then
    echo "Error: Node.js is not installed."
    exit 1
fi

# 2. Build Frontend
echo ""
echo "[1/3] Building React Frontend..."
cd frontend
npm install
npm run build
cd ..

# 3. Embed Assets
echo ""
echo "[2/3] Embedding Frontend Assets..."
if command -v python3 &> /dev/null; then
    python3 scripts/embed_frontend.py
else
    echo "Error: python3 not found. Required for asset embedding."
    exit 1
fi

# 4. Check CMake
if ! command -v cmake &> /dev/null; then
    echo "Error: CMake is not installed."
    exit 1
fi

# 5. Generate Project
echo ""
echo "[3/3] Generating Project..."
mkdir -p Build
cd Build

if [[ "$OSTYPE" == "darwin"* ]]; then
    # Xcode
    cmake .. -G "Xcode"
    echo ""
    echo "Success! Open 'Build/Petrichor.xcodeproj'."
else
    # Linux (Makefiles or Ninja)
    cmake ..
    echo ""
    echo "Success! Run 'make' in the Build directory."
fi

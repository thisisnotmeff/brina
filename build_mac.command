#!/bin/bash
# Doppio clic per compilare Brina e installarlo in Ableton Live (macOS).
cd "$(dirname "$0")" || exit 1

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Manca Xcode. Installalo dall'App Store, aprilo una volta, poi rilancia questo script."
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "Manca CMake. Installalo con:  brew install cmake"
    echo "(oppure scaricalo da https://cmake.org/download/)"
    exit 1
fi

set -e
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --config Release --parallel

echo
echo "Fatto. I plugin sono stati installati in:"
echo "  ~/Library/Audio/Plug-Ins/VST3/        (Brina.vst3, Brina FX.vst3)"
echo "  ~/Library/Audio/Plug-Ins/Components/  (Brina.component, Brina FX.component)"
echo "Apri Ableton Live e fai una nuova scansione dei plugin."

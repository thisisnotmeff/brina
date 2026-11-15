# Brina — instructions for Claude

Ambient granular plugin for Ableton Live (macOS), inspired by GrainBrick.

## Language
- Chat with the maintainer: Italian.
- Everything in this repository is in English: documentation, code comments, commit messages, PR descriptions. Existing Italian comments and docs are being translated gradually; write all new text in English.

## Layout
- `Source/Engine.h` — all the DSP (grains, Drift note generator, tape, delay, shimmer reverb). Host-independent: no dependency on `AudioProcessor`.
- `Source/PluginProcessor.*` — parameters (`createLayout`), MIDI, file loading, recording (FX only, max 60 s), state saving.
- `Source/PluginEditor.*` — UI (clickable waveform; Grains / Drift / Tape / Delay / Reverb / Out sections).
- `Tests/EngineTest.cpp` (engine, no host) and `Tests/PluginTest.cpp` (plugin, run for both Brina and Brina FX).
- Two plugins from the same code, selected by `BRINA_FX` (0 = instrument "Brina", 1 = effect "Brina FX").

## Build and test (run before and after every change)
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBRINA_INSTALL=OFF -DBRINA_TESTS=ON
    cmake --build build --config Release --parallel 4
    ./build/BrinaTest_artefacts/Release/BrinaTest
    ./build/BrinaPluginTest_artefacts/Release/BrinaPluginTest /tmp
    ./build/BrinaFXPluginTest_artefacts/Release/BrinaFXPluginTest /tmp

- CI (`.github/workflows/build.yml`) does the same on macOS on every push and produces `Brina-macOS.zip` in Actions. Claude's Linux workspace cannot build the macOS targets (VST3/AU); the engine test (`BrinaTest`) can be built there, the full check is CI.
- Targets macOS 10.15, universal (arm64 + x86_64), C++17, JUCE 8.0.17-security via FetchContent.
- Never use `BRINA_INSTALL=ON` outside the maintainer's Mac (it copies into `~/Library/Audio/Plug-Ins`).

## Hard rules
1. **Audio thread**: no allocation, no blocking locks, no I/O. Sample hand-off uses only `SpinLock::ScopedTryLockType` (if the lock is busy, retry on the next block). Buffers are sized in `prepare` (`Engine::maxBlock = 2048`).
2. **Parameter compatibility**: IDs, order, ranges and defaults of existing parameters never change (it would break saved Live sets). New features are appended **at the end** of `createLayout`, with `ParameterID { id, 1 }`, and also at the end of the `raw` array. See `docs/parametri.md`.
3. **Saved state**: `getStateInformation` / `setStateInformation` must keep loading old sets (missing parameters fall back to defaults).
4. **Tests**: they must pass before every change. A new feature comes with a test (engine → `EngineTest.cpp`, parameters/state → `PluginTest.cpp`).
5. `Engine.h` stays host-independent. Current limits: 12 voices, 160 grains.
6. Claude cannot judge how it sounds: for DSP, check numerically peak, RMS, finite values (NaN/inf), denormals, feedback stability and tuning; the maintainer listens in Live.

## Conventions
- One branch per feature, small commits. Never work directly on `main`.
- Update `README.md` ("Come si usa" section) and `docs/parametri.md` whenever a parameter changes.

## Future ideas
- Requested features: multigrain, per-grain effects.
- iPad app for iOS 12.5.8: must be built with Xcode 16, manual install only.

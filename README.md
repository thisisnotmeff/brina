# Brina

Ambient granular instrument for Ableton Live (VST3 / AU, macOS). It turns a sample into textures
that evolve on their own. Inspired by the features of GrainBrick, written from scratch.

![Brina](screenshot.png)

Two plugins share the same engine:

| Plugin | Where it goes | Audio source |
|---|---|---|
| **Brina** | MIDI track (instrument) | audio file |
| **Brina FX** | audio track (effect) | audio file, or **Rec** of the track input (max 60 s) |

There are four audio sources, the **lanes**, all visible at once: each lane has its own sample
and its own grain controls, like four granulators in the same plugin.

## Installation

**Without compiling (GitHub):** push this folder to a GitHub repository. The *Actions* tab builds
it and produces `Brina-macOS.zip`. Copy the contents of `VST3` into
`~/Library/Audio/Plug-Ins/VST3/` and of `Components` into `~/Library/Audio/Plug-Ins/Components/`,
then unblock the downloaded files from Terminal:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Brina*.vst3 ~/Library/Audio/Plug-Ins/Components/Brina*.component

**Compiling on the Mac:** you need Xcode and CMake (`brew install cmake`). Double-click
`build_mac.command`: it builds the plugins and installs them in the right folders.

In Live: Settings > Plug-Ins > enable VST3 (and/or Audio Units) > *Rescan*.

**Updating to a new version:** quit Live before replacing the plugins. If Live scans while the
files are being copied, it marks the plugin as unloadable and never tries again: the files in the
zip always have the same date, so to Live they have not changed. If Brina does not show up after
an update, quit Live, refresh the files' date (use the folder you installed them in; for
`/Library/...` prepend `sudo`) and reopen Live with *Rescan*:

    find ~/Library/Audio/Plug-Ins/VST3/Brina*.vst3 ~/Library/Audio/Plug-Ins/Components/Brina*.component -exec touch {} +

## How to use

1. Drag an audio file onto a lane's waveform (or use its **Load**). In Brina FX you can press
   **Rec** to capture the audio passing through the track into that lane; grains already play
   while you record. Dragging several files at once fills the lanes one after another.
   **Clear** empties the lane.
2. With **Drift** on, the plugin plays by itself. Turn it off to play it over MIDI
   (C3 = original pitch). To send MIDI to Brina FX: create a MIDI track and set its *MIDI To*
   to the audio track.
3. Click a lane's waveform to choose where its grains are born.
4. At the top: **Playing / Stopped** starts and stops the plugin (when stopped no notes or grains
   are born and the tails fade out on their own); **Kill** silences everything at once, clearing
   notes, grains and the delay and reverb tails. In Brina FX the *Dry In* signal keeps passing
   in both cases.
5. **Position: Auto / Fixed** (at the top, for all lanes): in *Auto* the position moves on its
   own at each lane's *Scan* speed (as always); in *Fixed* it stays where you put it.

The three controls are parameters (*Play*, *Kill*, *Position Mode*), so they can be automated or
mapped to a controller in Live. *Kill* fires on every value change.

**Lanes** - each with its own controls: *Position* read point · *Spray* scatter around the point ·
*Size* grain length · *Density* grains per second · *Pitch* transposition · *Detune* random
detuning · *Octaves* probability of grains at ±1 octave · *Reverse* probability of reversed
grains · *Spread* stereo width · *Scan* speed at which the position moves on its own ·
*Attack / Release* note envelope · *Level* lane volume. An empty lane does not play and its
controls are dimmed, but it can be set up before loading the file.

The sections below the lanes apply to all of them.

**Voices** (multigrain) - *Voices* how many grains are born together for each note, from 1 to 8:
the voices fan out around *Position* and across the stereo field, while the level stays the
same · *Spread* distance between the voices' read points · *Intervals* tuning of the voices:
*Unison* (slightly detuned from each other), *Octaves*, *Fifths*, *Chord* (stacked thirds on the
Drift scale).

**Grain FX** - effects applied to individual grains, not to the whole sound. For each one, the
first control sets how many grains get it (0 = none, 1 = all; at half, about one grain in two,
chosen at random), and a grain can get more than one. *Filter* low-pass, with *Cutoff* (each
grain gets a slightly different cutoff) and *Reso* · *Crush* bit and sample-rate reduction, with
*Amount* · *Drive* saturation, with *Amount*.

**Drift** - note generator: a random walk on the chosen scale. *Rate* step (synced to Live's
tempo) · *Chance* probability of a note at each step · *Root / Scale* key · *Octave / Range*
register and span · *Length* note length in steps.

**Tape** saturation, wow & flutter, filter · **Delay** cross-fed stereo, the time "slides" like
tape · **Reverb** with *Shimmer* (octave up in the feedback) · **Out** *Dry In* (FX only: the
original signal) and *Volume*.

The samples of the four lanes are saved with the Live project: files as a path, recordings inside
the set (FLAC).

Sets saved with earlier versions open and sound as before: their sample is in lane 1, with a
single voice and no per-grain effects. The window can be resized by dragging its bottom-right
corner.

## Development

    cmake -S . -B build -DBRINA_INSTALL=OFF -DBRINA_TESTS=ON && cmake --build build
    ./build/BrinaTest_artefacts/Release/BrinaTest
    ./build/BrinaPluginTest_artefacts/Release/BrinaPluginTest /tmp
    ./build/BrinaFXPluginTest_artefacts/Release/BrinaFXPluginTest /tmp

`Source/Engine.h` holds all the DSP and does not depend on the host. Built with
[JUCE](https://juce.com) 8 (AGPLv3 / commercial license: fine for personal use; distributing the
plugin closed-source requires a JUCE license). Parameter list: `docs/parametri.md`.

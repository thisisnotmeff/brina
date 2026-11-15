# Brina parameters (untouchable: ID, order, range, default)

Source: `BrinaProcessor::createLayout()` in `Source/PluginProcessor.cpp`. Parameter version
(`ParameterID` version hint): 1 for the 35 original parameters, 2 for multigrain, per-grain
effects and lanes, 3 for grain shape, Morph and Weather.
"skew" = non-linear range, with the given centre value.

| ID | Name | Type | Range | Default | Section |
|---|---|---|---|---|---|
| position | Position | float | 0–1 | 0.3 | Grains (lane 1) |
| spray | Spray | float | 0–1 | 0.25 | Grains (lane 1) |
| size | Size | float (ms) | 10–1500, skew centre 200 | 220 | Grains (lane 1) |
| density | Density | float (gr/s) | 1–80, skew centre 15 | 18 | Grains (lane 1) |
| pitch | Pitch | float (st) | −24–24, step 0.01 | 0 | Grains (lane 1) |
| detune | Detune | float (ct) | 0–100 | 8 | Grains (lane 1) |
| octaves | Octaves | float | 0–1 | 0.15 | Grains (lane 1) |
| reverse | Reverse | float | 0–1 | 0.2 | Grains (lane 1) |
| spread | Spread | float | 0–1 | 0.8 | Grains (lane 1) |
| scan | Scan | float (x) | −2–2, step 0.001 | 0.05 | Grains (lane 1) |
| attack | Attack | float (ms) | 5–8000, skew centre 600 | 500 | Grains (lane 1) |
| release | Release | float (ms) | 20–15000, skew centre 2000 | 3000 | Grains (lane 1) |
| genOn | Drift On | bool | — | true | Drift |
| genRate | Drift Rate | choice | 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars | 3 (1/2) | Drift |
| genProb | Drift Chance | float | 0–1 | 0.6 | Drift |
| genRoot | Drift Root | choice | C, C#, D, D#, E, F, F#, G, G#, A, A#, B | 0 (C) | Drift |
| genScale | Drift Scale | choice | Major, Minor, Penta Maj, Penta Min, Dorian, Lydian, Whole Tone, Chromatic | 2 (Penta Maj) | Drift |
| genOctave | Drift Octave | int | −3–3 | 0 | Drift |
| genRange | Drift Range | int | 1–4 | 2 | Drift |
| genLength | Drift Length | float (steps) | 0.25–8, skew centre 2 | 3 | Drift |
| drive | Tape Drive | float | 0–1 | 0.2 | Tape |
| wow | Tape Wow | float | 0–1 | 0.25 | Tape |
| tone | Tape Tone | float (Hz) | 300–20000, skew centre 3000 | 9000 | Tape |
| delayTime | Delay Time | float (ms) | 20–2500, skew centre 400 | 380 | Delay |
| delayFeedback | Delay Feedback | float | 0–0.95 | 0.4 | Delay |
| delayMix | Delay Mix | float | 0–1 | 0.25 | Delay |
| reverbSize | Reverb Size | float | 0–1 | 0.85 | Reverb |
| reverbDamp | Reverb Damp | float | 0–1 | 0.4 | Reverb |
| shimmer | Shimmer | float | 0–1 | 0.25 | Reverb |
| reverbMix | Reverb Mix | float | 0–1 | 0.4 | Reverb |
| dry | Dry Input | float | 0–1 | 1 in FX, 0 in the instrument | Out |
| volume | Volume | float (dB) | −48–12, step 0.1 | 0 | Out |
| play | Play | bool | — | true | Controls |
| kill | Kill | bool | — | false | Controls |
| posMode | Position Mode | choice | Auto, Fixed | 0 (Auto) | Grains |


`play`, `kill` and `posMode` were added at the end of version 1. Everything below was added
afterwards, after `posMode`, with version hint 2.

## Version 2: multigrain, per-grain effects, lanes

| ID | Name | Type | Range | Default | Section |
|---|---|---|---|---|---|
| voices | Grain Voices | int | 1–8 | 1 | Voices |
| voiceSpread | Voice Spread | float | 0–1 | 0.3 | Voices |
| voiceMode | Voice Mode | choice | Unison, Octaves, Fifths, Chord | 0 (Unison) | Voices |
| gfxFilter | Grain Filter Chance | float | 0–1 | 0 | Grain FX |
| gfxFilterFreq | Grain Filter Cutoff | float (Hz) | 80–12000, skew centre 1200 | 1500 | Grain FX |
| gfxFilterRes | Grain Filter Reso | float | 0–1 | 0.3 | Grain FX |
| gfxCrush | Grain Crush Chance | float | 0–1 | 0 | Grain FX |
| gfxCrushAmt | Grain Crush Amount | float | 0–1 | 0.5 | Grain FX |
| gfxDrive | Grain Drive Chance | float | 0–1 | 0 | Grain FX |
| gfxDriveAmt | Grain Drive Amount | float | 0–1 | 0.5 | Grain FX |
| level | Level 1 | float | 0–1 | 1 | Lane 1 |
| position2 … release2 | Position 2 … Release 2 | | same as lane 1 | same as lane 1 | Lane 2 |
| level2 | Level 2 | float | 0–1 | 1 | Lane 2 |
| position3 … release3 | Position 3 … Release 3 | | same as lane 1 | same as lane 1 | Lane 3 |
| level3 | Level 3 | float | 0–1 | 1 | Lane 3 |
| position4 … release4 | Position 4 … Release 4 | | same as lane 1 | same as lane 1 | Lane 4 |
| level4 | Level 4 | float | 0–1 | 1 | Lane 4 |

Each lane block repeats the twelve grain controls of lane 1 in the same order (`position`,
`spray`, `size`, `density`, `pitch`, `detune`, `octaves`, `reverse`, `spread`, `scan`, `attack`,
`release`) with the lane number appended to ID and name; ranges and defaults are identical.
`BrinaProcessor::laneParamId (base, lane)` builds the ID. Total: 85 parameters.

With the defaults (one voice, no per-grain effect, lanes 2-4 empty) the engine output is
identical, sample for sample, to version 1. When an older state is loaded, parameters it does
not contain are reset to their defaults.

## Version 3: grain shape, Morph, Weather

Appended after `level4`, with version hint 3.

| ID | Name | Type | Range | Default | Section |
|---|---|---|---|---|---|
| grainShape | Grain Shape | choice | Swell, Twin, Tremolo, Pluck, Random | 0 (Swell) | Shape |
| shapeChance | Shape Chance | float | 0–1 | 0 | Shape |
| morphX | Morph X | float | 0–1 | 0.5 | Morph |
| morphY | Morph Y | float | 0–1 | 0.5 | Morph |
| morphAmt | Morph Amount | float | 0–1 | 0 | Morph |
| weatherAmt | Weather Amount | float | 0–1 | 0 | Weather |
| weatherRate | Weather Rate | float (s) | 30–1200, skew centre 180 | 180 | Weather |
| weatherMood | Weather Mood | choice | Calm, Breeze, Storm, Changing | 1 (Breeze) | Weather |

Total: 93 parameters. With `shapeChance`, `morphAmt` and `weatherAmt` at 0 (the defaults) the
engine output is identical, sample for sample, to version 2: no extra random number is drawn and
the Weather has its own random generator.

## Saved state

Besides the parameters, the state stores each lane's sample: lane 1 uses the original attributes
`samplePath` (file) and `recording` (FLAC, base64); lanes 2-4 append their number
(`samplePath2`, `recording3`, ...).

## Internal order

The `raw` struct (shared parameters) and the `laneRaw` array (one entry per lane) in
`PluginProcessor.h` are internal and can be reordered freely; only the order in `createLayout`
matters to hosts.
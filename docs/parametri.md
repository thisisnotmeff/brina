# Parametri di Brina (intoccabili: ID, ordine, intervalli, default)

Fonte: `BrinaProcessor::createLayout()` in `Source/PluginProcessor.cpp`. Versione parametro: 1 per tutti.
"skew" = intervallo non lineare, con il valore centrale indicato.

| ID | Nome | Tipo | Intervallo | Default | Sezione |
|---|---|---|---|---|---|
| position | Position | float | 0–1 | 0.3 | Grains |
| spray | Spray | float | 0–1 | 0.25 | Grains |
| size | Size | float (ms) | 10–1500, skew centro 200 | 220 | Grains |
| density | Density | float (gr/s) | 1–80, skew centro 15 | 18 | Grains |
| pitch | Pitch | float (st) | −24–24, passo 0.01 | 0 | Grains |
| detune | Detune | float (ct) | 0–100 | 8 | Grains |
| octaves | Octaves | float | 0–1 | 0.15 | Grains |
| reverse | Reverse | float | 0–1 | 0.2 | Grains |
| spread | Spread | float | 0–1 | 0.8 | Grains |
| scan | Scan | float (x) | −2–2, passo 0.001 | 0.05 | Grains |
| attack | Attack | float (ms) | 5–8000, skew centro 600 | 500 | Grains |
| release | Release | float (ms) | 20–15000, skew centro 2000 | 3000 | Grains |
| genOn | Drift On | bool | — | true | Drift |
| genRate | Drift Rate | choice | 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars | 3 (1/2) | Drift |
| genProb | Drift Chance | float | 0–1 | 0.6 | Drift |
| genRoot | Drift Root | choice | C, C#, D, D#, E, F, F#, G, G#, A, A#, B | 0 (C) | Drift |
| genScale | Drift Scale | choice | Major, Minor, Penta Maj, Penta Min, Dorian, Lydian, Whole Tone, Chromatic | 2 (Penta Maj) | Drift |
| genOctave | Drift Octave | int | −3–3 | 0 | Drift |
| genRange | Drift Range | int | 1–4 | 2 | Drift |
| genLength | Drift Length | float (steps) | 0.25–8, skew centro 2 | 3 | Drift |
| drive | Tape Drive | float | 0–1 | 0.2 | Tape |
| wow | Tape Wow | float | 0–1 | 0.25 | Tape |
| tone | Tape Tone | float (Hz) | 300–20000, skew centro 3000 | 9000 | Tape |
| delayTime | Delay Time | float (ms) | 20–2500, skew centro 400 | 380 | Delay |
| delayFeedback | Delay Feedback | float | 0–0.95 | 0.4 | Delay |
| delayMix | Delay Mix | float | 0–1 | 0.25 | Delay |
| reverbSize | Reverb Size | float | 0–1 | 0.85 | Reverb |
| reverbDamp | Reverb Damp | float | 0–1 | 0.4 | Reverb |
| shimmer | Shimmer | float | 0–1 | 0.25 | Reverb |
| reverbMix | Reverb Mix | float | 0–1 | 0.4 | Reverb |
| dry | Dry Input | float | 0–1 | 1 in FX, 0 nello strumento | Out |
| volume | Volume | float (dB) | −48–12, passo 0.1 | 0 | Out |
| play | Play | bool | — | true | Controlli |
| kill | Kill | bool | — | false | Controlli |
| posMode | Position Mode | choice | Auto, Fixed | 0 (Auto) | Grains |

Gli ultimi tre (`play`, `kill`, `posMode`) sono stati aggiunti in coda: le funzioni nuove vanno aggiunte allo stesso modo, dopo `posMode`.
Ordine nell'array `raw`: come in tabella.

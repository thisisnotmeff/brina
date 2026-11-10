# Brina

Strumento granulare ambient per Ableton Live (VST3 / AU, macOS). Trasforma un campione in
texture che evolvono da sole. Ispirato alle funzioni di GrainBrick, scritto da zero.

![Brina](screenshot.png)

Sono due plugin con lo stesso motore:

| Plugin | Dove va | Sorgente audio |
|---|---|---|
| **Brina** | traccia MIDI (strumento) | file audio |
| **Brina FX** | traccia audio (effetto) | file audio oppure **Rec** dell'ingresso della traccia (max 60 s) |

## Installazione

**Senza compilare (GitHub):** carica questa cartella in un repository GitHub. La scheda *Actions*
compila da sola e produce `Brina-macOS.zip`. Copia il contenuto di `VST3` in
`~/Library/Audio/Plug-Ins/VST3/` e di `Components` in `~/Library/Audio/Plug-Ins/Components/`,
poi sblocca i file scaricati dal Terminale:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Brina*.vst3 ~/Library/Audio/Plug-Ins/Components/Brina*.component

**Compilando sul Mac:** servono Xcode e CMake (`brew install cmake`). Doppio clic su
`build_mac.command`: compila e installa i plugin nelle cartelle giuste.

In Live: Impostazioni > Plug-Ins > attiva VST3 (e/o Audio Units) > *Rescan*.

## Come si usa

1. Trascina un file audio sulla forma d'onda (o **Load**). In Brina FX puoi premere **Rec**
   per catturare l'audio che passa nella traccia; mentre registri i grani suonano gia'.
2. Con **Drift** acceso il plugin suona da solo. Spegnilo per suonare tu via MIDI
   (Do3 = intonazione originale). Per mandare MIDI a Brina FX: crea una traccia MIDI e
   imposta *MIDI To* sulla traccia audio.
3. Clicca sulla forma d'onda per scegliere da dove nascono i grani.

**Grains** - *Position* punto di lettura · *Spray* dispersione attorno al punto · *Size* durata
del grano · *Density* grani al secondo · *Pitch* trasposizione · *Detune* stonatura casuale ·
*Octaves* probabilita' di grani a ±1 ottava · *Reverse* probabilita' di grani al contrario ·
*Spread* apertura stereo · *Scan* velocita' con cui la posizione scorre da sola ·
*Attack / Release* inviluppo delle note.

**Drift** - generatore di note: passeggiata casuale sulla scala scelta. *Rate* passo (legato al
tempo di Live) · *Chance* probabilita' di una nota a ogni passo · *Root / Scale* tonalita' ·
*Octave / Range* registro ed estensione · *Length* durata delle note in passi.

**Tape** saturazione, wow & flutter, filtro · **Delay** stereo incrociato, il tempo "scivola"
come un nastro · **Reverb** con *Shimmer* (ottava sopra nel feedback) · **Out** *Dry In*
(solo FX: segnale originale) e *Volume*.

Il campione viene salvato col progetto di Live: i file come percorso, le registrazioni dentro
il set (FLAC).

## Sviluppo

    cmake -S . -B build -DBRINA_INSTALL=OFF -DBRINA_TESTS=ON && cmake --build build
    ./build/BrinaTest_artefacts/Release/BrinaTest

`Source/Engine.h` contiene tutto il DSP e non dipende dall'host. Costruito con
[JUCE](https://juce.com) 8 (licenza AGPLv3 / commerciale: va bene per uso personale; per
distribuire il plugin a codice chiuso serve una licenza JUCE).

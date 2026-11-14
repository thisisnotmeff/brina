# Brina — istruzioni per Claude

Plugin granulare ambient per Ableton Live (macOS), ispirato a GrainBrick. Rispondi e commenta in italiano.

## Struttura
- `Source/Engine.h` — tutto il DSP (grani, generatore Drift, nastro, delay, riverbero shimmer). Indipendente dall'host: nessuna dipendenza da `AudioProcessor`.
- `Source/PluginProcessor.*` — parametri (`createLayout`), MIDI, caricamento file, registrazione (solo FX, max 60 s), salvataggio stato.
- `Source/PluginEditor.*` — interfaccia (forma d'onda cliccabile; sezioni Grains / Drift / Tape / Delay / Reverb / Out).
- `Tests/EngineTest.cpp` (motore, senza host) e `Tests/PluginTest.cpp` (plugin, eseguito per Brina e Brina FX).
- Due plugin dallo stesso codice, selezionati da `BRINA_FX` (0 = strumento "Brina", 1 = effetto "Brina FX").

## Build e test (da eseguire prima di ogni modifica e dopo)
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBRINA_INSTALL=OFF -DBRINA_TESTS=ON
    cmake --build build --config Release --parallel 4
    ./build/BrinaTest_artefacts/Release/BrinaTest
    ./build/BrinaPluginTest_artefacts/Release/BrinaPluginTest /tmp
    ./build/BrinaFXPluginTest_artefacts/Release/BrinaFXPluginTest /tmp

- La CI (`.github/workflows/build.yml`) fa lo stesso su macOS a ogni push e produce `Brina-macOS.zip` in Actions. Il workspace Linux di Claude non compila il target macOS (VST3/AU): per il motore si può compilare `BrinaTest`, la verifica completa è la CI.
- Target macOS 10.15, universal (arm64 + x86_64), C++17, JUCE 8.0.17-security via FetchContent.
- Non usare `BRINA_INSTALL=ON` fuori dal Mac dell'utente (copia in `~/Library/Audio/Plug-Ins`).

## Regole inviolabili
1. **Thread audio**: nessuna allocazione, nessun lock bloccante, niente I/O. Il passaggio del campione usa solo `SpinLock::ScopedTryLockType` (se il lock non è libero si riprova al blocco dopo). I buffer si dimensionano in `prepare` (`Engine::maxBlock = 2048`).
2. **Compatibilità dei parametri**: ID, ordine, intervalli e default dei parametri esistenti non cambiano mai (rompono i set salvati di Live). Le funzioni nuove si aggiungono **in coda** a `createLayout`, con `ParameterID { id, 1 }` e in coda anche all'array `raw`. Vedi `docs/parametri.md`.
3. **Stato salvato**: `getStateInformation` / `setStateInformation` devono continuare a leggere i set vecchi (parametri mancanti → default).
4. **Test**: devono passare prima di ogni modifica. Una funzione nuova porta con sé un test (motore → `EngineTest.cpp`, parametri/stato → `PluginTest.cpp`).
5. `Engine.h` resta indipendente dall'host. Limiti attuali: 12 voci, 160 grani.
6. Il risultato sonoro non è verificabile da Claude: per il DSP si controllano numericamente picco, RMS, valori finiti (NaN/inf), denormali, stabilità del feedback e intonazione; l'ascolto lo fa l'utente in Live.

## Convenzioni
- Un branch per funzione, commit piccoli. Non lavorare direttamente su `main`.
- Commenti nel codice in italiano, come quelli esistenti.
- Aggiorna `README.md` (sezione "Come si usa") e `docs/parametri.md` quando cambia un parametro.

## Idee future
- Funzioni richieste: multigrain, effetti per grano.
- Versione app iPad con iOS 12.5.8: va compilata con Xcode 16, solo installazione manuale.

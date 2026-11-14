// Brina - motore DSP: granulare, generatore di note "Deriva", nastro, delay, riverbero shimmer.
// Dipende solo da juce_audio_basics, cosi' si puo' provare anche fuori dal plugin.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace brina
{

//==============================================================================
/** Il campione da granulare. Sempre stereo; "length" cresce mentre si registra. */
struct SampleData : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> buffer;
    std::atomic<int> length { 0 };
    double sampleRate = 44100.0;
    juce::String name;
    juce::String filePath;      // vuoto se registrato dall'ingresso
    bool recordable = false;    // true solo per i buffer creati dal tasto REC
};

//==============================================================================
struct Params
{
    // Grani
    float position = 0.3f, spray = 0.1f, sizeMs = 180.0f, density = 18.0f;
    float pitch = 0.0f, detuneCents = 8.0f, octaves = 0.0f, reverse = 0.0f;
    float spread = 0.8f, scan = 0.0f, attackMs = 400.0f, releaseMs = 2500.0f;
    // Deriva (generatore)
    bool  genOn = true;
    int   genRate = 3, genRoot = 0, genScale = 2, genOctave = 0, genRange = 2;
    float genProb = 0.6f, genLength = 3.0f;
    // Nastro
    float drive = 0.2f, wow = 0.25f, toneHz = 9000.0f;
    // Delay
    float delayMs = 380.0f, delayFeedback = 0.4f, delayMix = 0.25f;
    // Riverbero
    float reverbSize = 0.85f, reverbDamp = 0.4f, shimmer = 0.25f, reverbMix = 0.4f;
    // Uscita
    float dry = 0.0f, volumeDb = 0.0f;
    // Trasporto e modo della posizione
    bool  running = true;         // false: niente note ne' grani nuovi, le code sfumano da sole
    bool  fixedPosition = false;  // true: la posizione resta ferma su Position (Scan ignorato)
    // Voci di grani (multigrain): a ogni nascita partono piu' grani insieme, uno per voce
    int   grainVoices = 1;        // 1..8
    float voiceSpread = 0.3f;     // distanza tra i punti di lettura delle voci
    int   voiceMode = 0;          // intervalli tra le voci, vedi voiceModeNames()
    // Sorgenti: fino a 4 slot audio, ogni grano sceglie da quale leggere
    int   sourceMode = 1;         // vedi sourceModeNames()
    // Effetti per grano: probabilita' che un grano nasca con l'effetto, piu' i parametri dell'effetto
    float gfxFilterChance = 0.0f, gfxFilterHz = 1500.0f, gfxFilterRes = 0.3f;
    float gfxCrushChance = 0.0f, gfxCrush = 0.5f;
    float gfxDriveChance = 0.0f, gfxDrive = 0.5f;
    // Forma del grano: probabilita' che un grano usi la forma scelta invece della solita Hann
    int   grainShape = 0;         // vedi shapeNames()
    float shapeChance = 0.0f;
    // Morph: i quattro slot stanno agli angoli di un pad XY; con probabilita' morphAmt il grano sceglie
    // lo slot secondo i pesi bilineari di (morphX, morphY), altrimenti secondo sourceMode
    float morphX = 0.5f, morphY = 0.5f, morphAmt = 0.0f;
    // Meteo: modulazione lenta di density, spray, size e tone (le manopole non si muovono)
    float weatherAmt = 0.0f, weatherRate = 180.0f;   // secondi per un ciclo lento
    int   weatherMood = 1;        // vedi weatherMoodNames()
};

inline const juce::StringArray& shapeNames()
{
    static const juce::StringArray n { "Swell", "Twin", "Tremolo", "Pluck", "Random" };
    return n;
}

inline const juce::StringArray& weatherMoodNames()
{
    static const juce::StringArray n { "Calm", "Breeze", "Storm", "Changing" };
    return n;
}

inline const juce::StringArray& rateNames()
{
    static const juce::StringArray n { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
    return n;
}

inline const juce::StringArray& scaleNames()
{
    static const juce::StringArray n { "Major", "Minor", "Penta Maj", "Penta Min",
                                       "Dorian", "Lydian", "Whole Tone", "Chromatic" };
    return n;
}

inline const juce::StringArray& voiceModeNames()
{
    static const juce::StringArray n { "Unison", "Octaves", "Fifths", "Chord" };
    return n;
}

inline const juce::StringArray& sourceModeNames()
{
    static const juce::StringArray n { "Random", "Cycle", "Per Voice" };
    return n;
}

/** Semitoni del grado \p degree (anche oltre l'ottava) nella scala \p scale di scaleNames(). */
inline int scaleSemitones (int scale, int degree)
{
    static const int scales[8][12] = {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 },
        { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 6, 8, 10 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    static const int scaleLen[8] = { 7, 7, 5, 5, 7, 7, 6, 12 };

    const int sc = juce::jlimit (0, 7, scale);
    const int d = juce::jmax (0, degree);
    return scales[sc][d % scaleLen[sc]] + 12 * (d / scaleLen[sc]);
}

inline const juce::StringArray& noteNames()
{
    static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}

//==============================================================================
/** Pitch shifter +1 ottava a due testine, usato nel feedback del riverbero. */
class OctaveUp
{
public:
    static constexpr int lookahead = 66;   // deve superare il chunk di elaborazione (64)

    void prepare (double sr)
    {
        window = juce::jmax (256, (int) (0.09 * sr));
        size = juce::nextPowerOfTwo (window + lookahead + 8);
        mask = size - 1;
        buf.assign ((size_t) size, 0.0f);
        writePos = 0;
        phase = 0.0f;
    }

    /** Legge il campione "offset" in avanti rispetto all'ultima scrittura. */
    float read (int offset)
    {
        const float p2 = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
        const float s = std::sin (juce::MathConstants<float>::pi * phase);
        const float g1 = s * s;
        const float out = g1 * tap (offset, (float) lookahead + (float) window * (1.0f - phase))
                        + (1.0f - g1) * tap (offset, (float) lookahead + (float) window * (1.0f - p2));
        phase += 1.0f / (float) window;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }

    void write (float x)
    {
        buf[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

private:
    float tap (int offset, float delay) const
    {
        const float rp = (float) (writePos + offset + size) - delay;
        const int i = (int) rp;
        const float f = rp - (float) i;
        return buf[(size_t) (i & mask)] * (1.0f - f) + buf[(size_t) ((i + 1) & mask)] * f;
    }

    std::vector<float> buf;
    int size = 0, mask = 0, writePos = 0, window = 256;
    float phase = 0.0f;
};

//==============================================================================
class Engine
{
public:
    static constexpr int maxVoices = 12;        // note suonate insieme
    static constexpr int maxGrainVoices = 8;    // grani che nascono insieme per ogni nota (multigrain)
    static constexpr int numSlots = 4;          // sorgenti audio
    static constexpr int maxGrains = 384;       // con otto voci; con una sola restano i 160 di sempre
    static constexpr int numVis = 96;
    static constexpr int maxBlock = 2048;

    /** Grani attivi, per l'interfaccia: slot + posizione (0..1), cioe' 2.25 = terzo slot a un quarto.
        -1 = vuoto. */
    std::array<std::atomic<float>, numVis> grainVis;
    std::atomic<float> scanVis { 0.0f };

    Engine()
    {
        for (auto& v : grainVis) v.store (-1.0f);
        buildWindows();
    }

    static constexpr int windowSize = 2048;
    static constexpr int numShapes = 5;     // tabella 0 = Hann, 1..4 = le forme di shapeNames() tranne Random

    /** Finestra del grano: 0 = Hann, 1 = Swell, 2 = Twin, 3 = Tremolo, 4 = Pluck. windowSize + 1 valori. */
    const float* windowTable (int shape) const { return windows[(size_t) juce::jlimit (0, numShapes - 1, shape)].data(); }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        mixL.assign (maxBlock, 0.0f);
        mixR.assign (maxBlock, 0.0f);

        wowSize = juce::nextPowerOfTwo ((int) (0.03 * sr) + 8);
        wowL.assign ((size_t) wowSize, 0.0f);
        wowR.assign ((size_t) wowSize, 0.0f);

        delaySize = juce::nextPowerOfTwo ((int) (2.6 * sr) + 8);
        delayL.assign ((size_t) delaySize, 0.0f);
        delayR.assign ((size_t) delaySize, 0.0f);

        reverb.setSampleRate (sr);
        shimL.prepare (sr);
        shimR.prepare (sr);
        killFade = juce::jmax (32, (int) (0.006 * sr));
        killLeft = -1;
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = {};
        for (auto& g : grains) g.active = false;
        std::fill (wowL.begin(), wowL.end(), 0.0f);
        std::fill (wowR.begin(), wowR.end(), 0.0f);
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        reverb.reset();
        shimL.prepare (sr);
        shimR.prepare (sr);
        wowPos = delayPos = 0;
        hpL = hpR = toneL = toneR = fbLpL = fbLpR = shLpL = shLpR = shHpL = shHpR = 0.0f;
        delaySmoothed = -1.0f;
        gainSmoothed = -1.0f;
        samplesToStep = 0.0;
        lastReverbSize = lastReverbDamp = -1.0f;
    }

    /** Fissa il seme dei numeri casuali: due motori con lo stesso seme e gli stessi parametri danno
        la stessa uscita, campione per campione. Serve ai test; di norma il seme e' casuale. */
    void setRandomSeed (juce::int64 seed) { rng.setSeed (seed); }

    //==========================================================================
    void noteOn (int note, float velocity, int lengthSamples = -1)
    {
        if (! running)   // fermo (Stop): le note vengono ignorate
            return;

        Voice* target = nullptr;

        for (auto& v : voices)
            if (v.active && v.note == note) { target = &v; break; }

        if (target == nullptr)
            for (auto& v : voices)
                if (! v.active) { target = &v; break; }

        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.age < target->age) target = &v;
        }

        const float keepEnv = (target->active && target->note == note) ? target->env : 0.0f;
        target->active = true;
        target->held = true;
        target->note = note;
        target->vel = velocity;
        target->env = keepEnv;
        target->untilGrain = 0.0;
        target->timed = lengthSamples;
        target->age = ++ageCounter;
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.note == note && v.timed < 0)
                v.held = false;
    }

    void allNotesOff()
    {
        for (auto& v : voices) v.held = false;
    }

    /** Kill: sfuma l'uscita in pochi millisecondi (niente click), poi azzera voci, grani e
        tutte le code (nastro, delay, riverbero). Il segnale Dry non viene toccato. */
    void kill()
    {
        if (killLeft < 0)
            killLeft = killFade;
    }

    //==========================================================================
    /** Un solo campione, nel primo slot. */
    void process (SampleData* sample, const float* inL, const float* inR,
                  float* outL, float* outR, int n, const Params& p, double bpm)
    {
        SampleData* slots[numSlots] = { sample, nullptr, nullptr, nullptr };
        processSlots (slots, inL, inR, outL, outR, n, p, bpm);
    }

    /** slots: numSlots puntatori, anche nullptr (slot vuoto). in* puo' essere nullptr (strumento).
        in e out non devono sovrapporsi. n <= maxBlock. */
    void processSlots (SampleData* const* slots, const float* inL, const float* inR,
                       float* outL, float* outR, int n, const Params& p, double bpm)
    {
        jassert (n <= maxBlock);
        std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
        std::fill (mixR.begin(), mixR.begin() + n, 0.0f);

        // --- sorgenti suonabili in questo blocco
        numPlayable = 0;
        refSlot = -1;

        for (int s = 0; s < numSlots; ++s)
        {
            auto* data = slots[s];
            slotData[(size_t) s] = data;
            slotLen[(size_t) s] = data != nullptr ? juce::jmin (data->length.load(), data->buffer.getNumSamples()) : 0;

            if (slotLen[(size_t) s] >= 256)
            {
                playable[(size_t) numPlayable++] = s;
                if (refSlot < 0) refSlot = s;   // la prima sorgente suonabile da' il passo allo Scan
            }
        }

        // --- start / stop
        if (p.running != running)
        {
            running = p.running;

            if (running)
                samplesToStep = 0.0;    // alla ripartenza il generatore riparte subito
            else
                for (auto& v : voices) { v.held = false; v.timed = -1; }   // le note sfumano col Release
        }

        // --- generatore + grani, spezzati sui passi del generatore
        const double stepSamples = stepLength (p, bpm);
        int done = 0;

        while (done < n)
        {
            if (samplesToStep <= 0.0)
            {
                if (p.genOn && running)
                    generatorStep (p, stepSamples);
                samplesToStep += stepSamples;
            }

            const int chunk = juce::jmin (n - done, juce::jmax (1, (int) std::ceil (samplesToStep)));
            renderGrains (mixL.data() + done, mixR.data() + done, chunk, p);
            samplesToStep -= chunk;
            done += chunk;
        }

        updateVis();
        effects (mixL.data(), mixR.data(), n, p);

        // --- uscita
        const float targetGain = juce::Decibels::decibelsToGain (p.volumeDb, -60.0f);
        if (gainSmoothed < 0.0f) gainSmoothed = targetGain;
        const float gainStep = (targetGain - gainSmoothed) / (float) n;

        for (int i = 0; i < n; ++i)
        {
            gainSmoothed += gainStep;
            float l = softLimit (mixL[(size_t) i]), r = softLimit (mixR[(size_t) i]);

            if (killLeft >= 0)
            {
                const float k = (float) killLeft / (float) killFade;
                l *= k;
                r *= k;
                if (killLeft > 0) --killLeft;
            }

            if (inL != nullptr) { l += p.dry * inL[i]; r += p.dry * (inR != nullptr ? inR[i] : inL[i]); }
            outL[i] = l * gainSmoothed;
            outR[i] = r * gainSmoothed;
        }

        if (killLeft == 0)   // dissolvenza finita: si azzera tutto (nessuna allocazione)
        {
            reset();
            killLeft = -1;
        }
    }

private:
    //==========================================================================
    struct Voice
    {
        bool active = false, held = false;
        int note = 60;
        float vel = 1.0f, env = 0.0f;
        double untilGrain = 0.0;
        int timed = -1;            // >0: nota del generatore, campioni rimasti
        juce::uint32 age = 0;
    };

    enum GrainFx : juce::uint8 { fxFilter = 1, fxCrush = 2, fxDrive = 4 };

    struct Grain
    {
        bool active = false;
        int slot = 0;
        const float* win = nullptr;    // tabella della finestra, scelta alla nascita
        double pos = 0.0, inc = 1.0;
        float winPhase = 0.0f, winInc = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
        int remaining = 0, delay = 0;

        // effetti del grano, decisi alla nascita (maschera di GrainFx)
        juce::uint8 fx = 0;
        float fA1 = 0.0f, fA2 = 0.0f, fA3 = 0.0f, fGain = 1.0f;     // passa-basso a variabili di stato
        float s1L = 0.0f, s2L = 0.0f, s1R = 0.0f, s2R = 0.0f;
        float crushStep = 0.0f, crushInv = 0.0f, holdL = 0.0f, holdR = 0.0f;   // riduzione di bit + sample & hold
        int holdLen = 1, holdLeft = 0;
        float driveK = 1.0f, driveMakeup = 1.0f;
    };

    //==========================================================================
    double stepLength (const Params& p, double bpm) const
    {
        static const double beats[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        const double b = beats[juce::jlimit (0, 6, p.genRate)];
        return juce::jmax (64.0, sr * 60.0 / juce::jlimit (20.0, 400.0, bpm) * b);
    }

    void generatorStep (const Params& p, double stepSamples)
    {
        static const int scaleLen[8] = { 7, 7, 5, 5, 7, 7, 6, 12 };

        if (rng.nextFloat() >= p.genProb)
            return;

        const int sc = juce::jlimit (0, 7, p.genScale);
        const int degrees = scaleLen[sc];
        const int top = degrees * juce::jlimit (1, 4, p.genRange);

        // passeggiata casuale sui gradi della scala: movimenti piccoli, ogni tanto un salto
        int move = rng.nextInt (5) - 2;
        if (move == 0) move = rng.nextBool() ? 1 : -1;
        if (rng.nextFloat() < 0.12f) move *= 3;
        genDegree += move;
        if (genDegree < 0) genDegree = -genDegree;
        if (genDegree > top) genDegree = juce::jmax (0, 2 * top - genDegree);

        auto toNote = [&] (int degree)
        {
            const int n = 60 + p.genRoot + 12 * p.genOctave + scaleSemitones (sc, degree);
            return juce::jlimit (0, 127, n);
        };

        const int length = (int) juce::jmax (64.0, stepSamples * p.genLength * (0.75 + 0.5 * rng.nextDouble()));
        noteOn (toNote (genDegree), 0.55f + 0.45f * rng.nextFloat(), length);

        if (rng.nextFloat() < 0.22f)   // ogni tanto una seconda nota, una terza o una quinta sopra
            noteOn (toNote (genDegree + (rng.nextBool() ? 2 : 4)), 0.4f + 0.3f * rng.nextFloat(), length);
    }

    //==========================================================================
    /** Intervallo in semitoni della voce di grani k rispetto alla nota (la voce 0 resta sulla nota). */
    static float voiceInterval (int k, const Params& p)
    {
        static const float octaves[maxGrainVoices] = { 0, 12, -12, 24, 0, 12, -12, 24 };
        static const float fifths[maxGrainVoices]  = { 0, 7, 12, 19, -12, -5, 24, 14 };

        if (k <= 0)
            return 0.0f;

        k = juce::jmin (k, maxGrainVoices - 1);
        const float unison = (k & 1 ? 1.0f : -1.0f) * (float) ((k + 1) / 2) * 0.07f;   // +-7, 14, 21, 28 cent

        switch (p.voiceMode)
        {
            case 1:  return octaves[k] + (k >= 4 ? unison : 0.0f);         // i doppioni un filo stonati
            case 2:  return fifths[k];
            case 3:  return (float) scaleSemitones (p.genScale, 2 * k);    // terze sovrapposte sulla scala di Drift
            default: return unison;
        }
    }

    /** Lo slot da cui legge il prossimo grano della voce k, -1 se non c'e' niente da suonare. */
    int pickSlot (int k, const Params& p)
    {
        if (numPlayable <= 1)
            return numPlayable == 1 ? playable[0] : -1;

        // Morph: a probabilita' zero non si estrae nessun numero, la sequenza resta quella di prima
        if (p.morphAmt > 0.0f && rng.nextFloat() < p.morphAmt)
        {
            const int slot = morphSlot (p);
            if (slot >= 0)
                return slot;
        }

        switch (p.sourceMode)
        {
            case 0:  return playable[(size_t) rng.nextInt (numPlayable)];
            case 2:  return playable[(size_t) (k % numPlayable)];
            default: return playable[(size_t) (cycle++ % (juce::uint32) numPlayable)];
        }
    }

public:
    /** Peso dello slot \p slot nel pad del Morph: 1 in alto a sinistra, 2 in alto a destra,
        3 in basso a sinistra, 4 in basso a destra (Y = 1 in alto). */
    static float morphWeight (int slot, float x, float y)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        y = juce::jlimit (0.0f, 1.0f, y);
        const float wx = (slot & 1) != 0 ? x : 1.0f - x;
        const float wy = slot < 2 ? y : 1.0f - y;
        return wx * wy;
    }

private:
    /** Lo slot scelto dai pesi del pad, solo tra quelli suonabili; -1 se pesano tutti zero
        (il punto e' sull'angolo di uno slot vuoto): allora decide Source Mode. */
    int morphSlot (const Params& p)
    {
        float total = 0.0f;
        for (int i = 0; i < numPlayable; ++i)
            total += morphWeight (playable[(size_t) i], p.morphX, p.morphY);

        if (total <= 1e-6f)
            return -1;

        float r = rng.nextFloat() * total;
        int lastWeighted = -1;
        for (int i = 0; i < numPlayable; ++i)
        {
            const int slot = playable[(size_t) i];
            const float w = morphWeight (slot, p.morphX, p.morphY);
            if (w <= 0.0f)
                continue;

            lastWeighted = slot;
            r -= w;
            if (r < 0.0f)
                return slot;
        }

        return lastWeighted;   // arrotondamenti: l'ultimo slot con un peso
    }

    /** Una nascita: un grano per voce. La voce 0 parte subito, le altre si distribuiscono
        nell'intervallo fino alla nascita successiva, cosi' la nuvola resta fitta e non pulsa. */
    void spawnGrains (const Voice& v, int offset, const Params& p, float norm, double interval)
    {
        const int count = juce::jlimit (1, maxGrainVoices, p.grainVoices);

        for (int k = 0; k < count; ++k)
        {
            const int stagger = k == 0 ? 0 : (int) (interval * ((double) k + rng.nextDouble() - 0.5) / (double) count);
            spawnGrain (v, k, count, offset + stagger, p, norm);
        }
    }

    void spawnGrain (const Voice& v, int k, int count, int offset, const Params& p, float norm)
    {
        // il tetto dei grani cresce con le voci: con una voce e' quello di prima (stesso suono e
        // stesso carico anche nei set piu' densi), con otto arriva a maxGrains
        const int limit = juce::jmin (maxGrains, 160 + 32 * (count - 1));

        Grain* g = nullptr;
        for (int i = 0; i < limit; ++i)
            if (! grains[(size_t) i].active) { g = &grains[(size_t) i]; break; }
        if (g == nullptr)
            return;

        const int slot = pickSlot (k, p);
        if (slot < 0)
            return;

        const SampleData& sample = *slotData[(size_t) slot];
        const int len = slotLen[(size_t) slot];

        // Con una voce sola, un solo slot e nessun effetto, i numeri casuali vengono estratti nello
        // stesso ordine di prima: i set salvati suonano identici.
        float semis = (float) (v.note - 60) + p.pitch + voiceInterval (k, p)
                    + (rng.nextFloat() * 2.0f - 1.0f) * p.detuneCents * 0.01f;

        if (rng.nextFloat() < p.octaves)
        {
            const float r = rng.nextFloat();
            semis += r < 0.6f ? 12.0f : (r < 0.9f ? -12.0f : 24.0f);
        }

        double inc = std::pow (2.0, (double) semis / 12.0) * sample.sampleRate / sr;
        inc = juce::jlimit (0.02, 16.0, inc);

        // le voci si aprono a ventaglio attorno a Position (-0.5 .. +0.5) e nello stereo
        const float fan = count > 1 ? (float) k / (float) (count - 1) - 0.5f : 0.0f;

        const int grainLen = juce::jmax (32, (int) (p.sizeMs * 0.001 * sr));
        const double spray = (double) p.spray * p.spray * 0.5 * len * (rng.nextDouble() * 2.0 - 1.0);
        double pos = (double) wrap01 (p.position + (float) scanPhase + fan * p.voiceSpread * 0.5f) * len + spray;
        pos = std::fmod (pos, (double) len);
        if (pos < 0.0) pos += len;

        if (rng.nextFloat() < p.reverse)
        {
            pos = std::fmod (pos + inc * grainLen, (double) len);
            inc = -inc;
        }

        const float scatter = (rng.nextFloat() - 0.5f) * p.spread;
        const float pan = count > 1 ? juce::jlimit (0.0f, 1.0f, 0.5f + fan * 0.7f * p.spread + scatter * 0.6f)
                                    : 0.5f + scatter;
        const float amp = v.vel * v.env * norm;

        g->active = true;
        g->slot = slot;
        g->pos = pos;
        g->inc = inc;
        g->winPhase = 0.0f;
        g->winInc = (float) windowSize / (float) grainLen;
        g->gainL = amp * std::cos (pan * juce::MathConstants<float>::halfPi);
        g->gainR = amp * std::sin (pan * juce::MathConstants<float>::halfPi);
        g->remaining = grainLen;
        g->delay = offset;
        g->win = windows[0].data();
        assignGrainFx (*g, p);
        assignShape (*g, p);
    }

    /** Con probabilita' shapeChance il grano prende la forma scelta (Random: una delle quattro a caso).
        A probabilita' zero non si estrae nessun numero: la sequenza casuale resta quella di prima. */
    void assignShape (Grain& g, const Params& p)
    {
        if (p.shapeChance <= 0.0f || rng.nextFloat() >= p.shapeChance)
            return;

        const int choice = juce::jlimit (0, numShapes - 1, p.grainShape);   // 4 = Random
        const int table = choice < numShapes - 1 ? choice + 1 : 1 + rng.nextInt (numShapes - 1);
        g.win = windows[(size_t) table].data();
    }

    /** Le finestre partono e finiscono a zero (niente click) e hanno la stessa energia della Hann,
        cosi' cambiare forma non cambia il volume. */
    void buildWindows()
    {
        const float pi = juce::MathConstants<float>::pi;
        auto hannAt = [pi] (float x) { const float s = std::sin (pi * x); return s * s; };

        // sale piano e cade in fretta (o il contrario): una mezza Hann per lato
        auto asym = [pi] (float x, float peak)
        {
            if (x < peak) return 0.5f - 0.5f * std::cos (pi * x / peak);
            return 0.5f + 0.5f * std::cos (pi * (x - peak) / (1.0f - peak));
        };

        for (int i = 0; i <= windowSize; ++i)
        {
            const float x = (float) i / (float) windowSize;
            const float hann = hannAt (x);
            const float pluckTail = x < 0.03f ? 0.0f : (1.0f - (x - 0.03f) / 0.97f);

            // la Hann con la stessa formula di sempre: i set salvati suonano identici, bit per bit
            windows[0][(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) windowSize);
            windows[1][(size_t) i] = asym (x, 0.85f);                                                    // Swell
            windows[2][(size_t) i] = hann * (0.3f + 0.7f * hannAt (2.0f * x));                           // Twin
            windows[3][(size_t) i] = hann * (0.55f + 0.45f * std::cos (juce::MathConstants<float>::twoPi * 6.0f * x)); // Tremolo
            windows[4][(size_t) i] = x < 0.03f ? asym (x / 0.03f * 0.5f, 0.5f)                          // Pluck
                                               : pluckTail * pluckTail * pluckTail;
        }

        windows[1][0] = windows[1][(size_t) windowSize] = 0.0f;
        windows[4][(size_t) windowSize] = 0.0f;

        auto energy = [] (const std::array<float, windowSize + 1>& w)
        {
            double sum = 0.0;
            for (auto v : w) sum += (double) v * v;
            return sum;
        };

        const double reference = energy (windows[0]);
        for (int s = 1; s < numShapes; ++s)
        {
            const float gain = (float) std::sqrt (reference / energy (windows[(size_t) s]));
            for (auto& v : windows[(size_t) s]) v *= gain;
        }
    }

    void assignGrainFx (Grain& g, const Params& p)
    {
        g.fx = 0;

        if (p.gfxFilterChance > 0.0f && rng.nextFloat() < p.gfxFilterChance)
        {
            // ogni grano filtrato ha un taglio un po' diverso (+-mezza ottava): suona piu' vivo
            const float hz = juce::jlimit (30.0f, 0.45f * (float) sr, p.gfxFilterHz * std::exp2 (rng.nextFloat() - 0.5f));
            const float k = 2.0f - 1.75f * juce::jlimit (0.0f, 1.0f, p.gfxFilterRes);
            const float gc = std::tan (juce::MathConstants<float>::pi * hz / (float) sr);
            g.fA1 = 1.0f / (1.0f + gc * (gc + k));
            g.fA2 = gc * g.fA1;
            g.fA3 = gc * g.fA2;
            g.fGain = std::sqrt (k * 0.5f);   // compensa il picco della risonanza
            g.s1L = g.s2L = g.s1R = g.s2R = 0.0f;
            g.fx |= fxFilter;
        }

        if (p.gfxCrushChance > 0.0f && rng.nextFloat() < p.gfxCrushChance)
        {
            const float amount = juce::jlimit (0.0f, 1.0f, p.gfxCrush);
            g.crushStep = 2.0f / std::exp2 (16.0f - 13.0f * amount);       // da 16 a 3 bit
            g.crushInv = 1.0f / g.crushStep;
            g.holdLen = 1 + juce::roundToInt (amount * amount * 24.0f * (float) sr / 44100.0f);
            g.holdLeft = 0;
            g.holdL = g.holdR = 0.0f;
            g.fx |= fxCrush;
        }

        if (p.gfxDriveChance > 0.0f && rng.nextFloat() < p.gfxDriveChance)
        {
            g.driveK = 1.0f + 11.0f * juce::jlimit (0.0f, 1.0f, p.gfxDrive);
            g.driveMakeup = 1.0f / std::pow (g.driveK, 0.6f);
            g.fx |= fxDrive;
        }
    }

    /** Arrotonda al gradino piu' vicino. I valori sono piccoli (|x| / step < 2^16), il cast basta. */
    static float quantise (float x, float step, float inv)
    {
        const float q = x * inv;
        return (float) (int) (q + (q >= 0.0f ? 0.5f : -0.5f)) * step;
    }

    /** Saturazione morbida, molto vicina a tanh ma senza funzioni lente: gira per ogni campione di
        ogni grano. Arriva esattamente a +-1 per |x| >= 3. */
    static float softClip (float x)
    {
        x = juce::jlimit (-3.0f, 3.0f, x);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** Catena del grano: crush -> drive -> filtro. Viene prima della finestra, cosi' non fa click. */
    static void grainFx (Grain& g, float& l, float& r)
    {
        if (g.fx & fxCrush)
        {
            if (--g.holdLeft <= 0)
            {
                g.holdLeft = g.holdLen;
                g.holdL = quantise (l, g.crushStep, g.crushInv);
                g.holdR = quantise (r, g.crushStep, g.crushInv);
            }

            l = g.holdL;
            r = g.holdR;
        }

        if (g.fx & fxDrive)
        {
            l = softClip (l * g.driveK) * g.driveMakeup;
            r = softClip (r * g.driveK) * g.driveMakeup;
        }

        if (g.fx & fxFilter)
        {
            auto svf = [&g] (float x, float& s1, float& s2)
            {
                const float v3 = x - s2;
                const float v1 = g.fA1 * s1 + g.fA2 * v3;
                const float v2 = s2 + g.fA2 * s1 + g.fA3 * v3;
                s1 = 2.0f * v1 - s1;
                s2 = 2.0f * v2 - s2;
                return v2 * g.fGain;
            };

            l = svf (l, g.s1L, g.s2L);
            r = svf (r, g.s1R, g.s2R);
        }
    }

    void renderGrains (float* outL, float* outR, int n, const Params& p)
    {
        const bool canPlay = numPlayable > 0;
        const double interval = sr / juce::jmax (0.5f, p.density);
        // piu' voci = piu' grani sovrapposti: il livello resta quello di una voce sola
        const float voicesNorm = 1.0f / std::sqrt ((float) juce::jlimit (1, maxGrainVoices, p.grainVoices));
        const float norm = voicesNorm * 0.6f / std::sqrt (juce::jmax (1.0f, p.density * p.sizeMs * 0.001f));
        const float attackStep = 1.0f / juce::jmax (1.0f, p.attackMs * 0.001f * (float) sr);
        const float releaseStep = 1.0f / juce::jmax (1.0f, p.releaseMs * 0.001f * (float) sr);

        if (p.fixedPosition)
            scanPhase = 0.0;    // posizione fissa: i grani nascono esattamente su Position

        // --- voci: inviluppo e nascita dei grani
        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int i = 0; i < n; ++i)
            {
                if (v.timed > 0 && --v.timed == 0)
                {
                    v.held = false;
                    v.timed = -1;
                }

                if (v.held)
                    v.env = juce::jmin (1.0f, v.env + attackStep);
                else
                {
                    v.env -= releaseStep;
                    if (v.env <= 0.0f) { v = {}; break; }
                }

                v.untilGrain -= 1.0;
                if (v.untilGrain <= 0.0)
                {
                    if (canPlay)
                        spawnGrains (v, i, p, norm, interval);
                    v.untilGrain += interval * (0.5 + rng.nextDouble());
                }
            }
        }

        if (canPlay && running && ! p.fixedPosition)
        {
            scanPhase += (double) p.scan * n * slotData[(size_t) refSlot]->sampleRate / (sr * slotLen[(size_t) refSlot]);
            scanPhase -= std::floor (scanPhase);
        }

        // --- grani
        if (! canPlay)
        {
            for (auto& g : grains) g.active = false;
            return;
        }

        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            const int len = slotLen[(size_t) g.slot];
            const double dlen = (double) len;

            // slot svuotato, oppure campione cambiato con uno piu' corto: il grano si spegne
            if (len < 256 || g.pos >= dlen) { g.active = false; continue; }

            const auto& buf = slotData[(size_t) g.slot]->buffer;
            const float* srcL = buf.getReadPointer (0);
            const float* srcR = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);

            int i = g.delay;
            g.delay = 0;
            if (i >= n) { g.delay = i - n; continue; }

            for (; i < n && g.remaining > 0; ++i, --g.remaining)
            {
                const int wi = (int) g.winPhase;
                const float w = g.win[juce::jmin (wi, windowSize)];

                const int i1 = (int) g.pos;
                const float f = (float) (g.pos - (double) i1);
                int i0 = i1 - 1, i2 = i1 + 1, i3 = i1 + 2;
                if (i0 < 0) i0 += len;
                if (i2 >= len) i2 -= len;
                if (i3 >= len) i3 -= len;

                float sl = hermite (srcL[i0], srcL[i1], srcL[i2], srcL[i3], f);
                float sr2 = hermite (srcR[i0], srcR[i1], srcR[i2], srcR[i3], f);

                if (g.fx != 0)
                    grainFx (g, sl, sr2);

                outL[i] += g.gainL * w * sl;
                outR[i] += g.gainR * w * sr2;

                g.winPhase += g.winInc;
                g.pos += g.inc;
                if (g.pos >= dlen) g.pos -= dlen;
                else if (g.pos < 0.0) g.pos += dlen;
            }

            if (g.remaining <= 0)
                g.active = false;
        }
    }

    static float hermite (float y0, float y1, float y2, float y3, float f)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    /** Trasparente fino a 0.75, poi piega dolcemente: l'uscita dei grani non supera mai 1.5. */
    static float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.75f) return x;
        return std::copysign (0.75f + 0.75f * std::tanh ((a - 0.75f) / 0.75f), x);
    }

    static float wrap01 (float x) { return x - std::floor (x); }

    void updateVis()
    {
        int index = 0;

        for (auto& g : grains)
        {
            if (index >= numVis)
                break;

            const int len = slotLen[(size_t) g.slot];

            if (g.active && len > 0)
                grainVis[(size_t) index++].store ((float) g.slot + juce::jlimit (0.0f, 0.999f, (float) (g.pos / (double) len)),
                                                  std::memory_order_relaxed);
        }

        while (index < numVis)
            grainVis[(size_t) index++].store (-1.0f, std::memory_order_relaxed);

        scanVis.store ((float) scanPhase, std::memory_order_relaxed);
    }

    //==========================================================================
    void effects (float* l, float* r, int n, const Params& p)
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float fsr = (float) sr;

        // --- nastro: saturazione, wow & flutter, tono
        const float g = 1.0f + p.drive * 7.0f;
        const float makeup = 1.0f / std::pow (g, 0.6f);
        const float toneCoef = 1.0f - std::exp (-twoPi * juce::jlimit (200.0f, 20000.0f, p.toneHz) / fsr);
        const float wowDepth = p.wow * 0.003f * fsr, flutterDepth = p.wow * 0.0003f * fsr;
        const float base = 0.004f * fsr + 2.0f;
        const int wowMask = wowSize - 1;

        const float hpCoef = 1.0f - std::exp (-twoPi * 25.0f / fsr);

        for (int i = 0; i < n; ++i)
        {
            // passa-alto a 25 Hz: toglie la continua prima di saturazione e riverbero
            hpL += hpCoef * (l[i] - hpL);
            hpR += hpCoef * (r[i] - hpR);
            wowL[(size_t) wowPos] = std::tanh ((l[i] - hpL) * g) * makeup;
            wowR[(size_t) wowPos] = std::tanh ((r[i] - hpR) * g) * makeup;

            const float d = base + wowDepth * std::sin (wowPhase) + flutterDepth * std::sin (flutterPhase);
            const float rp = (float) (wowPos + wowSize) - d;
            const int ri = (int) rp;
            const float f = rp - (float) ri;
            const size_t a = (size_t) (ri & wowMask), b = (size_t) ((ri + 1) & wowMask);

            toneL += toneCoef * (wowL[a] * (1.0f - f) + wowL[b] * f - toneL);
            toneR += toneCoef * (wowR[a] * (1.0f - f) + wowR[b] * f - toneR);
            l[i] = toneL;
            r[i] = toneR;

            wowPos = (wowPos + 1) & wowMask;
            wowPhase += twoPi * 0.55f / fsr;
            flutterPhase += twoPi * 6.7f / fsr;
            if (wowPhase > twoPi) wowPhase -= twoPi;
            if (flutterPhase > twoPi) flutterPhase -= twoPi;
        }

        // --- delay stereo incrociato; il tempo scivola come un nastro quando cambia
        const float targetDelay = juce::jlimit (16.0f, (float) delaySize - 8.0f, p.delayMs * 0.001f * fsr);
        if (delaySmoothed < 0.0f) delaySmoothed = targetDelay;
        const float slide = 1.0f - std::exp (-1.0f / (0.15f * fsr));
        const float fbCoef = 1.0f - std::exp (-twoPi * 4500.0f / fsr);
        const float fb = juce::jlimit (0.0f, 0.95f, p.delayFeedback);
        const int dMask = delaySize - 1;

        for (int i = 0; i < n; ++i)
        {
            delaySmoothed += slide * (targetDelay - delaySmoothed);
            const float rp = (float) (delayPos + delaySize) - delaySmoothed;
            const int ri = (int) rp;
            const float f = rp - (float) ri;
            const size_t a = (size_t) (ri & dMask), b = (size_t) ((ri + 1) & dMask);
            const float dl = delayL[a] * (1.0f - f) + delayL[b] * f;
            const float dr = delayR[a] * (1.0f - f) + delayR[b] * f;

            fbLpL += fbCoef * (dr - fbLpL);
            fbLpR += fbCoef * (dl - fbLpR);
            delayL[(size_t) delayPos] = std::tanh (l[i] + fb * fbLpL);
            delayR[(size_t) delayPos] = std::tanh (r[i] + fb * fbLpR);
            delayPos = (delayPos + 1) & dMask;

            l[i] += p.delayMix * dl;
            r[i] += p.delayMix * dr;
        }

        // --- riverbero con shimmer (ottava sopra nel feedback)
        if (p.reverbSize != lastReverbSize || p.reverbDamp != lastReverbDamp)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.3f + 0.66f * juce::jlimit (0.0f, 1.0f, p.reverbSize);
            rp.damping = juce::jlimit (0.0f, 1.0f, p.reverbDamp);
            rp.wetLevel = 0.33f;
            rp.dryLevel = 0.0f;
            rp.width = 1.0f;
            reverb.setParameters (rp);
            lastReverbSize = p.reverbSize;
            lastReverbDamp = p.reverbDamp;
        }

        const float shCoef = 1.0f - std::exp (-twoPi * 5000.0f / fsr);
        const float shHpCoef = 1.0f - std::exp (-twoPi * 150.0f / fsr);
        const float shAmount = juce::jlimit (0.0f, 1.0f, p.shimmer) * 0.55f;
        float wetL[64], wetR[64];

        for (int start = 0; start < n; start += 64)
        {
            const int c = juce::jmin (64, n - start);

            for (int i = 0; i < c; ++i)
            {
                // il ritorno shimmer e' filtrato a banda (150 Hz - 5 kHz): senza il passa-alto
                // l'anello si blocca in continua, senza il passa-basso diventa stridulo
                shLpL += shCoef * (shimL.read (i) - shLpL);
                shLpR += shCoef * (shimR.read (i) - shLpR);
                shHpL += shHpCoef * (shLpL - shHpL);
                shHpR += shHpCoef * (shLpR - shHpR);
                wetL[i] = l[start + i] + std::tanh (shAmount * (shLpL - shHpL));
                wetR[i] = r[start + i] + std::tanh (shAmount * (shLpR - shHpR));
            }

            reverb.processStereo (wetL, wetR, c);

            for (int i = 0; i < c; ++i)
            {
                shimL.write (wetL[i]);
                shimR.write (wetR[i]);
                l[start + i] = l[start + i] * (1.0f - 0.6f * p.reverbMix) + wetL[i] * p.reverbMix;
                r[start + i] = r[start + i] * (1.0f - 0.6f * p.reverbMix) + wetR[i] * p.reverbMix;
            }
        }
    }

    //==========================================================================
    std::array<std::array<float, windowSize + 1>, numShapes> windows {};

    double sr = 44100.0;
    std::array<Voice, maxVoices> voices;
    std::array<Grain, maxGrains> grains;

    // sorgenti del blocco in corso (valide solo dentro processSlots)
    std::array<SampleData*, numSlots> slotData {};
    std::array<int, numSlots> slotLen {};
    std::array<int, numSlots> playable {};
    int numPlayable = 0, refSlot = -1;
    juce::uint32 cycle = 0;
    juce::uint32 ageCounter = 0;
    juce::Random rng;
    double scanPhase = 0.0, samplesToStep = 0.0;
    int genDegree = 4;
    bool running = true;
    int killFade = 256, killLeft = -1;   // killLeft >= 0: dissolvenza del Kill in corso

    std::vector<float> mixL, mixR;
    std::vector<float> wowL, wowR, delayL, delayR;
    int wowSize = 0, wowPos = 0, delaySize = 0, delayPos = 0;
    float wowPhase = 0.0f, flutterPhase = 0.0f, toneL = 0.0f, toneR = 0.0f, hpL = 0.0f, hpR = 0.0f;
    float fbLpL = 0.0f, fbLpR = 0.0f, delaySmoothed = -1.0f, gainSmoothed = -1.0f;

    juce::Reverb reverb;
    OctaveUp shimL, shimR;
    float shLpL = 0.0f, shLpR = 0.0f, shHpL = 0.0f, shHpR = 0.0f, lastReverbSize = -1.0f, lastReverbDamp = -1.0f;
};

} // namespace brina

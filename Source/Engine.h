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
};

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
    static constexpr int maxVoices = 12;
    static constexpr int maxGrains = 160;
    static constexpr int numVis = 48;
    static constexpr int maxBlock = 2048;

    /** Posizioni (0..1) dei grani attivi, per l'interfaccia. -1 = vuoto. */
    std::array<std::atomic<float>, numVis> grainVis;
    std::atomic<float> scanVis { 0.0f };

    Engine()
    {
        for (auto& v : grainVis) v.store (-1.0f);
        for (int i = 0; i <= windowSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) windowSize);
    }

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

    //==========================================================================
    void noteOn (int note, float velocity, int lengthSamples = -1)
    {
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

    //==========================================================================
    /** in* puo' essere nullptr (strumento). in e out non devono sovrapporsi. n <= maxBlock. */
    void process (SampleData* sample, const float* inL, const float* inR,
                  float* outL, float* outR, int n, const Params& p, double bpm)
    {
        jassert (n <= maxBlock);
        std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
        std::fill (mixR.begin(), mixR.begin() + n, 0.0f);

        const int len = sample != nullptr ? juce::jmin (sample->length.load(), sample->buffer.getNumSamples()) : 0;

        // --- generatore + grani, spezzati sui passi del generatore
        const double stepSamples = stepLength (p, bpm);
        int done = 0;

        while (done < n)
        {
            if (samplesToStep <= 0.0)
            {
                if (p.genOn)
                    generatorStep (p, stepSamples);
                samplesToStep += stepSamples;
            }

            const int chunk = juce::jmin (n - done, juce::jmax (1, (int) std::ceil (samplesToStep)));
            renderGrains (sample, len, mixL.data() + done, mixR.data() + done, chunk, p);
            samplesToStep -= chunk;
            done += chunk;
        }

        updateVis (len);
        effects (mixL.data(), mixR.data(), n, p);

        // --- uscita
        const float targetGain = juce::Decibels::decibelsToGain (p.volumeDb, -60.0f);
        if (gainSmoothed < 0.0f) gainSmoothed = targetGain;
        const float gainStep = (targetGain - gainSmoothed) / (float) n;

        for (int i = 0; i < n; ++i)
        {
            gainSmoothed += gainStep;
            float l = softLimit (mixL[(size_t) i]), r = softLimit (mixR[(size_t) i]);
            if (inL != nullptr) { l += p.dry * inL[i]; r += p.dry * (inR != nullptr ? inR[i] : inL[i]); }
            outL[i] = l * gainSmoothed;
            outR[i] = r * gainSmoothed;
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

    struct Grain
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;
        float winPhase = 0.0f, winInc = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
        int remaining = 0, delay = 0;
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
        static const int scales[8][12] = {
            { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 },
            { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 6, 8, 10 },
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
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
            const int n = 60 + p.genRoot + 12 * p.genOctave + scales[sc][degree % degrees] + 12 * (degree / degrees);
            return juce::jlimit (0, 127, n);
        };

        const int length = (int) juce::jmax (64.0, stepSamples * p.genLength * (0.75 + 0.5 * rng.nextDouble()));
        noteOn (toNote (genDegree), 0.55f + 0.45f * rng.nextFloat(), length);

        if (rng.nextFloat() < 0.22f)   // ogni tanto una seconda nota, una terza o una quinta sopra
            noteOn (toNote (genDegree + (rng.nextBool() ? 2 : 4)), 0.4f + 0.3f * rng.nextFloat(), length);
    }

    //==========================================================================
    void spawnGrain (const Voice& v, const SampleData& sample, int len, int offset, const Params& p, float norm)
    {
        Grain* g = nullptr;
        for (auto& c : grains)
            if (! c.active) { g = &c; break; }
        if (g == nullptr)
            return;

        float semis = (float) (v.note - 60) + p.pitch + (rng.nextFloat() * 2.0f - 1.0f) * p.detuneCents * 0.01f;

        if (rng.nextFloat() < p.octaves)
        {
            const float r = rng.nextFloat();
            semis += r < 0.6f ? 12.0f : (r < 0.9f ? -12.0f : 24.0f);
        }

        double inc = std::pow (2.0, (double) semis / 12.0) * sample.sampleRate / sr;
        inc = juce::jlimit (0.02, 16.0, inc);

        const int grainLen = juce::jmax (32, (int) (p.sizeMs * 0.001 * sr));
        const double spray = (double) p.spray * p.spray * 0.5 * len * (rng.nextDouble() * 2.0 - 1.0);
        double pos = (double) wrap01 (p.position + (float) scanPhase) * len + spray;
        pos = std::fmod (pos, (double) len);
        if (pos < 0.0) pos += len;

        if (rng.nextFloat() < p.reverse)
        {
            pos = std::fmod (pos + inc * grainLen, (double) len);
            inc = -inc;
        }

        const float pan = 0.5f + (rng.nextFloat() - 0.5f) * p.spread;
        const float amp = v.vel * v.env * norm;

        g->active = true;
        g->pos = pos;
        g->inc = inc;
        g->winPhase = 0.0f;
        g->winInc = (float) windowSize / (float) grainLen;
        g->gainL = amp * std::cos (pan * juce::MathConstants<float>::halfPi);
        g->gainR = amp * std::sin (pan * juce::MathConstants<float>::halfPi);
        g->remaining = grainLen;
        g->delay = offset;
    }

    void renderGrains (SampleData* sample, int len, float* outL, float* outR, int n, const Params& p)
    {
        const bool canPlay = sample != nullptr && len >= 256;
        const double interval = sr / juce::jmax (0.5f, p.density);
        const float norm = 0.6f / std::sqrt (juce::jmax (1.0f, p.density * p.sizeMs * 0.001f));
        const float attackStep = 1.0f / juce::jmax (1.0f, p.attackMs * 0.001f * (float) sr);
        const float releaseStep = 1.0f / juce::jmax (1.0f, p.releaseMs * 0.001f * (float) sr);

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
                        spawnGrain (v, *sample, len, i, p, norm);
                    v.untilGrain += interval * (0.5 + rng.nextDouble());
                }
            }
        }

        if (canPlay)
        {
            scanPhase += (double) p.scan * n * sample->sampleRate / (sr * len);
            scanPhase -= std::floor (scanPhase);
        }

        // --- grani
        if (! canPlay)
        {
            for (auto& g : grains) g.active = false;
            return;
        }

        const float* srcL = sample->buffer.getReadPointer (0);
        const float* srcR = sample->buffer.getReadPointer (sample->buffer.getNumChannels() > 1 ? 1 : 0);
        const double dlen = (double) len;

        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            if (g.pos >= dlen) { g.active = false; continue; }   // il campione e' cambiato

            int i = g.delay;
            g.delay = 0;
            if (i >= n) { g.delay = i - n; continue; }

            for (; i < n && g.remaining > 0; ++i, --g.remaining)
            {
                const int wi = (int) g.winPhase;
                const float w = window[(size_t) juce::jmin (wi, windowSize)];

                const int i1 = (int) g.pos;
                const float f = (float) (g.pos - (double) i1);
                int i0 = i1 - 1, i2 = i1 + 1, i3 = i1 + 2;
                if (i0 < 0) i0 += len;
                if (i2 >= len) i2 -= len;
                if (i3 >= len) i3 -= len;

                outL[i] += g.gainL * w * hermite (srcL[i0], srcL[i1], srcL[i2], srcL[i3], f);
                outR[i] += g.gainR * w * hermite (srcR[i0], srcR[i1], srcR[i2], srcR[i3], f);

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

    void updateVis (int len)
    {
        int slot = 0;
        if (len > 0)
            for (auto& g : grains)
                if (g.active && slot < numVis)
                    grainVis[(size_t) slot++].store ((float) (g.pos / (double) len), std::memory_order_relaxed);

        while (slot < numVis)
            grainVis[(size_t) slot++].store (-1.0f, std::memory_order_relaxed);

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
    static constexpr int windowSize = 2048;
    std::array<float, windowSize + 1> window {};

    double sr = 44100.0;
    std::array<Voice, maxVoices> voices;
    std::array<Grain, maxGrains> grains;
    juce::uint32 ageCounter = 0;
    juce::Random rng;
    double scanPhase = 0.0, samplesToStep = 0.0;
    int genDegree = 4;

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

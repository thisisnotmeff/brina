// Test del motore senza host: controlla che suoni, che resti stabile e che l'intonazione sia giusta.
#include "../Source/Engine.h"
#include <cstdio>

using namespace brina;

struct Stats { double peak = 0, rms = 0; bool finite = true; double zeroCrossHz = 0; };

static SampleData::Ptr makeSample (double sr, double seconds, double freq)
{
    SampleData::Ptr s = new SampleData();
    const int n = (int) (sr * seconds);
    s->buffer.setSize (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
        s->buffer.setSample (0, i, v);
        s->buffer.setSample (1, i, v);
    }
    s->sampleRate = sr;
    s->length = n;
    return s;
}

static Stats run (Engine& e, SampleData* s, const Params& p, double sr, double seconds, int block, bool withInput = false)
{
    std::vector<float> l ((size_t) block), r ((size_t) block), in ((size_t) block, 0.25f);
    Stats st;
    double sum = 0; long count = 0, crossings = 0; float prev = 0;
    for (long done = 0; done < (long) (sr * seconds); done += block)
    {
        e.process (s, withInput ? in.data() : nullptr, withInput ? in.data() : nullptr, l.data(), r.data(), block, p, 120.0);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) st.finite = false;
            st.peak = std::max ({ st.peak, (double) std::abs (l[(size_t) i]), (double) std::abs (r[(size_t) i]) });
            sum += l[(size_t) i] * l[(size_t) i]; ++count;
            if ((prev < 0) != (l[(size_t) i] < 0)) ++crossings;
            prev = l[(size_t) i];
        }
    }
    st.rms = std::sqrt (sum / (double) count);
    st.zeroCrossHz = crossings * 0.5 / seconds;
    return st;
}

static int failures = 0;
static void check (bool ok, const char* what, const Stats& s)
{
    std::printf ("%s  %-46s peak %.3f  rms %.4f  f0 %.0f Hz\n", ok ? "OK  " : "FAIL", what, s.peak, s.rms, s.zeroCrossHz);
    if (! ok) ++failures;
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("--- %.0f Hz\n", sr);
        auto sample = makeSample (44100.0, 4.0, 440.0);

        {   // 1. preset di default col generatore: deve suonare e restare sotto controllo
            Engine e; e.prepare (sr); Params p;
            auto s = run (e, sample.get(), p, sr, 30.0, 512);
            check (s.finite && s.rms > 0.005 && s.peak < 1.5, "default + generatore", s);
        }
        {   // 2. senza note e senza generatore: silenzio
            Engine e; e.prepare (sr); Params p; p.genOn = false;
            auto s = run (e, sample.get(), p, sr, 3.0, 512);
            check (s.finite && s.peak < 1e-6, "nessuna nota = silenzio", s);
        }
        {   // 3. intonazione: La 440 suonato un'ottava sopra (nota 72) deve dare 880 Hz
            Engine e; e.prepare (sr); Params p; p.genOn = false;
            p.detuneCents = 0; p.octaves = 0; p.reverse = 0; p.wow = 0; p.drive = 0; p.spray = 0.3f;
            p.delayMix = 0; p.reverbMix = 0; p.shimmer = 0; p.toneHz = 20000; p.attackMs = 5; p.density = 40;
            e.noteOn (72, 1.0f);
            auto s = run (e, sample.get(), p, sr, 4.0, 256);
            check (s.finite && std::abs (s.zeroCrossHz - 880.0) < 25.0 && s.rms > 0.02, "nota 72 su La 440 -> 880 Hz", s);

            e.noteOff (72); p.releaseMs = 50;
            run (e, sample.get(), p, sr, 2.0, 256);
            auto tail = run (e, sample.get(), p, sr, 1.0, 256);
            check (tail.peak < 1e-4, "note off -> silenzio", tail);
        }
        {   // 4. tutto al massimo per un minuto: nessuna esplosione
            Engine e; e.prepare (sr); Params p;
            p.density = 80; p.sizeMs = 1500; p.octaves = 1; p.drive = 1; p.wow = 1; p.delayFeedback = 0.95f;
            p.delayMix = 1; p.reverbSize = 1; p.reverbDamp = 0; p.shimmer = 1; p.reverbMix = 1; p.genRate = 0;
            p.genProb = 1; p.genLength = 8; p.dry = 1; p.volumeDb = 0;
            for (int n = 48; n < 60; ++n) e.noteOn (n, 1.0f);
            auto s = run (e, sample.get(), p, sr, 60.0, 2048, true);
            check (s.finite && s.peak <= 1.76, "tutto al massimo, 60 s", s);
        }
        {   // 4b. shimmer e riverbero al massimo: tolte le note, la coda deve spegnersi (niente drone infinito)
            Engine e; e.prepare (sr); Params p; p.genOn = false;
            p.reverbSize = 1; p.reverbDamp = 0; p.shimmer = 1; p.reverbMix = 1; p.delayFeedback = 0.5f; p.releaseMs = 100;
            for (int n = 55; n < 61; ++n) e.noteOn (n, 1.0f);
            auto loud = run (e, sample.get(), p, sr, 15.0, 512);
            e.allNotesOff();
            run (e, sample.get(), p, sr, 40.0, 512);
            auto tail = run (e, sample.get(), p, sr, 2.0, 512);
            std::printf ("      (livello con le note: rms %.3f)\n", loud.rms);
            check (tail.finite && tail.rms < loud.rms * 0.01, "la coda shimmer si spegne in 40 s", tail);
        }
        {   // 5. cambio campione con grani attivi (piu' corto), blocchi irregolari, campione nullo
            Engine e; e.prepare (sr); Params p; p.spray = 1; p.scan = 2;
            run (e, sample.get(), p, sr, 3.0, 333);
            auto tiny = makeSample (48000.0, 0.02, 200.0);
            auto a = run (e, tiny.get(), p, sr, 3.0, 77);
            auto b = run (e, nullptr, p, sr, 2.0, 1);
            check (a.finite && b.finite, "cambio campione / blocchi irregolari", a);
        }
    }

    std::printf (failures == 0 ? "\nTUTTI I TEST OK\n" : "\n%d TEST FALLITI\n", failures);
    return failures;
}

// Test del motore senza host: controlla che suoni, che resti stabile e che l'intonazione sia giusta.
#include "../Source/Engine.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>

using namespace brina;

// Ogni "new" del programma passa di qui: serve a controllare che process() non allochi mai.
static long allocations = 0;
void* operator new (std::size_t size)
{
    ++allocations;
    if (void* p = std::malloc (size != 0 ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

static long allocationsInProcess = 0;

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

/** Gli slot (0..3) da cui stanno leggendo i grani attivi in questo momento. */
static void collectSlots (Engine& e, std::set<int>& seen)
{
    for (auto& v : e.grainVis)
        if (v.load() >= 0.0f)
            seen.insert ((int) v.load());
}

static int activeGrains (Engine& e)
{
    int n = 0;
    for (auto& v : e.grainVis)
        if (v.load() >= 0.0f) ++n;
    return n;
}

/** Grani attivi in media, su 4 secondi. */
static double meanGrains (Engine& e, SampleData* s, const Params& p, double sr)
{
    std::vector<float> l (512), r (512);
    double sum = 0; int blocks = 0;
    for (long done = 0; done < (long) (sr * 4.0); done += 512, ++blocks)
    {
        e.process (s, nullptr, nullptr, l.data(), r.data(), 512, p, 120.0);
        sum += activeGrains (e);
    }
    return sum / blocks;
}

static Stats runSlots (Engine& e, SampleData* const* slots, const Params& p, double sr, double seconds, int block,
                       bool withInput = false, std::set<int>* seen = nullptr)
{
    std::vector<float> l ((size_t) block), r ((size_t) block), in ((size_t) block, 0.25f);
    Stats st;
    double sum = 0; long count = 0, crossings = 0; float prev = 0;
    for (long done = 0; done < (long) (sr * seconds); done += block)
    {
        const long before = allocations;
        e.processSlots (slots, withInput ? in.data() : nullptr, withInput ? in.data() : nullptr, l.data(), r.data(), block, p, 120.0);
        allocationsInProcess += allocations - before;
        if (seen != nullptr) collectSlots (e, *seen);
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

static Stats run (Engine& e, SampleData* s, const Params& p, double sr, double seconds, int block, bool withInput = false)
{
    SampleData* slots[Engine::numSlots] = { s, nullptr, nullptr, nullptr };
    return runSlots (e, slots, p, sr, seconds, block, withInput);
}

/** Parametri "puliti" per misurare l'intonazione: niente stonature ne' effetti. */
static Params cleanParams()
{
    Params p; p.genOn = false;
    p.detuneCents = 0; p.octaves = 0; p.reverse = 0; p.wow = 0; p.drive = 0; p.spray = 0.3f;
    p.delayMix = 0; p.reverbMix = 0; p.shimmer = 0; p.toneHz = 20000; p.attackMs = 5; p.density = 40;
    return p;
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
        {   // 6. stop: le code sfumano fino al silenzio, le note vengono ignorate, start fa ripartire
            Engine e; e.prepare (sr); Params p;
            auto playing = run (e, sample.get(), p, sr, 8.0, 512);
            p.running = false;
            run (e, sample.get(), p, sr, 40.0, 512);
            e.noteOn (60, 1.0f);
            auto stopped = run (e, sample.get(), p, sr, 3.0, 512);
            check (playing.rms > 0.005 && stopped.finite && stopped.peak < 1e-4, "stop -> silenzio, note ignorate", stopped);
            p.running = true;
            auto again = run (e, sample.get(), p, sr, 8.0, 512);
            check (again.finite && again.rms > 0.005, "start -> riparte", again);
        }
        {   // 7. kill: con note tenute e code al massimo, silenzio totale dopo pochi millisecondi
            Engine e; e.prepare (sr); Params p; p.genOn = false;
            p.reverbSize = 1; p.reverbDamp = 0; p.shimmer = 1; p.reverbMix = 1; p.delayFeedback = 0.95f; p.delayMix = 1;
            for (int n = 55; n < 61; ++n) e.noteOn (n, 1.0f);
            auto loud = run (e, sample.get(), p, sr, 10.0, 512);
            e.kill();
            auto fade = run (e, sample.get(), p, sr, 0.02, 16);     // la dissolvenza (6 ms), a blocchi piccoli
            auto dead = run (e, sample.get(), p, sr, 2.0, 512);
            check (loud.rms > 0.02 && fade.finite && fade.peak <= loud.peak + 1e-6 && dead.peak == 0.0, "kill -> silenzio immediato", dead);

            e.noteOn (60, 1.0f);    // dopo il kill si puo' suonare di nuovo
            auto after = run (e, sample.get(), p, sr, 3.0, 512);
            check (after.finite && after.rms > 0.005, "dopo il kill si risuona", after);
        }
        {   // 7b. kill col segnale Dry (FX): il dry passa intatto, il resto tace
            Engine e; e.prepare (sr); Params p; p.dry = 1;
            run (e, sample.get(), p, sr, 5.0, 512, true);
            p.genOn = false;
            e.kill();
            run (e, sample.get(), p, sr, 0.05, 512, true);
            auto dry = run (e, sample.get(), p, sr, 1.0, 512, true);
            check (std::abs (dry.peak - 0.25) < 1e-6 && std::abs (dry.rms - 0.25) < 1e-6, "kill non tocca il dry", dry);
        }
        {   // 8. posizione fissa: Scan ignorato; in automatico la posizione scorre come prima
            Engine e; e.prepare (sr); Params p; p.scan = 1.0f;
            e.noteOn (60, 1.0f);    // una nota tenuta: il test non dipende dalle note casuali del generatore
            auto a = run (e, sample.get(), p, sr, 1.3, 512);
            const float moving = e.scanVis.load();
            p.fixedPosition = true;
            auto f = run (e, sample.get(), p, sr, 1.3, 512);
            const float fixed = e.scanVis.load();
            p.fixedPosition = false;
            run (e, sample.get(), p, sr, 0.5, 512);
            const float resumed = e.scanVis.load();
            check (a.finite && f.finite && f.rms > 0.001 && std::abs (moving - 0.325f) < 0.02f && fixed == 0.0f
                       && std::abs (resumed - 0.125f) < 0.02f, "posizione fissa / automatica", f);
        }

        // ------------------------------------------------------------------ multigrain
        {   // 9. otto voci: piu' grani insieme, ma il livello resta quello di una voce sola
            Params p; p.genOn = false;
            Engine one; one.prepare (sr); one.noteOn (60, 1.0f);
            auto a = run (one, sample.get(), p, sr, 6.0, 512);
            const double grains1 = meanGrains (one, sample.get(), p, sr);

            p.grainVoices = 8;
            Engine eight; eight.prepare (sr); eight.noteOn (60, 1.0f);
            auto b = run (eight, sample.get(), p, sr, 6.0, 512);
            const double grains8 = meanGrains (eight, sample.get(), p, sr);

            std::printf ("      (1 voce: %.1f grani in media, rms %.4f - 8 voci: %.1f grani)\n", grains1, a.rms, grains8);
            check (b.finite && grains8 > 5.0 * grains1 && grains8 < 12.0 * grains1 && b.rms > 0.5 * a.rms && b.rms < 2.0 * a.rms,
                   "8 voci: otto volte i grani, stesso livello", b);
        }
        {   // 10. intervalli tra le voci: la voce 2 legge dallo slot 2 (Per Voice), la voce 1 da uno slot muto
            auto silent = makeSample (44100.0, 2.0, 0.0);
            SampleData* slots[Engine::numSlots] = { silent.get(), sample.get(), nullptr, nullptr };
            Params p = cleanParams(); p.grainVoices = 2; p.sourceMode = 2; p.voiceSpread = 0;

            p.voiceMode = 1;    // Octaves: +12
            Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
            auto oct = runSlots (e, slots, p, sr, 4.0, 256);
            check (oct.finite && std::abs (oct.zeroCrossHz - 880.0) < 25.0 && oct.rms > 0.01, "voce 2 in Octaves -> 880 Hz", oct);

            p.voiceMode = 2;    // Fifths: +7
            Engine e2; e2.prepare (sr); e2.noteOn (60, 1.0f);
            auto fifth = runSlots (e2, slots, p, sr, 4.0, 256);
            check (fifth.finite && std::abs (fifth.zeroCrossHz - 659.3) < 20.0, "voce 2 in Fifths -> 659 Hz", fifth);

            p.voiceMode = 3; p.genScale = 0;    // Chord in maggiore: una terza sopra (+4)
            Engine e3; e3.prepare (sr); e3.noteOn (60, 1.0f);
            auto third = runSlots (e3, slots, p, sr, 4.0, 256);
            check (third.finite && std::abs (third.zeroCrossHz - 554.4) < 18.0, "voce 2 in Chord -> 554 Hz", third);
        }

        // ------------------------------------------------------------------ quattro slot
        auto high = makeSample (48000.0, 3.0, 880.0);
        {   // 11. una voce in Per Voice legge sempre dal primo slot pieno
            Params p = cleanParams(); p.sourceMode = 2;
            SampleData* both[Engine::numSlots] = { sample.get(), high.get(), nullptr, nullptr };
            Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
            auto a = runSlots (e, both, p, sr, 4.0, 256);
            check (a.finite && std::abs (a.zeroCrossHz - 440.0) < 15.0 && a.rms > 0.02, "Per Voice, 1 voce -> solo slot 1", a);

            SampleData* second[Engine::numSlots] = { nullptr, nullptr, high.get(), nullptr };
            Engine e2; e2.prepare (sr); e2.noteOn (60, 1.0f);
            auto b = runSlots (e2, second, p, sr, 4.0, 256);
            check (b.finite && std::abs (b.zeroCrossHz - 880.0) < 25.0 && b.rms > 0.02, "solo slot 3 pieno -> suona quello", b);
        }
        {   // 12. Random e Cycle pescano da tutti gli slot pieni, e solo da quelli
            auto third = makeSample (22050.0, 1.0, 220.0);
            SampleData* slots[Engine::numSlots] = { sample.get(), nullptr, high.get(), third.get() };
            const std::set<int> expected { 0, 2, 3 };
            Stats last;
            bool ok = true;

            for (int mode : { 0, 1 })
            {
                Params p; p.sourceMode = mode;
                Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
                std::set<int> seen;
                last = runSlots (e, slots, p, sr, 10.0, 512, false, &seen);
                ok = ok && last.finite && last.rms > 0.005 && seen == expected;
            }

            Params p; p.sourceMode = 2; p.grainVoices = 8;      // Per Voice con otto voci: tutti e tre
            Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
            std::set<int> seen;
            last = runSlots (e, slots, p, sr, 10.0, 512, false, &seen);
            check (ok && last.finite && seen == expected, "Random / Cycle / Per Voice usano gli slot pieni", last);
        }
        {   // 13. slot svuotati e scambiati con i grani attivi, blocchi irregolari
            auto tiny = makeSample (48000.0, 0.02, 200.0);
            Params p; p.spray = 1; p.scan = 2; p.grainVoices = 8; p.sourceMode = 0;
            p.gfxFilterChance = p.gfxCrushChance = p.gfxDriveChance = 0.5f;
            Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
            SampleData* a[Engine::numSlots] = { sample.get(), high.get(), sample.get(), high.get() };
            SampleData* b[Engine::numSlots] = { tiny.get(), nullptr, high.get(), tiny.get() };
            SampleData* c[Engine::numSlots] = { nullptr, tiny.get(), nullptr, nullptr };
            SampleData* none[Engine::numSlots] = { nullptr, nullptr, nullptr, nullptr };
            auto s1 = runSlots (e, a, p, sr, 3.0, 333);
            auto s2 = runSlots (e, b, p, sr, 3.0, 77);
            auto s3 = runSlots (e, c, p, sr, 2.0, 1);
            auto s4 = runSlots (e, none, p, sr, 1.0, 64);
            auto s5 = runSlots (e, a, p, sr, 3.0, 2048);
            check (s1.finite && s2.finite && s3.finite && s4.finite && s5.finite && s5.rms > 0.005,
                   "slot svuotati / scambiati con grani attivi", s5);
        }

        // ------------------------------------------------------------------ effetti per grano
        {   // 14. a probabilita' zero gli effetti non esistono: uscita identica, campione per campione
            Params p;
            Engine plain; plain.prepare (sr); plain.setRandomSeed (77);
            auto a = run (plain, sample.get(), p, sr, 8.0, 512);
            p.gfxFilterHz = 200; p.gfxFilterRes = 1; p.gfxCrush = 1; p.gfxDrive = 1; p.voiceSpread = 1; p.voiceMode = 3;
            Engine same; same.prepare (sr); same.setRandomSeed (77);
            auto b = run (same, sample.get(), p, sr, 8.0, 512);
            check (a.rms == b.rms && a.peak == b.peak && a.zeroCrossHz == b.zeroCrossHz,
                   "effetti a 0 e una voce: suono invariato", b);
        }
        {   // 15. filtro su tutti i grani: un suono acuto (3 kHz) quasi sparisce con il taglio a 150 Hz
            auto bright = makeSample (44100.0, 3.0, 3000.0);
            Params p = cleanParams();
            Engine open; open.prepare (sr); open.noteOn (60, 1.0f);
            auto a = run (open, bright.get(), p, sr, 4.0, 512);
            p.gfxFilterChance = 1; p.gfxFilterHz = 150; p.gfxFilterRes = 0;
            Engine closed; closed.prepare (sr); closed.noteOn (60, 1.0f);
            auto b = run (closed, bright.get(), p, sr, 4.0, 512);
            check (b.finite && a.rms > 0.02 && b.rms < 0.05 * a.rms, "Grain Filter chiude gli acuti", b);

            p.gfxFilterChance = 0.5f;   // a meta': circa un grano su due resta aperto
            Engine half; half.prepare (sr); half.noteOn (60, 1.0f);
            auto c = run (half, bright.get(), p, sr, 8.0, 512);
            check (c.finite && c.rms > 0.4 * a.rms && c.rms < 0.95 * a.rms, "Grain Filter al 50%: meta' dei grani", c);
        }
        {   // 16. crush e drive: cambiano il suono, senza esplodere; la risonanza al massimo resta stabile
            Params p = cleanParams();
            Engine clean; clean.prepare (sr); clean.noteOn (60, 1.0f);
            auto a = run (clean, sample.get(), p, sr, 4.0, 512);

            Params crush = p; crush.gfxCrushChance = 1; crush.gfxCrush = 1;
            Engine e1; e1.prepare (sr); e1.noteOn (60, 1.0f);
            auto b = run (e1, sample.get(), crush, sr, 4.0, 512);

            Params drive = p; drive.gfxDriveChance = 1; drive.gfxDrive = 1;
            Engine e2; e2.prepare (sr); e2.noteOn (60, 1.0f);
            auto c = run (e2, sample.get(), drive, sr, 4.0, 512);

            Params reso = p; reso.gfxFilterChance = 1; reso.gfxFilterHz = 440; reso.gfxFilterRes = 1;
            Engine e3; e3.prepare (sr); e3.noteOn (60, 1.0f);
            auto d = run (e3, sample.get(), reso, sr, 4.0, 512);

            check (b.finite && b.rms > 0.01 && b.peak < 1.5 && b.rms != a.rms, "Grain Crush", b);
            check (c.finite && c.rms > 0.01 && c.peak < 1.5 && c.rms != a.rms, "Grain Drive", c);
            check (d.finite && d.rms > 0.01 && d.peak < 1.5, "Grain Filter, risonanza al massimo", d);
        }
        // ------------------------------------------------------------------ forme del grano
        {   // 18. ogni forma parte e finisce a zero e ha l'energia della Hann
            Engine e;
            const float* hann = e.windowTable (0);
            double ref = 0;
            for (int i = 0; i <= Engine::windowSize; ++i) ref += (double) hann[i] * hann[i];

            bool ok = true;
            juce::String detail;
            for (int s = 1; s < Engine::numShapes; ++s)
            {
                const float* w = e.windowTable (s);
                double energy = 0, peak = 0;
                bool finite = true;
                for (int i = 0; i <= Engine::windowSize; ++i)
                {
                    finite = finite && std::isfinite (w[i]) && w[i] >= 0.0f;
                    energy += (double) w[i] * w[i];
                    peak = std::max (peak, (double) w[i]);
                }
                // i primi e gli ultimi campioni restano piccolissimi: il grano entra ed esce senza click
                const bool edges = std::abs (w[0]) < 1e-6f && std::abs (w[Engine::windowSize]) < 1e-6f
                                   && std::abs (w[1]) < 0.01f && std::abs (w[Engine::windowSize - 1]) < 0.01f;
                const bool same = std::abs (energy / ref - 1.0) < 1e-3;
                if (! (finite && edges && same && peak < 2.0)) { ok = false; detail << " " << s; }
            }
            Stats none;
            check (ok, ("forme: zero agli estremi, energia della Hann" + detail).toRawUTF8(), none);
        }
        {   // 19. a probabilita' zero la forma non conta: uscita identica, campione per campione
            Params p;
            Engine plain; plain.prepare (sr); plain.setRandomSeed (91);
            auto a = run (plain, sample.get(), p, sr, 8.0, 512);
            p.grainShape = 3;
            Engine same; same.prepare (sr); same.setRandomSeed (91);
            auto b = run (same, sample.get(), p, sr, 8.0, 512);
            check (a.rms == b.rms && a.peak == b.peak && a.zeroCrossHz == b.zeroCrossHz, "Shape Chance a 0: suono invariato", b);
        }
        {   // 20. ogni forma su tutti i grani: suona, cambia il suono, il volume resta vicino
            Params p = cleanParams();
            Engine plain; plain.prepare (sr); plain.setRandomSeed (5); plain.noteOn (60, 1.0f);
            auto a = run (plain, sample.get(), p, sr, 4.0, 512);

            for (int shape = 0; shape < (int) shapeNames().size(); ++shape)
            {
                Params q = p; q.shapeChance = 1.0f; q.grainShape = shape;
                Engine e; e.prepare (sr); e.setRandomSeed (5); e.noteOn (60, 1.0f);
                auto b = run (e, sample.get(), q, sr, 4.0, 512);
                const juce::String name = "forma " + shapeNames()[shape] + ": suona, volume simile";
                check (b.finite && b.peak < 1.5 && b.rms != a.rms && b.rms > 0.6 * a.rms && b.rms < 1.6 * a.rms
                           && std::abs (b.zeroCrossHz - 440.0) < 30.0,
                       name.toRawUTF8(), b);
            }
        }
        {   // 17. tutto al massimo con otto voci, quattro slot e tutti gli effetti: nessuna esplosione
            Engine e; e.prepare (sr); Params p;
            p.density = 80; p.sizeMs = 1500; p.octaves = 1; p.drive = 1; p.wow = 1; p.delayFeedback = 0.95f;
            p.delayMix = 1; p.reverbSize = 1; p.reverbDamp = 0; p.shimmer = 1; p.reverbMix = 1; p.genRate = 0;
            p.genProb = 1; p.genLength = 8; p.dry = 1; p.volumeDb = 0;
            p.grainVoices = 8; p.voiceSpread = 1; p.voiceMode = 3; p.sourceMode = 0;
            p.gfxFilterChance = p.gfxCrushChance = p.gfxDriveChance = 1; p.gfxFilterRes = 1; p.gfxCrush = 1; p.gfxDrive = 1;
            p.shapeChance = 1; p.grainShape = 4;
            SampleData* slots[Engine::numSlots] = { sample.get(), high.get(), sample.get(), high.get() };
            for (int n = 48; n < 60; ++n) e.noteOn (n, 1.0f);

            const auto start = std::chrono::steady_clock::now();
            auto s = runSlots (e, slots, p, sr, 20.0, 2048, true);
            const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            std::printf ("      (%d grani attivi; 20 s di audio in %.1f s = %.0f%% di un core)\n", activeGrains (e), took, took * 5.0);
            check (s.finite && s.peak <= 1.76, "tutto al massimo, 8 voci + 4 slot + effetti", s);
        }
    }

    {
        Stats none;
        std::printf ("---\n");
        check (allocationsInProcess == 0, "process() non alloca memoria", none);
        if (allocationsInProcess != 0) std::printf ("      (%ld allocazioni)\n", allocationsInProcess);
    }

    std::printf (failures == 0 ? "\nTUTTI I TEST OK\n" : "\n%d TEST FALLITI\n", failures);
    return failures;
}

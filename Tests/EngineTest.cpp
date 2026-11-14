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
static int failures = 0;

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

/** Parametri "puliti" per misurare l'intonazione: niente stonature ne' effetti, in tutte le corsie. */
static Params cleanParams()
{
    Params p; p.genOn = false;
    p.wow = 0; p.drive = 0; p.delayMix = 0; p.reverbMix = 0; p.shimmer = 0; p.toneHz = 20000;
    for (int lane = 0; lane < numLanes; ++lane)
    {
        auto& g = p.lane (lane);
        g.detuneCents = 0; g.octaves = 0; g.reverse = 0; g.spray = 0.3f; g.attackMs = 5; g.density = 40;
    }
    return p;
}

/** Ampiezza di alcune frequenze nell'uscita (canale sinistro): Goertzel su finestre di 4096 campioni
    (Hann), con la potenza mediata. I grani hanno fasi casuali, quindi una finestra sola lunga
    quanto tutto il test li cancellerebbe a vicenda. */
static std::vector<double> runTones (Engine& e, SampleData* const* slots, const Params& p, double sr, double seconds,
                                     std::initializer_list<double> freqs)
{
    std::vector<float> l (512), r (512), all;
    for (long done = 0; done < (long) (sr * seconds); done += 512)
    {
        e.processSlots (slots, nullptr, nullptr, l.data(), r.data(), 512, p, 120.0);
        all.insert (all.end(), l.begin(), l.end());
    }

    constexpr int win = 4096;
    std::vector<double> hann (win);
    double hannSum = 0;
    for (int i = 0; i < win; ++i) hannSum += hann[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / win);

    std::vector<double> out;
    for (double f : freqs)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = 2.0 * std::cos (w);
        double power = 0; int windows = 0;

        for (size_t start = 0; start + win <= all.size(); start += win, ++windows)
        {
            double s1 = 0, s2 = 0;
            for (int i = 0; i < win; ++i) { const double s0 = all[start + (size_t) i] * hann[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
            const double mag = 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / hannSum;
            power += mag * mag;
        }

        out.push_back (windows > 0 ? std::sqrt (power / windows) : 0.0);
    }
    return out;
}

static void checkTones (bool ok, const char* what, const std::vector<double>& a, std::initializer_list<double> freqs)
{
    std::printf ("%s  %-46s", ok ? "OK  " : "FAIL", what);
    size_t i = 0;
    for (double f : freqs) std::printf ("  %.0f Hz %.4f", f, a[i++]);
    std::printf ("\n");
    if (! ok) ++failures;
}

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
            const float moving = e.scanVis[0].load();
            p.fixedPosition = true;
            auto f = run (e, sample.get(), p, sr, 1.3, 512);
            const float fixed = e.scanVis[0].load();
            p.fixedPosition = false;
            run (e, sample.get(), p, sr, 0.5, 512);
            const float resumed = e.scanVis[0].load();
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
        {   // 10. intervalli tra le voci: con due voci compare la seconda nota, con una no
            SampleData* slots[Engine::numSlots] = { sample.get(), nullptr, nullptr, nullptr };
            Params p = cleanParams(); p.voiceSpread = 0;
            const std::initializer_list<double> f { 440.0, 880.0, 659.26, 554.37 };

            Engine e0; e0.prepare (sr); e0.noteOn (60, 1.0f);
            auto one = runTones (e0, slots, p, sr, 4.0, f);

            p.grainVoices = 2;
            p.voiceMode = 1;    // Octaves: +12
            Engine e1; e1.prepare (sr); e1.noteOn (60, 1.0f);
            auto oct = runTones (e1, slots, p, sr, 4.0, f);
            p.voiceMode = 2;    // Fifths: +7
            Engine e2; e2.prepare (sr); e2.noteOn (60, 1.0f);
            auto fifth = runTones (e2, slots, p, sr, 4.0, f);
            p.voiceMode = 3; p.genScale = 0;    // Chord in maggiore: una terza sopra (+4)
            Engine e3; e3.prepare (sr); e3.noteOn (60, 1.0f);
            auto third = runTones (e3, slots, p, sr, 4.0, f);

            checkTones (one[0] > 0.02 && one[1] < 0.1 * one[0] && one[2] < 0.1 * one[0], "1 voce: solo 440 Hz", one, f);
            checkTones (oct[1] > 0.2 * oct[0], "voce 2 in Octaves -> 880 Hz", oct, f);
            checkTones (fifth[2] > 0.2 * fifth[0] && fifth[1] < 0.1 * fifth[0], "voce 2 in Fifths -> 659 Hz", fifth, f);
            checkTones (third[3] > 0.2 * third[0] && third[2] < 0.1 * third[0], "voce 2 in Chord -> 554 Hz", third, f);
        }

        // ------------------------------------------------------------------ quattro corsie
        auto high = makeSample (48000.0, 3.0, 880.0);
        {   // 11. ogni corsia suona il suo campione con i suoi controlli
            const std::initializer_list<double> f { 440.0, 880.0, 659.26, 1760.0 };
            Params p = cleanParams();
            p.lane (1).pitch = 12;          // corsia 2: lo stesso La un'ottava sopra
            SampleData* two[Engine::numSlots] = { sample.get(), sample.get(), nullptr, nullptr };
            Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
            auto a = runTones (e, two, p, sr, 4.0, f);
            checkTones (a[0] > 0.02 && a[1] > 0.3 * a[0] && a[3] < 0.1 * a[0], "corsia 1 a 440, corsia 2 (+12 st) a 880", a, f);

            p.level = 0;                    // corsia 1 a volume zero: resta solo la corsia 2
            Engine e2; e2.prepare (sr); e2.noteOn (60, 1.0f);
            auto b = runTones (e2, two, p, sr, 4.0, f);
            checkTones (b[1] > 0.02 && b[0] < 0.05 * b[1], "Level della corsia 1 a zero", b, f);

            Params q = cleanParams(); q.lane (2).pitch = 7;     // solo la corsia 3 piena, con i suoi controlli
            SampleData* third[Engine::numSlots] = { nullptr, nullptr, sample.get(), nullptr };
            Engine e3; e3.prepare (sr); e3.noteOn (60, 1.0f);
            auto c = runTones (e3, third, q, sr, 4.0, f);
            checkTones (c[2] > 0.02 && c[0] < 0.1 * c[2], "solo la corsia 3 piena, Pitch +7 -> 659 Hz", c, f);
        }
        {   // 12. inviluppi per corsia: tolta la nota, la corsia col Release corto tace, l'altra continua
            const std::initializer_list<double> f { 440.0, 880.0 };
            Params p = cleanParams();
            p.releaseMs = 30; p.lane (1).releaseMs = 6000;
            SampleData* two[Engine::numSlots] = { sample.get(), high.get(), nullptr, nullptr };
            Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
            auto held = runTones (e, two, p, sr, 2.0, f);
            e.noteOff (60);
            runTones (e, two, p, sr, 0.5, f);
            auto tail = runTones (e, two, p, sr, 1.0, f);
            checkTones (held[0] > 0.02 && held[1] > 0.02 && tail[0] < 0.02 * held[0] && tail[1] > 0.3 * held[1],
                        "Release per corsia", tail, f);

            p.lane (1).attackMs = 3000;     // Attack lento solo sulla corsia 2
            Engine e2; e2.prepare (sr); e2.noteOn (60, 1.0f);
            auto start = runTones (e2, two, p, sr, 0.3, f);
            checkTones (start[0] > 0.02 && start[1] < 0.25 * start[0] * held[1] / held[0], "Attack per corsia", start, f);
        }
        {   // 13. Scan per corsia; ogni corsia legge solo il suo campione
            Params p; p.scan = 0; p.lane (1).scan = 1.0f; p.lane (3).scan = -0.5f;
            auto third = makeSample (22050.0, 1.0, 220.0);
            SampleData* slots[Engine::numSlots] = { sample.get(), high.get(), nullptr, third.get() };
            Engine e; e.prepare (sr); e.noteOn (60, 1.0f);
            std::set<int> seen;
            auto a = runSlots (e, slots, p, sr, 1.5, 512, false, &seen);
            const float s0 = e.scanVis[0].load(), s1 = e.scanVis[1].load(), s3 = e.scanVis[3].load();
            check (a.finite && a.rms > 0.005 && seen == std::set<int> { 0, 1, 3 } && s0 == 0.0f
                       && std::abs (s1 - 0.5f) < 0.02f && std::abs (s3 - 0.25f) < 0.02f,
                   "Scan per corsia, ogni corsia il suo campione", a);
        }
        {   // 14. corsie svuotate e scambiate con i grani attivi, blocchi irregolari
            auto tiny = makeSample (48000.0, 0.02, 200.0);
            Params p; p.spray = 1; p.scan = 2; p.grainVoices = 8;
            for (int lane = 1; lane < numLanes; ++lane) { p.lane (lane).spray = 1; p.lane (lane).scan = -2; p.lane (lane).density = 80; }
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
                   "corsie svuotate / scambiate con grani attivi", s5);
        }
        {   // 15. corsie 2-4 vuote: i loro controlli non cambiano nulla (stessa uscita, campione per campione)
            Params p;
            Engine plain; plain.prepare (sr); plain.setRandomSeed (99);
            auto a = run (plain, sample.get(), p, sr, 6.0, 512);
            for (int lane = 1; lane < numLanes; ++lane)
            {
                auto& g = p.lane (lane);
                g.density = 80; g.releaseMs = 15000; g.attackMs = 5; g.scan = 2; g.level = 0.1f; g.pitch = 7;
            }
            Engine same; same.prepare (sr); same.setRandomSeed (99);
            auto b = run (same, sample.get(), p, sr, 6.0, 512);
            check (a.rms == b.rms && a.peak == b.peak && a.zeroCrossHz == b.zeroCrossHz,
                   "corsie vuote: suono della corsia 1 invariato", b);
        }

        // ------------------------------------------------------------------ effetti per grano
        {   // 16. a probabilita' zero gli effetti non esistono: uscita identica, campione per campione
            Params p;
            Engine plain; plain.prepare (sr); plain.setRandomSeed (77);
            auto a = run (plain, sample.get(), p, sr, 8.0, 512);
            p.gfxFilterHz = 200; p.gfxFilterRes = 1; p.gfxCrush = 1; p.gfxDrive = 1; p.voiceSpread = 1; p.voiceMode = 3;
            Engine same; same.prepare (sr); same.setRandomSeed (77);
            auto b = run (same, sample.get(), p, sr, 8.0, 512);
            check (a.rms == b.rms && a.peak == b.peak && a.zeroCrossHz == b.zeroCrossHz,
                   "effetti a 0 e una voce: suono invariato", b);
        }
        {   // 17. filtro su tutti i grani: un suono acuto (3 kHz) quasi sparisce con il taglio a 150 Hz
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
        {   // 18. crush e drive: cambiano il suono, senza esplodere; la risonanza al massimo resta stabile
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
        {   // 19. tutto al massimo con otto voci, quattro corsie e tutti gli effetti: nessuna esplosione
            Engine e; e.prepare (sr); Params p;
            p.density = 80; p.sizeMs = 1500; p.octaves = 1; p.drive = 1; p.wow = 1; p.delayFeedback = 0.95f;
            p.delayMix = 1; p.reverbSize = 1; p.reverbDamp = 0; p.shimmer = 1; p.reverbMix = 1; p.genRate = 0;
            p.genProb = 1; p.genLength = 8; p.dry = 1; p.volumeDb = 0;
            p.grainVoices = 8; p.voiceSpread = 1; p.voiceMode = 3;
            for (int lane = 1; lane < numLanes; ++lane) p.lane (lane) = p.lane (0);   // tutte le corsie al massimo
            p.gfxFilterChance = p.gfxCrushChance = p.gfxDriveChance = 1; p.gfxFilterRes = 1; p.gfxCrush = 1; p.gfxDrive = 1;
            SampleData* slots[Engine::numSlots] = { sample.get(), high.get(), sample.get(), high.get() };
            for (int n = 48; n < 60; ++n) e.noteOn (n, 1.0f);

            const auto start = std::chrono::steady_clock::now();
            auto s = runSlots (e, slots, p, sr, 20.0, 2048, true);
            const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            std::printf ("      (%d grani attivi; 20 s di audio in %.1f s = %.0f%% di un core)\n", activeGrains (e), took, took * 5.0);
            check (s.finite && s.peak <= 1.76, "tutto al massimo, 8 voci + 4 corsie + effetti", s);
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

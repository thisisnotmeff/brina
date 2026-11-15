// Test del plugin vero e proprio (senza host): caricamento file, registrazione, stato, interfaccia.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static double render (BrinaProcessor& p, double seconds, bool feedInput, bool noteOn = false)
{
    juce::AudioBuffer<float> buffer (2, 512);
    double sum = 0; long count = 0; double phase = 0;
    for (long done = 0; done < (long) (48000 * seconds); done += 512)
    {
        juce::MidiBuffer midi;
        if (noteOn && done == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.9f), 100);
        for (int i = 0; i < 512; ++i)
        {
            const float v = feedInput ? 0.4f * (float) std::sin (phase) : 0.0f;
            phase += 2.0 * juce::MathConstants<double>::pi * 330.0 / 48000.0;
            buffer.setSample (0, i, v); buffer.setSample (1, i, v);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < 512; ++i) { sum += buffer.getSample (0, i) * buffer.getSample (0, i); ++count; }
    }
    return std::sqrt (sum / (double) count);
}

// The parameters of the previous version: ID, position, range and default must never change,
// otherwise saved Live sets (and their automation) break.
struct OldParam { const char* id; float lo, hi, def; };

static void checkOldParameters (BrinaProcessor& p, bool fx)
{
    const OldParam old[] = {
        { "position", 0, 1, 0.3f }, { "spray", 0, 1, 0.25f }, { "size", 10, 1500, 220 }, { "density", 1, 80, 18 },
        { "pitch", -24, 24, 0 }, { "detune", 0, 100, 8 }, { "octaves", 0, 1, 0.15f }, { "reverse", 0, 1, 0.2f },
        { "spread", 0, 1, 0.8f }, { "scan", -2, 2, 0.05f }, { "attack", 5, 8000, 500 }, { "release", 20, 15000, 3000 },
        { "genOn", 0, 1, 1 }, { "genRate", 0, 6, 3 }, { "genProb", 0, 1, 0.6f }, { "genRoot", 0, 11, 0 },
        { "genScale", 0, 7, 2 }, { "genOctave", -3, 3, 0 }, { "genRange", 1, 4, 2 }, { "genLength", 0.25f, 8, 3 },
        { "drive", 0, 1, 0.2f }, { "wow", 0, 1, 0.25f }, { "tone", 300, 20000, 9000 },
        { "delayTime", 20, 2500, 380 }, { "delayFeedback", 0, 0.95f, 0.4f }, { "delayMix", 0, 1, 0.25f },
        { "reverbSize", 0, 1, 0.85f }, { "reverbDamp", 0, 1, 0.4f }, { "shimmer", 0, 1, 0.25f }, { "reverbMix", 0, 1, 0.4f },
        { "dry", 0, 1, fx ? 1.0f : 0.0f }, { "volume", -48, 12, 0 },
        { "play", 0, 1, 1 }, { "kill", 0, 1, 0 }, { "posMode", 0, 1, 0 } };

    const auto& params = p.getParameters();
    bool ok = params.size() >= (int) std::size (old);
    juce::String wrong;

    for (int i = 0; ok && i < (int) std::size (old); ++i)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
        const auto& o = old[i];

        if (param == nullptr || param->getParameterID() != o.id || param->getVersionHint() != 1)
        {
            ok = false;
        }
        else
        {
            const auto range = param->getNormalisableRange();
            const float def = param->convertFrom0to1 (param->getDefaultValue());
            ok = std::abs (range.start - o.lo) < 1e-4f && std::abs (range.end - o.hi) < 1e-4f
                 && std::abs (def - o.def) < 1e-3f * juce::jmax (1.0f, std::abs (o.def));
        }

        if (! ok)
            wrong = "  (" + juce::String (o.id) + ")";
    }

    check (ok, "the 35 existing parameters are unchanged: ID, order, ranges, defaults" + wrong);

    // the new ones are appended, with a higher version hint, and by default they do not change the sound
    bool tail = true;
    for (int i = (int) std::size (old); i < params.size(); ++i)
        tail = tail && params[i]->getVersionHint() > 1;

    auto raw = [&p] (const char* id) { auto* v = p.apvts.getRawParameterValue (id); return v != nullptr ? v->load() : -1.0f; };
    // 10 for voices and per-grain effects, 12 controls for each of lanes 2-4, one Level per lane
    check (tail && params.size() == (int) std::size (old) + 10 + 3 * 12 + 4
               && raw ("voices") == 1.0f && raw ("gfxFilter") == 0.0f && raw ("gfxCrush") == 0.0f && raw ("gfxDrive") == 0.0f
               && raw ("level") == 1.0f && raw ("level4") == 1.0f,
           "new parameters appended; defaults: one voice, no effect, Level at maximum");

    // lanes 2-4 have the same controls as lane 1: same ranges and same defaults
    bool same = true;
    for (int lane = 1; lane < BrinaProcessor::numSlots; ++lane)
        for (auto& base : BrinaProcessor::laneParamBases())
        {
            auto* a = p.apvts.getParameter (BrinaProcessor::laneParamId (base, 0));
            auto* b = p.apvts.getParameter (BrinaProcessor::laneParamId (base, lane));
            same = same && a != nullptr && b != nullptr
                   && a->getNormalisableRange().start == b->getNormalisableRange().start
                   && a->getNormalisableRange().end == b->getNormalisableRange().end
                   && a->getDefaultValue() == b->getDefaultValue()
                   && b->getName (64).endsWith (juce::String (lane + 1));
        }
    check (same, "lanes 2-4: same controls as lane 1 (position2 ... release4)");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp"));
    const bool fx = BrinaProcessor::hasAudioInput();
    std::printf ("--- %s\n", fx ? "Brina FX" : "Brina (strumento)");

    // un file WAV di prova
    auto wav = outDir.getChildFile ("brina_test.wav");
    wav.deleteFile();
    {
        juce::AudioBuffer<float> b (1, 96000);
        for (int i = 0; i < 96000; ++i)
            b.setSample (0, i, 0.5f * (float) std::sin (i * 0.03) * (float) std::exp (-(i % 24000) / 6000.0));
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = wav.createOutputStream();
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (16));
        writer->writeFromAudioSampleBuffer (b, 0, 96000);
    }

    auto wav2 = outDir.getChildFile ("brina_test_2.wav");
    wav2.deleteFile();
    {
        juce::AudioBuffer<float> b (2, 30000);
        for (int i = 0; i < 30000; ++i)
        {
            b.setSample (0, i, 0.4f * (float) std::sin (i * 0.11));
            b.setSample (1, i, 0.4f * (float) std::sin (i * 0.12));
        }
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = wav2.createOutputStream();
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (b, 0, 30000);
    }

    auto slotLength = [] (BrinaProcessor& p, int slot)
    {
        auto s = p.getSample (slot);
        return s != nullptr ? s->length.load() : 0;
    };

    juce::MemoryBlock state;
    {
        BrinaProcessor p;
        p.setPlayConfigDetails (fx ? 2 : 0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        checkOldParameters (p, fx);
        p.apvts.getParameter ("genProb")->setValueNotifyingHost (1.0f);   // a note at every step: the tests do not depend on chance

        p.apvts.getParameter ("dry")->setValueNotifyingHost (0.0f);
        check (render (p, 2.0, true) < 1e-5, "senza campione: silenzio");
        check (p.loadFile (juce::File ("/nonexistent.wav")).isNotEmpty(), "file inesistente: errore gestito");
        check (p.loadFile (wav).isEmpty() && p.getSample()->length.load() == 96000, "caricamento WAV mono");
        check (render (p, 6.0, false) > 0.003, "il generatore suona il file caricato");

        p.apvts.getParameter ("genOn")->setValueNotifyingHost (0.0f);
        render (p, 30.0, false);
        check (render (p, 1.0, false) < 1e-4, "generatore spento: torna il silenzio");
        check (render (p, 3.0, false, true) > 0.003, "una nota MIDI suona");

        // --- kill, start/stop, posizione fissa
        auto setParam = [&p] (const char* id, float v) { p.apvts.getParameter (id)->setValueNotifyingHost (v); };
        check (p.apvts.getRawParameterValue ("play")->load() > 0.5f && p.apvts.getRawParameterValue ("posMode")->load() < 0.5f,
               "default: Play acceso, posizione automatica");

        p.triggerKill();
        render (p, 0.05, false);
        check (render (p, 1.0, false) == 0.0, "Kill: silenzio immediato");
        p.triggerKill();     // il parametro torna a 0: e' un altro kill, innocuo
        render (p, 0.05, false);

        setParam ("genOn", 1.0f);
        check (render (p, 5.0, false) > 0.003, "dopo il Kill il generatore riparte");
        setParam ("play", 0.0f);
        render (p, 40.0, false);
        check (render (p, 2.0, false, true) < 1e-4, "Stop: silenzio, anche con una nota MIDI");
        setParam ("play", 1.0f);
        check (render (p, 5.0, false) > 0.003, "Play: riparte");

        check (std::abs (p.engine.scanVis[0].load()) > 1e-4f, "posizione automatica: scorre");
        setParam ("posMode", 1.0f);
        render (p, 1.0, false);
        check (p.engine.scanVis[0].load() == 0.0f, "posizione fissa: ferma su Position");
        setParam ("posMode", 0.0f);
        setParam ("genOn", 0.0f);
        render (p, 30.0, false);

        // --- four lanes, multigrain, per-grain effects
        check (slotLength (p, 1) == 0 && slotLength (p, 2) == 0 && slotLength (p, 3) == 0, "lanes 2-4 start empty");
        check (p.loadFile (wav2, 2).isEmpty() && slotLength (p, 2) == 30000 && slotLength (p, 0) == 96000,
               "loading into lane 3, lane 1 unchanged");
        check (p.loadFile (wav, 7).isNotEmpty(), "nonexistent lane: error handled");
        check (p.loadFile (wav2, 3).isEmpty(), "loading into lane 4");
        p.clearSlot (3);
        check (slotLength (p, 3) == 0 && slotLength (p, 2) == 30000, "Clear empties only the chosen lane");
        setParam ("pitch3", 0.75f);          // lane 3 an octave up
        setParam ("density3", 0.8f);

        setParam ("voices", 1.0f);          // 8 voices
        setParam ("voiceMode", 1.0f);       // Chord
        setParam ("gfxFilter", 0.5f); setParam ("gfxCrush", 0.5f); setParam ("gfxDrive", 0.5f);
        check (p.apvts.getRawParameterValue ("voices")->load() == 8.0f, "Grain Voices arriva a 8");
        check (render (p, 3.0, false, true) > 0.003, "8 voices, 2 lanes and per-grain effects: it plays");

        bool sawFirst = false, sawThird = false, sawOther = false;
        for (int i = 0; i < 40; ++i)
        {
            render (p, 0.05, false);
            for (auto& v : p.engine.grainVis)
            {
                const int slot = (int) std::floor (v.load());
                sawFirst = sawFirst || slot == 0;
                sawThird = sawThird || slot == 2;
                sawOther = sawOther || slot == 1 || slot == 3;
            }
        }
        check (sawFirst && sawThird && ! sawOther, "grains read from lanes 1 and 3, not from the empty ones");

        p.triggerKill(); render (p, 0.05, false);   // release the held note
        p.triggerKill(); render (p, 0.05, false);   // and the Kill parameter goes back to 0
        setParam ("gfxFilter", 0.0f); setParam ("gfxDrive", 0.0f);

        if (fx)
        {
            // recording into lane 2 does not touch the others
            p.startRecording (1);
            render (p, 1.0, true);
            p.stopRecording();
            check (std::abs (slotLength (p, 1) - 48000) <= 512 && slotLength (p, 0) == 96000 && slotLength (p, 2) == 30000
                       && p.getRecordingSlot() == 1,
                   "recording into lane 2, the others unchanged");

            p.startRecording();
            render (p, 3.0, true);
            p.stopRecording();
            const int len = p.getSample()->length.load();
            check (std::abs (len - 144000) <= 512, "registrazione di 3 s dall'ingresso (" + juce::String (len) + " campioni)");

            p.startRecording();
            render (p, 62.0, true);
            check (! p.isRecording() && p.getSample()->length.load() == 60 * 48000, "la registrazione si ferma da sola a 60 s");
            p.startRecording(); render (p, 2.0, true); p.stopRecording();
        }

        p.apvts.getParameter ("pitch")->setValueNotifyingHost (0.75f);
        p.getStateInformation (state);
        std::printf ("      stato salvato: %d KB\n", (int) (state.getSize() / 1024));
    }
    {
        BrinaProcessor p;
        p.setPlayConfigDetails (fx ? 2 : 0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.setStateInformation (state.getData(), (int) state.getSize());
        auto s = p.getSample();
        check (std::abs (p.apvts.getRawParameterValue ("pitch")->load() - 12.0f) < 0.01f, "parametri ripristinati");
        check (s != nullptr && (fx ? std::abs (s->length.load() - 96000) <= 512 : s->length.load() == 96000),
               "campione ripristinato dallo stato");
        check (slotLength (p, 2) == 30000 && slotLength (p, 3) == 0
                   && (fx ? std::abs (slotLength (p, 1) - 48000) <= 512 : slotLength (p, 1) == 0),
               "lanes 2-4 restored from the state (file and recording)");
        check (p.apvts.getRawParameterValue ("voices")->load() == 8.0f
                   && std::abs (p.apvts.getRawParameterValue ("gfxCrush")->load() - 0.5f) < 1e-4f
                   && std::abs (p.apvts.getRawParameterValue ("pitch3")->load() - 12.0f) < 0.01f
                   && std::abs (p.apvts.getRawParameterValue ("pitch")->load() - 12.0f) < 0.01f
                   && std::abs (p.apvts.getRawParameterValue ("pitch2")->load()) < 0.01f,
               "voices, per-grain effects and every lane's controls restored");

        // restoring the same state twice leaves nothing behind; saving it again gives the same result
        p.loadFile (wav2, 3);
        p.setStateInformation (state.getData(), (int) state.getSize());
        juce::MemoryBlock again;
        p.getStateInformation (again);
        check (slotLength (p, 3) == 0 && (fx || again.getSize() == state.getSize()),   // (recordings are re-encoded)
               "the restored state is exactly the saved one");
        p.apvts.getParameter ("genOn")->setValueNotifyingHost (1.0f);
        check (render (p, 5.0, false) > 0.003, "suona dopo il ripristino");

        p.apvts.getParameter ("voices")->setValueNotifyingHost (3.0f / 7.0f);   // for the screenshot: four voices
        render (p, 2.0, false);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setVisible (true);

        // the UI at the default size and at the minimum size
        for (auto [w, h, name] : std::initializer_list<std::tuple<int, int, const char*>> {
                 { BrinaEditor::defaultWidth, BrinaEditor::defaultHeight, fx ? "brina_fx.png" : "brina.png" },
                 { 1060, 660, fx ? "brina_fx_small.png" : "brina_small.png" } })
        {
            editor->setSize (w, h);
            for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            auto png = outDir.getChildFile (name);
            png.deleteFile();
            juce::FileOutputStream out (png);
            check (editor->getWidth() == w && juce::PNGImageFormat().writeImageToStream (image, out),
                   "interfaccia disegnata -> " + png.getFileName());
        }
    }

    {   // --- a set saved with the previous version: only the parameters of that time and "samplePath"
        juce::XmlElement old ("BRINA");
        auto addParam = [&old] (const char* id, double value)
        {
            auto* e = old.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
            e->setAttribute ("value", value);
        };
        addParam ("pitch", 7.0);
        addParam ("density", 30.0);
        addParam ("genScale", 4.0);
        addParam ("posMode", 1.0);
        old.setAttribute ("samplePath", wav.getFullPathName());

        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (old, oldState);

        BrinaProcessor p;
        p.setPlayConfigDetails (fx ? 2 : 0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.loadFile (wav2, 1);       // anything already loaded in the other lanes must disappear
        p.apvts.getParameter ("voices")->setValueNotifyingHost (1.0f);
        p.apvts.getParameter ("gfxDrive")->setValueNotifyingHost (1.0f);
        p.apvts.getParameter ("pitch4")->setValueNotifyingHost (1.0f);
        p.apvts.getParameter ("level2")->setValueNotifyingHost (0.0f);
        p.setStateInformation (oldState.getData(), (int) oldState.getSize());

        auto raw = [&p] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
        auto near = [&raw] (const char* id, float v) { return std::abs (raw (id) - v) < 1e-3f; };   // i float passano dalla scala 0..1
        check (near ("pitch", 7.0f) && near ("density", 30.0f) && raw ("genScale") == 4.0f && raw ("posMode") == 1.0f
                   && slotLength (p, 0) == 96000,
               "previous-version set: parameters and sample in place");
        check (raw ("voices") == 1.0f && raw ("gfxFilter") == 0.0f && raw ("gfxCrush") == 0.0f && raw ("gfxDrive") == 0.0f
                   && near ("pitch4", 0.0f) && near ("level2", 1.0f) && slotLength (p, 1) == 0 && slotLength (p, 2) == 0 && slotLength (p, 3) == 0,
               "previous-version set: one voice, no effect, lanes 2-4 empty");
        p.apvts.getParameter ("genProb")->setValueNotifyingHost (1.0f);
        check (render (p, 5.0, false) > 0.003, "previous-version set: it plays");
    }

    wav.deleteFile();
    wav2.deleteFile();

    std::printf (failures == 0 ? "TUTTO OK\n" : "%d FALLITI\n", failures);
    return failures;
}

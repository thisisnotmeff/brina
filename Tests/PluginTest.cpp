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

    juce::MemoryBlock state;
    {
        BrinaProcessor p;
        p.setPlayConfigDetails (fx ? 2 : 0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

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

        check (std::abs (p.engine.scanVis.load()) > 1e-4f, "posizione automatica: scorre");
        setParam ("posMode", 1.0f);
        render (p, 1.0, false);
        check (p.engine.scanVis.load() == 0.0f, "posizione fissa: ferma su Position");
        setParam ("posMode", 0.0f);
        setParam ("genOn", 0.0f);
        render (p, 30.0, false);

        if (fx)
        {
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
        p.apvts.getParameter ("genOn")->setValueNotifyingHost (1.0f);
        check (render (p, 5.0, false) > 0.003, "suona dopo il ripristino");

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setVisible (true);
        for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto png = outDir.getChildFile (fx ? "brina_fx.png" : "brina.png");
        png.deleteFile();
        juce::FileOutputStream out (png);
        check (juce::PNGImageFormat().writeImageToStream (image, out), "interfaccia disegnata -> " + png.getFileName());
    }

    std::printf (failures == 0 ? "TUTTO OK\n" : "%d FALLITI\n", failures);
    return failures;
}

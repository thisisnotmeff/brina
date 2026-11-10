#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

bool BrinaProcessor::hasAudioInput()
{
   #if BRINA_FX
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
APVTS::ParameterLayout BrinaProcessor::createLayout()
{
    using FloatParam = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;
    APVTS::ParameterLayout layout;

    auto skewed = [] (float lo, float hi, float centre)
    {
        Range r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    auto add = [&layout] (const char* id, const char* name, Range range, float def, const char* unit = "")
    {
        const juce::String suffix = juce::String (unit).isEmpty() ? juce::String() : " " + juce::String (unit);
        auto toText = [suffix] (float v, int)
        {
            return (std::abs (v) >= 100.0f ? juce::String (juce::roundToInt (v)) : juce::String (v, 2)) + suffix;
        };
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { id, 1 }, name, range, def,
                                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
    };

    auto addChoice = [&layout] (const char* id, const char* name, const juce::StringArray& items, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    };

    auto addInt = [&layout] (const char* id, const char* name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    };

    add ("position", "Position", Range (0.0f, 1.0f), 0.3f);
    add ("spray",    "Spray",    Range (0.0f, 1.0f), 0.25f);
    add ("size",     "Size",     skewed (10.0f, 1500.0f, 200.0f), 220.0f, "ms");
    add ("density",  "Density",  skewed (1.0f, 80.0f, 15.0f), 18.0f, "gr/s");
    add ("pitch",    "Pitch",    Range (-24.0f, 24.0f, 0.01f), 0.0f, "st");
    add ("detune",   "Detune",   Range (0.0f, 100.0f), 8.0f, "ct");
    add ("octaves",  "Octaves",  Range (0.0f, 1.0f), 0.15f);
    add ("reverse",  "Reverse",  Range (0.0f, 1.0f), 0.2f);
    add ("spread",   "Spread",   Range (0.0f, 1.0f), 0.8f);
    add ("scan",     "Scan",     Range (-2.0f, 2.0f, 0.001f), 0.05f, "x");
    add ("attack",   "Attack",   skewed (5.0f, 8000.0f, 600.0f), 500.0f, "ms");
    add ("release",  "Release",  skewed (20.0f, 15000.0f, 2000.0f), 3000.0f, "ms");

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "genOn", 1 }, "Drift On", true));
    addChoice ("genRate",  "Drift Rate",  brina::rateNames(), 3);
    add ("genProb", "Drift Chance", Range (0.0f, 1.0f), 0.6f);
    addChoice ("genRoot",  "Drift Root",  brina::noteNames(), 0);
    addChoice ("genScale", "Drift Scale", brina::scaleNames(), 2);
    addInt ("genOctave", "Drift Octave", -3, 3, 0);
    addInt ("genRange",  "Drift Range", 1, 4, 2);
    add ("genLength", "Drift Length", skewed (0.25f, 8.0f, 2.0f), 3.0f, "steps");

    add ("drive", "Tape Drive", Range (0.0f, 1.0f), 0.2f);
    add ("wow",   "Tape Wow",   Range (0.0f, 1.0f), 0.25f);
    add ("tone",  "Tape Tone",  skewed (300.0f, 20000.0f, 3000.0f), 9000.0f, "Hz");

    add ("delayTime",     "Delay Time",     skewed (20.0f, 2500.0f, 400.0f), 380.0f, "ms");
    add ("delayFeedback", "Delay Feedback", Range (0.0f, 0.95f), 0.4f);
    add ("delayMix",      "Delay Mix",      Range (0.0f, 1.0f), 0.25f);

    add ("reverbSize", "Reverb Size", Range (0.0f, 1.0f), 0.85f);
    add ("reverbDamp", "Reverb Damp", Range (0.0f, 1.0f), 0.4f);
    add ("shimmer",    "Shimmer",     Range (0.0f, 1.0f), 0.25f);
    add ("reverbMix",  "Reverb Mix",  Range (0.0f, 1.0f), 0.4f);

    add ("dry",    "Dry Input", Range (0.0f, 1.0f), hasAudioInput() ? 1.0f : 0.0f);
    add ("volume", "Volume",    Range (-48.0f, 12.0f, 0.1f), 0.0f, "dB");

    return layout;
}

//==============================================================================
BrinaProcessor::BrinaProcessor()
    : AudioProcessor (hasAudioInput()
                        ? BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        : BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BRINA", createLayout())
{
    formatManager.registerBasicFormats();

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw = { get ("position"), get ("spray"), get ("size"), get ("density"), get ("pitch"), get ("detune"),
            get ("octaves"), get ("reverse"), get ("spread"), get ("scan"), get ("attack"), get ("release"),
            get ("genOn"), get ("genRate"), get ("genProb"), get ("genRoot"), get ("genScale"),
            get ("genOctave"), get ("genRange"), get ("genLength"),
            get ("drive"), get ("wow"), get ("tone"), get ("delayTime"), get ("delayFeedback"), get ("delayMix"),
            get ("reverbSize"), get ("reverbDamp"), get ("shimmer"), get ("reverbMix"), get ("dry"), get ("volume") };
}

bool BrinaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (! hasAudioInput())
        return true;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void BrinaProcessor::prepareToPlay (double sampleRate, int)
{
    hostSampleRate = sampleRate;
    engine.prepare (sampleRate);
    inputCopy.setSize (2, brina::Engine::maxBlock);
}

void BrinaProcessor::readParams (brina::Params& p) const
{
    auto f = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto i = [] (const std::atomic<float>* a) { return juce::roundToInt (a->load (std::memory_order_relaxed)); };

    p.position = f (raw.position);   p.spray = f (raw.spray);       p.sizeMs = f (raw.size);
    p.density = f (raw.density);     p.pitch = f (raw.pitch);       p.detuneCents = f (raw.detune);
    p.octaves = f (raw.octaves);     p.reverse = f (raw.reverse);   p.spread = f (raw.spread);
    p.scan = f (raw.scan);           p.attackMs = f (raw.attack);   p.releaseMs = f (raw.release);
    p.genOn = f (raw.genOn) > 0.5f;  p.genRate = i (raw.genRate);   p.genProb = f (raw.genProb);
    p.genRoot = i (raw.genRoot);     p.genScale = i (raw.genScale); p.genOctave = i (raw.genOctave);
    p.genRange = i (raw.genRange);   p.genLength = f (raw.genLength);
    p.drive = f (raw.drive);         p.wow = f (raw.wow);           p.toneHz = f (raw.tone);
    p.delayMs = f (raw.delayTime);   p.delayFeedback = f (raw.delayFeedback); p.delayMix = f (raw.delayMix);
    p.reverbSize = f (raw.reverbSize); p.reverbDamp = f (raw.reverbDamp);
    p.shimmer = f (raw.shimmer);     p.reverbMix = f (raw.reverbMix);
    p.dry = hasAudioInput() ? f (raw.dry) : 0.0f;
    p.volumeDb = f (raw.volume);
}

//==============================================================================
void BrinaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();

    if (buffer.getNumChannels() < 2 || total == 0)
        return;

    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked() && audioSample != current)
            audioSample = current;
    }

    brina::Params p;
    readParams (p);

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto position = ph->getPosition())
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    const bool hasInput = hasAudioInput() && getTotalNumInputChannels() > 0;
    const bool stereoIn = getTotalNumInputChannels() > 1;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    auto it = midi.begin();
    int pos = 0;

    while (pos < total)
    {
        while (it != midi.end() && (*it).samplePosition <= pos)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn())                              engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff())                        engine.noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
            ++it;
        }

        const int next = it != midi.end() ? juce::jmin (total, (*it).samplePosition) : total;
        const int n = juce::jmin (next - pos, (int) brina::Engine::maxBlock);

        const float* inL = nullptr;
        const float* inR = nullptr;

        if (hasInput)
        {
            inputCopy.copyFrom (0, 0, buffer, 0, pos, n);
            inputCopy.copyFrom (1, 0, buffer, stereoIn ? 1 : 0, pos, n);
            inL = inputCopy.getReadPointer (0);
            inR = inputCopy.getReadPointer (1);

            if (recording.load (std::memory_order_relaxed) && audioSample != nullptr && audioSample->recordable)
            {
                const int len = audioSample->length.load();
                const int room = audioSample->buffer.getNumSamples() - len;
                const int count = juce::jmin (n, room);

                if (count > 0)
                {
                    audioSample->buffer.copyFrom (0, len, inL, count);
                    audioSample->buffer.copyFrom (1, len, inR, count);
                    audioSample->length.store (len + count);
                }

                if (count < n)
                    recording.store (false);
            }
        }

        engine.process (audioSample.get(), inL, inR, outL + pos, outR + pos, n, p, bpm);
        pos += n;
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, total);
}

//==============================================================================
void BrinaProcessor::setSample (brina::SampleData::Ptr s)
{
    brina::SampleData::Ptr old;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        old = current;
        current = s;
    }

    // I vecchi campioni restano qui finche' il thread audio non li ha lasciati:
    // cosi' la memoria non viene mai liberata durante l'elaborazione audio.
    const juce::ScopedLock sl (graveyardLock);
    for (int i = graveyard.size(); --i >= 0;)
        if (graveyard.getObjectPointer (i)->getReferenceCount() == 1)
            graveyard.remove (i);

    if (old != nullptr)
        graveyard.add (old);
}

brina::SampleData::Ptr BrinaProcessor::getSample()
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return current;
}

juce::String BrinaProcessor::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
        return "Formato non supportato: " + file.getFileName();

    const auto length = (int) juce::jmin ((juce::int64) (maxFileSeconds * reader->sampleRate), reader->lengthInSamples);

    if (length < 256)
        return "File troppo corto";

    brina::SampleData::Ptr s = new brina::SampleData();
    s->buffer.setSize (2, length);
    s->buffer.clear();
    reader->read (&s->buffer, 0, length, 0, true, true);

    if (reader->numChannels == 1)
        s->buffer.copyFrom (1, 0, s->buffer, 0, 0, length);

    s->sampleRate = reader->sampleRate;
    s->name = file.getFileNameWithoutExtension();
    s->filePath = file.getFullPathName();
    s->length.store (length);

    recording.store (false);
    setSample (s);
    return {};
}

void BrinaProcessor::startRecording()
{
    if (! hasAudioInput())
        return;

    brina::SampleData::Ptr s = new brina::SampleData();
    s->buffer.setSize (2, (int) (maxRecordSeconds * hostSampleRate));
    s->buffer.clear();
    s->sampleRate = hostSampleRate;
    s->name = "Recording";
    s->recordable = true;

    setSample (s);
    recording.store (true);
}

void BrinaProcessor::stopRecording()
{
    recording.store (false);
}

//==============================================================================
void BrinaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();

    if (xml == nullptr)
        return;

    if (auto s = getSample())
    {
        const int length = s->length.load();

        if (s->filePath.isNotEmpty())
        {
            xml->setAttribute ("samplePath", s->filePath);
        }
        else if (length > 0)
        {
            // le registrazioni viaggiano dentro il progetto di Live, compresse in FLAC
            juce::MemoryBlock data;
            {
                juce::FlacAudioFormat flac;
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (data, false);
                auto options = juce::AudioFormatWriterOptions().withSampleRate (s->sampleRate)
                                                               .withNumChannels (2)
                                                               .withBitsPerSample (16);
                if (auto writer = flac.createWriterFor (stream, options))
                    writer->writeFromAudioSampleBuffer (s->buffer, 0, length);
            }

            if (data.getSize() > 0)
                xml->setAttribute ("recording", data.toBase64Encoding());
        }
    }

    copyXmlToBinary (*xml, dest);
}

void BrinaProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto path = xml->getStringAttribute ("samplePath");
    const auto rec = xml->getStringAttribute ("recording");
    xml->removeAttribute ("samplePath");
    xml->removeAttribute ("recording");
    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (path.isNotEmpty())
    {
        if (juce::File (path).existsAsFile())
            loadFile (juce::File (path));
    }
    else if (rec.isNotEmpty())
    {
        juce::MemoryBlock block;

        if (block.fromBase64Encoding (rec))
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> reader (
                flac.createReaderFor (new juce::MemoryInputStream (block, false), true));

            if (reader != nullptr && reader->lengthInSamples > 0)
            {
                const int length = (int) reader->lengthInSamples;
                brina::SampleData::Ptr s = new brina::SampleData();
                s->buffer.setSize (2, length);
                reader->read (&s->buffer, 0, length, 0, true, true);
                s->sampleRate = reader->sampleRate;
                s->name = "Recording";
                s->length.store (length);
                setSample (s);
            }
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* BrinaProcessor::createEditor()
{
    return new BrinaEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrinaProcessor();
}

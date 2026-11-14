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

    // Il numero di versione dei parametri: quelli aggiunti dopo la prima uscita ne prendono uno piu'
    // alto, cosi' negli Audio Unit finiscono in coda e l'ordine di quelli vecchi non cambia.
    int version = 1;

    auto add = [&layout, &version] (const char* id, const char* name, Range range, float def, const char* unit = "")
    {
        const juce::String suffix = juce::String (unit).isEmpty() ? juce::String() : " " + juce::String (unit);
        auto toText = [suffix] (float v, int)
        {
            return (std::abs (v) >= 100.0f ? juce::String (juce::roundToInt (v)) : juce::String (v, 2)) + suffix;
        };
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { id, version }, name, range, def,
                                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
    };

    auto addChoice = [&layout, &version] (const char* id, const char* name, const juce::StringArray& items, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, version }, name, items, def));
    };

    auto addInt = [&layout, &version] (const char* id, const char* name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, version }, name, lo, hi, def));
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

    // Aggiunti in coda: i parametri gia' esistenti restano identici (ID, ordine, intervalli).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "play", 1 }, "Play", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "kill", 1 }, "Kill", false));
    addChoice ("posMode", "Position Mode", juce::StringArray { "Auto", "Fixed" }, 0);

    // --- Multigrain, sorgenti ed effetti per grano. Con i valori di default (una voce, nessun
    // effetto) il suono e' quello di prima: i set salvati senza questi parametri non cambiano.
    version = 2;
    addInt ("voices", "Grain Voices", 1, brina::Engine::maxGrainVoices, 1);
    add ("voiceSpread", "Voice Spread", Range (0.0f, 1.0f), 0.3f);
    addChoice ("voiceMode", "Voice Mode", brina::voiceModeNames(), 0);
    addChoice ("sourceMode", "Source Mode", brina::sourceModeNames(), 1);

    add ("gfxFilter",     "Grain Filter Chance", Range (0.0f, 1.0f), 0.0f);
    add ("gfxFilterFreq", "Grain Filter Cutoff", skewed (80.0f, 12000.0f, 1200.0f), 1500.0f, "Hz");
    add ("gfxFilterRes",  "Grain Filter Reso",   Range (0.0f, 1.0f), 0.3f);
    add ("gfxCrush",      "Grain Crush Chance",  Range (0.0f, 1.0f), 0.0f);
    add ("gfxCrushAmt",   "Grain Crush Amount",  Range (0.0f, 1.0f), 0.5f);
    add ("gfxDrive",      "Grain Drive Chance",  Range (0.0f, 1.0f), 0.0f);
    add ("gfxDriveAmt",   "Grain Drive Amount",  Range (0.0f, 1.0f), 0.5f);

    // --- Forme del grano, Morph e Meteo. Di default sono spenti (probabilita' e quantita' a zero):
    // i set salvati prima suonano identici, campione per campione.
    version = 3;
    addChoice ("grainShape", "Grain Shape", brina::shapeNames(), 0);
    add ("shapeChance", "Shape Chance", Range (0.0f, 1.0f), 0.0f);
    add ("morphX",   "Morph X",      Range (0.0f, 1.0f), 0.5f);
    add ("morphY",   "Morph Y",      Range (0.0f, 1.0f), 0.5f);
    add ("morphAmt", "Morph Amount", Range (0.0f, 1.0f), 0.0f);

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
            get ("reverbSize"), get ("reverbDamp"), get ("shimmer"), get ("reverbMix"), get ("dry"), get ("volume"),
            get ("play"), get ("kill"), get ("posMode"),
            get ("voices"), get ("voiceSpread"), get ("voiceMode"), get ("sourceMode"),
            get ("gfxFilter"), get ("gfxFilterFreq"), get ("gfxFilterRes"),
            get ("gfxCrush"), get ("gfxCrushAmt"), get ("gfxDrive"), get ("gfxDriveAmt"),
            get ("grainShape"), get ("shapeChance"), get ("morphX"), get ("morphY"), get ("morphAmt") };
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
    lastKill = raw.kill->load() > 0.5f;
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
    p.running = f (raw.play) > 0.5f;
    p.fixedPosition = i (raw.posMode) == 1;
    p.grainVoices = i (raw.voices);  p.voiceSpread = f (raw.voiceSpread);
    p.voiceMode = i (raw.voiceMode); p.sourceMode = i (raw.sourceMode);
    p.gfxFilterChance = f (raw.gfxFilter); p.gfxFilterHz = f (raw.gfxFilterFreq); p.gfxFilterRes = f (raw.gfxFilterRes);
    p.gfxCrushChance = f (raw.gfxCrush);   p.gfxCrush = f (raw.gfxCrushAmt);
    p.gfxDriveChance = f (raw.gfxDrive);   p.gfxDrive = f (raw.gfxDriveAmt);
    p.grainShape = i (raw.grainShape);     p.shapeChance = f (raw.shapeChance);
    p.morphX = f (raw.morphX);  p.morphY = f (raw.morphY);  p.morphAmt = f (raw.morphAmt);
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

        if (lock.isLocked())
            for (size_t s = 0; s < (size_t) numSlots; ++s)
                if (audioSample[s] != current[s])
                    audioSample[s] = current[s];
    }

    brina::SampleData* slots[numSlots];
    for (size_t s = 0; s < (size_t) numSlots; ++s)
        slots[s] = audioSample[s].get();

    auto* recTarget = slots[(size_t) juce::jlimit (0, numSlots - 1, recordSlot.load (std::memory_order_relaxed))];

    brina::Params p;
    readParams (p);

    // Kill e' un comando, non uno stato: scatta a ogni cambio del parametro, cosi' funziona
    // sia dal tasto dell'interfaccia sia mappato in Live a un tasto MIDI (momentaneo o toggle).
    const bool killNow = raw.kill->load (std::memory_order_relaxed) > 0.5f;
    if (killNow != lastKill)
    {
        lastKill = killNow;
        engine.kill();
    }

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

            if (recording.load (std::memory_order_relaxed) && recTarget != nullptr && recTarget->recordable)
            {
                const int len = recTarget->length.load();
                const int room = recTarget->buffer.getNumSamples() - len;
                const int count = juce::jmin (n, room);

                if (count > 0)
                {
                    recTarget->buffer.copyFrom (0, len, inL, count);
                    recTarget->buffer.copyFrom (1, len, inR, count);
                    recTarget->length.store (len + count);
                }

                if (count < n)
                    recording.store (false);
            }
        }

        engine.processSlots (slots, inL, inR, outL + pos, outR + pos, n, p, bpm);
        pos += n;
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, total);
}

//==============================================================================
void BrinaProcessor::setSample (int slot, brina::SampleData::Ptr s)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    brina::SampleData::Ptr old;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        old = current[(size_t) slot];
        current[(size_t) slot] = s;
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

brina::SampleData::Ptr BrinaProcessor::getSample (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return {};

    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return current[(size_t) slot];
}

void BrinaProcessor::clearSlot (int slot)
{
    if (recordSlot.load() == slot)
        recording.store (false);

    setSample (slot, nullptr);
}

juce::String BrinaProcessor::loadFile (const juce::File& file, int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return "Slot non valido";

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

    if (recordSlot.load() == slot)
        recording.store (false);

    setSample (slot, s);
    return {};
}

void BrinaProcessor::startRecording (int slot)
{
    if (! hasAudioInput() || ! juce::isPositiveAndBelow (slot, numSlots))
        return;

    recording.store (false);    // se stava registrando in un altro slot, quello si ferma qui

    brina::SampleData::Ptr s = new brina::SampleData();
    s->buffer.setSize (2, (int) (maxRecordSeconds * hostSampleRate));
    s->buffer.clear();
    s->sampleRate = hostSampleRate;
    s->name = "Recording";
    s->recordable = true;

    setSample (slot, s);
    recordSlot.store (slot);
    recording.store (true);
}

void BrinaProcessor::stopRecording()
{
    recording.store (false);
}

void BrinaProcessor::triggerKill()
{
    if (auto* param = apvts.getParameter ("kill"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
        param->endChangeGesture();
    }
}

//==============================================================================
// Lo slot 1 usa gli attributi di sempre ("samplePath", "recording"): i set salvati con le versioni
// precedenti si aprono identici. Gli altri slot aggiungono il numero: "samplePath2", "recording2"...
static juce::String slotAttribute (const char* name, int slot)
{
    return slot == 0 ? juce::String (name) : juce::String (name) + juce::String (slot + 1);
}

void BrinaProcessor::saveSlot (juce::XmlElement& xml, int slot)
{
    auto s = getSample (slot);

    if (s == nullptr)
        return;

    const int length = s->length.load();

    if (s->filePath.isNotEmpty())
    {
        xml.setAttribute (slotAttribute ("samplePath", slot), s->filePath);
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
            xml.setAttribute (slotAttribute ("recording", slot), data.toBase64Encoding());
    }
}

void BrinaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();

    if (xml == nullptr)
        return;

    for (int slot = 0; slot < numSlots; ++slot)
        saveSlot (*xml, slot);

    xml->setAttribute ("uiSlot", selectedSlot.load());
    copyXmlToBinary (*xml, dest);
}

void BrinaProcessor::restoreSlot (int slot, const juce::String& path, const juce::String& recordingData)
{
    if (path.isNotEmpty())
    {
        if (juce::File (path).existsAsFile() && loadFile (juce::File (path), slot).isEmpty())
            return;
    }
    else if (recordingData.isNotEmpty())
    {
        juce::MemoryBlock block;

        if (block.fromBase64Encoding (recordingData))
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
                setSample (slot, s);
                return;
            }
        }
    }

    // Niente da caricare. Lo slot 1 resta com'era (come nelle versioni precedenti); gli altri
    // tornano vuoti, cosi' lo stato ripristinato e' esattamente quello salvato.
    if (slot > 0 && getSample (slot) != nullptr)
        clearSlot (slot);
}

void BrinaProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    juce::String paths[numSlots], recordings[numSlots];

    for (int slot = 0; slot < numSlots; ++slot)
    {
        paths[slot] = xml->getStringAttribute (slotAttribute ("samplePath", slot));
        recordings[slot] = xml->getStringAttribute (slotAttribute ("recording", slot));
        xml->removeAttribute (slotAttribute ("samplePath", slot));
        xml->removeAttribute (slotAttribute ("recording", slot));
    }

    selectedSlot.store (juce::jlimit (0, numSlots - 1, xml->getIntAttribute ("uiSlot", 0)));
    xml->removeAttribute ("uiSlot");

    juce::StringArray saved;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        saved.add (e->getStringAttribute ("id"));

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // I parametri che lo stato non conosce (aggiunti dopo che e' stato salvato) tornano al default,
    // invece di restare dove sono: cosi' un set o un preset vecchio suona come quando e' stato salvato.
    for (auto* param : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            if (! saved.contains (withId->paramID))
                param->setValueNotifyingHost (param->getDefaultValue());

    for (int slot = 0; slot < numSlots; ++slot)
        restoreSlot (slot, paths[slot], recordings[slot]);
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

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine.h"

class BrinaProcessor : public juce::AudioProcessor
{
public:
    static constexpr double maxRecordSeconds = 60.0;
    static constexpr double maxFileSeconds = 300.0;
    static constexpr int numSlots = brina::Engine::numSlots;

    BrinaProcessor();
    ~BrinaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    static bool hasAudioInput();

    /** Le sorgenti audio sono numSlots; "slot" va da 0 a numSlots - 1. Senza indicarlo si usa
        il primo, che e' il campione unico delle versioni precedenti.

        Thread dei messaggi. Restituisce un messaggio d'errore, vuoto se e' andata bene. */
    juce::String loadFile (const juce::File&, int slot = 0);
    void clearSlot (int slot);
    void startRecording (int slot = 0);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    int getRecordingSlot() const { return recordSlot.load(); }

    /** Kill: silenzio immediato, azzera note, grani e code degli effetti. */
    void triggerKill();

    brina::SampleData::Ptr getSample (int slot = 0);

    /** Lo slot mostrato nell'interfaccia (su cui agiscono Load, Rec e il trascinamento). */
    std::atomic<int> selectedSlot { 0 };
    juce::String getFormatWildcard() { return formatManager.getWildcardForAllFormats(); }

    juce::AudioProcessorValueTreeState apvts;
    brina::Engine engine;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setSample (int slot, brina::SampleData::Ptr);
    void readParams (brina::Params&) const;
    void saveSlot (juce::XmlElement&, int slot);
    void restoreSlot (int slot, const juce::String& path, const juce::String& recordingData);

    juce::AudioFormatManager formatManager;

    juce::SpinLock sampleLock;
    std::array<brina::SampleData::Ptr, numSlots> current;      // protetto da sampleLock
    juce::ReferenceCountedArray<brina::SampleData> graveyard; // mai liberati dal thread audio
    juce::CriticalSection graveyardLock;
    std::array<brina::SampleData::Ptr, numSlots> audioSample;  // usato solo dal thread audio

    std::atomic<bool> recording { false };
    std::atomic<int> recordSlot { 0 };
    double hostSampleRate = 44100.0;
    bool lastKill = false;              // usato solo dal thread audio
    juce::AudioBuffer<float> inputCopy;

    struct Raw
    {
        std::atomic<float> *position, *spray, *size, *density, *pitch, *detune, *octaves, *reverse,
                           *spread, *scan, *attack, *release,
                           *genOn, *genRate, *genProb, *genRoot, *genScale, *genOctave, *genRange, *genLength,
                           *drive, *wow, *tone, *delayTime, *delayFeedback, *delayMix,
                           *reverbSize, *reverbDamp, *shimmer, *reverbMix, *dry, *volume,
                           *play, *kill, *posMode,
                           *voices, *voiceSpread, *voiceMode, *sourceMode,
                           *gfxFilter, *gfxFilterFreq, *gfxFilterRes, *gfxCrush, *gfxCrushAmt, *gfxDrive, *gfxDriveAmt,
                           *grainShape, *shapeChance, *morphX, *morphY, *morphAmt;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrinaProcessor)
};

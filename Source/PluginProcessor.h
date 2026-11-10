#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine.h"

class BrinaProcessor : public juce::AudioProcessor
{
public:
    static constexpr double maxRecordSeconds = 60.0;
    static constexpr double maxFileSeconds = 300.0;

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

    /** Thread dei messaggi. Restituisce un messaggio d'errore, vuoto se e' andata bene. */
    juce::String loadFile (const juce::File&);
    void startRecording();
    void stopRecording();
    bool isRecording() const { return recording.load(); }

    brina::SampleData::Ptr getSample();
    juce::String getFormatWildcard() { return formatManager.getWildcardForAllFormats(); }

    juce::AudioProcessorValueTreeState apvts;
    brina::Engine engine;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setSample (brina::SampleData::Ptr);
    void readParams (brina::Params&) const;

    juce::AudioFormatManager formatManager;

    juce::SpinLock sampleLock;
    brina::SampleData::Ptr current;    // protetto da sampleLock
    juce::ReferenceCountedArray<brina::SampleData> graveyard; // mai liberati dal thread audio
    juce::CriticalSection graveyardLock;
    brina::SampleData::Ptr audioSample; // usato solo dal thread audio

    std::atomic<bool> recording { false };
    double hostSampleRate = 44100.0;
    juce::AudioBuffer<float> inputCopy;

    struct Raw
    {
        std::atomic<float> *position, *spray, *size, *density, *pitch, *detune, *octaves, *reverse,
                           *spread, *scan, *attack, *release,
                           *genOn, *genRate, *genProb, *genRoot, *genScale, *genOctave, *genRange, *genLength,
                           *drive, *wow, *tone, *delayTime, *delayFeedback, *delayMix,
                           *reverbSize, *reverbDamp, *shimmer, *reverbMix, *dry, *volume;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrinaProcessor)
};

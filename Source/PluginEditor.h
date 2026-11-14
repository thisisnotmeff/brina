#pragma once

#include "PluginProcessor.h"

namespace brina::ui
{
const juce::Colour background { 0xff12161b };
const juce::Colour panel      { 0xff1a2028 };
const juce::Colour outline    { 0xff2a333e };
const juce::Colour accent     { 0xff8fd6ea };
const juce::Colour warm       { 0xffe8a87c };
const juce::Colour text       { 0xffc8d3dc };
const juce::Colour dim        { 0xff6f7d8a };
}

//==============================================================================
class BrinaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BrinaLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

//==============================================================================
/** Forma d'onda dello slot selezionato, con posizione, zona di spray, voci e grani attivi. */
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (BrinaProcessor&);

    void refresh();     // chiamato dal timer dell'editor
    void paint (juce::Graphics&) override;
    void resized() override { lastLength = -1; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool dragHover = false;

private:
    void rebuildPeaks();
    void setPositionFromMouse (const juce::MouseEvent&);

    BrinaProcessor& proc;
    brina::SampleData::Ptr sample;
    int lastLength = -1;
    std::vector<float> peakMin, peakMax;
};

//==============================================================================
/** Pad XY del Morph: gli slot 1-4 agli angoli, il punto sceglie da quali slot leggono i grani.
    Gli angoli si accendono con i grani che leggono da quello slot. Doppio clic: torna al centro. */
class MorphPad : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit MorphPad (BrinaProcessor&);

    void refresh();     // chiamato dal timer dell'editor
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> padArea() const;
    void setFromMouse (const juce::MouseEvent&);

    BrinaProcessor& proc;
    juce::ParameterAttachment xAttachment, yAttachment;
    float x = 0.5f, y = 0.5f, amount = 0.0f;
    std::array<float, BrinaProcessor::numSlots> activity {};
    std::array<bool, BrinaProcessor::numSlots> filled {};
};

//==============================================================================
/** Il cielo del Meteo: una striscia da calma a tempesta con il punto dove si trova adesso. */
class WeatherMeter : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    explicit WeatherMeter (BrinaProcessor& p) : proc (p) {}

    void refresh();     // chiamato dal timer dell'editor
    void paint (juce::Graphics&) override;

private:
    BrinaProcessor& proc;
    float intensity = -1.0f;
    std::vector<float> history;      // ultimi valori, per la scia
};

//==============================================================================
class BrinaEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit BrinaEditor (BrinaProcessor&);
    ~BrinaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { waveform.dragHover = true; }
    void fileDragExit (const juce::StringArray&) override { waveform.dragHover = false; }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Section { juce::String title; juce::Rectangle<int> bounds; };

    // posizioni in "knobs" e "choices", nell'ordine in cui vengono creati
    enum KnobIndex { scanKnob = 9, firstDriftKnob = 12, firstFxKnob = 16, dryKnob = 26,
                     voicesKnob = 28, voiceSpreadKnob = 29, firstGrainFxKnob = 30, numGrainFxKnobs = 7,
                     shapeChanceKnob = 37, morphAmtKnob = 38, weatherAmtKnob = 39, weatherRateKnob = 40 };
    enum ChoiceIndex { numDriftChoices = 3, voiceModeChoice = 3, sourceModeChoice = 4, shapeChoice = 5, weatherMoodChoice = 6 };

    Knob& addKnob (const char* paramId, const char* title);
    Choice& addChoice (const char* paramId, const char* title);
    void timerCallback() override;
    void load (const juce::File&, int slot);
    void openChooser();
    void selectSlot (int slot);
    int selectedSlot() const { return juce::jlimit (0, BrinaProcessor::numSlots - 1, proc.selectedSlot.load()); }

    BrinaProcessor& proc;
    BrinaLookAndFeel lookAndFeel;
    WaveformView waveform;
    MorphPad morphPad;
    WeatherMeter weatherMeter;

    juce::TextButton loadButton { "Load" }, recButton { "Rec" }, clearButton { "Clear" };
    std::array<juce::TextButton, BrinaProcessor::numSlots> slotButtons;
    juce::Rectangle<int> sourceRow;
    std::vector<juce::Rectangle<int>> dividers;
    juce::TextButton playButton { "Playing" }, killButton { "Kill" }, positionButton { "Position: Auto" };
    juce::ToggleButton driftButton { "On" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> driftAttachment, playAttachment, positionAttachment;
    juce::Label sampleLabel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Choice> choices;
    std::vector<Section> sections;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrinaEditor)
};

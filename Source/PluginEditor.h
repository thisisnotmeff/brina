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
/** Forma d'onda del campione, con posizione, zona di spray e grani attivi. */
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

    Knob& addKnob (const char* paramId, const char* title);
    Choice& addChoice (const char* paramId, const char* title);
    void timerCallback() override;
    void load (const juce::File&);
    void openChooser();

    BrinaProcessor& proc;
    BrinaLookAndFeel lookAndFeel;
    WaveformView waveform;

    juce::TextButton loadButton { "Load" }, recButton { "Rec" };
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

#pragma once

#include "PluginProcessor.h"
#include <map>

namespace brina::ui
{
const juce::Colour background { 0xff12161b };
const juce::Colour panel      { 0xff1a2028 };
const juce::Colour outline    { 0xff2a333e };
const juce::Colour accent     { 0xff8fd6ea };
const juce::Colour warm       { 0xffe8a87c };
const juce::Colour text       { 0xffc8d3dc };
const juce::Colour dim        { 0xff6f7d8a };

/** Il colore di ogni corsia: forma d'onda, manopole e numero. */
inline juce::Colour laneColour (int lane)
{
    static const juce::Colour colours[] { accent, warm, juce::Colour (0xffa8d59a), juce::Colour (0xffc3a6e8) };
    return colours[(size_t) juce::jlimit (0, 3, lane)];
}
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
/** Forma d'onda di una corsia, con posizione, zona di spray, voci e grani attivi. */
class WaveformView : public juce::Component
{
public:
    WaveformView (BrinaProcessor&, int lane);

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
    juce::RangedAudioParameter* positionParam() const;

    BrinaProcessor& proc;
    const int lane;
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
    void fileDragEnter (const juce::StringArray&, int x, int y) override { setDragLane (laneAt (x, y)); }
    void fileDragMove (const juce::StringArray&, int x, int y) override  { setDragLane (laneAt (x, y)); }
    void fileDragExit (const juce::StringArray&) override                { setDragLane (-1); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

    static constexpr int defaultWidth = 1200, defaultHeight = 760;

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

    /** Una corsia: forma d'onda, Load / Rec / Clear e i suoi controlli dei grani. */
    struct Lane
    {
        Lane (BrinaProcessor& p, int index) : wave (p, index) {}

        WaveformView wave;
        juce::TextButton load { "Load" }, rec { "Rec" }, clear { "Clear" };
        juce::Label name;
        std::vector<Knob*> knobs;           // nell'ordine di BrinaProcessor::laneParamBases()
        juce::Rectangle<int> bounds, badge;
        bool wasFull = true;
    };

    struct Section { juce::String title; juce::Rectangle<int> bounds; };

    Knob& addKnob (const juce::String& paramId, const juce::String& title, juce::Colour colour = brina::ui::accent);
    Choice& addChoice (const char* paramId, const char* title);
    Knob& knob (const juce::String& paramId) { return *knobById.at (paramId); }
    Choice& choice (const juce::String& paramId) { return *choiceById.at (paramId); }
    void placeKnob (Knob&, juce::Rectangle<int> cell);
    void placeChoice (Choice&, juce::Rectangle<int> cell);

    void timerCallback() override;
    int laneAt (int x, int y) const;
    void setDragLane (int lane);
    void load (const juce::File&, int lane);
    void openChooser (int lane);
    void setStatus (const juce::String&);

    BrinaProcessor& proc;
    BrinaLookAndFeel lookAndFeel;

    juce::OwnedArray<Lane> lanes;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Choice> choices;
    std::map<juce::String, Knob*> knobById;
    std::map<juce::String, Choice*> choiceById;

    juce::TextButton playButton { "Playing" }, killButton { "Kill" }, positionButton { "Position: Auto" };
    juce::ToggleButton driftButton { "On" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> driftAttachment, playAttachment, positionAttachment;
    juce::Label statusLabel;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<Section> sections;
    std::vector<juce::Rectangle<int>> dividers;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    juce::uint32 statusTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrinaEditor)
};

#include "PluginEditor.h"
#include <cstring>

using namespace brina;

//==============================================================================
BrinaLookAndFeel::BrinaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, ui::background);
    setColour (juce::Slider::textBoxTextColourId, ui::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, ui::accent);
    setColour (juce::Label::textColourId, ui::dim);
    setColour (juce::TextButton::buttonColourId, ui::panel);
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc8443c));
    setColour (juce::TextButton::textColourOffId, ui::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, ui::background);
    setColour (juce::ComboBox::outlineColourId, ui::outline);
    setColour (juce::ComboBox::textColourId, ui::text);
    setColour (juce::ComboBox::arrowColourId, ui::dim);
    setColour (juce::PopupMenu::backgroundColourId, ui::panel);
    setColour (juce::PopupMenu::textColourId, ui::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::accent.withAlpha (0.25f));
    setColour (juce::ToggleButton::textColourId, ui::text);
    setColour (juce::ToggleButton::tickColourId, ui::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, ui::dim);
}

void BrinaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 2.0f;
    const auto centre = area.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float stroke = radius > 22.0f ? 3.0f : 2.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (ui::outline);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // i parametri bipolari (pitch, scan...) partono dal centro
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                               : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::rotarySliderFillColourId) : ui::dim);
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tip = centre.getPointOnCircumference (radius - 6.0f, angle);
    g.setColour (ui::text);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.35f, angle), tip }, 2.0f);
}

//==============================================================================
WaveformView::WaveformView (BrinaProcessor& p, int laneIndex) : proc (p), lane (laneIndex)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

juce::RangedAudioParameter* WaveformView::positionParam() const
{
    return proc.apvts.getParameter (BrinaProcessor::laneParamId ("position", lane));
}

void WaveformView::refresh()
{
    auto now = proc.getSample (lane);
    const int length = now != nullptr ? now->length.load() : 0;

    if (now != sample || length != lastLength)
    {
        sample = now;
        lastLength = length;
        rebuildPeaks();
    }

    repaint();
}

void WaveformView::rebuildPeaks()
{
    const int width = juce::jmax (1, getWidth());
    peakMin.assign ((size_t) width, 0.0f);
    peakMax.assign ((size_t) width, 0.0f);

    if (sample == nullptr || lastLength <= 0)
        return;

    const int length = juce::jmin (lastLength, sample->buffer.getNumSamples());
    const float* l = sample->buffer.getReadPointer (0);
    const float* r = sample->buffer.getReadPointer (1);
    const double perPixel = (double) length / width;
    const int stride = juce::jmax (1, (int) (perPixel / 96.0));

    for (int x = 0; x < width; ++x)
    {
        const int a = (int) (x * perPixel);
        const int b = juce::jmin (length, juce::jmax (a + 1, (int) ((x + 1) * perPixel)));
        float lo = 0.0f, hi = 0.0f;

        for (int i = a; i < b; i += stride)
        {
            const float v = 0.5f * (l[i] + r[i]);
            lo = juce::jmin (lo, v);
            hi = juce::jmax (hi, v);
        }

        peakMin[(size_t) x] = lo;
        peakMax[(size_t) x] = hi;
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto colour = ui::laneColour (lane);
    g.setColour (ui::background.withAlpha (0.6f));
    g.fillRoundedRectangle (area, 4.0f);

    const float w = area.getWidth(), h = area.getHeight(), mid = h * 0.5f;
    const bool empty = sample == nullptr || lastLength <= 0;

    if (empty)
    {
        g.setColour (ui::dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (BrinaProcessor::hasAudioInput()
                              ? "Trascina qui un file audio, oppure Load o Rec"
                              : "Trascina qui un file audio, oppure Load",
                          getLocalBounds().reduced (10), juce::Justification::centred, 2);
    }
    else
    {
        auto value = [this] (const char* base) { return proc.apvts.getRawParameterValue (BrinaProcessor::laneParamId (base, lane))->load(); };
        const float position = value ("position");
        const float spray = value ("spray");
        const float centre = position + proc.engine.scanVis[(size_t) lane].load();
        const float cx = (centre - std::floor (centre)) * w;
        const float half = spray * spray * 0.5f * w;

        // zona da cui nascono i grani
        g.setColour (colour.withAlpha (0.12f));
        for (float offset : { -w, 0.0f, w })
            g.fillRect (juce::Rectangle<float> (cx - half + offset, 0.0f, half * 2.0f, h).getIntersection (area));

        g.setColour (colour.withAlpha (0.6f));
        for (size_t x = 0; x < peakMax.size(); ++x)
        {
            const float top = mid - peakMax[x] * mid * 0.92f;
            const float bottom = mid - peakMin[x] * mid * 0.92f;
            g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));
        }

        // i grani di questa corsia (il valore e' corsia + posizione)
        g.setColour (ui::text.withAlpha (0.8f));
        for (auto& vis : proc.engine.grainVis)
        {
            const float p = vis.load (std::memory_order_relaxed) - (float) lane;
            if (p >= 0.0f && p < 1.0f)
                g.fillRect (p * w - 0.75f, 3.0f, 1.5f, h - 6.0f);
        }

        // i punti di lettura delle altre voci, aperti a ventaglio attorno alla posizione
        const int voices = juce::roundToInt (proc.apvts.getRawParameterValue ("voices")->load());
        const float fan = proc.apvts.getRawParameterValue ("voiceSpread")->load() * 0.5f;

        if (voices > 1 && fan > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));

            for (int k = 0; k < voices; ++k)
            {
                const float v = centre + ((float) k / (float) (voices - 1) - 0.5f) * fan;
                g.fillRect ((v - std::floor (v)) * w - 0.5f, h * 0.25f, 1.0f, h * 0.5f);
            }
        }

        g.setColour (juce::Colours::white);
        g.fillRect (cx - 0.75f, 0.0f, 1.5f, h);
    }

    if (dragHover)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (area.reduced (1.0f), 4.0f, 2.0f);
    }
}

void WaveformView::setPositionFromMouse (const juce::MouseEvent& e)
{
    if (auto* param = positionParam())
    {
        // tolgo lo scorrimento automatico (Scan), cosi' il cursore finisce dove si clicca
        float v = (float) e.x / (float) juce::jmax (1, getWidth()) - proc.engine.scanVis[(size_t) lane].load();
        v -= std::floor (v);
        param->setValueNotifyingHost (param->convertTo0to1 (juce::jlimit (0.0f, 1.0f, v)));
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* param = positionParam())
        param->beginChangeGesture();
    setPositionFromMouse (e);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e) { setPositionFromMouse (e); }

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (auto* param = positionParam())
        param->endChangeGesture();
}

//==============================================================================
BrinaEditor::BrinaEditor (BrinaProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lookAndFeel);

    // --- trasporto: Play/Stop (le code sfumano) e Kill (silenzio immediato)
    playButton.setClickingTogglesState (true);
    playButton.setColour (juce::TextButton::buttonOnColourId, ui::accent);
    playButton.setColour (juce::TextButton::textColourOnId, ui::background);
    playButton.setTooltip ("Start / stop: da fermo non nascono note ne' grani, le code sfumano da sole");
    addAndMakeVisible (playButton);
    playAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "play", playButton);

    killButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff7a2a26));
    killButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    killButton.setTooltip ("Kill: silenzio immediato, azzera note, grani e code di delay e riverbero");
    killButton.onClick = [this] { proc.triggerKill(); };
    addAndMakeVisible (killButton);

    // --- posizione: Auto (scorre con Scan) oppure Fixed (ferma su Position), per tutte le corsie
    positionButton.setClickingTogglesState (true);
    positionButton.setColour (juce::TextButton::buttonColourId, ui::background);
    positionButton.setColour (juce::TextButton::buttonOnColourId, ui::warm);
    positionButton.setColour (juce::TextButton::textColourOnId, ui::background);
    positionButton.setTooltip ("Auto: in ogni corsia la posizione scorre da sola (Scan). Fixed: resta ferma dove la metti");
    addAndMakeVisible (positionButton);
    positionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "posMode", positionButton);

    statusLabel.setColour (juce::Label::textColourId, ui::warm);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    // --- le corsie
    static const char* laneTitles[] { "Position", "Spray", "Size", "Density", "Pitch", "Detune", "Octaves",
                                      "Reverse", "Spread", "Scan", "Attack", "Release", "Level" };
    jassert (BrinaProcessor::laneParamBases().size() == (int) std::size (laneTitles));

    for (int i = 0; i < BrinaProcessor::numSlots; ++i)
    {
        auto* lane = lanes.add (new Lane (proc, i));
        const auto colour = ui::laneColour (i);

        addAndMakeVisible (lane->wave);

        for (auto* b : { &lane->load, &lane->rec, &lane->clear })
        {
            b->setColour (juce::TextButton::buttonColourId, ui::background);
            addAndMakeVisible (*b);
        }

        lane->load.onClick = [this, i] { openChooser (i); };
        lane->clear.onClick = [this, i] { setStatus ({}); proc.clearSlot (i); };
        lane->clear.setTooltip ("Svuota la corsia");
        lane->rec.setVisible (BrinaProcessor::hasAudioInput());
        lane->rec.setTooltip ("Registra l'ingresso della traccia in questa corsia (max 60 s)");
        lane->rec.onClick = [this, i]
        {
            setStatus ({});
            if (proc.isRecording() && proc.getRecordingSlot() == i) proc.stopRecording();
            else                                                    proc.startRecording (i);
        };

        lane->name.setColour (juce::Label::textColourId, ui::text);
        lane->name.setFont (juce::FontOptions (12.5f));
        lane->name.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (lane->name);

        for (int k = 0; k < (int) std::size (laneTitles); ++k)
            lane->knobs.push_back (&addKnob (BrinaProcessor::laneParamId (BrinaProcessor::laneParamBases()[k], i),
                                             laneTitles[k], colour));

        lane->knobs.back()->slider.setTooltip ("Volume della corsia " + juce::String (i + 1));
    }

    // --- sezioni comuni a tutte le corsie
    for (auto [id, title] : std::initializer_list<std::pair<const char*, const char*>> {
             { "voices", "Voices" }, { "voiceSpread", "Spread" },
             { "gfxFilter", "Filter" }, { "gfxFilterFreq", "Cutoff" }, { "gfxFilterRes", "Reso" },
             { "gfxCrush", "Crush" }, { "gfxCrushAmt", "Amount" },
             { "gfxDrive", "Drive" }, { "gfxDriveAmt", "Amount" },
             { "genProb", "Chance" }, { "genOctave", "Octave" }, { "genRange", "Range" }, { "genLength", "Length" },
             { "drive", "Drive" }, { "wow", "Wow" }, { "tone", "Tone" },
             { "delayTime", "Time" }, { "delayFeedback", "Feedback" }, { "delayMix", "Mix" },
             { "reverbSize", "Size" }, { "reverbDamp", "Damp" }, { "shimmer", "Shimmer" }, { "reverbMix", "Mix" },
             { "dry", "Dry In" }, { "volume", "Volume" } })
        addKnob (id, title);

    addChoice ("voiceMode", "Intervals");
    addChoice ("genRate", "Rate");
    addChoice ("genRoot", "Root");
    addChoice ("genScale", "Scale");

    addAndMakeVisible (driftButton);
    driftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "genOn", driftButton);

    knob ("voices").slider.setTooltip ("Quanti grani nascono insieme per ogni nota, in ogni corsia (multigrain)");
    knob ("voiceSpread").slider.setTooltip ("Distanza tra i punti di lettura delle voci, attorno a Position");
    choice ("voiceMode").box.setTooltip ("Intervalli tra le voci: Unison (appena stonate), Octaves, Fifths, "
                                         "Chord (terze sulla scala di Drift)");

    for (auto* id : { "gfxFilter", "gfxCrush", "gfxDrive" })
        knob (id).slider.setTooltip ("Quanti grani ricevono l'effetto, in tutte le corsie: 0 nessuno, 1 tutti. "
                                     "Ogni grano puo' averne piu' di uno");

    knob ("dry").slider.setVisible (BrinaProcessor::hasAudioInput());
    knob ("dry").label.setVisible (BrinaProcessor::hasAudioInput());

    setResizable (true, true);
    setResizeLimits (1060, 660, 1900, 1300);
    setSize (defaultWidth, defaultHeight);
    startTimerHz (30);
}

BrinaEditor::~BrinaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

BrinaEditor::Knob& BrinaEditor::addKnob (const juce::String& paramId, const juce::String& title, juce::Colour colour)
{
    auto* k = knobs.add (new Knob());
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setColour (juce::Slider::textBoxTextColourId, ui::text);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    k->label.setText (title, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (12.0f));
    k->label.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k->slider);
    knobById[paramId] = k;
    return *k;
}

BrinaEditor::Choice& BrinaEditor::addChoice (const char* paramId, const char* title)
{
    auto* c = choices.add (new Choice());

    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        c->box.addItemList (param->choices, 1);

    c->label.setText (title, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centred);
    c->label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (c->box);
    addAndMakeVisible (c->label);
    c->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, paramId, c->box);
    choiceById[paramId] = c;
    return *c;
}

//==============================================================================
void BrinaEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);

    g.setColour (ui::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("BRINA", 16, 8, 100, 30, juce::Justification::centredLeft);
    g.setColour (ui::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (BrinaProcessor::hasAudioInput() ? "granular fx" : "granular instrument",
                96, 12, 160, 26, juce::Justification::centredLeft);

    for (int i = 0; i < lanes.size(); ++i)
    {
        const auto& lane = *lanes[i];
        const auto colour = ui::laneColour (i);
        const auto b = lane.bounds.toFloat();

        g.setColour (ui::panel);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (colour.withAlpha (lane.wasFull ? 0.9f : 0.35f));
        g.fillRoundedRectangle (b.withWidth (4.0f), 2.0f);

        g.setColour (colour);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), lane.badge, juce::Justification::centredLeft);
    }

    for (auto& s : sections)
    {
        g.setColour (ui::panel);
        g.fillRoundedRectangle (s.bounds.toFloat(), 6.0f);
        g.setColour (ui::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.title, s.bounds.getX() + 10, s.bounds.getY() + 5, s.bounds.getWidth() - 20, 14,
                    juce::Justification::centredLeft);
    }

    g.setColour (ui::outline);
    for (auto& d : dividers)
        g.fillRect (d);
}

void BrinaEditor::placeKnob (Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds (cell.removeFromTop (14));
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmax (40, cell.getWidth()), 16);
    k.slider.setBounds (cell);
}

void BrinaEditor::placeChoice (Choice& c, juce::Rectangle<int> cell)
{
    c.label.setBounds (cell.removeFromTop (14));
    c.box.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 26));
}

void BrinaEditor::resized()
{
    const int margin = 12, gap = 8, laneGap = 6, rowHeight = 108;
    auto area = getLocalBounds().reduced (margin, 0);
    sections.clear();
    dividers.clear();

    // --- intestazione
    auto header = area.removeFromTop (46).reduced (0, 9);
    header.removeFromLeft (214);    // titolo
    playButton.setBounds (header.removeFromLeft (72));
    header.removeFromLeft (gap);
    killButton.setBounds (header.removeFromLeft (52));
    header.removeFromLeft (gap);
    positionButton.setBounds (header.removeFromLeft (124));
    header.removeFromLeft (gap);
    statusLabel.setBounds (header);

    area.removeFromBottom (margin);
    auto bottomRow = area.removeFromBottom (rowHeight);
    area.removeFromBottom (gap);
    auto middleRow = area.removeFromBottom (rowHeight);
    area.removeFromBottom (gap);

    auto content = [] (juce::Rectangle<int> section) { return section.withTrimmedTop (22).reduced (6, 4); };

    // --- le corsie: a sinistra il campione, a destra i dodici controlli dei grani e il volume
    const int laneHeight = (area.getHeight() - laneGap * (lanes.size() - 1)) / lanes.size();

    for (auto* lane : lanes)
    {
        lane->bounds = area.removeFromTop (laneHeight);
        area.removeFromTop (laneGap);

        auto inner = lane->bounds.reduced (8, 5).withTrimmedLeft (6);
        auto left = inner.removeFromLeft (juce::roundToInt ((float) inner.getWidth() * 0.3f));
        inner.removeFromLeft (gap + 4);

        auto top = left.removeFromTop (20);
        left.removeFromTop (4);
        lane->badge = top.removeFromLeft (16);

        for (auto* b : { &lane->clear, &lane->rec, &lane->load })
        {
            if (! b->isVisible())
                continue;
            b->setBounds (top.removeFromRight (b == &lane->clear ? 48 : 44));
            top.removeFromRight (4);
        }

        lane->name.setBounds (top);
        lane->wave.setBounds (left);

        const int count = (int) lane->knobs.size();
        const int cell = inner.getWidth() / count;

        for (int k = 0; k < count; ++k)
        {
            if (k == count - 1)     // un filo prima del volume della corsia
                dividers.push_back ({ inner.getX() - 1, inner.getY() + 8, 1, inner.getHeight() - 16 });

            placeKnob (*lane->knobs[(size_t) k], inner.removeFromLeft (cell));
        }
    }

    // --- Voices + Grain FX + Out, comuni a tutte le corsie
    {
        auto row = middleRow;
        const float unit = (float) (row.getWidth() - 2 * gap) / 13.0f;

        auto voices = row.removeFromLeft (juce::roundToInt (unit * 4.0f));
        row.removeFromLeft (gap);
        sections.push_back ({ "VOICES  -  multigrain", voices });
        {
            auto inner = content (voices);
            const int cell = inner.getWidth() / 4;
            placeKnob (knob ("voices"), inner.removeFromLeft (cell));
            placeKnob (knob ("voiceSpread"), inner.removeFromLeft (cell));
            placeChoice (choice ("voiceMode"), inner.reduced (8, 0));
        }

        auto out = row.removeFromRight (juce::roundToInt (unit * 2.0f));
        row.removeFromRight (gap);
        sections.push_back ({ "OUT", out });
        {
            auto inner = content (out);
            const int cell = inner.getWidth() / 2;
            placeKnob (knob ("dry"), inner.removeFromLeft (cell));
            placeKnob (knob ("volume"), inner);
        }

        sections.push_back ({ "GRAIN FX  -  how many grains get each effect", row });
        {
            auto inner = content (row);
            const char* ids[] { "gfxFilter", "gfxFilterFreq", "gfxFilterRes", "gfxCrush", "gfxCrushAmt", "gfxDrive", "gfxDriveAmt" };
            const int cell = inner.getWidth() / (int) std::size (ids);

            for (int i = 0; i < (int) std::size (ids); ++i)
            {
                if (i == 3 || i == 5)   // un filo tra filtro, crush e drive
                    dividers.push_back ({ inner.getX(), inner.getY() + 6, 1, inner.getHeight() - 12 });

                placeKnob (knob (ids[i]), inner.removeFromLeft (cell));
            }
        }
    }

    // --- Drift + Tape + Delay + Reverb
    {
        auto row = bottomRow;
        const float unit = (float) (row.getWidth() - 3 * gap) / 20.0f;
        auto take = [&] (float units)
        {
            auto r = row.removeFromLeft (juce::roundToInt (unit * units));
            row.removeFromLeft (gap);
            return r;
        };

        auto drift = take (10.0f);
        sections.push_back ({ "DRIFT  -  generative notes", drift });
        {
            auto inner = content (drift);
            driftButton.setBounds (inner.removeFromLeft (juce::roundToInt (unit)).withSizeKeepingCentre (56, 24));

            // Scale ha i nomi piu' lunghi ("Penta Maj", "Whole Tone"): le do piu' spazio
            for (auto [id, units] : std::initializer_list<std::pair<const char*, float>> {
                     { "genRate", 1.3f }, { "genRoot", 1.0f }, { "genScale", 2.5f } })
                placeChoice (choice (id), inner.removeFromLeft (juce::roundToInt (unit * units)).reduced (4, 0));

            const int cell = inner.getWidth() / 4;
            for (auto* id : { "genProb", "genOctave", "genRange", "genLength" })
                placeKnob (knob (id), inner.removeFromLeft (cell));
        }

        for (auto [title, ids] : std::initializer_list<std::pair<const char*, std::vector<const char*>>> {
                 { "TAPE",   { "drive", "wow", "tone" } },
                 { "DELAY",  { "delayTime", "delayFeedback", "delayMix" } },
                 { "REVERB", { "reverbSize", "reverbDamp", "shimmer", "reverbMix" } } })
        {
            auto section = std::strcmp (title, "REVERB") == 0 ? row : take ((float) ids.size());
            sections.push_back ({ title, section });

            auto inner = content (section);
            const int cell = inner.getWidth() / (int) ids.size();
            for (auto* id : ids)
                placeKnob (knob (id), inner.removeFromLeft (cell));
        }
    }

    repaint();
}

//==============================================================================
void BrinaEditor::timerCallback()
{
    const bool rec = proc.isRecording();
    const bool fixed = positionButton.getToggleState();
    bool repaintLanes = false;

    for (int i = 0; i < lanes.size(); ++i)
    {
        auto& lane = *lanes[i];
        lane.wave.refresh();

        auto s = proc.getSample (i);
        const int length = s != nullptr ? s->length.load() : 0;
        const bool full = length > 0;
        const bool recHere = rec && proc.getRecordingSlot() == i;

        const juce::String name = full ? s->name + "   " + juce::String ((double) length / s->sampleRate, 1) + " s"
                                       : juce::String ("vuota");
        if (name != lane.name.getText())
            lane.name.setText (name, juce::dontSendNotification);
        lane.name.setColour (juce::Label::textColourId, full ? ui::text : ui::dim);

        lane.rec.setToggleState (recHere, juce::dontSendNotification);
        lane.rec.setButtonText (recHere ? "Stop" : "Rec");
        lane.clear.setEnabled (s != nullptr);

        // una corsia vuota resta regolabile, ma si vede che non suona
        if (full != lane.wasFull)
        {
            lane.wasFull = full;
            repaintLanes = true;
            for (auto* k : lane.knobs)
            {
                k->slider.setAlpha (full ? 1.0f : 0.45f);
                k->label.setAlpha (full ? 1.0f : 0.6f);
            }
        }

        lane.knobs[9]->slider.setEnabled (! fixed);     // Scan non ha effetto a posizione fissa
    }

    if (repaintLanes)
        repaint();

    playButton.setButtonText (playButton.getToggleState() ? "Playing" : "Stopped");
    positionButton.setButtonText (fixed ? "Position: Fixed" : "Position: Auto");

    // --- i controlli che al momento non hanno effetto si spengono
    auto value = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const bool multi = juce::roundToInt (value ("voices")) > 1;
    knob ("voiceSpread").slider.setEnabled (multi);
    choice ("voiceMode").box.setEnabled (multi);
    knob ("gfxFilterFreq").slider.setEnabled (value ("gfxFilter") > 0.0f);
    knob ("gfxFilterRes").slider.setEnabled (value ("gfxFilter") > 0.0f);
    knob ("gfxCrushAmt").slider.setEnabled (value ("gfxCrush") > 0.0f);
    knob ("gfxDriveAmt").slider.setEnabled (value ("gfxDrive") > 0.0f);

    if (status.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime > 6000)
        status = {};

    if (status != statusLabel.getText())
        statusLabel.setText (status, juce::dontSendNotification);
}

void BrinaEditor::setStatus (const juce::String& text)
{
    status = text;
    statusTime = juce::Time::getMillisecondCounter();
}

int BrinaEditor::laneAt (int x, int y) const
{
    for (int i = 0; i < lanes.size(); ++i)
        if (lanes[i]->bounds.contains (x, y))
            return i;

    return -1;
}

void BrinaEditor::setDragLane (int lane)
{
    for (int i = 0; i < lanes.size(); ++i)
        lanes[i]->wave.dragHover = i == lane;
}

void BrinaEditor::load (const juce::File& file, int lane)
{
    const auto error = proc.loadFile (file, lane);
    setStatus (error.isEmpty() ? juce::String() : "Corsia " + juce::String (lane + 1) + ": " + error);
}

void BrinaEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    setDragLane (-1);

    // fuori dalle corsie: la prima corsia vuota (o la prima, se sono tutte piene)
    int first = laneAt (x, y);
    for (int i = 0; first < 0 && i < lanes.size(); ++i)
        if (proc.getSample (i) == nullptr)
            first = i;
    first = juce::jmax (0, first);

    // piu' file insieme riempiono le corsie una dopo l'altra
    for (int i = 0; i < files.size() && first + i < BrinaProcessor::numSlots; ++i)
    {
        load (juce::File (files[i]), first + i);

        if (status.isNotEmpty())
            break;
    }
}

void BrinaEditor::openChooser (int lane)
{
    chooser = std::make_unique<juce::FileChooser> ("Scegli un file audio per la corsia " + juce::String (lane + 1),
                                                   juce::File(), proc.getFormatWildcard());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, lane] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  load (file, lane);
                          });
}

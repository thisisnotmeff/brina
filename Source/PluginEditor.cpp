#include "PluginEditor.h"

using namespace brina;

//==============================================================================
BrinaLookAndFeel::BrinaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, ui::background);
    setColour (juce::Slider::textBoxTextColourId, ui::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
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
    const float stroke = 3.0f;

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
    g.setColour (slider.isEnabled() ? ui::accent : ui::dim);
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tip = centre.getPointOnCircumference (radius - 7.0f, angle);
    g.setColour (ui::text);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.35f, angle), tip }, 2.0f);
}

//==============================================================================
WaveformView::WaveformView (BrinaProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

static int shownSlot (BrinaProcessor& proc)
{
    return juce::jlimit (0, BrinaProcessor::numSlots - 1, proc.selectedSlot.load());
}

void WaveformView::refresh()
{
    auto now = proc.getSample (shownSlot (proc));
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
    g.setColour (ui::panel);
    g.fillRoundedRectangle (area, 6.0f);

    const float w = area.getWidth(), h = area.getHeight(), mid = h * 0.5f;
    const bool empty = sample == nullptr || lastLength <= 0;

    if (empty)
    {
        g.setColour (ui::dim);
        g.setFont (15.0f);
        g.drawFittedText ("Slot " + juce::String (shownSlot (proc) + 1) + " vuoto. "
                              + (BrinaProcessor::hasAudioInput()
                                     ? "Trascina qui un file audio, premi Load, oppure Rec per registrare l'ingresso della traccia"
                                     : "Trascina qui un file audio oppure premi Load"),
                          getLocalBounds().reduced (20), juce::Justification::centred, 2);
    }
    else
    {
        const float position = proc.apvts.getRawParameterValue ("position")->load();
        const float spray = proc.apvts.getRawParameterValue ("spray")->load();
        const float centre = position + proc.engine.scanVis.load();
        const float cx = (centre - std::floor (centre)) * w;
        const float half = spray * spray * 0.5f * w;

        // zona da cui nascono i grani
        g.setColour (ui::accent.withAlpha (0.10f));
        for (float offset : { -w, 0.0f, w })
            g.fillRect (juce::Rectangle<float> (cx - half + offset, 0.0f, half * 2.0f, h).getIntersection (area));

        g.setColour (ui::accent.withAlpha (0.55f));
        for (size_t x = 0; x < peakMax.size(); ++x)
        {
            const float top = mid - peakMax[x] * mid * 0.95f;
            const float bottom = mid - peakMin[x] * mid * 0.95f;
            g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));
        }

        // solo i grani che stanno leggendo da questo slot (il valore e' slot + posizione)
        const float slot = (float) shownSlot (proc);
        g.setColour (ui::warm);
        for (auto& vis : proc.engine.grainVis)
        {
            const float p = vis.load (std::memory_order_relaxed) - slot;
            if (p >= 0.0f && p < 1.0f)
                g.fillRect (p * w - 1.0f, 6.0f, 2.0f, h - 12.0f);
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
        g.setColour (ui::accent);
        g.drawRoundedRectangle (area.reduced (1.0f), 6.0f, 2.0f);
    }
}

void WaveformView::setPositionFromMouse (const juce::MouseEvent& e)
{
    if (auto* param = proc.apvts.getParameter ("position"))
    {
        // tolgo lo scorrimento automatico (Scan), cosi' il cursore finisce dove si clicca
        float v = (float) e.x / (float) juce::jmax (1, getWidth()) - proc.engine.scanVis.load();
        v -= std::floor (v);
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* param = proc.apvts.getParameter ("position"))
        param->beginChangeGesture();
    setPositionFromMouse (e);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e) { setPositionFromMouse (e); }

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (auto* param = proc.apvts.getParameter ("position"))
        param->endChangeGesture();
}

//==============================================================================
BrinaEditor::BrinaEditor (BrinaProcessor& p)
    : AudioProcessorEditor (p), proc (p), waveform (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (waveform);

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { openChooser(); };

    recButton.setClickingTogglesState (false);
    recButton.onClick = [this]
    {
        status = {};
        if (proc.isRecording()) proc.stopRecording();
        else                    proc.startRecording (selectedSlot());
    };
    addChildComponent (recButton);
    recButton.setVisible (BrinaProcessor::hasAudioInput());

    // --- sorgenti: quattro slot, quello selezionato e' mostrato nella forma d'onda
    for (int i = 0; i < BrinaProcessor::numSlots; ++i)
    {
        auto& b = slotButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setColour (juce::TextButton::buttonColourId, ui::background);
        b.setColour (juce::TextButton::buttonOnColourId, ui::accent);
        b.setColour (juce::TextButton::textColourOnId, ui::background);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                             | (i < BrinaProcessor::numSlots - 1 ? juce::Button::ConnectedOnRight : 0));
        b.setTooltip ("Slot " + juce::String (i + 1) + ": Load, Rec e il trascinamento caricano l'audio nello slot selezionato");
        b.onClick = [this, i] { selectSlot (i); };
        addAndMakeVisible (b);
    }

    clearButton.setColour (juce::TextButton::buttonColourId, ui::background);
    clearButton.setTooltip ("Svuota lo slot selezionato");
    clearButton.onClick = [this]
    {
        status = {};
        proc.clearSlot (selectedSlot());
    };
    addAndMakeVisible (clearButton);

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

    // --- posizione: Auto (scorre con Scan) oppure Fixed (ferma su Position)
    positionButton.setClickingTogglesState (true);
    positionButton.setColour (juce::TextButton::buttonColourId, ui::background);
    positionButton.setColour (juce::TextButton::buttonOnColourId, ui::warm);
    positionButton.setColour (juce::TextButton::textColourOnId, ui::background);
    positionButton.setTooltip ("Auto: la posizione scorre da sola (Scan). Fixed: resta ferma dove la metti");
    addAndMakeVisible (positionButton);
    positionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "posMode", positionButton);

    sampleLabel.setColour (juce::Label::textColourId, ui::text);
    sampleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (sampleLabel);

    for (auto [id, title] : std::initializer_list<std::pair<const char*, const char*>> {
             { "position", "Position" }, { "spray", "Spray" }, { "size", "Size" }, { "density", "Density" },
             { "pitch", "Pitch" }, { "detune", "Detune" }, { "octaves", "Octaves" }, { "reverse", "Reverse" },
             { "spread", "Spread" }, { "scan", "Scan" }, { "attack", "Attack" }, { "release", "Release" },
             { "genProb", "Chance" }, { "genOctave", "Octave" }, { "genRange", "Range" }, { "genLength", "Length" },
             { "drive", "Drive" }, { "wow", "Wow" }, { "tone", "Tone" },
             { "delayTime", "Time" }, { "delayFeedback", "Feedback" }, { "delayMix", "Mix" },
             { "reverbSize", "Size" }, { "reverbDamp", "Damp" }, { "shimmer", "Shimmer" }, { "reverbMix", "Mix" },
             { "dry", "Dry In" }, { "volume", "Volume" },
             { "voices", "Voices" }, { "voiceSpread", "Spread" },
             { "gfxFilter", "Filter" }, { "gfxFilterFreq", "Cutoff" }, { "gfxFilterRes", "Reso" },
             { "gfxCrush", "Crush" }, { "gfxCrushAmt", "Amount" },
             { "gfxDrive", "Drive" }, { "gfxDriveAmt", "Amount" },
             { "shapeChance", "Chance" } })
        addKnob (id, title);

    addChoice ("genRate", "Rate");
    addChoice ("genRoot", "Root");
    addChoice ("genScale", "Scale");
    addChoice ("voiceMode", "Intervals");
    addChoice ("sourceMode", "Grains pick");
    addChoice ("grainShape", "Shape");
    choices[sourceModeChoice]->label.setJustificationType (juce::Justification::centredRight);

    knobs[voicesKnob]->slider.setTooltip ("Quanti grani nascono insieme per ogni nota (multigrain)");
    knobs[voiceSpreadKnob]->slider.setTooltip ("Distanza tra i punti di lettura delle voci, attorno a Position");
    choices[voiceModeChoice]->box.setTooltip ("Intervalli tra le voci: Unison (appena stonate), Octaves, Fifths, "
                                              "Chord (terze sulla scala di Drift)");
    choices[sourceModeChoice]->box.setTooltip ("Da quale slot legge ogni grano: Random a caso, Cycle a rotazione, "
                                               "Per Voice una sorgente fissa per ogni voce");

    choices[shapeChoice]->box.setTooltip ("Forma del grano: Swell sale piano e cade in fretta, Twin ha due gobbe, "
                                          "Tremolo pulsa, Pluck attacca secco e sfuma, Random sceglie a ogni grano");
    knobs[shapeChanceKnob]->slider.setTooltip ("Quanti grani prendono la forma scelta: 0 nessuno (la solita Hann), 1 tutti");

    for (int i : { 0, 3, 5 })
        knobs[firstGrainFxKnob + i]->slider.setTooltip ("Quanti grani ricevono l'effetto: 0 nessuno, 1 tutti. "
                                                        "Ogni grano puo' averne piu' di uno");

    addAndMakeVisible (driftButton);
    driftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "genOn", driftButton);

    knobs[dryKnob]->slider.setVisible (BrinaProcessor::hasAudioInput());
    knobs[dryKnob]->label.setVisible (BrinaProcessor::hasAudioInput());

    setSize (880, 878);
    startTimerHz (30);
}

BrinaEditor::~BrinaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

BrinaEditor::Knob& BrinaEditor::addKnob (const char* paramId, const char* title)
{
    auto* k = knobs.add (new Knob());
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setColour (juce::Slider::textBoxTextColourId, ui::text);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    k->label.setText (title, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k->slider);
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

    for (auto& s : sections)
    {
        g.setColour (ui::panel);
        g.fillRoundedRectangle (s.bounds.toFloat(), 6.0f);
        g.setColour (ui::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.title, s.bounds.getX() + 10, s.bounds.getY() + 5, 300, 14, juce::Justification::centredLeft);
    }

    g.setColour (ui::accent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SOURCE", sourceRow.getX() + 10, sourceRow.getY(), 60, sourceRow.getHeight(), juce::Justification::centredLeft);

    g.setColour (ui::outline);
    for (auto& d : dividers)
        g.fillRect (d);
}

void BrinaEditor::resized()
{
    const int margin = 12, gap = 8, sectionHeight = 118;
    auto area = getLocalBounds().reduced (margin, 0);

    auto header = area.removeFromTop (46).reduced (0, 9);
    if (recButton.isVisible())
    {
        recButton.setBounds (header.removeFromRight (64));
        header.removeFromRight (gap);
    }
    loadButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (gap);
    header.removeFromLeft (214);    // titolo
    playButton.setBounds (header.removeFromLeft (72));
    header.removeFromLeft (gap);
    killButton.setBounds (header.removeFromLeft (52));
    header.removeFromLeft (gap);
    sampleLabel.setBounds (header);

    // --- sorgenti: slot 1-4, Clear, e come i grani scelgono lo slot
    sourceRow = area.removeFromTop (24);
    area.removeFromTop (6);
    {
        auto row = sourceRow.withTrimmedLeft (70);
        for (auto& b : slotButtons)
            b.setBounds (row.removeFromLeft (38));
        row.removeFromLeft (gap);
        clearButton.setBounds (row.removeFromLeft (56));

        auto& pick = *choices[sourceModeChoice];
        pick.box.setBounds (row.removeFromRight (118));
        row.removeFromRight (6);
        pick.label.setBounds (row.removeFromRight (90));
    }

    waveform.setBounds (area.removeFromTop (150));
    area.removeFromTop (gap);

    sections.clear();
    dividers.clear();

    auto placeKnob = [this] (int index, juce::Rectangle<int> cell)
    {
        auto& k = *knobs[index];
        k.label.setBounds (cell.removeFromTop (14));
        k.slider.setBounds (cell.reduced (2, 0));
    };

    auto content = [] (juce::Rectangle<int> section) { return section.withTrimmedTop (22).reduced (6, 4); };

    // --- Grains
    auto grains = area.removeFromTop (sectionHeight);
    area.removeFromTop (gap);
    sections.push_back ({ "GRAINS", grains });
    positionButton.setBounds (grains.getX() + 70, grains.getY() + 3, 112, 18);
    {
        auto row = content (grains);
        const int cell = row.getWidth() / 12;
        for (int i = 0; i < 12; ++i)
            placeKnob (i, row.removeFromLeft (cell));
    }

    auto placeChoice = [this] (int index, juce::Rectangle<int> cell)
    {
        auto& c = *choices[index];
        c.label.setBounds (cell.removeFromTop (14));
        c.box.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 26));
    };

    // --- Voices (multigrain) e Grain FX (effetti per grano)
    {
        auto row = area.removeFromTop (sectionHeight);
        area.removeFromTop (gap);

        auto voices = row.removeFromLeft (286);
        row.removeFromLeft (gap);
        sections.push_back ({ "VOICES  -  multigrain", voices });
        {
            auto inner = content (voices);
            placeKnob (voicesKnob, inner.removeFromLeft (72));
            placeKnob (voiceSpreadKnob, inner.removeFromLeft (72));
            placeChoice (voiceModeChoice, inner.reduced (8, 0));
        }

        sections.push_back ({ "GRAIN FX  -  how many grains get each effect", row });
        {
            auto inner = content (row);
            const int cell = inner.getWidth() / numGrainFxKnobs;

            for (int i = 0; i < numGrainFxKnobs; ++i)
            {
                if (i == 3 || i == 5)   // un filo tra filtro, crush e drive
                    dividers.push_back ({ inner.getX(), inner.getY() + 6, 1, inner.getHeight() - 12 });

                placeKnob (firstGrainFxKnob + i, inner.removeFromLeft (cell));
            }
        }
    }

    // --- Shape (forma del grano), Morph, Weather
    {
        auto row = area.removeFromTop (sectionHeight);
        area.removeFromTop (gap);

        auto shape = row.removeFromLeft (214);
        row.removeFromLeft (gap);
        sections.push_back ({ "SHAPE  -  grain envelope", shape });
        {
            auto inner = content (shape);
            placeKnob (shapeChanceKnob, inner.removeFromRight (72));
            placeChoice (shapeChoice, inner.reduced (8, 0));
        }
    }

    // --- Drift (generatore di note)
    auto drift = area.removeFromTop (sectionHeight);
    area.removeFromTop (gap);
    sections.push_back ({ "DRIFT  -  generative notes", drift });
    {
        auto row = content (drift);
        driftButton.setBounds (row.removeFromLeft (70).withSizeKeepingCentre (60, 24));

        for (int i = 0; i < numDriftChoices; ++i)
            placeChoice (i, row.removeFromLeft (130).reduced (8, 0));

        const int cell = row.getWidth() / 4;
        for (int i = firstDriftKnob; i < firstFxKnob; ++i)
            placeKnob (i, row.removeFromLeft (cell).withSizeKeepingCentre (76, row.getHeight()));
    }

    // --- Tape / Delay / Reverb / Out
    auto fx = area.removeFromTop (sectionHeight);
    const int unit = (fx.getWidth() - 3 * gap) / 12;
    int knob = firstFxKnob;

    for (auto [title, count] : std::initializer_list<std::pair<const char*, int>> {
             { "TAPE", 3 }, { "DELAY", 3 }, { "REVERB", 4 }, { "OUT", 2 } })
    {
        auto section = fx.removeFromLeft (unit * count);
        fx.removeFromLeft (gap);
        sections.push_back ({ title, section });

        auto row = content (section);
        const int cell = row.getWidth() / count;
        for (int i = 0; i < count; ++i)
            placeKnob (knob++, row.removeFromLeft (cell));
    }
}

//==============================================================================
void BrinaEditor::timerCallback()
{
    waveform.refresh();

    const bool rec = proc.isRecording();
    recButton.setToggleState (rec, juce::dontSendNotification);
    recButton.setButtonText (rec ? "Stop" : "Rec");

    playButton.setButtonText (playButton.getToggleState() ? "Playing" : "Stopped");

    const bool fixed = positionButton.getToggleState();
    positionButton.setButtonText (fixed ? "Position: Fixed" : "Position: Auto");
    knobs[scanKnob]->slider.setEnabled (! fixed);   // Scan non ha effetto a posizione fissa

    // --- slot: selezionato, pieno o vuoto, in registrazione
    const int slot = selectedSlot();
    int filled = 0;

    for (int i = 0; i < BrinaProcessor::numSlots; ++i)
    {
        auto s = proc.getSample (i);
        const bool full = s != nullptr && s->length.load() > 0;
        const bool recHere = rec && proc.getRecordingSlot() == i;
        filled += full ? 1 : 0;

        auto& b = slotButtons[(size_t) i];
        b.setToggleState (i == slot, juce::dontSendNotification);
        b.setColour (juce::TextButton::textColourOffId, recHere ? juce::Colour (0xffe0655c) : (full ? ui::text : ui::dim.withAlpha (0.6f)));
        b.setColour (juce::TextButton::buttonOnColourId, recHere ? juce::Colour (0xffc8443c) : ui::accent);
    }

    clearButton.setEnabled (proc.getSample (slot) != nullptr);
    choices[sourceModeChoice]->box.setEnabled (filled > 1);     // con una sola sorgente non c'e' scelta

    // --- i controlli che al momento non hanno effetto si spengono
    auto value = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const bool multi = juce::roundToInt (value ("voices")) > 1;
    knobs[voiceSpreadKnob]->slider.setEnabled (multi);
    choices[voiceModeChoice]->box.setEnabled (multi);

    const bool filterOn = value ("gfxFilter") > 0.0f, crushOn = value ("gfxCrush") > 0.0f, driveOn = value ("gfxDrive") > 0.0f;
    knobs[firstGrainFxKnob + 1]->slider.setEnabled (filterOn);
    knobs[firstGrainFxKnob + 2]->slider.setEnabled (filterOn);
    knobs[firstGrainFxKnob + 4]->slider.setEnabled (crushOn);
    knobs[firstGrainFxKnob + 6]->slider.setEnabled (driveOn);
    choices[shapeChoice]->box.setEnabled (value ("shapeChance") > 0.0f);

    juce::String text = status;

    if (text.isEmpty())
        if (auto s = proc.getSample (slot))
            if (s->length.load() > 0)
                text = s->name + "   " + juce::String ((double) s->length.load() / s->sampleRate, 1) + " s";

    if (text != sampleLabel.getText())
        sampleLabel.setText (text, juce::dontSendNotification);
}

void BrinaEditor::selectSlot (int slot)
{
    status = {};
    proc.selectedSlot.store (juce::jlimit (0, BrinaProcessor::numSlots - 1, slot));
    waveform.refresh();
}

void BrinaEditor::load (const juce::File& file, int slot)
{
    status = proc.loadFile (file, slot);
}

void BrinaEditor::filesDropped (const juce::StringArray& files, int, int)
{
    waveform.dragHover = false;

    // piu' file insieme riempiono gli slot uno dopo l'altro, a partire da quello selezionato
    const int first = selectedSlot();

    for (int i = 0; i < files.size() && first + i < BrinaProcessor::numSlots; ++i)
    {
        load (juce::File (files[i]), first + i);

        if (status.isNotEmpty())
            break;
    }
}

void BrinaEditor::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Scegli un file audio", juce::File(), proc.getFormatWildcard());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot = selectedSlot()] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  load (file, slot);
                          });
}

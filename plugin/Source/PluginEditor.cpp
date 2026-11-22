#include "PluginEditor.h"
#include "Parameters.h"

#include <cmath>

using namespace HsStyle;

namespace
{
    float param (const HillsideProcessor& p, const char* id)
    {
        return const_cast<HillsideProcessor&> (p).getValueTreeState().getRawParameterValue (id)->load();
    }

    // A round light with a soft halo; level 0 to 1 decides how brightly it shines.
    void drawLight (juce::Graphics& g, float cx, float cy, float r, juce::Colour c, float level)
    {
        level = juce::jlimit (0.0f, 1.0f, level);
        if (level > 0.15f)
        {
            g.setColour (c.withAlpha (0.12f * level));
            g.fillEllipse (cx - r - 4.0f, cy - r - 4.0f, 2.0f * (r + 4.0f), 2.0f * (r + 4.0f));
        }
        g.setColour (knob);
        g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
        if (level <= 0.0f)
        {
            g.setColour (line2);
            g.drawEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r, 1.0f);
        }
        g.setColour (c.withAlpha (0.12f + 0.88f * level));
        g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
    }

    // Width of a stage on the display for its time: 24 px at 3 ms to 120 px at 12 s, on a log scale.
    float stageWidth (float ms)
    {
        const float x = std::log (juce::jlimit (3.0f, 12000.0f, ms) / 3.0f) / std::log (4000.0f);
        return 24.0f + 96.0f * x;
    }
}

// ================================================================================================ look and feel
HillsideLookAndFeel::HillsideLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, bright);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff262b27));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, sageDark);
    setColour (juce::PopupMenu::highlightedTextColourId, bright);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void HillsideLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                            juce::Slider::SliderStyle, juce::Slider&)
{
    const float cx = (float) x + (float) w * 0.5f;
    g.setColour (display);
    g.fillRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) h, 3.0f);
    g.setColour (line);
    g.drawRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) h, 3.0f, 1.0f);
    g.setColour (sageDark);
    g.fillRoundedRectangle (cx - 2.0f, pos, 4.0f, (float) (y + h) - pos, 2.0f);
    g.setColour (text);
    g.fillRoundedRectangle (cx - 14.0f, pos - 6.0f, 28.0f, 12.0f, 3.0f);
    g.setColour (panel);
    g.drawLine (cx - 9.0f, pos, cx + 9.0f, pos, 1.5f);
}

void HillsideLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
{
    const float size = (float) juce::jmin (w, h) - 6.0f, r = size * 0.5f;
    const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    g.setColour (knob);
    g.fillEllipse (c.x - r, c.y - r, size, size);
    g.setColour (line2);
    g.drawEllipse (c.x - r, c.y - r, size, size, 2.0f);
    const float angle = start + pos * (end - start);
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r + 2.5f, r + 2.5f, 0.0f, start, angle, true);
    g.setColour (sageMid);
    g.strokePath (arc, juce::PathStrokeType (2.0f));
    const juce::Point<float> tip (c.x + r * 0.8f * std::sin (angle), c.y - r * 0.8f * std::cos (angle));
    g.setColour (bright);
    g.drawLine ({ c, tip }, 3.0f);
}

void HillsideLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto r = b.getLocalBounds().toFloat().withSizeKeepingCentre (26.0f, 14.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? sageDark : knob);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (line2);
    if (! on) g.drawRoundedRectangle (r, 7.0f, 1.0f);
    const float kx = on ? r.getRight() - 7.0f : r.getX() + 7.0f;
    g.setColour (on ? bright : dim);
    g.fillEllipse (kx - 5.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
}

void HillsideLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (display);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (line2);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (dim);
    g.fillPath (arrow);
}

void HillsideLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 18, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// ================================================================================================ envelope display
EnvelopeView::EnvelopeView (HillsideProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

EnvelopeView::Snap EnvelopeView::read() const
{
    const auto& d = proc.getDisplay();
    Snap s;
    s.stage = d.stage.load();
    s.progress = std::round (d.progress.load() * 400.0f) / 400.0f;
    s.level = std::round (d.shaped.load() * 400.0f) / 400.0f;
    s.gate = d.gate.load();
    s.a = param (proc, HsParams::attack); s.d = param (proc, HsParams::decay); s.s = param (proc, HsParams::sustain);
    s.r = param (proc, HsParams::release); s.shape = param (proc, HsParams::shape);
    return s;
}

void EnvelopeView::timerCallback()
{
    const Snap now = read();
    if (! (now == shown)) { shown = now; repaint(); }
}

EnvelopeView::Segments EnvelopeView::segments() const
{
    Segments sg { 20.0f, stageWidth (param (proc, HsParams::attack)), stageWidth (param (proc, HsParams::decay)), 90.0f,
                  stageWidth (param (proc, HsParams::release)) };
    const float total = sg.a + sg.d + sg.s + sg.r, room = (float) kWidth - 40.0f;
    if (total > room)
    {
        const float k = room / total;
        sg.a *= k; sg.d *= k; sg.s *= k; sg.r *= k;
    }
    return sg;
}

void EnvelopeView::paint (juce::Graphics& g)
{
    const Snap s = read();
    const auto box = juce::Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kBoxHeight);
    g.setColour (display);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (line);
    g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);

    const Segments sg = segments();
    const float base = 168.0f, top = 18.0f;
    auto yOf = [&] (double level) { return base - (float) level * (base - top); };
    const float xa = sg.x0, xd = xa + sg.a, xs = xd + sg.d, xr = xs + sg.s;
    const double sus = s.s / 100.0;

    // Stage separators.
    g.setColour (line);
    for (float x : { xd, xs, xr })
    {
        juce::Path p;
        p.startNewSubPath (x, 8.0f);
        p.lineTo (x, (float) kBoxHeight - 8.0f);
        juce::Path dashed;
        const float dashes[] = { 3.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
    }

    // The curve for the current settings: linear dashed, shaped solid.
    hillside::Curve curve;
    curve.setShape (s.shape / 100.0);
    auto build = [&] (bool shaped)
    {
        juce::Path p;
        auto f = [&] (double x) { return shaped ? curve (x) : x; };
        p.startNewSubPath (xa, yOf (0.0));
        for (int i = 1; i <= 40; ++i) { const double t = i / 40.0; p.lineTo (xa + (float) t * sg.a, yOf (f (t))); }
        for (int i = 1; i <= 40; ++i) { const double t = i / 40.0; p.lineTo (xd + (float) t * sg.d, yOf (1.0 + (sus - 1.0) * f (t))); }
        p.lineTo (xr, yOf (sus));
        for (int i = 1; i <= 40; ++i) { const double t = i / 40.0; p.lineTo (xr + (float) t * sg.r, yOf (sus * (1.0 - f (t)))); }
        return p;
    };
    {
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, build (false), dashes, 2);
        g.setColour (faint);
        g.fillPath (dashed);
    }
    g.setColour (sage);
    g.strokePath (build (true), juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (font (11.0f));
    g.setColour (sage);
    g.drawText ("shaped", (int) kWidth - 70, 10, 60, 14, juce::Justification::centredLeft);
    g.setColour (faint);
    g.drawText ("linear", (int) kWidth - 70, 26, 60, 14, juce::Justification::centredLeft);

    // The gate bar and the moving point.
    if (s.gate)
    {
        g.setColour (sageDark);
        g.fillRoundedRectangle (xa, (float) kBoxHeight - 10.0f, xr - xa, 4.0f, 2.0f);
    }
    const auto stage = (hillside::Stage) s.stage;
    float px = -1.0f;
    switch (stage)
    {
        case hillside::Stage::attack:  px = xa + s.progress * sg.a; break;
        case hillside::Stage::decay:   px = xd + s.progress * sg.d; break;
        case hillside::Stage::sustain: px = xs + sg.s * 0.5f; break;
        case hillside::Stage::release: px = xr + s.progress * sg.r; break;
        case hillside::Stage::idle:    if (s.gate) px = xs; break;   // AD envelope resting while the key is held
    }
    if (px >= 0.0f)
    {
        const float py = yOf (s.level);
        g.setColour (amber.withAlpha (0.25f));
        g.fillEllipse (px - 10.0f, py - 10.0f, 20.0f, 20.0f);
        g.setColour (amber);
        g.fillEllipse (px - 6.0f, py - 6.0f, 12.0f, 12.0f);
    }

    // The four stage lights, under their part of the curve.
    const float lx[4] = { xa + sg.a * 0.5f, xd + sg.d * 0.5f, xs + sg.s * 0.5f, xr + sg.r * 0.5f };
    const char* letters[4] = { "A", "D", "S", "R" };
    for (int i = 0; i < 4; ++i)
    {
        const bool lit = s.stage == i + 1;
        drawLight (g, lx[i], (float) kBoxHeight + 20.0f, 7.0f, amber, lit ? 1.0f : 0.0f);
        g.setColour (lit ? amber : dim);
        g.setFont (font (12.0f));
        g.drawText (letters[i], (int) lx[i] - 10, kBoxHeight + 32, 20, 16, juce::Justification::centred);
    }
}

// ================================================================================================ Gate button
GateButton::GateButton (HillsideProcessor& p)
    : proc (p), param (p.getValueTreeState().getParameter (HsParams::manualGate))
{
    startTimerHz (30);
}

void GateButton::press (bool down)
{
    if (down == pressed) return;
    pressed = down;
    if (down) { param->beginChangeGesture(); param->setValueNotifyingHost (1.0f); }
    else      { param->setValueNotifyingHost (0.0f); param->endChangeGesture(); }
    repaint();
}

void GateButton::mouseDown (const juce::MouseEvent&) { press (true); }
void GateButton::mouseUp (const juce::MouseEvent&)   { press (false); }

void GateButton::timerCallback()
{
    const float lv = std::round (proc.getDisplay().shaped.load() * 100.0f) / 100.0f;
    const bool gi = proc.getDisplay().gate.load();
    if (gi != gateIn)
        if (auto* parent = getParentComponent()) parent->repaint (592, 114, 20, 20);   // the gate-in light
    if (! juce::exactlyEqual (lv, level) || gi != gateIn) { level = lv; gateIn = gi; repaint(); }
}

void GateButton::paint (juce::Graphics& g)
{
    // The button: glows with the envelope, as the module's button does.
    const float glow = juce::jlimit (0.0f, 1.0f, proc.getDisplay().shaped.load());
    const float r = 30.0f, cx = 36.0f, cy = 36.0f;
    g.setColour (amber.withAlpha (0.15f * glow));
    g.fillEllipse (cx - r - 5.0f, cy - r - 5.0f, 2.0f * (r + 5.0f), 2.0f * (r + 5.0f));
    g.setColour (pressed ? amberDeep.brighter (0.3f) : amberDeep.interpolatedWith (juce::Colour (0xff5a4520), glow));
    g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
    g.setColour (amber.withAlpha (0.35f + 0.65f * glow));
    g.drawEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r, 2.0f);
    g.setColour (amberText);
    g.setFont (font (12.0f));
    g.drawText ("Gate", juce::Rectangle<float> (cx - r, cy - 8.0f, 2.0f * r, 16.0f), juce::Justification::centred);
}

// ================================================================================================ segmented switch
Segmented::Segmented (juce::RangedAudioParameter& p, const juce::StringArray& n)
    : names (n), attachment (p, [this] (float v) { index = juce::roundToInt (v); repaint(); })
{
    attachment.sendInitialUpdate();
}

void Segmented::choose (int i)
{
    i = juce::jlimit (0, names.size() - 1, i);
    attachment.setValueAsCompleteGesture ((float) i);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    choose ((int) ((float) e.x / ((float) getWidth() / (float) names.size())));
}

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (display);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (line2);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const float w = r.getWidth() / (float) names.size();
    for (int i = 0; i < names.size(); ++i)
    {
        const auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f);
        if (i == index)
        {
            g.setColour (sageDark);
            g.fillRoundedRectangle (seg, 4.0f);
        }
        g.setColour (i == index ? bright : dim);
        g.setFont (font (getHeight() < 24 ? 11.0f : 12.0f));
        g.drawText (names[i], seg, juce::Justification::centred);
    }
}

// ================================================================================================ level bar
LevelBar::LevelBar (HillsideProcessor& p, int o, juce::Colour c) : proc (p), output (o), colour (c)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void LevelBar::timerCallback()
{
    const int v = proc.getDisplay().sent[output].load();
    if (v != shownValue) { shownValue = v; repaint(); }
}

void LevelBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (display);
    g.fillRoundedRectangle (r, 2.0f);
    const int v = proc.getDisplay().sent[output].load();
    const float level = v > 0 ? (float) v / 16383.0f : 0.0f;
    if (level > 0.0f)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (2.0f, r.getWidth() * level)), 2.0f);
    }
}

// ================================================================================================ pulse lights
PulseLights::PulseLights (HillsideProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    for (int k = 0; k < hillside::kNumPulseOutputs; ++k)
    {
        seenStarts[k] = proc.getDisplay().pulseStarts[k].load();
        shown[k] = proc.getDisplay().pulseOn[k].load();
    }
    startTimerHz (30);
}

void PulseLights::timerCallback()
{
    bool changed = false;
    for (int k = 0; k < hillside::kNumPulseOutputs; ++k)
    {
        const unsigned st = proc.getDisplay().pulseStarts[k].load();
        if (st != seenStarts[k]) { seenStarts[k] = st; hold[k] = 3; }
        else if (hold[k] > 0) --hold[k];
        const bool on = proc.getDisplay().pulseOn[k].load() || hold[k] > 0;
        if (on != shown[k]) { shown[k] = on; changed = true; }
    }
    if (changed) repaint();
    // The numbers beside the lights follow Send as (CC 85, E1, or both).
    const int mode = juce::roundToInt (proc.getValueTreeState().getRawParameterValue (HsParams::sendAs)->load());
    if (mode != sendAsShown)
    {
        sendAsShown = mode;
        if (auto* parent = getParentComponent()) parent->repaint (618, 255, 72, 130);
    }
}

void PulseLights::paint (juce::Graphics& g)
{
    for (int k = 0; k < hillside::kNumPulseOutputs; ++k)
        drawLight (g, 10.0f, 10.0f + (float) (k * kRowStep), 5.0f, amber, shown[k] ? 1.0f : 0.0f);
}

// ================================================================================================ panel
HillsidePanel::HillsidePanel (HillsideProcessor& p)
    : proc (p), envelopeView (p), gateButton (p),
      modeSwitch (*p.getValueTreeState().getParameter (HsParams::gateMode), HsParams::gateModeNames),
      envBar (p, hillside::outEnvelope, sage), linBar (p, hillside::outLinear, sageMid),
      sendAs (*p.getValueTreeState().getParameter (HsParams::sendAs), HsParams::sendAsNames),
      pulseLights (p)
{
    setLookAndFeel (&lnf);
    auto& vts = proc.getValueTreeState();

    addAndMakeVisible (envelopeView);
    const char* faderIds[4] = { HsParams::attack, HsParams::decay, HsParams::sustain, HsParams::release };
    for (int i = 0; i < 4; ++i)
    {
        auto& f = faders[i];
        f.setSliderStyle (juce::Slider::LinearVertical);
        f.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        f.setName (faderIds[i]);
        f.onValueChange = [this] { repaint (20, 430, 340, 36); };
        addAndMakeVisible (f);
        faderAtt[i] = std::make_unique<SliderAttachment> (vts, faderIds[i], f);
    }
    shapeKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    shapeKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    shapeKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    shapeKnob.onValueChange = [this] { repaint (370, 446, 84, 20); };
    addAndMakeVisible (shapeKnob);
    shapeAtt = std::make_unique<SliderAttachment> (vts, HsParams::shape, shapeKnob);

    addAndMakeVisible (gateButton);
    addAndMakeVisible (modeSwitch);

    for (int ch = 1; ch <= 16; ++ch) channelBox.addItem (juce::String (ch), ch);
    addAndMakeVisible (channelBox);
    channelAtt = std::make_unique<ComboAttachment> (vts, HsParams::outChannel, channelBox);

    for (auto* b : { &envOn, &linOn }) addAndMakeVisible (*b);
    envOnAtt = std::make_unique<ButtonAttachment> (vts, HsParams::outEnvOn, envOn);
    linOnAtt = std::make_unique<ButtonAttachment> (vts, HsParams::outLinOn, linOn);
    addAndMakeVisible (envBar);
    addAndMakeVisible (linBar);

    // Milestone 2: the gate block (dimmed in 0.1.0, live from 0.2.0).
    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (gateOn[i]);
        gateOnAtt[i] = std::make_unique<ButtonAttachment> (vts, HsParams::gateOnIds[i], gateOn[i]);
    }
    addAndMakeVisible (trigOn);
    trigOnAtt = std::make_unique<ButtonAttachment> (vts, HsParams::trigOn, trigOn);
    addAndMakeVisible (sendAs);
    addAndMakeVisible (pulseLights);
    if (! HillsideProcessor::milestone2Live)
        for (juce::Component* c : { (juce::Component*) &gateOn[0], (juce::Component*) &gateOn[1], (juce::Component*) &gateOn[2],
                                    (juce::Component*) &gateOn[3], (juce::Component*) &trigOn, (juce::Component*) &sendAs })
        {
            c->setEnabled (false);
            c->setAlpha (kDimAlpha);
        }
}

HillsidePanel::~HillsidePanel() { setLookAndFeel (nullptr); }

std::vector<HillsidePanel::ControlInfo> HillsidePanel::controls()
{
    std::vector<ControlInfo> v {
        { &faders[0], HsParams::attack, true }, { &faders[1], HsParams::decay, true },
        { &faders[2], HsParams::sustain, true }, { &faders[3], HsParams::release, true },
        { &shapeKnob, HsParams::shape, true }, { &modeSwitch, HsParams::gateMode, true },
        { &gateButton, HsParams::manualGate, true }, { &channelBox, HsParams::outChannel, true },
        { &envOn, HsParams::outEnvOn, true }, { &linOn, HsParams::outLinOn, true },
    };
    for (int i = 0; i < 4; ++i) v.push_back ({ &gateOn[i], HsParams::gateOnIds[i], HillsideProcessor::milestone2Live });
    v.push_back ({ &trigOn, HsParams::trigOn, HillsideProcessor::milestone2Live });
    v.push_back ({ &sendAs, HsParams::sendAs, HillsideProcessor::milestone2Live });
    return v;
}

void HillsidePanel::resized()
{
    envelopeView.setBounds (20, 58, EnvelopeView::kWidth, EnvelopeView::kHeight);
    const int fx[4] = { 70, 150, 230, 310 };
    for (int i = 0; i < 4; ++i) faders[i].setBounds (fx[i] - 20, 300, 40, 130);
    shapeKnob.setBounds (380, 330, 64, 64);

    gateButton.setBounds (500, 64, 72, 72);
    modeSwitch.setBounds (592, 74, 150, 26);
    channelBox.setBounds (676, 15, 48, 24);

    envOn.setBounds (498, 193, 30, 20);
    linOn.setBounds (498, 219, 30, 20);
    envBar.setBounds (690, 198, 60, 10);
    linBar.setBounds (690, 224, 60, 10);
    for (int i = 0; i < 4; ++i) gateOn[i].setBounds (498, 259 + 26 * i, 30, 20);
    trigOn.setBounds (498, 363, 30, 20);
    sendAs.setBounds (552, 400, 198, 22);
    pulseLights.setBounds (PulseLights::kX, PulseLights::kY, 20, 20 + 4 * PulseLights::kRowStep);
}

void HillsidePanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    // Header.
    g.setColour (bright);
    g.setFont (font (21.0f));
    g.drawText ("Hillside", 20, 12, 100, 26, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (font (12.0f));
    g.drawText (juce::String::fromUTF8 ("envelope  \xc2\xb7  particledots"), 112, 17, 200, 18, juce::Justification::centredLeft);
    g.drawText ("MIDI out ch", 590, 17, 82, 18, juce::Justification::centredRight);
    g.setColour (faint);
    g.setFont (font (11.0f));
    g.drawText (JucePlugin_VersionString, 728, 17, 44, 18, juce::Justification::centredLeft);
    g.setColour (line);
    g.drawHorizontalLine (46, 20.0f, 760.0f);

    // Fader and knob captions and values.
    auto& vts = proc.getValueTreeState();
    const char* names[4] = { "Attack", "Decay", "Sustain", "Release" };
    const char* ids[4] = { HsParams::attack, HsParams::decay, HsParams::sustain, HsParams::release };
    const int fx[4] = { 70, 150, 230, 310 };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (text);
        g.setFont (font (12.0f));
        g.drawText (names[i], fx[i] - 40, 432, 80, 16, juce::Justification::centred);
        g.setColour (dim);
        g.setFont (font (11.0f));
        auto* p = vts.getParameter (ids[i]);
        g.drawText (p->getCurrentValueAsText(), fx[i] - 40, 449, 80, 14, juce::Justification::centred);
    }
    g.setColour (faint);
    g.setFont (font (11.0f));
    g.drawText ("lin", 366, 400, 30, 14, juce::Justification::centredLeft);
    g.drawText ("log/exp", 424, 400, 50, 14, juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (font (12.0f));
    g.drawText ("Shape", 372, 432, 80, 16, juce::Justification::centred);
    g.setColour (dim);
    g.setFont (font (11.0f));
    g.drawText (vts.getParameter (HsParams::shape)->getCurrentValueAsText(), 372, 449, 80, 14, juce::Justification::centred);

    // Right column.
    g.setColour (line);
    g.drawVerticalLine (486, 58.0f, 452.0f);
    g.drawHorizontalLine (164, 500.0f, 760.0f);
    g.setColour (dim);
    g.setFont (font (11.0f));
    g.drawText ("hold to play", 500, 141, 72, 14, juce::Justification::centred);
    g.drawText ("gate in: notes + Gate button", 614, 118, 150, 14, juce::Justification::centredLeft);

    g.setColour (text);
    g.setFont (font (12.0f));
    g.drawText ("Outputs", 500, 172, 100, 16, juce::Justification::centredLeft);
    struct Row { const char* name; const char* cc; int y; bool live; };
    const Row rows[] = {
        { "Envelope", "CC 3 / 35", 203, true }, { "Linear", "CC 9 / 41", 229, true },
        { "Gate A", "CC 85", 269, false }, { "Gate D", "CC 86", 295, false }, { "Gate S", "CC 87", 321, false },
        { "Gate R", "CC 88", 347, false }, { "Trig", "CC 89", 373, false },
    };
    for (const auto& r : rows)
    {
        const float a = r.live || HillsideProcessor::milestone2Live ? 1.0f : kDimAlpha;
        g.setColour (bright.withAlpha (a));
        g.setFont (font (12.0f));
        g.drawText (r.name, 534, r.y - 8, 80, 16, juce::Justification::centredLeft);
        g.setColour (dim.withAlpha (a));
        g.setFont (font (11.0f));
        juce::String number (r.cc);
        if (! r.live)
        {   // a gate or Trig row: its number follows Send as
            static const char* const noteNames[] = { "E1", "F1", "F#1", "G1", "G#1" };
            const int k = (r.y - 269) / 26;
            const int mode = juce::roundToInt (proc.getValueTreeState().getRawParameterValue (HsParams::sendAs)->load());
            if (mode == hillside::sendAsNote)      number = juce::String ("note ") + noteNames[k];
            else if (mode == hillside::sendAsBoth) number = juce::String (r.cc) + " + " + noteNames[k];
        }
        g.drawText (number, 620, r.y - 8, 66, 16, juce::Justification::centredLeft);
        if (! r.live && ! HillsideProcessor::milestone2Live) drawLight (g, 742.0f, (float) r.y, 5.0f, amber, 0.0f);
    }
    g.setColour (dim.withAlpha (HillsideProcessor::milestone2Live ? 1.0f : kDimAlpha));
    g.drawText ("Send as", 500, 404, 50, 14, juce::Justification::centredLeft);
    if (! HillsideProcessor::milestone2Live)
    {
        g.setColour (amber);
        g.drawRoundedRectangle (660.5f, 248.5f, 90.0f, 16.0f, 4.0f, 1.0f);
        g.setFont (font (11.0f));
        g.drawText ("milestone 2", 660, 249, 90, 15, juce::Justification::centred);
    }

    // The gate-in light (also shown while the Gate button is held).
    drawLight (g, 602.0f, 124.0f, 5.0f, amber, proc.getDisplay().gate.load() ? 1.0f : 0.0f);
}

// ================================================================================================ editor
HillsideEditor::HillsideEditor (HillsideProcessor& p)
    : AudioProcessorEditor (&p), panel (p), proc (p)
{
    addAndMakeVisible (panel);
    constrainer.setFixedAspectRatio ((double) kWidth / (double) kHeight);
    constrainer.setSizeLimits (kMinWidth, heightForWidth (kMinWidth), kMaxWidth, heightForWidth (kMaxWidth));
    setConstrainer (&constrainer);
    setResizable (true, true);                       // with the drag corner

    const int saved = (int) proc.getValueTreeState().state.getProperty (widthProperty, kWidth);
    const int width = juce::jlimit (kMinWidth, kMaxWidth, saved);
    setSize (width, heightForWidth (width));
}

HillsideEditor::~HillsideEditor() = default;

void HillsideEditor::paint (juce::Graphics& g) { g.fillAll (HsStyle::panel); }

void HillsideEditor::resized()
{
    const double scale = juce::jmin ((double) getWidth() / kWidth, (double) getHeight() / kHeight);
    panel.setTransform (juce::AffineTransform::scale ((float) scale));
    panel.setBounds (0, 0, kWidth, kHeight);

    auto& state = proc.getValueTreeState().state;
    if ((int) state.getProperty (widthProperty, -1) != getWidth())
        state.setProperty (widthProperty, getWidth(), nullptr);
}

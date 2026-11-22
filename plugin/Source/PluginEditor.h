// The Hillside window, as approved in the draft (2026-11-22):
//   left:  the envelope display (the curve for the current settings, the linear one dashed, a moving point on the live
//          envelope, the gate bar), the four stage lights A D S R, the four faders and the Shape knob;
//   right: the Gate button (lights up with the envelope) and the Gate | Trig switch, the gate-in light, then the
//          outputs: Envelope and Linear with their switches, CC numbers and level bars; the milestone 2 block (Gate A,
//          D, S, R, Trig, Send as) shown dimmed and tagged;
//   top:   the name, the MIDI out channel and the version.
// The panel is laid out at 780 x 470 and scaled to the window (80% to 200%, proportions locked).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Style.h"

class HillsideLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HillsideLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return HsStyle::font (12.5f); }
    juce::Font getPopupMenuFont() override { return HsStyle::font (13.0f); }
};

// The envelope display and the four stage lights under it.
class EnvelopeView : public juce::Component, private juce::Timer
{
public:
    explicit EnvelopeView (HillsideProcessor& p);
    ~EnvelopeView() override { stopTimer(); }
    void paint (juce::Graphics&) override;

    static constexpr int kWidth = 450, kHeight = 234, kBoxHeight = 184;
    struct Segments { float x0, a, d, s, r; };
    Segments segments() const;                      // segment widths for the current settings (for the checks too)

private:
    void timerCallback() override;
    HillsideProcessor& proc;
    struct Snap { int stage = -1; float progress = -1, level = -1; bool gate = false; float a = 0, d = 0, s = 0, r = 0, shape = 0;
                  bool operator== (const Snap& o) const
                  {
                      using juce::exactlyEqual;
                      return stage == o.stage && gate == o.gate && exactlyEqual (progress, o.progress) && exactlyEqual (level, o.level)
                          && exactlyEqual (a, o.a) && exactlyEqual (d, o.d) && exactlyEqual (s, o.s) && exactlyEqual (r, o.r) && exactlyEqual (shape, o.shape);
                  } };
    Snap shown;
    Snap read() const;
};

// The round Gate button: held = gate high (in Gate and Trig mode, as the module's button). Glows with the envelope.
class GateButton : public juce::Component, private juce::Timer
{
public:
    explicit GateButton (HillsideProcessor& p);
    ~GateButton() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void press (bool down);                         // also used by the checks

private:
    void timerCallback() override;
    HillsideProcessor& proc;
    juce::RangedAudioParameter* param;
    bool pressed = false;
    float level = 0.0f;
    bool gateIn = false;
};

// A row of segments for a menu control (Gate | Trig, Send as CC | Note | Both).
class Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter& p, const juce::StringArray& names);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void choose (int index);                        // also used by the checks
    int selected() const { return index; }

private:
    juce::StringArray names;
    int index = 0;
    juce::ParameterAttachment attachment;
};

// A level bar for one output (the value it last sent).
class LevelBar : public juce::Component, private juce::Timer
{
public:
    LevelBar (HillsideProcessor& p, int output, juce::Colour c);
    ~LevelBar() override { stopTimer(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    HillsideProcessor& proc;
    int output;
    juce::Colour colour;
    int shownValue = -2;
};

// The whole panel at its design size (780 x 470). The editor below scales it to the window.
class HillsidePanel : public juce::Component
{
public:
    explicit HillsidePanel (HillsideProcessor&);
    ~HillsidePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // For the offline checks: every control and the parameter it moves.
    struct ControlInfo { juce::Component* control; juce::String paramId; bool live; };
    std::vector<ControlInfo> controls();
    GateButton& getGateButton() { return gateButton; }
    Segmented& getModeSwitch() { return modeSwitch; }

    static constexpr int kWidth = 780, kHeight = 470;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    HillsideProcessor& proc;
    HillsideLookAndFeel lnf;

    EnvelopeView envelopeView;
    juce::Slider faders[4];                         // Attack, Decay, Sustain, Release
    juce::Slider shapeKnob;
    GateButton gateButton;
    Segmented modeSwitch;
    juce::ComboBox channelBox;
    juce::ToggleButton envOn, linOn;
    LevelBar envBar, linBar;
    juce::ToggleButton gateOn[4], trigOn;           // milestone 2 (dimmed)
    Segmented sendAs;                               // milestone 2 (dimmed)

    std::unique_ptr<SliderAttachment> faderAtt[4], shapeAtt;
    std::unique_ptr<ComboAttachment> channelAtt;
    std::unique_ptr<ButtonAttachment> envOnAtt, linOnAtt, gateOnAtt[4], trigOnAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HillsidePanel)
};

// The plugin window: resizable between 80% and 200% of the design size, proportions locked. The width is saved with
// the project.
class HillsideEditor : public juce::AudioProcessorEditor
{
public:
    explicit HillsideEditor (HillsideProcessor&);
    ~HillsideEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = HillsidePanel::kWidth, kHeight = HillsidePanel::kHeight;
    static constexpr int kMinWidth = 624, kMaxWidth = 1560;     // 80% and 200%
    static inline const juce::Identifier widthProperty { "editorWidth" };
    static int heightForWidth (int width) { return juce::roundToInt ((double) width * kHeight / kWidth); }

    HillsidePanel panel;

private:
    HillsideProcessor& proc;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HillsideEditor)
};

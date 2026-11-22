#include "Parameters.h"

#include <cmath>

namespace HsParams
{
const char* const gateOnIds[4] = { "gateAOn", "gateDOn", "gateSOn", "gateROn" };
const juce::StringArray gateModeNames { "Gate", "Trig" };
const juce::StringArray sendAsNames   { "CC", "Note", "Both" };

juce::String timeText (float ms)
{
    if (ms < 10.0f)    return juce::String (ms, 1) + " ms";
    if (ms < 1000.0f)  return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms / 1000.0f, 2) + " s";
}

namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    // A time control: the knob position moves the time on a logarithmic scale (equal ratios for equal moves), so the
    // centre of Attack is 173 ms and the centre of Decay and Release 190 ms.
    void addTime (Layout& l, const char* id, const char* name, float lo, float hi, float def)
    {
        juce::NormalisableRange<float> range (lo, hi,
            [] (float a, float b, float x) { return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, x)); },
            [] (float a, float b, float v) { return std::log (juce::jlimit (a, b, v) / a) / std::log (b / a); });
        l.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes()
                .withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) { return timeText (v); })
                .withValueFromStringFunction ([] (const juce::String& t) {
                    const float x = t.getFloatValue();
                    return t.containsIgnoreCase ("ms") || ! t.containsIgnoreCase ("s") ? x : x * 1000.0f; })));
    }
    void addPercent (Layout& l, const char* id, const char* name, float def)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 100.0f), def,
            juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));
    }
    void addBool (Layout& l, const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }
}

Layout createLayout()
{
    Layout l;
    addTime    (l, attack,  "Attack",  kAttackMinMs, kAttackMaxMs, 5.0f);
    addTime    (l, decay,   "Decay",   kTimeMinMs, kTimeMaxMs, 300.0f);
    addPercent (l, sustain, "Sustain", 50.0f);
    addTime    (l, release, "Release", kTimeMinMs, kTimeMaxMs, 400.0f);
    addPercent (l, shape,   "Shape",   100.0f);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { gateMode, 1 }, "Gate/Trig", gateModeNames, 0));
    addBool    (l, manualGate, "Gate button", false);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { outChannel, 1 }, "MIDI out channel", 1, 16, hillside::kDefaultChannel));
    addBool    (l, outEnvOn, "Envelope out", true);
    addBool    (l, outLinOn, "Linear out", true);
    // Milestone 2
    const char* gateNames[4] = { "Gate A out", "Gate D out", "Gate S out", "Gate R out" };
    for (int i = 0; i < 4; ++i) addBool (l, gateOnIds[i], gateNames[i], true);
    addBool    (l, trigOn, "Trig out", true);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { sendAs, 1 }, "Send as", sendAsNames, 0));
    return l;
}

hillside::Settings read (const juce::AudioProcessorValueTreeState& apvts)
{
    auto v = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    hillside::Settings s;
    s.env.attack   = v (attack) / 1000.0;
    s.env.decay    = v (decay) / 1000.0;
    s.env.sustain  = v (sustain) / 100.0;
    s.env.release  = v (release) / 1000.0;
    s.env.shape    = v (shape) / 100.0;
    s.env.trigMode = juce::roundToInt (v (gateMode)) == 1;
    s.outChannel   = juce::jlimit (1, 16, juce::roundToInt (v (outChannel)));
    s.outOn[hillside::outEnvelope] = v (outEnvOn) >= 0.5f;
    s.outOn[hillside::outLinear]   = v (outLinOn) >= 0.5f;
    return s;
}
}

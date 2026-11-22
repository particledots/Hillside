// Every control of Hillside, as Logic sees it.
//
// IMPORTANT: these ids, their order, ranges and menu lists were fixed in the first build (0.1.0) and must never
// change. Saved projects and automation refer to them. Milestone 2's controls (the four stage gates, Trig and
// Send as) were already here in 0.1.0 (stored, shown dimmed) and are live from 0.2.0.
// If something must be added one day, add it at the END with ParameterID version 2.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <hillside/Engine.h>

namespace HsParams
{
    inline constexpr const char* attack     = "attack";      // 3 ms .. 10 s
    inline constexpr const char* decay      = "decay";       // 3 ms .. 12 s
    inline constexpr const char* sustain    = "sustain";     // 0 .. 100 %
    inline constexpr const char* release    = "release";     // 3 ms .. 12 s
    inline constexpr const char* shape      = "shape";       // 0 (straight) .. 100 % (log / exp)
    inline constexpr const char* gateMode   = "gateMode";    // Gate / Trig
    inline constexpr const char* manualGate = "manualGate";  // the panel's Gate button (never saved as held)
    inline constexpr const char* outChannel = "outChannel";  // 1 .. 16
    inline constexpr const char* outEnvOn   = "outEnvOn";    // Envelope output on
    inline constexpr const char* outLinOn   = "outLinOn";    // Linear output on
    // Milestone 2 (live from 0.2.0)
    extern const char* const gateOnIds[4];                   // gateAOn, gateDOn, gateSOn, gateROn
    inline constexpr const char* trigOn     = "trigOn";
    inline constexpr const char* sendAs     = "sendAs";      // CC / Note / Both

    inline constexpr float kAttackMinMs = 3.0f, kAttackMaxMs = 10000.0f;
    inline constexpr float kTimeMinMs = 3.0f, kTimeMaxMs = 12000.0f;   // decay and release

    extern const juce::StringArray gateModeNames;   // Gate, Trig
    extern const juce::StringArray sendAsNames;     // CC, Note, Both

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // The current values, for the engine.
    hillside::Settings read (const juce::AudioProcessorValueTreeState& apvts);

    juce::String timeText (float ms);   // "5.0 ms", "300 ms", "1.20 s"
}

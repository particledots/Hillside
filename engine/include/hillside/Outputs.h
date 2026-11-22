// Hillside's MIDI numbers. Fixed in the first build (0.1.0) and never changed: the mapper and saved projects rely on them.
//
// Chosen so Hillside, Ogdoad (channel 16: CC 12-31, 44-63, 102-105, notes 36-39) and Strange Drift (channel 15:
// CC 106-119) can share one track. CC 3, 9 and 85-90 are the numbers MIDI leaves without a standard job that neither
// of the other two uses; 35 and 41 are the standard fine partners of 3 and 9 (coarse + 32).

#pragma once

namespace hillside
{
    enum OutputIndex
    {
        outEnvelope = 0,   // follows the Shape knob (the module's one CV output)
        outLinear   = 1,   // always straight, whatever Shape is
        kNumSmoothOutputs = 2
    };

    inline constexpr int kEnvelopeCoarseCc = 3,  kEnvelopeFineCc = 35;
    inline constexpr int kLinearCoarseCc   = 9,  kLinearFineCc   = 41;

    // Milestone 2: the four stage gates and the combined trigger.
    // CC: 127 while the gate is high (0 after), or 127 for 20 ms for the trigger. Note: velocity 100, same timing.
    inline constexpr int kGateCc[4]  = { 85, 86, 87, 88 };   // A, D, S, R
    inline constexpr int kTrigCc     = 89;
    inline constexpr int kGateNote[4] = { 40, 41, 42, 43 };  // E1, F1, F#1, G1
    inline constexpr int kTrigNote   = 44;                   // G#1

    // The five pulse outputs in one list: Gate A, Gate D, Gate S, Gate R, Trig.
    inline constexpr int kNumPulseOutputs = 5;
    inline constexpr int kTrigIndex = 4;
    inline constexpr int kPulseCc[kNumPulseOutputs]   = { 85, 86, 87, 88, 89 };
    inline constexpr int kPulseNote[kNumPulseOutputs] = { 40, 41, 42, 43, 44 };
    inline constexpr int kPulseVelocity = 100;
    inline constexpr double kPulseSeconds = 0.020;   // the trigger's length, and the shortest gate

    // "Send as" (the control's menu order): CC, Note, Both.
    enum SendAs { sendAsCc = 0, sendAsNote = 1, sendAsBoth = 2 };

    inline constexpr int kDefaultChannel = 16;
}

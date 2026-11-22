// One ADSR envelope, worked out sample by sample. Plain C++, no plugin code.
//
// Two outputs share one timeline: "linear" (always straight) and "shaped" (follows the Shape curve). Each stage moves
// from where the envelope was when the stage began to the stage's target:
//   Attack  -> 1 (the top)       takes Attack x (1 - where the linear output started): from 0 the full Attack time,
//                                from part way up (a new note during the release) proportionally less, same speed.
//   Decay   -> Sustain level     always takes the Decay time. Skipped when Sustain is at 100% (ASR).
//   Sustain    holds the Sustain level while the gate is high. Never entered when Sustain is at 0% (AD): after the
//              decay the envelope rests at 0 until the next rising gate.
//   Release -> 0                 always takes the Release time, from wherever it starts.
// Gate mode: the gate falling during Attack, Decay or Sustain starts the Release at once.
// Trig mode: the Attack always completes; if the gate is low by then the Release follows straight away (no Decay or
// Sustain). Falling during Decay or Sustain starts the Release at once, as in Gate mode.
// A rising gate during Release (or at rest) starts a new Attack from the current level (no jump to zero). A rising
// gate while the Attack is still running (Trig mode, gate fell and rose again) lets it carry on.
//
// Timing: every stage lasts a whole number of samples, the stage time rounded to the nearest sample, so stages start
// and end within half a sample of the ideal moment at any sample rate, whatever the block size. Changing a time while
// a stage runs changes its speed from that sample on, without a jump.

#pragma once

#include "hillside/Curve.h"
#include <cstdint>

namespace hillside
{
    enum class Stage : std::uint8_t { idle = 0, attack = 1, decay = 2, sustain = 3, release = 4 };

    struct EnvSettings
    {
        double attack  = 0.005;   // seconds, 0.003 .. 10
        double decay   = 0.300;   // seconds, 0.003 .. 12
        double sustain = 0.5;     // 0 .. 1
        double release = 0.400;   // seconds, 0.003 .. 12
        double shape   = 1.0;     // 0 (straight) .. 1 (log attack, exponential decay and release)
        bool   trigMode = false;  // false = Gate mode
    };

    inline constexpr double kSustainEdge = 1.0e-6;   // Sustain at or below this = 0% (AD); at or above 1 - this = 100% (ASR)

    struct EnvSample
    {
        double linear, shaped;   // 0 .. 1
        Stage stage;
        bool corner;             // a stage reached its target at this sample (the top, the sustain level, or 0)
        unsigned run;            // counts every stage that starts; a new number at this sample = a stage started here
    };

    class Envelope
    {
    public:
        void prepare (double sampleRate);
        void setSettings (const EnvSettings& s);   // applies from the next sample; a running stage keeps its progress
        const EnvSettings& settings() const { return set; }

        void gateRise();   // events at a sample are applied before that sample is worked out
        void gateFall();
        void reset();      // straight to rest: level 0, no stage, gate low

        EnvSample next();  // the values at this sample, then one sample on

        Stage stage() const      { return st; }
        unsigned runs() const    { return runCount; }   // stages started so far (A, D, S, R; never rest)
        double progress() const  { return p; }   // 0 .. 1 through the current stage
        double linear() const    { return lin; }
        double shaped() const    { return shp; }
        bool gate() const        { return gateHigh; }
        double sampleRate() const { return sr; }

        // Stage length in samples (at least one): the time rounded to the nearest sample.
        static long long samplesFor (double seconds, double sampleRate);

    private:
        void enter (Stage s);
        void finishStage();
        void updateIncrement();

        double sr = 48000.0;
        EnvSettings set;
        Curve curve;
        Stage st = Stage::idle;
        double p = 0.0, inc = 0.0;
        double startLin = 0.0, startShp = 0.0;   // where the current stage began
        double lin = 0.0, shp = 0.0;             // the latest values
        double attackFraction = 1.0;             // the part of the full rise this attack has to cover (linear)
        bool gateHigh = false;
        unsigned runCount = 0;
    };
}

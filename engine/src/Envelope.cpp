#include "hillside/Envelope.h"

#include <cmath>

namespace hillside
{
    namespace
    {
        constexpr double kDone = 1.0 - 1.0e-9;   // progress counted as complete (rounding of N steps of 1/N)
        bool timed (Stage s) { return s == Stage::attack || s == Stage::decay || s == Stage::release; }
    }

    long long Envelope::samplesFor (double seconds, double sampleRate)
    {
        const long long n = (long long) std::llround (seconds * sampleRate);
        return n < 1 ? 1 : n;
    }

    void Envelope::prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
        updateIncrement();
    }

    void Envelope::setSettings (const EnvSettings& s)
    {
        set = s;
        curve.setShape (s.shape);
        updateIncrement();
    }

    void Envelope::updateIncrement()
    {
        switch (st)
        {
            case Stage::attack:
            {
                const long long n = (long long) std::llround (set.attack * sr * attackFraction);
                inc = 1.0 / (double) (n < 1 ? 1 : n);
                break;
            }
            case Stage::decay:   inc = 1.0 / (double) samplesFor (set.decay, sr); break;
            case Stage::release: inc = 1.0 / (double) samplesFor (set.release, sr); break;
            default:             inc = 0.0; break;
        }
    }

    void Envelope::reset()
    {
        st = Stage::idle;
        p = 0.0; inc = 0.0;
        startLin = startShp = lin = shp = 0.0;
        attackFraction = 1.0;
        gateHigh = false;
    }

    void Envelope::enter (Stage s)
    {
        st = s;
        p = 0.0;
        startLin = lin;
        startShp = shp;
        if (s == Stage::attack)
        {
            attackFraction = 1.0 - startLin;
            if (attackFraction < 0.0) attackFraction = 0.0;
        }
        if (s == Stage::release && startLin <= 0.0 && startShp <= 0.0)
            st = Stage::idle;   // nothing to release
        updateIncrement();
    }

    void Envelope::gateRise()
    {
        gateHigh = true;
        if (st == Stage::idle || st == Stage::release)
            enter (Stage::attack);
        // Attack: carries on (Trig mode, the gate fell and rose again). Decay / Sustain: the gate was already high.
    }

    void Envelope::gateFall()
    {
        gateHigh = false;
        switch (st)
        {
            case Stage::attack:  if (! set.trigMode) enter (Stage::release); break;
            case Stage::decay:
            case Stage::sustain: enter (Stage::release); break;
            default: break;   // at rest (also after an AD envelope) or already releasing
        }
    }

    void Envelope::finishStage()
    {
        switch (st)
        {
            case Stage::attack:
                lin = shp = 1.0;
                if (! gateHigh)                              enter (Stage::release);   // Trig mode: straight to release
                else if (set.sustain >= 1.0 - kSustainEdge)  enter (Stage::sustain);   // ASR: no decay
                else                                         enter (Stage::decay);
                break;
            case Stage::decay:
            {
                const double s = set.sustain <= kSustainEdge ? 0.0 : set.sustain;
                lin = shp = s;
                enter (s <= 0.0 ? Stage::idle : Stage::sustain);                     // AD: rests at 0
                break;
            }
            case Stage::release:
                lin = shp = 0.0;
                enter (Stage::idle);
                break;
            default: break;
        }
    }

    EnvSample Envelope::next()
    {
        bool corner = false;
        while (timed (st) && p >= kDone)
        {
            finishStage();
            corner = true;
        }

        const double s = set.sustain <= kSustainEdge ? 0.0 : (set.sustain >= 1.0 - kSustainEdge ? 1.0 : set.sustain);
        switch (st)
        {
            case Stage::idle:    lin = shp = 0.0; break;
            case Stage::attack:  lin = startLin + (1.0 - startLin) * p; shp = startShp + (1.0 - startShp) * curve (p); break;
            case Stage::decay:   lin = 1.0 + (s - 1.0) * p;             shp = 1.0 + (s - 1.0) * curve (p); break;
            case Stage::sustain: lin = shp = s; break;
            case Stage::release: lin = startLin * (1.0 - p);            shp = startShp * (1.0 - curve (p)); break;
        }

        const EnvSample out { lin, shp, st, corner };
        if (timed (st)) p += inc;
        return out;
    }
}

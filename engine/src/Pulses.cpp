#include "hillside/Pulses.h"

#include <cmath>

namespace hillside
{
    void Pulses::prepare (double sampleRate)
    {
        const long long n = (long long) std::llround (kPulseSeconds * (sampleRate > 0.0 ? sampleRate : 48000.0));
        length = n < 1 ? 1 : n;
        for (auto& o : outs) { o.until = 0; o.source = false; }
    }

    bool Pulses::anyOn() const
    {
        for (int k = 0; k < kNumPulseOutputs; ++k) if (isOn (k)) return true;
        return false;
    }

    void Pulses::configure (const bool en[kNumPulseOutputs], int ch, int sendAs, int offset, MidiSink& sink)
    {
        if (sendAs < sendAsCc || sendAs > sendAsBoth) sendAs = sendAsCc;
        if (ch != channel || sendAs != mode) closeAll (offset, sink);
        for (int k = 0; k < kNumPulseOutputs; ++k)
        {
            if (! en[k] && isOn (k)) close (k, offset, sink);
            enabled[k] = en[k];
        }
        channel = ch;
        mode = sendAs;
    }

    void Pulses::open (int k, long long absSample, int offset, MidiSink& sink)
    {
        auto& o = outs[k];
        if (! isOn (k))
        {
            if (mode != sendAsNote) { sink.cc (offset, channel, kPulseCc[k], 127); o.cc = true; }
            if (mode != sendAsCc)   { sink.note (offset, channel, kPulseNote[k], kPulseVelocity); o.note = true; }
            o.channel = channel;
            ++o.starts;
        }
        o.until = absSample + length;   // on again while still on: the two merge
    }

    void Pulses::close (int k, int offset, MidiSink& sink)
    {
        auto& o = outs[k];
        if (o.cc)   sink.cc (offset, o.channel, kPulseCc[k], 0);
        if (o.note) sink.note (offset, o.channel, kPulseNote[k], 0);
        o.cc = o.note = false;
    }

    void Pulses::closeAll (int offset, MidiSink& sink)
    {
        for (int k = 0; k < kNumPulseOutputs; ++k)
        {
            if (isOn (k)) close (k, offset, sink);
            outs[k].source = false;
        }
    }

    void Pulses::step (long long absSample, int offset, Stage stage, bool started, MidiSink& sink)
    {
        for (int k = 0; k < kNumPulseOutputs; ++k)
        {
            auto& o = outs[k];
            bool rise;
            if (k < 4)
            {
                o.source = stage == (Stage) (k + 1);
                rise = o.source && started;
            }
            else
                rise = started && stage != Stage::idle;

            if (rise && enabled[k])                                      open (k, absSample, offset, sink);
            else if (isOn (k) && ! o.source && absSample >= o.until)     close (k, offset, sink);
        }
    }
}

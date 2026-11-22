// Prints the engine's real MIDI stream, for the mapper test: "sample channel cc value" per CC.
// Plays a fixed pattern of notes (short, long, overlapping, back-to-back) with settings that change between phases.
// Usage: hillside_ccdump [seconds] [out channel]

#include "hillside/Engine.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace hillside;

struct Printer : MidiSink
{
    long long blockStart = 0;
    void cc (int offset, int channel, int number, int v) override { std::printf ("%lld %d %d %d\n", blockStart + offset, channel, number, v); }
    void note (int offset, int channel, int number, int v) override { std::printf ("note %lld %d %d %d\n", blockStart + offset, channel, number, v); }
};

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 30.0;
    const int channel = argc > 2 ? std::atoi (argv[2]) : 16;
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr);
    Settings s;
    s.outChannel = channel;
    s.env.attack = 0.02; s.env.decay = 0.3; s.env.sustain = 0.4; s.env.release = 0.5; s.env.shape = 1.0;
    e.setSettings (s);
    Printer p;
    const long long total = (long long) (seconds * sr);
    std::vector<InputEvent> buf;
    long long pos = 0;
    int phase = 0;
    while (pos < total)
    {
        const int n = (int) (total - pos > 512 ? 512 : total - pos);
        buf.clear();
        for (int i = 0; i < n; ++i)
        {
            const long long t = pos + i, cycle = (long long) (2.0 * sr), x = t % cycle;
            if (x == 0) buf.push_back ({ i, InputEvent::noteOn, 0, (std::uint8_t) (48 + (t / cycle) % 24) });
            if (x == (long long) (0.9 * sr)) buf.push_back ({ i, InputEvent::noteOff, 0, (std::uint8_t) (48 + (t / cycle) % 24) });
            if (x == (long long) (1.2 * sr)) buf.push_back ({ i, InputEvent::noteOn, 1, 70 });
            if (x == (long long) (1.25 * sr)) buf.push_back ({ i, InputEvent::noteOff, 1, 70 });
        }
        const int newPhase = (int) (pos / (long long) (5.0 * sr));
        if (newPhase != phase)
        {
            phase = newPhase;
            s.env.shape = (phase % 3) / 2.0;
            s.env.trigMode = phase % 2 == 1;
            s.env.sustain = phase % 4 == 3 ? 1.0 : 0.4;
            s.env.attack = phase % 2 == 0 ? 0.003 : 0.2;
            e.setSettings (s);
        }
        p.blockStart = pos;
        e.render (n, buf.data(), (int) buf.size(), false, p);
        pos += n;
    }
    // Let the last envelope finish, then report where each output ended (14-bit), for the test to compare with.
    for (int k = 0; k < 400; ++k) { p.blockStart = pos; e.render (512, nullptr, 0, false, p); pos += 512; }
    std::printf ("final 0 %d\n", e.ccStream().lastSent (outEnvelope));
    std::printf ("final 1 %d\n", e.ccStream().lastSent (outLinear));
    return 0;
}

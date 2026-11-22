// Hillside engine self-test. Runs on the Linux checking computer before every Mac build.
//
// What it proves, without Logic:
//   1. the Shape curve: exact ends, always rising, straight at 0%;
//   2. stage times: every stage starts and ends within one sample of the ideal moment at 44.1, 48 and 96 kHz, with
//      blocks of 32 to 1024 samples and random sizes; the curve is the same at every block size (identical, sample for
//      sample) and at every sample rate;
//   3. Gate / Trig modes with notes ending in every stage, and zero-length notes;
//   4. the sustain edge cases: 0% (AD: no sustain stage, no release afterwards) and 100% (ASR: no decay);
//   5. Shape: linear output never bends, shaped output bends the right way in every stage;
//   6. a new note during the release starts the attack from where the envelope is, at the same speed, no jump;
//   7. overlapping notes, back-to-back notes (both orders, also the same key), velocity 0, All Notes Off;
//   8. the Gate button, alone and with notes, and after a stop;
//   9. rest (transport stop / reset / bypass): straight to 0, 0 sent;
//  10. changes while a note is held: times, mode, sustain, channel, output switches; no jump, nothing stuck;
//  11. the CC rules: one value per block, 6 ms, 0.2% or 100 ms, corners exact, coarse before fine, nothing at rest,
//      every value sent is the envelope's value at that sample, the receiver always ends where the engine is;
//  12. a stuck-gate fuzz: hundreds of thousands of random events, then everything released: at rest, 0 sent;
//  13. CPU use and the MIDI rate.

#include "hillside/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace hillside;

namespace
{
    long long checks = 0, failures = 0;

    void expect (bool ok, const std::string& what)
    {
        ++checks;
        if (! ok) { ++failures; if (failures < 60) std::printf ("FAIL: %s\n", what.c_str()); }
    }
    std::string str (long long v) { return std::to_string (v); }
    std::string str (double v)    { char b[64]; std::snprintf (b, sizeof (b), "%.6g", v); return b; }

    struct Msg { long long at; int ch, num, val; bool note; long long block; };

    struct Collector : MidiSink
    {
        std::vector<Msg> msgs;
        long long blockStart = 0, block = 0;
        void cc (int off, int ch, int n, int v) override   { msgs.push_back ({ blockStart + off, ch, n, v, false, block }); }
        void note (int off, int ch, int n, int v) override { msgs.push_back ({ blockStart + off, ch, n, v, true, block }); }
    };

    struct Ev { long long at; InputEvent::Kind kind; int ch, note; };
    Ev on (long long at, int note = 60, int ch = 0)  { return { at, InputEvent::noteOn, ch, note }; }
    Ev off (long long at, int note = 60, int ch = 0) { return { at, InputEvent::noteOff, ch, note }; }

    struct Run
    {
        Engine eng;
        Collector col;
        std::vector<TracePoint> tr;
        std::vector<long long> edges;   // the first sample after each block
        std::function<int()> blockSize = [] { return 512; };
        std::function<void (Engine&, long long)> onBlock;      // before each block (settings changes, panic)
        std::function<bool (long long)> manual = [] (long long) { return false; };

        void go (double sr, const Settings& s, std::vector<Ev> evs, long long total)
        {
            std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });
            eng.prepare (sr);
            eng.setSettings (s);
            eng.trace = &tr;
            tr.reserve ((size_t) total);
            long long pos = 0;
            size_t ei = 0;
            std::vector<InputEvent> buf;
            while (pos < total)
            {
                const int n = (int) std::min<long long> (blockSize(), total - pos);
                if (onBlock) onBlock (eng, pos);
                buf.clear();
                while (ei < evs.size() && evs[ei].at < pos + n)
                {
                    buf.push_back ({ (int) (evs[ei].at - pos), evs[ei].kind, (std::uint8_t) evs[ei].ch, (std::uint8_t) evs[ei].note });
                    ++ei;
                }
                col.blockStart = pos;
                eng.render (n, buf.data(), (int) buf.size(), manual (pos), col);
                ++col.block;
                pos += n;
                edges.push_back (pos);
            }
            eng.trace = nullptr;
        }
    };

    struct Change { long long at; Stage stage; };
    std::vector<Change> changes (const std::vector<TracePoint>& tr)
    {
        std::vector<Change> c;
        for (size_t i = 0; i < tr.size(); ++i)
            if (i == 0 || tr[i].stage != tr[i - 1].stage) c.push_back ({ (long long) i, tr[i].stage });
        return c;
    }
    const char* stageName (Stage s)
    {
        switch (s) { case Stage::idle: return "rest"; case Stage::attack: return "A"; case Stage::decay: return "D";
                     case Stage::sustain: return "S"; case Stage::release: return "R"; }
        return "?";
    }
    std::string show (const std::vector<Change>& c)
    {
        std::string s;
        for (auto& x : c) s += std::string (stageName (x.stage)) + "@" + str (x.at) + " ";
        return s;
    }

    Settings basic (double a, double d, double s, double r, double shape, bool trig = false)
    {
        Settings st;
        st.env.attack = a; st.env.decay = d; st.env.sustain = s; st.env.release = r; st.env.shape = shape; st.env.trigMode = trig;
        return st;
    }
    long long N (double seconds, double sr) { return Envelope::samplesFor (seconds, sr); }

    // The receiver: what a Scripter mapper (or anything reading the 14-bit pairs) knows after each message.
    struct Receiver
    {
        int coarse[128] {};
        int value[128];
        Receiver() { for (auto& v : value) v = -1; }
        // returns the fine CC number that completed a value, or -1
        int take (const Msg& m)
        {
            if (m.note) return -1;
            if (m.num == kEnvelopeCoarseCc || m.num == kLinearCoarseCc) { coarse[m.num] = m.val; return -1; }
            if (m.num == kEnvelopeFineCc || m.num == kLinearFineCc) { value[m.num] = coarse[m.num - 32] * 128 + m.val; return m.num; }
            return -1;
        }
    };

    // ------------------------------------------------------------------------------------------------ 1. curve
    void testCurve()
    {
        for (double shape : { 0.0, 0.001, 0.25, 0.5, 0.75, 1.0 })
        {
            Curve c; c.setShape (shape);
            expect (c (0.0) == 0.0 && c (1.0) == 1.0, "curve ends exact at shape " + str (shape));
            double prev = 0.0; bool rising = true, bendsUp = true;
            for (int i = 1; i <= 1000; ++i)
            {
                const double p = i / 1000.0, v = c (p);
                rising = rising && v >= prev;
                bendsUp = bendsUp && v >= p - 1e-12;
                prev = v;
            }
            expect (rising && bendsUp, "curve always rising, never below straight, shape " + str (shape));
        }
        Curve lin; lin.setShape (0.0);
        expect (lin (0.37) == 0.37, "shape 0% is straight");
        Curve full; full.setShape (1.0);
        expect (std::abs (full (0.5) - 0.9241) < 1e-3, "shape 100%: 92% of the way at half time (" + str (full (0.5)) + ")");
    }

    // ------------------------------------------------------------------------------------------------ 2. timing
    void testTiming()
    {
        const double A = 0.25, D = 0.4, S = 0.5, R = 0.7;
        const std::vector<int> sizes { 32, 64, 100, 128, 256, 333, 512, 1000, 1024 };
        std::vector<std::vector<TracePoint>> perRate;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (double shape : { 0.0, 0.5, 1.0 })
            {
                const long long onS = std::llround (0.1 * sr), offS = std::llround (1.5 * sr), total = offS + (long long) sr;
                Run ref;
                ref.go (sr, basic (A, D, S, R, shape), { on (onS), off (offS) }, total);
                const auto c = changes (ref.tr);
                const long long a = onS, d = a + N (A, sr), s = d + N (D, sr), r = offS, end = r + N (R, sr);
                const std::vector<Change> want { { 0, Stage::idle }, { a, Stage::attack }, { d, Stage::decay }, { s, Stage::sustain }, { r, Stage::release }, { end, Stage::idle } };
                bool same = c.size() == want.size();
                for (size_t i = 0; same && i < c.size(); ++i) same = c[i].at == want[i].at && c[i].stage == want[i].stage;
                expect (same, "stages at " + str (sr) + " Hz shape " + str (shape) + ": " + show (c));
                // Each moment within one sample of the ideal (exact time x sample rate).
                const double ideal[] = { 0.1 * sr, (0.1 + A) * sr, (0.1 + A + D) * sr, 1.5 * sr, (1.5 + R) * sr };
                for (int k = 0; k < 5 && c.size() == 6; ++k)
                    expect (std::abs ((double) c[(size_t) k + 1].at - ideal[k]) <= 1.0, "stage change " + str ((long long) k) + " within one sample at " + str (sr) + " Hz");

                // Same trace at every block size, and with random block sizes.
                for (int bs : sizes)
                {
                    Run r2; r2.blockSize = [bs] { return bs; };
                    r2.go (sr, basic (A, D, S, R, shape), { on (onS), off (offS) }, total);
                    bool eq = r2.tr.size() == ref.tr.size();
                    for (size_t i = 0; eq && i < ref.tr.size(); ++i)
                        eq = r2.tr[i].linear == ref.tr[i].linear && r2.tr[i].shaped == ref.tr[i].shaped && r2.tr[i].stage == ref.tr[i].stage;
                    expect (eq, "identical envelope with blocks of " + str ((long long) bs) + " at " + str (sr) + " Hz");
                }
                std::mt19937 rng ((unsigned) sr);
                Run r3; r3.blockSize = [&rng] { return 1 + (int) (rng() % 1024); };
                r3.go (sr, basic (A, D, S, R, shape), { on (onS), off (offS) }, total);
                bool eq = r3.tr.size() == ref.tr.size();
                for (size_t i = 0; eq && i < ref.tr.size(); ++i) eq = r3.tr[i].shaped == ref.tr[i].shaped && r3.tr[i].stage == ref.tr[i].stage;
                expect (eq, "identical envelope with random block sizes at " + str (sr) + " Hz");

                // The values: exact at the corners, the curve at half way.
                expect (ref.tr[(size_t) d].linear == 1.0f && ref.tr[(size_t) d].shaped == 1.0f && ref.tr[(size_t) d].corner, "top reached exactly at the end of the attack");
                expect (ref.tr[(size_t) s].linear == 0.5f && ref.tr[(size_t) s].shaped == 0.5f && ref.tr[(size_t) s].corner, "sustain level reached exactly");
                expect (ref.tr[(size_t) end].linear == 0.0f && ref.tr[(size_t) end].shaped == 0.0f && ref.tr[(size_t) end].corner, "0 reached exactly at the end of the release");
                Curve cv; cv.setShape (shape);
                const long long half = a + N (A, sr) / 2;
                const double p = (double) (half - a) / (double) N (A, sr);
                expect (std::abs (ref.tr[(size_t) half].shaped - cv (p)) < 1e-6, "attack follows the curve");
                expect (std::abs (ref.tr[(size_t) half].linear - p) < 1e-6, "linear attack is straight");
                if (shape == 1.0) perRate.push_back (ref.tr);
            }
        // Same curve at every sample rate: compare at the same moments in time.
        const double rates[] = { 44100.0, 48000.0, 96000.0 };
        double worst = 0.0;
        for (int t = 0; t < 2600; ++t)
        {
            const double sec = t / 1000.0;
            double v[3];
            for (int k = 0; k < 3; ++k)
            {
                const double x = sec * rates[k];
                const size_t i = (size_t) x;
                if (i + 1 >= perRate[(size_t) k].size()) { v[k] = 0; continue; }
                const double f = x - (double) i;
                v[k] = perRate[(size_t) k][i].shaped * (1 - f) + perRate[(size_t) k][i + 1].shaped * f;
            }
            worst = std::max ({ worst, std::abs (v[0] - v[1]), std::abs (v[1] - v[2]) });
        }
        expect (worst < 2e-3, "same curve at 44.1, 48 and 96 kHz (largest difference " + str (worst) + ")");
        std::printf ("timing: stages within one sample at 44.1 / 48 / 96 kHz, identical at every block size; curve difference across rates %.2g\n", worst);
    }

    // ------------------------------------------------------------------------------------------------ 3. gate / trig
    void testModes()
    {
        const double sr = 48000.0, A = 0.2, D = 0.3, S = 0.6, R = 0.25;
        const long long a = 4800, na = N (A, sr), nd = N (D, sr), nr = N (R, sr);
        struct Case { const char* name; bool trig; long long len; std::vector<Change> want; };
        const long long L1 = std::llround (0.05 * sr), L2 = std::llround (0.35 * sr), L3 = std::llround (1.0 * sr);
        const std::vector<Case> cases {
            { "Gate, note ends in attack", false, L1, { { 0, Stage::idle }, { a, Stage::attack }, { a + L1, Stage::release }, { a + L1 + nr, Stage::idle } } },
            { "Gate, note ends in decay",  false, L2, { { 0, Stage::idle }, { a, Stage::attack }, { a + na, Stage::decay }, { a + L2, Stage::release }, { a + L2 + nr, Stage::idle } } },
            { "Gate, long note",           false, L3, { { 0, Stage::idle }, { a, Stage::attack }, { a + na, Stage::decay }, { a + na + nd, Stage::sustain }, { a + L3, Stage::release }, { a + L3 + nr, Stage::idle } } },
            { "Gate, zero-length note",    false, 0,  { { 0, Stage::idle }, { a, Stage::attack }, { a + 1, Stage::idle } } },
            { "Trig, note ends in attack", true,  L1, { { 0, Stage::idle }, { a, Stage::attack }, { a + na, Stage::release }, { a + na + nr, Stage::idle } } },
            { "Trig, note ends in decay",  true,  L2, { { 0, Stage::idle }, { a, Stage::attack }, { a + na, Stage::decay }, { a + L2, Stage::release }, { a + L2 + nr, Stage::idle } } },
            { "Trig, long note",           true,  L3, { { 0, Stage::idle }, { a, Stage::attack }, { a + na, Stage::decay }, { a + na + nd, Stage::sustain }, { a + L3, Stage::release }, { a + L3 + nr, Stage::idle } } },
            { "Trig, zero-length note",    true,  0,  { { 0, Stage::idle }, { a, Stage::attack }, { a + na, Stage::release }, { a + na + nr, Stage::idle } } },
        };
        for (const auto& k : cases)
            for (int bs : { 64, 512, 1024 })
            {
                Run r; r.blockSize = [bs] { return bs; };
                r.go (sr, basic (A, D, S, R, 1.0, k.trig), { on (a), off (a + k.len) }, a + L3 + nr + 4800);
                const auto c = changes (r.tr);
                bool same = c.size() == k.want.size();
                for (size_t i = 0; same && i < c.size(); ++i) same = c[i].at == k.want[i].at && c[i].stage == k.want[i].stage;
                expect (same, std::string (k.name) + " (blocks " + str ((long long) bs) + "): " + show (c));
                expect (r.tr.back().stage == Stage::idle && r.tr.back().linear == 0.0f && ! r.tr.back().gate, std::string (k.name) + ": ends at rest");
            }
        // Trig mode: the release after a short note starts from the very top.
        Run t; t.go (sr, basic (A, D, S, R, 1.0, true), { on (a), off (a + L1) }, a + na + 10);
        expect (t.tr[(size_t) (a + na)].linear == 1.0f && t.tr[(size_t) (a + na)].stage == Stage::release, "Trig: release starts at the top");
        // Gate mode: the release after a short note starts from where the attack had got to, and takes the full time.
        Run g; g.go (sr, basic (A, D, S, R, 0.0, false), { on (a), off (a + L1) }, a + L1 + nr + 10);
        const double startLevel = g.tr[(size_t) (a + L1)].linear;
        expect (std::abs (startLevel - (double) (L1 - 1) / (double) na) < 1e-4, "Gate: release starts where the attack was (" + str (startLevel) + ")");
    }

    // ------------------------------------------------------------------------------------------------ 4. sustain edges
    void testSustainEdges()
    {
        const double sr = 48000.0;
        const long long a = 480, na = N (0.1, sr), nd = N (0.2, sr), nr = N (0.3, sr), off1 = a + 48000;
        Run ad; ad.go (sr, basic (0.1, 0.2, 0.0, 0.3, 1.0), { on (a), off (off1) }, off1 + 48000);
        auto c = changes (ad.tr);
        expect (c.size() == 4 && c[1].stage == Stage::attack && c[2].stage == Stage::decay && c[2].at == a + na
                && c[3].stage == Stage::idle && c[3].at == a + na + nd, "Sustain 0%: attack, decay, then rest (AD): " + show (c));
        bool zeroWhileHeld = true;
        for (long long i = a + na + nd; i < off1; ++i) zeroWhileHeld = zeroWhileHeld && ad.tr[(size_t) i].linear == 0.0f && ad.tr[(size_t) i].gate;
        expect (zeroWhileHeld, "Sustain 0%: rests at 0 while the note is still held, no sustain stage");
        bool noRelease = true;
        for (auto& x : c) noRelease = noRelease && x.stage != Stage::release && x.stage != Stage::sustain;
        expect (noRelease, "Sustain 0%: no release after the note ends");

        Run asr; asr.go (sr, basic (0.1, 0.2, 1.0, 0.3, 1.0), { on (a), off (off1) }, off1 + 48000);
        c = changes (asr.tr);
        expect (c.size() == 5 && c[1].stage == Stage::attack && c[2].stage == Stage::sustain && c[2].at == a + na
                && c[3].stage == Stage::release && c[3].at == off1 && c[4].stage == Stage::idle && c[4].at == off1 + nr,
                "Sustain 100%: attack, sustain at the top, release (ASR, no decay): " + show (c));
        expect (asr.tr[(size_t) (a + na + 100)].linear == 1.0f, "ASR holds the top");

        Run mid; mid.go (sr, basic (0.1, 0.2, 0.999, 0.3, 1.0), { on (a), off (off1) }, off1 + 48000);
        c = changes (mid.tr);
        expect (c.size() == 6, "Sustain 99.9%: all four stages: " + show (c));
        Run low; low.go (sr, basic (0.1, 0.2, 0.001, 0.3, 1.0), { on (a), off (off1) }, off1 + 48000);
        c = changes (low.tr);
        expect (c.size() == 6, "Sustain 0.1%: all four stages: " + show (c));
    }

    // ------------------------------------------------------------------------------------------------ 5. shape
    void testShape()
    {
        const double sr = 48000.0;
        const std::vector<Ev> ev { on (480), off (480 + 48000) };
        Run straight; straight.go (sr, basic (0.3, 0.3, 0.4, 0.5, 0.0), ev, 120000);
        bool eq = true;
        for (auto& p : straight.tr) eq = eq && p.linear == p.shaped;
        expect (eq, "Shape 0%: the Envelope output equals the Linear output at every sample");

        Run bent; bent.go (sr, basic (0.3, 0.3, 0.4, 0.5, 1.0), ev, 120000);
        Run halfway; halfway.go (sr, basic (0.3, 0.3, 0.4, 0.5, 0.5), ev, 120000);
        bool rightWay = true, monotone = true, between = true;
        for (size_t i = 1; i < bent.tr.size(); ++i)
        {
            const auto& p = bent.tr[i];
            const auto& q = bent.tr[i - 1];
            if (p.stage == Stage::attack)
            {
                rightWay = rightWay && p.shaped >= p.linear - 1e-6f;
                monotone = monotone && (q.stage != Stage::attack || (p.shaped >= q.shaped && p.linear >= q.linear));
                between = between && halfway.tr[i].shaped <= p.shaped + 1e-6f && halfway.tr[i].shaped >= p.linear - 1e-6f;
            }
            if (p.stage == Stage::decay || p.stage == Stage::release)
            {
                rightWay = rightWay && p.shaped <= p.linear + 1e-6f;
                monotone = monotone && (q.stage != p.stage || (p.shaped <= q.shaped && p.linear <= q.linear));
            }
        }
        expect (rightWay, "Shape 100%: log attack above straight, exponential decay and release below");
        expect (monotone, "every stage moves one way only");
        expect (between, "Shape 50% lies between straight and 100%");
    }

    // ------------------------------------------------------------------------------------------------ 6. new note in release
    void testRetrigger()
    {
        const double sr = 48000.0;
        for (double shape : { 0.0, 1.0 })
        {
            Run r;
            const long long on1 = 4800, off1 = 48000, on2 = off1 + 24000;   // half way through a 1 s release
            r.go (sr, basic (0.2, 0.2, 0.8, 1.0, shape), { on (on1), off (off1), on (on2) }, on2 + 48000);
            const auto c = changes (r.tr);
            const double start = r.tr[(size_t) (on2 - 1)].linear;
            const long long expectLen = std::llround (0.2 * sr * (1.0 - start));
            long long attackLen = -1;
            for (size_t i = 0; i + 1 < c.size(); ++i)
                if (c[i].at == on2 && c[i].stage == Stage::attack) attackLen = c[i + 1].at - c[i].at;
            expect (attackLen >= 0 && std::abs (attackLen - expectLen) <= 1,
                    "new note in release: attack from " + str (start) + " takes " + str (attackLen) + " samples (expected " + str (expectLen) + ")");
            float worst = 0;
            for (size_t i = 1; i < r.tr.size(); ++i) worst = std::max (worst, std::abs (r.tr[i].linear - r.tr[i - 1].linear));
            expect (worst < 2.0f / (0.2f * 48000.0f), "no jump anywhere (largest step " + str ((double) worst) + ")");
            expect (r.tr[(size_t) on2].linear >= (float) start - 1e-6f, "attack starts from where the release was");
        }
    }

    // ------------------------------------------------------------------------------------------------ 7. notes
    void testNotes()
    {
        const double sr = 48000.0;
        const Settings s = basic (0.05, 0.1, 0.5, 0.2, 1.0);
        {   // overlapping notes: one envelope, released by the last key
            Run r;
            r.go (sr, s, { on (480, 60), on (6000, 64), off (20000, 60), off (40000, 64) }, 60000);
            const auto c = changes (r.tr);
            expect (c.size() == 6 && c[4].stage == Stage::release && c[4].at == 40000, "overlapping notes: no restart, release when the last key is up: " + show (c));
        }
        {   // a different channel counts too; All Notes Off on that channel releases
            Run r;
            std::vector<Ev> ev { on (480, 60, 3), { 30000, InputEvent::allNotesOff, 3, 0 } };
            r.go (sr, s, ev, 60000);
            const auto c = changes (r.tr);
            expect (c.size() >= 5 && c[4].stage == Stage::release && c[4].at == 30000, "All Notes Off releases: " + show (c));
            Run r2;   // All Notes Off on another channel does not
            r2.go (sr, s, { on (480, 60, 3), { 30000, InputEvent::allNotesOff, 4, 0 }, off (40000, 60, 3) }, 60000);
            const auto c2 = changes (r2.tr);
            expect (c2.size() >= 5 && c2[4].at == 40000, "All Notes Off on another channel leaves the gate: " + show (c2));
        }
        {   // back-to-back, every order
            struct BB { const char* name; std::vector<Ev> ev; };
            const long long t = 30000;
            const std::vector<BB> cases {
                { "off then on (two keys)", { on (480, 60), off (t, 60), on (t, 62) } },
                { "on then off (two keys)", { on (480, 60), on (t, 62), off (t, 60) } },
                { "same key off then on",   { on (480, 60), off (t, 60), on (t, 60) } },
                { "same key on then off",   { on (480, 60), on (t, 60), off (t, 60) } },
            };
            for (const auto& k : cases)
                for (int bs : { 1, 7, 512 })
                {
                    Run r; r.blockSize = [bs] { return bs; };
                    auto ev = k.ev; ev.push_back (off (50000, 60)); ev.push_back (off (50000, 62));
                    r.go (sr, s, ev, 70000);
                    const auto c = changes (r.tr);
                    bool restart = false, releaseBefore = false;
                    for (auto& x : c) { if (x.at == t && x.stage == Stage::attack) restart = true; if (x.at < t && x.stage == Stage::release) releaseBefore = true; }
                    expect (restart && ! releaseBefore, std::string ("back-to-back ") + k.name + ", blocks " + str ((long long) bs) + ": restarts at that sample: " + show (c));
                    expect (r.tr[(size_t) (t - 1)].linear == r.tr[(size_t) t].linear, std::string ("back-to-back ") + k.name + ": attack starts from the sustain level, no jump");
                    expect (r.tr.back().stage == Stage::idle && ! r.tr.back().gate, std::string ("back-to-back ") + k.name + ": ends at rest");
                }
        }
        {   // the same key twice without a release, then one release: the gate falls (never stuck)
            Run r;
            r.go (sr, s, { on (480, 60), on (5000, 60), off (20000, 60) }, 60000);
            expect (r.tr.back().stage == Stage::idle && ! r.tr.back().gate, "same key twice, one release: nothing stuck");
            const auto c = changes (r.tr);
            expect (c.size() == 6, "same key twice: no restart: " + show (c));
        }
        {   // a note-off for a key that was never pressed changes nothing; velocity 0 is handled by the plugin as a release
            Run r;
            r.go (sr, s, { off (1000, 70), on (2000, 60), off (3000, 99), off (30000, 60) }, 60000);
            const auto c = changes (r.tr);
            expect (c.size() == 6 && c[4].at == 30000, "stray note-offs are ignored: " + show (c));
        }
        {   // events in a block of no samples still count
            Engine e; e.prepare (sr); e.setSettings (s);
            Collector col;
            InputEvent onEv { 0, InputEvent::noteOn, 0, 60 }, offEv { 0, InputEvent::noteOff, 0, 60 };
            e.render (512, &onEv, 1, false, col);
            for (int k = 0; k < 20; ++k) e.render (512, nullptr, 0, false, col);
            e.render (0, &offEv, 1, false, col);
            for (int k = 0; k < 100; ++k) e.render (512, nullptr, 0, false, col);
            expect (e.envelope().stage() == Stage::idle && ! e.gate(), "a release in a block of 0 samples is not lost");
        }
    }

    // ------------------------------------------------------------------------------------------------ 8. Gate button
    void testManual()
    {
        const double sr = 48000.0;
        const Settings s = basic (0.05, 0.1, 0.5, 0.2, 1.0);
        {
            Run r;
            r.manual = [] (long long pos) { return pos >= 5120 && pos < 30720; };
            r.go (sr, s, {}, 60000);
            const auto c = changes (r.tr);
            expect (c.size() == 6 && c[1].at == 5120 && c[1].stage == Stage::attack && c[4].at == 30720 && c[4].stage == Stage::release,
                    "Gate button: attack when pressed, release when let go: " + show (c));
        }
        {   // button and a key together: the gate falls only when both are up; no restart in between
            Run r;
            r.manual = [] (long long pos) { return pos >= 5120 && pos < 30720; };
            r.go (sr, s, { on (10000), off (20000) }, 60000);
            const auto c = changes (r.tr);
            expect (c.size() == 6 && c[4].at == 30720, "Gate button and a key: one envelope until both are up: " + show (c));
            Run r2;
            r2.manual = [] (long long pos) { return pos >= 5120 && pos < 30720; };
            r2.go (sr, s, { on (10000), off (40000) }, 60000);
            const auto c2 = changes (r2.tr);
            expect (c2.size() == 6 && c2[4].at == 40000, "key held after the button is let go: release with the key: " + show (c2));
        }
        {   // a stop while the button is held: rest, and the button must be let go and pressed again
            Run r;
            r.manual = [] (long long pos) { return (pos >= 5120 && pos < 30720) || pos >= 40960; };
            r.onBlock = [] (Engine& e, long long pos) { if (pos == 15360) e.panic(); };
            r.go (sr, s, {}, 60000);
            const auto c = changes (r.tr);
            expect (c.size() >= 6 && c[4].at == 15360 && c[4].stage == Stage::idle && c[5].at == 40960 && c[5].stage == Stage::attack,
                    "stop with the button held: rest until it is pressed again: " + show (c));
        }
    }

    // ------------------------------------------------------------------------------------------------ 9. rest
    void testPanic()
    {
        const double sr = 48000.0;
        Run r;
        r.onBlock = [] (Engine& e, long long pos) { if (pos == 20480) e.panic(); };
        r.go (sr, basic (0.05, 0.1, 0.5, 0.2, 1.0), { on (480), off (40000) }, 60000);
        const auto c = changes (r.tr);
        expect (c.size() == 5 && c[4].at == 20480 && c[4].stage == Stage::idle && r.tr[20480].linear == 0.0f, "stop / reset: straight to rest: " + show (c));
        expect (r.tr[39999].stage == Stage::idle && r.tr[45000].stage == Stage::idle, "the key's later release does nothing");
        int zeros = 0;
        for (auto& m : r.col.msgs) if (m.at == 20480 && (m.num == kEnvelopeFineCc || m.num == kLinearFineCc) && m.val == 0) ++zeros;
        expect (zeros == 2, "0 sent on both outputs at that moment (" + str ((long long) zeros) + ")");
        Receiver rx;
        for (auto& m : r.col.msgs) rx.take (m);
        expect (rx.value[kEnvelopeFineCc] == 0 && rx.value[kLinearFineCc] == 0, "receiver at 0 after the stop");

        // flushOnly (bypassed path): rest sent too.
        Engine e; e.prepare (sr); e.setSettings (basic (0.01, 0.1, 0.5, 0.2, 1.0));
        Collector col;
        InputEvent onEv { 0, InputEvent::noteOn, 0, 60 };
        e.render (512, &onEv, 1, false, col);
        for (int k = 0; k < 40; ++k) e.render (512, nullptr, 0, false, col);
        e.panic();
        col.msgs.clear();
        e.flushOnly (512, col);
        Receiver rx2;
        for (auto& m : col.msgs) rx2.take (m);
        expect (rx2.value[kEnvelopeFineCc] == 0 && rx2.value[kLinearFineCc] == 0 && e.envelope().stage() == Stage::idle, "bypass: rest sent, envelope at rest");
        col.msgs.clear();
        for (int k = 0; k < 40; ++k) e.flushOnly (512, col);
        expect (col.msgs.empty(), "bypassed: nothing more is sent");
    }

    // ------------------------------------------------------------------------------------------------ 10. changes while held
    void testChangesWhileHeld()
    {
        const double sr = 48000.0;
        {   // attack time changed half way through the attack: no jump, carries on faster
            Run r;
            r.onBlock = [] (Engine& e, long long pos) { if (pos == 24576) { auto s = e.settings(); s.env.attack = 0.1; e.setSettings (s); } };
            r.go (sr, basic (2.0, 0.3, 0.5, 0.3, 1.0), { on (0), off (100000) }, 130000);
            float worst = 0;
            for (size_t i = 1; i < r.tr.size(); ++i) if (r.tr[i].stage == Stage::attack) worst = std::max (worst, std::abs (r.tr[i].linear - r.tr[i - 1].linear));
            expect (worst < 0.001f, "attack time changed mid-attack: no jump (" + str ((double) worst) + ")");
            const auto c = changes (r.tr);
            expect (c.size() == 5 && r.tr.back().stage == Stage::idle, "and ends normally: " + show (c));
        }
        {   // every control changed at random while held, then released: always ends at rest, receiver at 0
            std::mt19937 rng (5);
            Run r;
            r.blockSize = [&rng] { return 16 + (int) (rng() % 1000); };
            r.onBlock = [&rng] (Engine& e, long long) {
                if (rng() % 3) return;
                auto s = e.settings();
                switch (rng() % 8)
                {
                    case 0: s.env.attack = 0.003 + (rng() % 1000) / 1000.0; break;
                    case 1: s.env.decay = 0.003 + (rng() % 1000) / 1000.0; break;
                    case 2: s.env.sustain = (rng() % 3 == 0) ? (double) (rng() % 2) : (rng() % 1000) / 1000.0; break;
                    case 3: s.env.release = 0.003 + (rng() % 1000) / 1000.0; break;
                    case 4: s.env.shape = (rng() % 1000) / 1000.0; break;
                    case 5: s.env.trigMode = ! s.env.trigMode; break;
                    case 6: s.outChannel = 1 + (int) (rng() % 16); break;
                    case 7: s.outOn[rng() % 2] = (rng() % 2) == 0; break;
                }
                e.setSettings (s);
            };
            std::vector<Ev> ev;
            for (int k = 0; k < 40; ++k) { ev.push_back (on (k * 24000 + 100, 60 + k % 5)); ev.push_back (off (k * 24000 + 100 + (long long) (rng() % 30000), 60 + k % 5)); }
            r.go (sr, basic (0.05, 0.2, 0.5, 0.3, 1.0), ev, 40 * 24000 + 48000 * 3);
            expect (r.tr.back().stage == Stage::idle && ! r.tr.back().gate, "random changes while held: ends at rest");
            // The receiver on each channel: every output that was ever sent on a channel ends at 0 there.
            bool allZero = true;
            for (int ch = 1; ch <= 16; ++ch)
            {
                Receiver rx;
                for (auto& m : r.col.msgs) if (m.ch == ch) rx.take (m);
                allZero = allZero && rx.value[kEnvelopeFineCc] <= 0 && rx.value[kLinearFineCc] <= 0;
            }
            expect (allZero, "random changes while held: every channel's receiver ends at 0");
        }
        {   // channel changed mid-sustain: 0 on the old channel, the value continues on the new one
            Run r;
            r.onBlock = [] (Engine& e, long long pos) { if (pos == 20480) { auto s = e.settings(); s.outChannel = 5; e.setSettings (s); } };
            r.go (sr, basic (0.01, 0.05, 0.5, 0.2, 1.0), { on (100), off (40000) }, 60000);
            Receiver old, neu;
            bool oldAfter = false;
            for (auto& m : r.col.msgs)
            {
                if (m.ch == 16) { old.take (m); if (m.at > 20480) oldAfter = true; }
                if (m.ch == 5) neu.take (m);
            }
            expect (old.value[kEnvelopeFineCc] == 0 && old.value[kLinearFineCc] == 0 && ! oldAfter, "channel change: old channel gets 0, then nothing");
            bool newGotSustain = false;
            for (auto& m : r.col.msgs) if (m.ch == 5 && m.at == 20480 && m.num == kEnvelopeFineCc) newGotSustain = true;
            expect (newGotSustain && neu.value[kEnvelopeFineCc] == 0, "new channel gets the current value at once, and ends at 0");
        }
        {   // an output switched off while moving: 0 once, then silent; switched on again: current value at once
            Run r;
            r.onBlock = [] (Engine& e, long long pos) {
                auto s = e.settings();
                if (pos == 10240) { s.outOn[outEnvelope] = false; e.setSettings (s); }
                if (pos == 30720) { s.outOn[outEnvelope] = true; e.setSettings (s); }
            };
            r.go (sr, basic (0.01, 0.5, 0.5, 0.2, 1.0), { on (100), off (40000) }, 60000);
            int during = 0; bool zeroAtOff = false, resent = false;
            for (auto& m : r.col.msgs)
            {
                if (m.num != kEnvelopeFineCc && m.num != kEnvelopeCoarseCc) continue;
                if (m.at == 10240 && m.num == kEnvelopeFineCc && m.val == 0) zeroAtOff = true;
                if (m.at > 10240 && m.at < 30720) ++during;
                if (m.at == 30720 && m.num == kEnvelopeFineCc) resent = true;
            }
            expect (zeroAtOff && during == 0 && resent, "output switch: 0 when switched off, silent while off, value at once when back on");
            int linDuring = 0;
            for (auto& m : r.col.msgs) if (m.num == kLinearFineCc && m.at > 10240 && m.at < 30720) ++linDuring;
            expect (linDuring > 0, "the other output keeps going");
        }
        {   // sustain moved while sustaining: the level follows
            Run r;
            r.onBlock = [] (Engine& e, long long pos) { if (pos == 20480) { auto s = e.settings(); s.env.sustain = 0.8; e.setSettings (s); } };
            r.go (sr, basic (0.01, 0.05, 0.3, 0.2, 1.0), { on (100), off (40000) }, 60000);
            expect (r.tr[20000].linear == 0.3f && r.tr[25000].linear == 0.8f && r.tr[25000].stage == Stage::sustain, "Sustain moved while held: level follows");
        }
        {   // Gate -> Trig while a short note's attack runs
            Run r;
            r.onBlock = [] (Engine& e, long long pos) { if (pos == 2048) { auto s = e.settings(); s.env.trigMode = true; e.setSettings (s); } };
            r.go (sr, basic (0.2, 0.1, 0.5, 0.2, 1.0), { on (100), off (4800) }, 40000);
            const auto c = changes (r.tr);
            expect (c.size() == 4 && c[2].stage == Stage::release && c[2].at == 100 + N (0.2, sr), "switched to Trig before the note ended: attack completes: " + show (c));
        }
    }

    // ------------------------------------------------------------------------------------------------ 11. CC rules
    void testCcRules()
    {
        long long totalMsgs = 0, corners = 0;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int session = 0; session < 6; ++session)
            {
                std::mt19937 rng ((unsigned) (sr + session * 7));
                const bool fixedBlocks = session % 2 == 0;
                const int fixedSize = 32 << (session % 6);
                Run r;
                r.blockSize = [&rng, fixedBlocks, fixedSize] { return fixedBlocks ? std::min (fixedSize, 1024) : 1 + (int) (rng() % 1024); };
                Settings s = basic (0.003 + (rng() % 300) / 1000.0, 0.003 + (rng() % 600) / 1000.0, (rng() % 101) / 100.0,
                                    0.003 + (rng() % 800) / 1000.0, (rng() % 101) / 100.0, rng() % 2 == 0);
                std::vector<Ev> ev;
                long long t = 1000;
                const long long total = (long long) (sr * 60);
                while (t < total - (long long) sr * 5)
                {
                    const long long len = (long long) (rng() % (unsigned) (sr * 1.5));
                    const int note = 40 + (int) (rng() % 40);
                    ev.push_back (on (t, note));
                    ev.push_back (off (t + len, note));
                    t += (long long) (rng() % (unsigned) (sr * 1.2));
                }
                r.go (sr, s, ev, total);

                const long long minInt = (long long) std::ceil (kMinIntervalSeconds * sr - 1e-9);
                Receiver rx;
                long long lastAt[128], lastBlock[128];
                for (int i = 0; i < 128; ++i) { lastAt[i] = -1000000; lastBlock[i] = -1; }
                bool onePerBlock = true, spacing = true, coarseFirst = true, valueRight = true, coarseOnlyWhenChanged = true;
                int lastCoarse[128];
                for (auto& c : lastCoarse) c = -1;
                for (size_t k = 0; k < r.col.msgs.size(); ++k)
                {
                    const auto& m = r.col.msgs[k];
                    if (m.num == kEnvelopeCoarseCc || m.num == kLinearCoarseCc)
                    {
                        coarseFirst = coarseFirst && k + 1 < r.col.msgs.size() && r.col.msgs[k + 1].num == m.num + 32 && r.col.msgs[k + 1].at == m.at;
                        coarseOnlyWhenChanged = coarseOnlyWhenChanged && m.val != lastCoarse[m.num];
                        lastCoarse[m.num] = m.val;
                    }
                    const int lane = rx.take (m);
                    if (lane < 0) continue;
                    ++totalMsgs;
                    onePerBlock = onePerBlock && m.block != lastBlock[lane];
                    const bool corner = r.tr[(size_t) m.at].corner;
                    corners += corner ? 1 : 0;
                    spacing = spacing && (corner || m.at - lastAt[lane] >= minInt);
                    const float v = lane == kEnvelopeFineCc ? r.tr[(size_t) m.at].shaped : r.tr[(size_t) m.at].linear;
                    valueRight = valueRight && std::abs (rx.value[lane] - toFourteenBit (v)) <= 1;
                    lastAt[lane] = m.at; lastBlock[lane] = m.block;
                }
                const std::string tag = " (" + str (sr) + " Hz, session " + str ((long long) session) + ")";
                expect (onePerBlock, "at most one value per output per block" + tag);
                expect (spacing, "values at least 6 ms apart, except corners" + tag);
                expect (coarseFirst, "coarse part immediately before the fine part, same sample" + tag);
                expect (coarseOnlyWhenChanged, "coarse part sent only when it changes" + tag);
                expect (valueRight, "every value sent is the envelope's value at that sample" + tag);

                // Corners: after each block, the receiver holds exactly the last corner of that block (the top, the
                // sustain level or 0). Only one value per block goes out, so of two corners in one block the later wins.
                Receiver rx2;
                size_t k = 0;
                bool cornersExact = true;
                const auto& blockEnd = r.edges;
                long long lastCorner = -1;
                size_t bi = 0;
                for (size_t i = 0; i < r.tr.size() && cornersExact; ++i)
                {
                    if (r.tr[i].corner) lastCorner = (long long) i;
                    if ((long long) i + 1 == blockEnd[bi])
                    {
                        while (k < r.col.msgs.size() && r.col.msgs[k].at <= (long long) i) rx2.take (r.col.msgs[k++]);
                        if (lastCorner >= 0)
                        {
                            const int want = toFourteenBit (r.tr[(size_t) lastCorner].shaped), wantLin = toFourteenBit (r.tr[(size_t) lastCorner].linear);
                            cornersExact = rx2.value[kEnvelopeFineCc] == want && rx2.value[kLinearFineCc] == wantLin;
                            if (! cornersExact) std::printf ("corner at %lld stage %d want %d got %d\n", lastCorner, (int) r.tr[(size_t) lastCorner].stage, want, rx2.value[kEnvelopeFineCc]);
                        }
                        lastCorner = -1;
                        ++bi;
                    }
                }
                expect (cornersExact, "the top, the sustain level and 0 arrive exactly at their sample" + tag);
                expect (rx.value[kEnvelopeFineCc] == 0 && rx.value[kLinearFineCc] == 0 && r.tr.back().stage == Stage::idle, "ends at rest, receiver at 0" + tag);
                bool quietAtEnd = true;
                for (auto& m : r.col.msgs) if (m.at > total - (long long) sr) quietAtEnd = false;
                expect (quietAtEnd, "nothing sent while at rest" + tag);
            }
        // Nothing sent while sustaining.
        Run h;
        h.go (48000.0, basic (0.01, 0.1, 0.6, 0.2, 1.0), { on (100), off (48000 * 20) }, 48000 * 21);
        int during = 0;
        for (auto& m : h.col.msgs) if (m.at > 48000 && m.at < 48000 * 20) ++during;
        expect (during == 0, "nothing sent during a 19 s sustain (" + str ((long long) during) + ")");
        // First block after prepare: the rest value is sent once on each output.
        expect (h.col.msgs.size() >= 4 && h.col.msgs[0].at == 0 && h.col.msgs[1].num == kEnvelopeFineCc && h.col.msgs[1].val == 0, "rest value sent once at the start");
        std::printf ("CC rules: %lld values checked, %lld at corners\n", totalMsgs, corners);
    }

    // ------------------------------------------------------------------------------------------------ 12. fuzz
    void testFuzz()
    {
        long long events = 0;
        for (int round = 0; round < 6; ++round)
        {
            std::mt19937 rng ((unsigned) (1234 + round));
            const double sr = round % 3 == 0 ? 44100.0 : (round % 3 == 1 ? 48000.0 : 96000.0);
            Engine e; e.prepare (sr);
            Settings s = basic (0.003, 0.02, 0.5, 0.02, 1.0);
            e.setSettings (s);
            Collector col;
            bool down[16][128] {};
            bool manual = false;
            std::vector<InputEvent> buf;
            for (int block = 0; block < 12000; ++block)
            {
                const int n = rng() % 50 == 0 ? (int) (rng() % 2) : 1 + (int) (rng() % 1024);
                buf.clear();
                const int count = (int) (rng() % 8);
                std::vector<int> offs;
                for (int k = 0; k < count; ++k) offs.push_back (n > 0 ? (int) (rng() % (unsigned) n) : 0);
                std::sort (offs.begin(), offs.end());
                for (int k = 0; k < count; ++k)
                {
                    const int ch = (int) (rng() % 3), note = 58 + (int) (rng() % 6);
                    const unsigned kind = rng() % 20;
                    if (kind == 0) { buf.push_back ({ offs[(size_t) k], InputEvent::allNotesOff, (std::uint8_t) ch, 0 }); for (auto& d : down[ch]) d = false; }
                    else if (kind < 10) { buf.push_back ({ offs[(size_t) k], InputEvent::noteOn, (std::uint8_t) ch, (std::uint8_t) note }); down[ch][note] = true; }
                    else { buf.push_back ({ offs[(size_t) k], InputEvent::noteOff, (std::uint8_t) ch, (std::uint8_t) note }); down[ch][note] = false; }
                    ++events;
                }
                if (rng() % 200 == 0) manual = ! manual;
                if (rng() % 500 == 0) { e.panic(); for (auto& c : down) for (auto& d : c) d = false; }
                if (rng() % 50 == 0)
                {
                    s.env.attack = 0.003 + (rng() % 50) / 1000.0; s.env.decay = 0.003 + (rng() % 50) / 1000.0;
                    s.env.sustain = (rng() % 5) / 4.0; s.env.release = 0.003 + (rng() % 50) / 1000.0;
                    s.env.trigMode = rng() % 2 == 0; s.env.shape = (rng() % 5) / 4.0;
                    s.outChannel = rng() % 10 == 0 ? 1 + (int) (rng() % 16) : s.outChannel;
                    if (rng() % 10 == 0) s.outOn[rng() % 2] = rng() % 3 != 0;
                    e.setSettings (s);
                }
                if (rng() % 300 == 0) { col.msgs.clear(); e.flushOnly (n, col); continue; }
                e.render (n, buf.data(), (int) buf.size(), manual, col);
                col.msgs.clear();
            }
            // Release every key that is down, let go of the button, turn the outputs on, and wait.
            buf.clear();
            for (int ch = 0; ch < 16; ++ch) for (int nt = 0; nt < 128; ++nt) if (down[ch][nt]) buf.push_back ({ 0, InputEvent::noteOff, (std::uint8_t) ch, (std::uint8_t) nt });
            e.render (512, buf.data(), (int) buf.size(), false, col);
            s.outOn[0] = s.outOn[1] = true; e.setSettings (s);
            for (int k = 0; k < 400; ++k) e.render (512, nullptr, 0, false, col);
            const bool rest = e.envelope().stage() == Stage::idle && ! e.gate() && e.heldKeys() == 0;
            expect (rest, "fuzz round " + str ((long long) round) + ": at rest after every key is released");
            expect (e.ccStream().lastSent (0) == 0 && e.ccStream().lastSent (1) == 0, "fuzz round " + str ((long long) round) + ": both outputs sent 0 last");
        }
        std::printf ("stuck-gate fuzz: %lld random events in 72,000 blocks, nothing stuck\n", events);
    }

    // ------------------------------------------------------------------------------------------------ 13. CPU and MIDI
    void testCpuAndRate()
    {
        const double sr = 48000.0;
        Engine e; e.prepare (sr);
        e.setSettings (basic (0.01, 0.3, 0.5, 0.4, 1.0));
        Collector col;
        const int seconds = 120, block = 64;
        long long pos = 0, messages = 0;
        std::vector<InputEvent> buf;
        const auto t0 = std::chrono::steady_clock::now();
        for (long long b = 0; b < (long long) (seconds * sr) / block; ++b)
        {
            buf.clear();
            const long long cycle = (long long) (1.5 * sr);
            for (int i = 0; i < block; ++i)
            {
                const long long x = (pos + i) % cycle;
                if (x == 0) buf.push_back ({ i, InputEvent::noteOn, 0, 60 });
                if (x == (long long) sr) buf.push_back ({ i, InputEvent::noteOff, 0, 60 });
            }
            e.render (block, buf.data(), (int) buf.size(), false, col);
            messages += (long long) col.msgs.size();
            col.msgs.clear();
            pos += block;
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double percent = 100.0 * secs / seconds;
        std::printf ("CPU: %.4f%% of one core at 48 kHz, 64-sample blocks, both outputs on\n", percent);
        expect (percent < 1.0, "CPU under 1% of one core (" + str (percent) + "%)");
        std::printf ("MIDI: %.0f messages a second (both outputs; a 1 s note every 1.5 s, attack 10 ms, decay 300 ms, release 400 ms)\n", messages / (double) seconds);
    }
}

int main()
{
    testCurve();
    testTiming();
    testModes();
    testSustainEdges();
    testShape();
    testRetrigger();
    testNotes();
    testManual();
    testPanic();
    testChangesWhileHeld();
    testCcRules();
    testFuzz();
    testCpuAndRate();
    std::printf ("engine tests: %lld checks, %lld failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

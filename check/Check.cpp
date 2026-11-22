// HillsideCheck: runs the plugin layer without a host. The build workflow runs it on every push and fails if any
// check fails.
//
//   1. The control list is exactly what 0.1.0 and 0.2.0 shipped (ids, order, ranges, defaults, menu lists): golden
//      file layout-<version>.txt for every version that shipped one.
//   2. Projects saved by every earlier version (golden saved states, every control away from its default) load
//      unchanged; the Gate button never opens held.
//   3. It is a MIDI effect: no audio buses, MIDI in and out, no latency, no tail, 16 controls.
//   4. The MIDI path: everything passes unchanged and in order (notes keep channel, velocity and sample); the envelope
//      goes out on channel 16 as CC 3 / 35 and CC 9 / 41, the gates and Trig as CC 85-89, notes 40-44 or both;
//      rest sent at the start; the top reached exactly.
//   5. Automation: every control moved through its range, with and without a held note: never more than one value
//      per output per block, no bursts from the gates, and changing a control never starts a note or a gate.
//   6. Transport stop, reset / releaseResources / prepareToPlay, All Notes Off, bypass and a long pause in Logic's
//      calls while a note is held (gates sent as CC and as notes): the envelope goes to rest, 0 and note-offs are
//      sent at once, and the key's later release does nothing.
//   7. Stuck-gate and stuck-note fuzz through the real plugin: random notes, controllers, block sizes (including 0 and
//      1), control changes (Send as, channel and switches too), stops, resets and bypass. After every key is
//      released: at rest, 0 last sent on every channel used, no gate note left on.
//   8. The panel: opens at its design size, draws, resizes between 80% and 200% with locked proportions, every
//      control inside the window, live, and moving only its own parameter, the Gate button plays the envelope, the
//      gate lights follow the outputs. --snapshot <folder> writes pictures of it.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <vector>

static int checks = 0, failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static constexpr int kNumControls = 16;
static const char* const kVersions[] = { "0.1.0", "0.2.0" };   // every version that shipped a golden list and state

static std::vector<juce::RangedAudioParameter*> allParams (HillsideProcessor& proc)
{
    std::vector<juce::RangedAudioParameter*> v;
    for (auto* base : proc.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base)) v.push_back (p);
    return v;
}
static juce::RangedAudioParameter* P (HillsideProcessor& proc, const char* id)
{
    return dynamic_cast<juce::RangedAudioParameter*> (proc.getValueTreeState().getParameter (id));
}
static void setReal (HillsideProcessor& proc, const char* id, float v)
{
    auto* p = P (proc, id);
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// A play head we can start and stop.
struct FakePlayHead final : juce::AudioPlayHead
{
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        return info;
    }
};

struct Out { int pos; juce::MidiMessage m; };

static std::vector<Out> run (HillsideProcessor& proc, juce::MidiBuffer midi, int numSamples, bool bypassed = false)
{
    juce::AudioBuffer<float> audio (0, numSamples);
    if (bypassed) proc.processBlockBypassed (audio, midi);
    else          proc.processBlock (audio, midi);
    std::vector<Out> v;
    for (const auto meta : midi) v.push_back ({ meta.samplePosition, meta.getMessage() });
    return v;
}
static std::vector<Out> runEmpty (HillsideProcessor& proc, int blocks, int n = 512)
{
    std::vector<Out> all;
    for (int i = 0; i < blocks; ++i) for (auto& o : run (proc, {}, n)) all.push_back (o);
    return all;
}

static bool isOurCc (const juce::MidiMessage& m)
{
    if (! m.isController()) return false;
    const int n = m.getControllerNumber();
    return n == 3 || n == 35 || n == 9 || n == 41 || (n >= 85 && n <= 89);
}

// What a mapper on each channel knows: the latest 14-bit value of Envelope (35) and Linear (41).
struct Receiver
{
    int coarse[17][128] {};
    int value[17][128];
    Receiver() { for (auto& c : value) for (auto& v : c) v = -1; }
    void take (const juce::MidiMessage& m)
    {
        if (! m.isController()) return;
        const int ch = m.getChannel(), n = m.getControllerNumber();
        if (n == 3 || n == 9) coarse[ch][n] = m.getControllerValue();
        if (n == 35 || n == 41) value[ch][n] = coarse[ch][n - 32] * 128 + m.getControllerValue();
    }
    void take (const std::vector<Out>& v) { for (auto& o : v) take (o.m); }
};

// The gates and Trig as a receiver sees them, per channel: CC 85-89 values and notes 40-44 on or off. Notes from
// elsewhere (the player's) are not counted: the checks play other note numbers.
struct PulseWatch
{
    int ccVal[17][128] {};
    bool noteOn[17][128] {};
    int doubled = 0, strayOff = 0, ccRepeat = 0, starts = 0;
    void take (const juce::MidiMessage& m)
    {
        const int ch = m.getChannel();
        if (m.isController() && m.getControllerNumber() >= 85 && m.getControllerNumber() <= 89)
        {
            const int n = m.getControllerNumber(), v = m.getControllerValue();
            ccRepeat += ccVal[ch][n] == v ? 1 : 0;
            starts += v == 127 ? 1 : 0;
            ccVal[ch][n] = v;
        }
        if (m.isNoteOnOrOff() && m.getNoteNumber() >= 40 && m.getNoteNumber() <= 44)
        {
            const int n = m.getNoteNumber();
            if (m.isNoteOn()) { doubled += noteOn[ch][n] ? 1 : 0; noteOn[ch][n] = true; ++starts; }
            else              { strayOff += noteOn[ch][n] ? 0 : 1; noteOn[ch][n] = false; }
        }
    }
    void take (const std::vector<Out>& v) { for (auto& o : v) take (o.m); }
    bool anythingOn() const
    {
        for (int ch = 0; ch <= 16; ++ch) for (int n = 0; n < 128; ++n) if (ccVal[ch][n] != 0 || noteOn[ch][n]) return true;
        return false;
    }
};

static void prepare (HillsideProcessor& proc, double sr = 48000.0, int block = 512)
{
    proc.setRateAndBufferSizeDetails (sr, block);
    proc.prepareToPlay (sr, block);
}
static juce::MidiBuffer one (const juce::MidiMessage& m, int pos = 0) { juce::MidiBuffer b; b.addEvent (m, pos); return b; }

// ------------------------------------------------------------------------------------------------ 1. control list

static juce::StringArray dumpLayout (HillsideProcessor& proc)
{
    juce::StringArray lines;
    int i = 0;
    for (auto* p : allParams (proc))
    {
        const auto& r = p->getNormalisableRange();
        juce::String line;
        line << i++ << " " << p->paramID << " v" << p->getVersionHint() << " | " << p->getName (100)
             << " | range " << juce::String (r.start, 3) << ".." << juce::String (r.end, 3) << " step " << juce::String (r.interval, 3)
             << " | default " << juce::String (p->convertFrom0to1 (p->getDefaultValue()), 3)
             << " | centre " << juce::String (p->convertFrom0to1 (0.5f), 3);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p)) line << " | choices " << c->choices.joinIntoString (";");
        if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr) line << " | switch";
        lines.add (line);
    }
    return lines;
}

// The normalised value every control gets in a golden project: away from its default. Each version its own pattern.
static float goldenValue (juce::RangedAudioParameter* p, int i, const juce::String& version)
{
    const int n = juce::jmax (2, p->getNumSteps() < 1000 ? p->getNumSteps() : 10);
    int k = version == "0.1.0" ? (i * 7 + 3) % n : (i * 5 + 2) % n;
    float v = (float) k / (float) (n - 1);
    if (std::abs (v - p->getDefaultValue()) < 1.0e-3f) v = (float) ((k + 1) % n) / (float) (n - 1);
    return p->convertTo0to1 (p->convertFrom0to1 (v));
}

static void testControlList (const juce::File& goldenDir, bool writeGolden)
{
    HillsideProcessor proc;
    const auto now = dumpLayout (proc);
    if (writeGolden)
    {   // writes the golden files of THIS version only, and never overwrites a version that already shipped
        const juce::String version (JucePlugin_VersionString);
        const juce::File golden = goldenDir.getChildFile ("layout-" + version + ".txt");
        const juce::File state  = goldenDir.getChildFile ("state-" + version + ".xml");
        if (golden.existsAsFile() || state.existsAsFile()) { std::printf ("golden files for %s already exist: not touched\n", version.toRawUTF8()); return; }
        golden.replaceWithText (now.joinIntoString ("\n") + "\n");
        auto params = allParams (proc);
        for (int i = 0; i < (int) params.size(); ++i) params[(size_t) i]->setValueNotifyingHost (goldenValue (params[(size_t) i], i, version));
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        state.replaceWithText (xml->toString());
        std::printf ("wrote %s (%d controls) and %s\n", golden.getFullPathName().toRawUTF8(), now.size(), state.getFullPathName().toRawUTF8());
        return;
    }
    for (const char* version : kVersions)
    {
        juce::StringArray old;
        old.addLines (goldenDir.getChildFile (juce::String ("layout-") + version + ".txt").loadFileAsString());
        old.removeEmptyStrings();
        CHECK (old.size() == kNumControls, "reference list %s has %d controls", version, old.size());
        CHECK (now.size() >= old.size(), "control count %d is smaller than %s's %d", now.size(), version, old.size());
        int same = 0;
        for (int i = 0; i < old.size() && i < now.size(); ++i)
        {
            if (old[i] == now[i]) ++same;
            else std::printf ("control %d differs from %s:\n   shipped: %s\n   now:     %s\n", i, version, old[i].toRawUTF8(), now[i].toRawUTF8());
        }
        CHECK (same == old.size(), "%d of %d controls identical to %s", same, old.size(), version);
        std::printf ("control list: %d controls, first %d identical to %s\n", now.size(), same, version);
    }
    for (int i = kNumControls; i < now.size(); ++i)
        CHECK (now[i].contains (" v2 "), "control added after 0.1.0 must be version 2: %s", now[i].toRawUTF8());
}

// ------------------------------------------------------------------------------------------------ 2. old projects

static void testOldProjects (const juce::File& goldenDir)
{
    for (const char* version : kVersions)
    {
        const auto xml = juce::parseXML (goldenDir.getChildFile (juce::String ("state-") + version + ".xml"));
        CHECK (xml != nullptr, "golden state %s readable", version);
        if (xml == nullptr) continue;
        CHECK (xml->getStringAttribute ("pluginVersion") == version, "golden state is from %s", version);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        HillsideProcessor proc;
        proc.setStateInformation (mb.getData(), (int) mb.getSize());
        auto params = allParams (proc);
        int same = 0;
        for (int i = 0; i < kNumControls; ++i)
        {
            auto* p = params[(size_t) i];
            const float want = p->paramID == HsParams::manualGate ? 0.0f : goldenValue (p, i, version);
            if (std::abs (p->getValue() - want) < 1.0e-4f) ++same;
            else std::printf ("  %s: %f, saved %f\n", p->paramID.toRawUTF8(), p->getValue(), want);
        }
        CHECK (same == kNumControls, "project saved by %s: %d of %d controls restored (the Gate button released)", version, same, kNumControls);
        CHECK (P (proc, HsParams::manualGate)->getValue() < 0.5f, "a project never opens with the Gate button held");
        // Saving again and loading gives the same.
        juce::MemoryBlock again;
        proc.getStateInformation (again);
        HillsideProcessor proc2;
        proc2.setStateInformation (again.getData(), (int) again.getSize());
        auto p2 = allParams (proc2);
        int same2 = 0;
        for (int i = 0; i < kNumControls; ++i) same2 += std::abs (p2[(size_t) i]->getValue() - params[(size_t) i]->getValue()) < 1.0e-6f ? 1 : 0;
        CHECK (same2 == kNumControls, "save and reopen: %d of %d", same2, kNumControls);
        // Playing after loading it ends at rest.
        prepare (proc);
        runEmpty (proc, 2);
        run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
        runEmpty (proc, 20);
        run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512);
        runEmpty (proc, 1200);
        CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "%s project: plays and ends at rest", version);
        std::printf ("old projects: %s restored (%d controls)\n", version, same);
    }
    // Garbage leaves everything at its defaults.
    HillsideProcessor proc;
    const char junk[] = "not a saved state";
    proc.setStateInformation (junk, (int) sizeof (junk));
    for (auto* p : allParams (proc)) CHECK (juce::approximatelyEqual (p->getValue(), p->getDefaultValue()), "junk state ignored (%s)", p->paramID.toRawUTF8());
    // A project saved with the button held (by automation) opens with it released.
    HillsideProcessor held;
    setReal (held, HsParams::manualGate, 1.0f);
    juce::MemoryBlock mb;
    held.getStateInformation (mb);
    HillsideProcessor reopened;
    reopened.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (P (reopened, HsParams::manualGate)->getValue() < 0.5f, "Gate button saved held reopens released");
}

// ------------------------------------------------------------------------------------------------ 3. flags

static void testFlags()
{
    HillsideProcessor proc;
    CHECK (proc.isMidiEffect() && proc.acceptsMidi() && proc.producesMidi(), "MIDI effect, MIDI in and out");
    CHECK (proc.getBusCount (true) == 0 && proc.getBusCount (false) == 0, "no audio buses (%d in, %d out)", proc.getBusCount (true), proc.getBusCount (false));
    CHECK (proc.getLatencySamples() == 0 && proc.getTailLengthSeconds() <= 0.0, "no latency, no tail");
    CHECK (proc.getName() == "Hillside", "name");
    CHECK ((int) allParams (proc).size() == kNumControls, "%d controls", (int) allParams (proc).size());
    // Defaults: the time knobs' ranges and centres.
    CHECK (juce::approximatelyEqual (P (proc, HsParams::attack)->convertFrom0to1 (0.0f), 3.0f)
           && std::abs (P (proc, HsParams::attack)->convertFrom0to1 (1.0f) - 10000.0f) < 0.5f, "Attack 3 ms .. 10 s");
    CHECK (std::abs (P (proc, HsParams::decay)->convertFrom0to1 (1.0f) - 12000.0f) < 0.5f
           && std::abs (P (proc, HsParams::release)->convertFrom0to1 (1.0f) - 12000.0f) < 0.5f, "Decay and Release up to 12 s");
    CHECK (P (proc, HsParams::attack)->getCurrentValueAsText() == "5.0 ms" && P (proc, HsParams::decay)->getCurrentValueAsText() == "300 ms",
           "time text: %s, %s", P (proc, HsParams::attack)->getCurrentValueAsText().toRawUTF8(), P (proc, HsParams::decay)->getCurrentValueAsText().toRawUTF8());
    CHECK (std::abs (P (proc, HsParams::release)->getValueForText ("1.2 s") - P (proc, HsParams::release)->convertTo0to1 (1200.0f)) < 1e-4f, "typing 1.2 s");
}

// ------------------------------------------------------------------------------------------------ 4. MIDI path

static void testMidiPath()
{
    HillsideProcessor proc;
    prepare (proc);
    auto first = run (proc, {}, 512);
    Receiver rx;
    rx.take (first);
    CHECK (first.size() == 4 && rx.value[16][35] == 0 && rx.value[16][41] == 0, "first block: rest (0) sent once on both outputs, channel 16 (%d messages)", (int) first.size());
    CHECK (run (proc, {}, 512).empty(), "nothing more while at rest");

    juce::MidiBuffer in;
    in.addEvent (juce::MidiMessage::controllerEvent (3, 1, 64), 10);
    in.addEvent (juce::MidiMessage::pitchWheel (3, 9000), 20);
    in.addEvent (juce::MidiMessage::channelPressureChange (3, 50), 30);
    in.addEvent (juce::MidiMessage::programChange (3, 7), 40);
    in.addEvent (juce::MidiMessage::aftertouchChange (3, 60, 33), 50);
    const juce::uint8 sysex[] = { 0x7d, 0x01, 0x02 };
    in.addEvent (juce::MidiMessage::createSysExMessage (sysex, 3), 60);
    in.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 90), 100);
    in.addEvent (juce::MidiMessage::controllerEvent (3, 64, 127), 100);
    auto out = run (proc, in, 512);
    std::vector<Out> theirs, ours;
    for (auto& o : out) (isOurCc (o.m) && o.m.getChannel() == 16 ? ours : theirs).push_back (o);
    CHECK (theirs.size() == 8, "all 8 incoming messages passed on (%d)", (int) theirs.size());
    if (theirs.size() == 8)
    {
        CHECK (theirs[0].pos == 10 && theirs[0].m.isController() && theirs[0].m.getControllerValue() == 64, "CC 1 unchanged");
        CHECK (theirs[1].pos == 20 && theirs[1].m.getPitchWheelValue() == 9000, "pitch bend unchanged");
        CHECK (theirs[2].pos == 30 && theirs[2].m.getChannelPressureValue() == 50, "channel pressure unchanged");
        CHECK (theirs[3].pos == 40 && theirs[3].m.getProgramChangeNumber() == 7, "program change unchanged");
        CHECK (theirs[4].pos == 50 && theirs[4].m.isAftertouch(), "poly aftertouch unchanged");
        CHECK (theirs[5].pos == 60 && theirs[5].m.isSysEx() && theirs[5].m.getSysExDataSize() == 3, "sysex unchanged");
        CHECK (theirs[6].pos == 100 && theirs[6].m.isNoteOn() && theirs[6].m.getNoteNumber() == 60 && theirs[6].m.getChannel() == 3
               && theirs[6].m.getVelocity() == 90, "note: same note, channel 3, velocity 90, sample 100");
        CHECK (theirs[7].pos == 100 && theirs[7].m.isSustainPedalOn(), "pedal after the note, same sample, in order");
    }
    // The 5 ms attack has reached the top exactly by the end of this block (the corner is sent at its sample: 100 + 240).
    rx.take (out);
    bool topAt = false;
    for (auto& o : ours) if (o.m.getControllerNumber() == 35 && o.m.getControllerValue() == 127 && o.pos == 340) topAt = true;
    CHECK (rx.value[16][35] == 16383 && rx.value[16][41] == 16383 && topAt, "the top reached exactly, at sample 340 (5 ms after the note)");
    for (auto& o : ours) CHECK (o.m.getChannel() == 16, "envelope CC on channel 16");
    // Release: velocity 0 counts as a note-off, and passes on unchanged.
    auto rel = run (proc, one (juce::MidiMessage (0x92, 60, 0), 5), 512);
    bool passed = false;
    for (auto& o : rel) if (o.m.isNoteOff() && o.m.getChannel() == 3 && o.pos == 5) passed = true;
    CHECK (passed, "velocity-0 note-on passed on");
    runEmpty (proc, 40);
    CHECK (proc.getDisplay().stage.load() == 0, "velocity 0 released the gate");
    // 0- and 1-sample blocks.
    run (proc, one (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100)), 0);
    out = run (proc, one (juce::MidiMessage::noteOff (1, 62)), 1);
    CHECK (out.size() >= 1 && out[0].pos == 0, "0- and 1-sample blocks");
    runEmpty (proc, 100);
    CHECK (proc.getDisplay().stage.load() == 0, "and the note in the 0-sample block was released");
    // Another channel set: the envelope goes there.
    setReal (proc, HsParams::outChannel, 7);
    Receiver rx2;
    run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
    auto o2 = runEmpty (proc, 4);
    rx2.take (o2);
    bool only7 = true;
    for (auto& o : o2) if (isOurCc (o.m)) only7 = only7 && o.m.getChannel() == 7;
    CHECK (only7 && rx2.value[7][35] > 0, "MIDI out channel 7: the envelope goes out on channel 7");
    run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512);
    runEmpty (proc, 200);

    // The gates and Trig (0.2.0), default Send as CC: Gate A on with the note, at its sample, on the out channel.
    {
        HillsideProcessor p2;
        prepare (p2);
        runEmpty (p2, 2);
        auto o = run (p2, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 100), 512);
        bool gateA = false, trig = false, notes = false;
        for (auto& x : o)
        {
            if (x.m.isController() && x.m.getControllerNumber() == 85 && x.m.getControllerValue() == 127 && x.pos == 100 && x.m.getChannel() == 16) gateA = true;
            if (x.m.isController() && x.m.getControllerNumber() == 89 && x.m.getControllerValue() == 127 && x.pos == 100) trig = true;
            if (x.m.isNoteOnOrOff() && x.m.getNoteNumber() != 60) notes = true;
        }
        CHECK (gateA && trig && ! notes, "Send as CC (default): Gate A and Trig CC 127 at the note's sample, channel 16, no notes");
        run (p2, one (juce::MidiMessage::noteOff (1, 60)), 512);
        runEmpty (p2, 100);
    }
    for (int mode : { 1, 2 })
    {
        HillsideProcessor p2;
        prepare (p2);
        runEmpty (p2, 2);
        setReal (p2, HsParams::sendAs, (float) mode);
        PulseWatch w;
        int ccs = 0;
        auto o = run (p2, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), 100), 512);
        w.take (o);
        bool noteAt = false, playedPassed = false;
        for (auto& x : o)
        {
            if (x.m.isNoteOn() && x.m.getNoteNumber() == 40 && x.m.getVelocity() == 100 && x.pos == 100 && x.m.getChannel() == 16) noteAt = true;
            if (x.m.isNoteOn() && x.m.getNoteNumber() == 60 && x.m.getVelocity() == 90 && x.m.getChannel() == 1 && x.pos == 100) playedPassed = true;
        }
        for (int i = 0; i < 40; ++i) for (auto& x : run (p2, {}, 512)) { w.take (x.m); ccs += x.m.isController() && x.m.getControllerNumber() >= 85 && x.m.getControllerNumber() <= 89 ? 1 : 0; }
        w.take (run (p2, one (juce::MidiMessage::noteOff (1, 60)), 512));
        for (int i = 0; i < 100; ++i) w.take (run (p2, {}, 512));
        CHECK (noteAt && playedPassed, "Send as %s: gate note E1 (40) at the note's sample, velocity 100, channel 16; the played note passes unchanged", mode == 1 ? "Note" : "Both");
        CHECK ((mode == 1) == (ccs == 0 && w.ccVal[16][85] == 0), "Send as %s: %s", mode == 1 ? "Note" : "Both", mode == 1 ? "no gate CC" : "gate CC too");
        CHECK (! w.anythingOn() && w.doubled == 0 && w.strayOff == 0 && w.starts >= 7, "Send as %s: every gate note released, none doubled (%d starts)", mode == 1 ? "Note" : "Both", w.starts);
    }
}

// ------------------------------------------------------------------------------------------------ 5. automation

static void testAutomation()
{
    HillsideProcessor proc;
    prepare (proc);
    runEmpty (proc, 2);
    auto params = allParams (proc);
    int bad = 0, notes = 0, nonZero = 0;
    // No notes held: moving any control sends at most the rest value 0 (after a channel change or switching an
    // output back on), never anything else.
    for (auto* p : params)
    {
        if (p->paramID == HsParams::manualGate) continue;
        for (int i = 0; i <= 20; ++i)
        {
            p->setValueNotifyingHost ((float) i / 20.0f);
            for (auto& o : run (proc, {}, 256))
            {
                notes += o.m.isNoteOnOrOff() ? 1 : 0;
                if (isOurCc (o.m) && o.m.getControllerValue() != 0) ++nonZero;
            }
        }
        p->setValueNotifyingHost (p->getDefaultValue());
        runEmpty (proc, 2);
    }
    CHECK (notes == 0 && nonZero == 0, "moving controls with no note held: only rest values (%d notes, %d others)", notes, nonZero);

    // A note held: automate everything hard. Never more than one value per output per block (two pairs if the
    // channel changes in that block: 0 on the old one); the gates (which follow the stages the times now give) never
    // more than one on and one off per output per block, never a doubled note.
    PulseWatch watch;
    run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
    std::mt19937 rng (42);
    int burst = 0;
    for (int k = 0; k < 3000; ++k)
    {
        auto* p = params[rng() % params.size()];
        if (p->paramID == HsParams::manualGate) continue;
        p->setValueNotifyingHost ((float) (rng() % 1001) / 1000.0f);
        const auto out = run (proc, {}, 64 + (int) (rng() % 512));
        watch.take (out);
        std::map<int, int> perChannelFine, perPulse;
        for (auto& o : out)
        {
            if (o.m.isController() && (o.m.getControllerNumber() == 35 || o.m.getControllerNumber() == 41))
                ++perChannelFine[o.m.getChannel() * 128 + o.m.getControllerNumber()];
            if (o.m.isController() && o.m.getControllerNumber() >= 85 && o.m.getControllerNumber() <= 89) ++perPulse[o.m.getControllerNumber() - 85];
            if (o.m.isNoteOnOrOff() && o.m.getNoteNumber() >= 40 && o.m.getNoteNumber() <= 44) ++perPulse[o.m.getNoteNumber() - 40];
            notes += o.m.isNoteOnOrOff() && (o.m.getNoteNumber() < 40 || o.m.getNoteNumber() > 44) ? 1 : 0;
        }
        for (auto& [key, count] : perChannelFine) bad += count > 1 ? 1 : 0;
        for (auto& [key, count] : perPulse) burst += count > 4 ? 1 : 0;
    }
    CHECK (notes == 0, "automation while held sends no other notes (%d)", notes);
    CHECK (bad == 0, "automation while held: never more than one value per output per block (%d blocks over)", bad);
    CHECK (burst == 0 && watch.doubled == 0 && watch.strayOff == 0, "automation while held: no gate bursts (%d), no doubled or stray gate notes (%d, %d)", burst, watch.doubled, watch.strayOff);
    for (auto* p : params) p->setValueNotifyingHost (p->getDefaultValue());
    watch.take (run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512));
    for (int i = 0; i < 300; ++i) watch.take (run (proc, {}, 512));
    CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "after automation and release: at rest");
    CHECK (! watch.anythingOn(), "after automation and release: no gate left on, on any channel");

    // Sustaining, gates sent as both: Send as, the channel, the gate switches and the envelope switches changed over
    // and over. These only ever end a gate; none of them starts one.
    {
        HillsideProcessor p2;
        prepare (p2);
        setReal (p2, HsParams::sendAs, 2.0f);
        runEmpty (p2, 2);
        PulseWatch w;
        w.take (run (p2, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512));
        for (int i = 0; i < 60; ++i) w.take (run (p2, {}, 512));
        const int startsBefore = w.starts;
        CHECK (w.noteOn[16][42] && w.ccVal[16][87] == 127, "sustaining: Gate S on as a note and as CC");
        std::mt19937 r2 (7);
        const char* changeable[] = { HsParams::sendAs, HsParams::outChannel, HsParams::gateOnIds[0], HsParams::gateOnIds[1],
                                     HsParams::gateOnIds[2], HsParams::gateOnIds[3], HsParams::trigOn, HsParams::outEnvOn, HsParams::outLinOn };
        for (int k = 0; k < 600; ++k)
        {
            P (p2, changeable[r2() % 9])->setValueNotifyingHost ((float) (r2() % 1001) / 1000.0f);
            w.take (run (p2, {}, 32 + (int) (r2() % 600)));
        }
        CHECK (w.starts == startsBefore && w.doubled == 0 && w.strayOff == 0, "changing Send as, channel and switches while held never starts a gate or a note (%d new)", w.starts - startsBefore);
        for (auto* p : allParams (p2)) p->setValueNotifyingHost (p->getDefaultValue());
        w.take (run (p2, one (juce::MidiMessage::noteOff (1, 60)), 512));
        for (int i = 0; i < 200; ++i) w.take (run (p2, {}, 512));
        CHECK (! w.anythingOn(), "released: nothing left on");
    }
}

// ------------------------------------------------------------------------------------------------ 6. stops

static void testStops()
{
    struct Case { const char* name; std::function<void (HillsideProcessor&, FakePlayHead&, double&)> stop; bool bypass; };
    const std::vector<Case> cases {
        { "transport stop", [] (HillsideProcessor&, FakePlayHead& ph, double&) { ph.playing = false; }, false },
        { "reset", [] (HillsideProcessor& p, FakePlayHead&, double&) { p.reset(); }, false },
        { "releaseResources", [] (HillsideProcessor& p, FakePlayHead&, double&) { p.releaseResources(); }, false },
        { "prepareToPlay", [] (HillsideProcessor& p, FakePlayHead&, double&) { p.prepareToPlay (44100.0, 256); }, false },
        { "All Notes Off", [] (HillsideProcessor&, FakePlayHead&, double&) {}, false },
        { "bypass", [] (HillsideProcessor&, FakePlayHead&, double&) {}, true },
        { "a 2 s pause in Logic's calls", [] (HillsideProcessor&, FakePlayHead&, double& t) { t += 2000.0; }, false },
    };
    for (int mode : { 0, 2 })   // gates sent as CC, then as CC and notes
    for (const auto& c : cases)
    {
        HillsideProcessor proc;
        FakePlayHead ph;
        ph.playing = true;
        proc.setPlayHead (&ph);
        double t = 0.0;
        proc.clock = [&t] { return t; };
        prepare (proc);
        setReal (proc, HsParams::sendAs, (float) mode);
        Receiver rx;
        PulseWatch w;
        auto step = [&] (juce::MidiBuffer m, bool bypassed = false) { t += 10.0; auto o = run (proc, m, 512, bypassed); rx.take (o); w.take (o); return o; };
        step ({});
        step (one (juce::MidiMessage::noteOn (2, 64, (juce::uint8) 100)));
        for (int i = 0; i < 40; ++i) step ({});
        CHECK (proc.getDisplay().stage.load() == (int) hillside::Stage::sustain && rx.value[16][35] > 0, "%s: sustaining before", c.name);
        CHECK (w.ccVal[16][87] == 127 && w.noteOn[16][42] == (mode == 2), "%s: Gate S on before (%s)", c.name, mode == 2 ? "CC and note" : "CC");
        c.stop (proc, ph, t);
        if (juce::String (c.name) == "All Notes Off")
        {
            step (one (juce::MidiMessage::allNotesOff (2)));
            CHECK (proc.getDisplay().stage.load() == (int) hillside::Stage::release, "All Notes Off: the envelope releases");
            for (int i = 0; i < 100; ++i) step ({});
        }
        else
        {
            const auto o = step ({}, c.bypass);
            int zeros = 0;
            for (auto& m : o) if (m.m.isController() && (m.m.getControllerNumber() == 35 || m.m.getControllerNumber() == 41) && m.m.getControllerValue() == 0 && m.pos == 0) ++zeros;
            CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "%s: at rest at once", c.name);
            CHECK (zeros == 2 && rx.value[16][35] == 0 && rx.value[16][41] == 0, "%s: 0 sent on both outputs at the first sample (%d)", c.name, zeros);
            int gateEnds = 0;
            for (auto& m : o)
                if (m.pos == 0 && ((m.m.isController() && m.m.getControllerNumber() == 87 && m.m.getControllerValue() == 0) || (m.m.isNoteOff() && m.m.getNoteNumber() == 42)))
                    ++gateEnds;
            CHECK (gateEnds == (mode == 2 ? 2 : 1) && ! w.anythingOn(), "%s: Gate S ended at the first sample (%s)", c.name, mode == 2 ? "CC 0 and note-off" : "CC 0");
            if (c.bypass)
            {
                int ours = 0;
                for (int i = 0; i < 20; ++i) for (auto& m : step (one (juce::MidiMessage::noteOn (1, 70, (juce::uint8) 100)), true)) ours += isOurCc (m.m) ? 1 : 0;
                CHECK (ours == 0, "bypassed: notes pass, nothing is sent (%d)", ours);
                step (one (juce::MidiMessage::noteOff (1, 70)), true);
            }
        }
        // The key's later release passes on and changes nothing; nothing more is sent.
        ph.playing = false;
        const auto o = step (one (juce::MidiMessage::noteOff (2, 64)));
        int ours = 0; bool offPassed = false;
        for (auto& m : o) { ours += isOurCc (m.m) ? 1 : 0; offPassed = offPassed || (m.m.isNoteOff() && m.m.getNoteNumber() == 64); }
        for (int i = 0; i < 20; ++i) for (auto& m : step ({})) ours += isOurCc (m.m) ? 1 : 0;
        CHECK (offPassed && ours == 0 && proc.getDisplay().stage.load() == 0, "%s: the key's release passes on, nothing else happens (%d)", c.name, ours);
        // And the next note plays normally.
        step (one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)));
        CHECK (proc.getDisplay().stage.load() != 0, "%s: the next note plays", c.name);
        step (one (juce::MidiMessage::noteOff (1, 60)));
        for (int i = 0; i < 100; ++i) step ({});
        CHECK (proc.getDisplay().stage.load() == 0 && rx.value[16][35] == 0, "%s: and ends at rest", c.name);
        CHECK (! w.anythingOn() && w.doubled == 0 && w.strayOff == 0, "%s: no gate or gate note left on (%s)", c.name, mode == 2 ? "Both" : "CC");
        proc.setPlayHead (nullptr);
    }
}

// ------------------------------------------------------------------------------------------------ 7. fuzz

static void fuzz (unsigned seed, bool withBypass)
{
    HillsideProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    double t = 0.0;
    proc.clock = [&t] { return t; };
    prepare (proc);
    std::mt19937 rng (seed);
    auto params = allParams (proc);
    Receiver rx;
    PulseWatch w;
    bool down[17][128] {};
    long long events = 0;
    for (int block = 0; block < 18000; ++block)
    {
        t += rng() % 400 == 0 ? 1500.0 : 10.0;
        const int n = rng() % 40 == 0 ? (int) (rng() % 2) : 1 + (int) (rng() % 1024);
        juce::MidiBuffer in;
        const int count = (int) (rng() % 6);
        for (int k = 0; k < count; ++k)
        {
            const int at = n > 0 ? (int) (rng() % (unsigned) n) : 0, ch = 1 + (int) (rng() % 3), note = 58 + (int) (rng() % 6);
            const unsigned kind = rng() % 24;
            if (kind == 0)      { in.addEvent (juce::MidiMessage::allNotesOff (ch), at); for (auto& d : down[ch]) d = false; }
            else if (kind < 2)  in.addEvent (juce::MidiMessage::controllerEvent (ch, 1, (int) (rng() % 128)), at);
            else if (kind < 12) { in.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) (1 + rng() % 127)), at); down[ch][note] = true; }
            else if (kind < 14) { in.addEvent (juce::MidiMessage (0x90 + ch - 1, note, 0), at); down[ch][note] = false; }
            else                { in.addEvent (juce::MidiMessage::noteOff (ch, note), at); down[ch][note] = false; }
            ++events;
        }
        if (rng() % 30 == 0)
        {
            auto* p = params[rng() % params.size()];
            p->setValueNotifyingHost ((float) (rng() % 1001) / 1000.0f);
        }
        if (rng() % 200 == 0) ph.playing = ! ph.playing;
        if (rng() % 500 == 0) proc.reset();
        const bool bypassed = withBypass && (block / 300) % 4 == 3;
        const auto o = run (proc, in, n, bypassed);
        rx.take (o);
        w.take (o);
    }
    // Release everything (and the Gate button), the outputs on, then wait.
    juce::MidiBuffer offs;
    for (int ch = 1; ch <= 16; ++ch) for (int nt = 0; nt < 128; ++nt) if (down[ch][nt]) offs.addEvent (juce::MidiMessage::noteOff (ch, nt), 0);
    setReal (proc, HsParams::manualGate, 0.0f);
    setReal (proc, HsParams::outEnvOn, 1.0f);
    setReal (proc, HsParams::outLinOn, 1.0f);
    for (const char* id : { HsParams::attack, HsParams::decay, HsParams::release }) setReal (proc, id, 50.0f);
    t += 10.0;
    { const auto o = run (proc, offs, 512); rx.take (o); w.take (o); }
    for (int i = 0; i < 400; ++i) { t += 10.0; const auto o = run (proc, {}, 512); rx.take (o); w.take (o); }
    bool allZero = true;
    for (int ch = 1; ch <= 16; ++ch) allZero = allZero && rx.value[ch][35] <= 0 && rx.value[ch][41] <= 0;
    CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "fuzz %u: at rest after every key is released", seed);
    CHECK (allZero, "fuzz %u: every channel's receiver at 0", seed);
    CHECK (! w.anythingOn() && w.doubled == 0 && w.strayOff == 0, "fuzz %u: no gate CC or gate note left on, none doubled (%d starts)", seed, w.starts);
    std::printf ("stuck-gate and stuck-note fuzz%s: %lld random events in 18,000 blocks, %d gate starts, nothing stuck\n", withBypass ? " with bypass" : "", events, w.starts);
    proc.setPlayHead (nullptr);
}

// ------------------------------------------------------------------------------------------------ 8. panel

static void testPanel()
{
    HillsideProcessor proc;
    prepare (proc);
    runEmpty (proc, 2);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorAndMakeActive());
    auto* editor = dynamic_cast<HillsideEditor*> (ed.get());
    CHECK (editor != nullptr, "panel opens");
    if (editor == nullptr) return;
    CHECK (ed->getWidth() == HillsideEditor::kWidth && ed->getHeight() == HillsideEditor::kHeight, "design size %dx%d", ed->getWidth(), ed->getHeight());
    const auto image = ed->createComponentSnapshot (ed->getLocalBounds());
    CHECK (image.isValid() && image.getWidth() == HillsideEditor::kWidth, "panel draws");

    // Every control inside the panel, moves only its own parameter; milestone 2's are disabled.
    auto params = allParams (proc);
    for (auto& c : editor->panel.controls())
    {
        CHECK (juce::Rectangle<int> (0, 0, HillsidePanel::kWidth, HillsidePanel::kHeight).contains (c.control->getBounds()), "%s inside the panel", c.paramId.toRawUTF8());
        CHECK (c.live && c.control->isEnabled() && c.control->getAlpha() > 0.99f, "%s live (not dimmed)", c.paramId.toRawUTF8());
        std::vector<float> before;
        for (auto* p : params) before.push_back (p->getValue());
        if (auto* s = dynamic_cast<juce::Slider*> (c.control))            s->setValue (s->getMinimum() + (s->getMaximum() - s->getMinimum()) * 0.71, juce::sendNotificationSync);
        else if (auto* b = dynamic_cast<juce::ToggleButton*> (c.control)) b->setToggleState (! b->getToggleState(), juce::sendNotificationSync);
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (c.control))     cb->setSelectedId (5, juce::sendNotificationSync);
        else if (auto* sg = dynamic_cast<Segmented*> (c.control))          sg->choose (1 - sg->selected());
        else if (auto* gb = dynamic_cast<GateButton*> (c.control))         gb->press (true);
        int changed = 0; bool own = false;
        for (size_t i = 0; i < params.size(); ++i)
            if (std::abs (params[i]->getValue() - before[i]) > 1e-6f) { ++changed; own = own || params[i]->paramID == c.paramId; }
        CHECK (changed == 1 && own, "%s moves only its own parameter (%d changed)", c.paramId.toRawUTF8(), changed);
        if (auto* gb = dynamic_cast<GateButton*> (c.control)) gb->press (false);
        for (size_t i = 0; i < params.size(); ++i) params[i]->setValueNotifyingHost (params[i]->getDefaultValue());
    }

    // The Gate button plays the envelope and lets it go.
    editor->panel.getGateButton().press (true);
    runEmpty (proc, 2);
    CHECK (proc.getDisplay().gate.load() && proc.getDisplay().stage.load() != 0, "Gate button pressed: the envelope runs");
    editor->panel.getGateButton().press (false);
    runEmpty (proc, 1);
    CHECK (proc.getDisplay().stage.load() == (int) hillside::Stage::release, "Gate button let go: release");
    // The gate lights: Gate S lit while sustaining, Trig flashes at a stage start, dark when switched off.
    run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
    runEmpty (proc, 60);
    auto& lights = editor->panel.getPulseLights();
    for (int i = 0; i < 5; ++i) lights.refresh();
    CHECK (lights.lit (2) && ! lights.lit (0) && ! lights.lit (1) && ! lights.lit (3), "sustaining: only the Gate S light is lit");
    run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512);
    lights.refresh();
    CHECK (lights.lit (3) && lights.lit (4), "release: Gate R lit, Trig flashes");
    setReal (proc, HsParams::gateOnIds[3], 0.0f);
    runEmpty (proc, 1);
    for (int i = 0; i < 5; ++i) lights.refresh();
    CHECK (! lights.lit (3), "Gate R switched off: its light goes dark");
    setReal (proc, HsParams::gateOnIds[3], 1.0f);
    runEmpty (proc, 200);
    editor->panel.getModeSwitch().choose (1);
    CHECK (juce::roundToInt (P (proc, HsParams::gateMode)->convertFrom0to1 (P (proc, HsParams::gateMode)->getValue())) == 1, "Gate | Trig switch sets Trig");
    editor->panel.getModeSwitch().choose (0);

    // Resizing: 80% to 200%, proportions locked, size saved with the project.
    ed->setSize (HillsideEditor::kWidth * 3 / 2, HillsideEditor::kHeight * 3 / 2);
    CHECK (ed->getWidth() == HillsideEditor::kWidth * 3 / 2, "panel resizes to 150%%");
    auto* cons = ed->getConstrainer();
    CHECK (cons != nullptr && cons->getMinimumWidth() == 624 && cons->getMaximumWidth() == 1560 && std::abs (cons->getFixedAspectRatio() - 780.0 / 470.0) < 1e-9,
           "size limits 624..1560 wide, proportions locked");
    const int savedWidth = (int) proc.getValueTreeState().state.getProperty (HillsideEditor::widthProperty, -1);
    CHECK (savedWidth == HillsideEditor::kWidth * 3 / 2, "window size saved with the project (%d)", savedWidth);
    proc.editorBeingDeleted (ed.get());
    ed.reset();
    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    HillsideProcessor proc2;
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> ed2 (proc2.createEditorAndMakeActive());
    CHECK (ed2->getWidth() == HillsideEditor::kWidth * 3 / 2, "reopened at the saved size (%d)", ed2->getWidth());
    proc2.editorBeingDeleted (ed2.get());
}

static void writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto shoot = [&] (const char* name, int width, std::function<void (HillsideProcessor&)> setup) {
        HillsideProcessor proc;
        prepare (proc);
        runEmpty (proc, 2);
        setup (proc);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorAndMakeActive());
        ed->setSize (width, HillsideEditor::heightForWidth (width));
        const auto image = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (dir.getChildFile (name));
        os.setPosition (0); os.truncate();
        png.writeImageToStream (image, os);
        proc.editorBeingDeleted (ed.get());
        std::printf ("wrote %s\n", dir.getChildFile (name).getFullPathName().toRawUTF8());
    };
    auto hold = [] (HillsideProcessor& p, int blocks) {
        run (p, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
        runEmpty (p, blocks);
    };
    shoot ("window-1-default.png", 780, [] (HillsideProcessor&) {});
    shoot ("window-2-decay.png", 780, [&] (HillsideProcessor& p) { setReal (p, HsParams::decay, 2000.0f); hold (p, 60); });
    shoot ("window-3-sustain-held.png", 780, [&] (HillsideProcessor& p) { hold (p, 60); });
    shoot ("window-4-release.png", 780, [&] (HillsideProcessor& p) {
        setReal (p, HsParams::release, 3000.0f);
        hold (p, 60);
        run (p, one (juce::MidiMessage::noteOff (1, 60)), 512);
        runEmpty (p, 80);
    });
    shoot ("window-5-attack-trig-linear.png", 780, [&] (HillsideProcessor& p) {
        setReal (p, HsParams::attack, 4000.0f);
        setReal (p, HsParams::shape, 0.0f);
        setReal (p, HsParams::gateMode, 1.0f);
        setReal (p, HsParams::outLinOn, 0.0f);
        hold (p, 150);
    });
    shoot ("window-6-small-80.png", 624, [&] (HillsideProcessor& p) { hold (p, 10); });
    shoot ("window-8-gates-note-sustain.png", 780, [&] (HillsideProcessor& p) { setReal (p, HsParams::sendAs, 1.0f); hold (p, 60); });
    shoot ("window-9-gates-both-attack.png", 780, [&] (HillsideProcessor& p) { setReal (p, HsParams::sendAs, 2.0f); setReal (p, HsParams::attack, 3000.0f); setReal (p, HsParams::trigOn, 0.0f); hold (p, 20); });
    shoot ("window-7-large-200.png", 1560, [&] (HillsideProcessor& p) { hold (p, 10); });
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    bool writeGolden = false;
    juce::File goldenDir (HILLSIDE_GOLDEN_DIR);
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--write-golden") writeGolden = true;
        if (juce::String (argv[i]) == "--snapshot" && i + 1 < argc) { writeSnapshots (juce::File (argv[i + 1])); return 0; }
    }
    testControlList (goldenDir, writeGolden);
    if (writeGolden) return 0;
    testOldProjects (goldenDir);
    testFlags();
    testMidiPath();
    testAutomation();
    testStops();
    fuzz (777, false);
    fuzz (4242, true);
    testPanel();
    std::printf ("plugin checks: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

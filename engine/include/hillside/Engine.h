// The whole Hillside engine: notes in, the envelope out as 14-bit MIDI CC, sample-accurate. Plain C++, no plugin code,
// so it is tested without Logic (engine/tests) and can be reused (cnoc).
//
// Per block: the plugin passes the block's note events (with their sample offsets), the Gate button, and the
// settings; the engine works out every sample, and sends the chosen CC messages to a MidiSink at their exact samples.
// It never touches the incoming MIDI: passing everything through is the plugin's job.

#pragma once

#include "hillside/CcStream.h"
#include "hillside/Envelope.h"
#include "hillside/Keys.h"
#include "hillside/Outputs.h"

#include <vector>

namespace hillside
{
    struct InputEvent
    {
        enum Kind : std::uint8_t { noteOn, noteOff, allNotesOff };
        int offset;            // sample within the block
        Kind kind;
        std::uint8_t channel;  // 0..15
        std::uint8_t note;     // 0..127 (ignored for allNotesOff)
    };

    struct Settings
    {
        EnvSettings env;
        int  outChannel = kDefaultChannel;          // 1..16
        bool outOn[kNumSmoothOutputs] = { true, true };   // Envelope, Linear
    };

    // One sample, for the tests and the panel.
    struct TracePoint { float linear, shaped; Stage stage; bool gate; bool corner; };

    class Engine
    {
    public:
        Engine();

        void prepare (double sampleRate);         // also goes to rest and sends everything afresh
        void setSettings (const Settings& s);     // applied at the start of the next block
        const Settings& settings() const { return pending; }

        // Events must be in sample order. manualGate = the panel's Gate button (applied at the block's first sample).
        void render (int numSamples, const InputEvent* events, int numEvents, bool manualGate, MidiSink& sink);

        // Straight to rest at the start of the next rendered block (or of flushOnly): level 0, every key released, the
        // Gate button forgotten until it is let go, and 0 sent on every output that is not already at 0.
        // Used for transport stop, reset, bypass.
        void panic();

        // For the bypassed path: no notes are read and nothing moves; a pending rest is still sent.
        void flushOnly (int numSamples, MidiSink& sink);

        // What the panel shows.
        const Envelope& envelope() const { return env; }
        bool gate() const { return gateNow; }
        int heldKeys() const { return keys.held(); }
        const CcStream& ccStream() const { return cc; }
        long long samplesDone() const { return abs; }

        // Tests only: when set, every sample is appended here.
        std::vector<TracePoint>* trace = nullptr;

    private:
        void applySettings (MidiSink& sink);
        void setGate (bool high);

        Settings pending, active;
        bool settingsChanged = true;
        Envelope env;
        Keys keys;
        CcStream cc;
        long long abs = 0;
        bool manualHeld = false, manualBlocked = false;   // after a panic the button must be let go first
        bool gateNow = false;
        bool panicPending = false;
        bool blipPending = false;                          // a zero-length note: the gate falls one sample later
    };
}

#include "hillside/Engine.h"

namespace hillside
{
    Engine::Engine()
    {
        cc.setLane (outEnvelope, kEnvelopeCoarseCc, kEnvelopeFineCc);
        cc.setLane (outLinear,   kLinearCoarseCc,   kLinearFineCc);
        prepare (48000.0);
    }

    void Engine::prepare (double sampleRate)
    {
        env.prepare (sampleRate);
        env.setSettings (active.env);
        cc.prepare (sampleRate);
        keys.clear();
        abs = 0;
        gateNow = false;
        manualHeld = manualBlocked = false;
        panicPending = false;
        settingsChanged = true;
    }

    void Engine::setSettings (const Settings& s)
    {
        pending = s;
        if (pending.outChannel < 1) pending.outChannel = 1;
        if (pending.outChannel > 16) pending.outChannel = 16;
        settingsChanged = true;
    }

    void Engine::panic() { panicPending = true; }

    void Engine::applySettings (MidiSink& sink)
    {
        settingsChanged = false;
        if (pending.outChannel != active.outChannel)
            cc.moveChannel (active.outChannel, 0, sink);
        for (int i = 0; i < kNumSmoothOutputs; ++i)
            if (pending.outOn[i] != cc.isEnabled (i))
                cc.setEnabled (i, pending.outOn[i], abs, 0);
        active = pending;
        env.setSettings (active.env);
    }

    void Engine::flushOnly (int numSamples, MidiSink& sink)
    {
        if (settingsChanged) applySettings (sink);
        if (panicPending)
        {
            panicPending = false;
            env.reset();
            keys.clear();
            gateNow = false;
            manualBlocked = manualHeld;
            for (int i = 0; i < kNumSmoothOutputs; ++i) cc.force (i, abs, 0, 0);
        }
        cc.flush (active.outChannel, sink);
        if (numSamples > 0) abs += numSamples;
    }

    void Engine::render (int numSamples, const InputEvent* events, int numEvents, bool manualGate, MidiSink& sink)
    {
        if (settingsChanged) applySettings (sink);
        manualHeld = manualGate;
        if (panicPending)
        {
            panicPending = false;
            env.reset();
            keys.clear();
            gateNow = false;
            manualBlocked = manualGate;   // a button held through a stop must be let go and pressed again
            for (int i = 0; i < kNumSmoothOutputs; ++i) cc.force (i, abs, 0, 0);
        }
        if (! manualGate) manualBlocked = false;
        const bool manual = manualGate && ! manualBlocked;

        if (numSamples <= 0)
        {   // a block of no samples: its key events still count (a release must never be lost)
            if (numEvents > 0)
            {
                keys.beginSample (abs);
                for (int k = 0; k < numEvents; ++k)
                {
                    const auto& ev = events[k];
                    if (ev.kind == InputEvent::noteOn)       keys.press (ev.channel, ev.note);
                    else if (ev.kind == InputEvent::noteOff) keys.release (ev.channel, ev.note);
                    else                                     keys.releaseChannel (ev.channel);
                }
                const bool want = keys.held() > 0 || manual;
                if (want && ! gateNow)       env.gateRise();
                else if (! want && gateNow)  env.gateFall();
                gateNow = want;
            }
            cc.flush (active.outChannel, sink);
            return;
        }

        int e = 0;
        const int last = numSamples - 1;
        for (int i = 0; i < numSamples; ++i)
        {
            const bool keyEvents = e < numEvents && (events[e].offset <= i || i == last);
            if (keyEvents)
            {
                keys.beginSample (abs);
                while (e < numEvents && (events[e].offset <= i || i == last))
                {
                    const auto& ev = events[e++];
                    if (ev.kind == InputEvent::noteOn)       keys.press (ev.channel, ev.note);
                    else if (ev.kind == InputEvent::noteOff) keys.release (ev.channel, ev.note);
                    else                                     keys.releaseChannel (ev.channel);
                }
            }

            bool want = keys.held() > 0 || manual;
            const bool retrigger = keyEvents && ! manual && gateNow && want && keys.backToBack();
            if (keyEvents && ! want && ! gateNow && keys.blip())
                want = true;   // a zero-length note: high for this one sample, falls at the next

            if (retrigger)               { env.gateFall(); env.gateRise(); }
            else if (want && ! gateNow)  env.gateRise();
            else if (! want && gateNow)  env.gateFall();
            gateNow = want;

            const EnvSample s = env.next();
            cc.consider (outEnvelope, abs, i, toFourteenBit (s.shaped), s.corner);
            cc.consider (outLinear,   abs, i, toFourteenBit (s.linear), s.corner);
            if (trace != nullptr) trace->push_back ({ (float) s.linear, (float) s.shaped, s.stage, gateNow, s.corner });
            ++abs;
        }
        cc.flush (active.outChannel, sink);
    }
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

HillsideProcessor::HillsideProcessor()
    : AudioProcessor (BusesProperties()),   // a MIDI effect: no audio buses
      apvts (*this, nullptr, "Hillside", HsParams::createLayout())
{
    manualParam = apvts.getRawParameterValue (HsParams::manualGate);
    events.reserve (4096);
    out.ensureSize (65536);
    engine.prepare (48000.0);
}

bool HillsideProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet().isDisabled() && layouts.getMainInputChannelSet().isDisabled();
}

// Logic may call these when it stops, resets or rebuilds the plugin. A MIDI effect cannot send MIDI from here, so the
// next block starts by sending the envelope to rest (0 on every output that is not already at 0).
void HillsideProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);      // at rest, everything sent afresh
    out.ensureSize (65536);
    pendingRest = true;
}
void HillsideProcessor::releaseResources() { pendingRest = true; }
void HillsideProcessor::reset()            { pendingRest = true; }

void HillsideProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)          { process (m, b.getNumSamples()); }
void HillsideProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer& m)         { process (m, b.getNumSamples()); }
void HillsideProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)  { processBypassed (m, b.getNumSamples()); }
void HillsideProcessor::processBlockBypassed (juce::AudioBuffer<double>& b, juce::MidiBuffer& m) { processBypassed (m, b.getNumSamples()); }

// The whole MIDI path. Every incoming event passes on unchanged at its own sample; the notes are also read for the gate
// (velocity 0 counts as a release; All Notes Off / All Sound Off release that channel's keys). The engine adds its CC.
void HillsideProcessor::process (juce::MidiBuffer& midi, int numSamples)
{
    const double now = clock();
    if (lastCall >= 0.0 && now - lastCall > kGapMs) pendingRest = true;
    lastCall = now;
    wasBypassed = false;

    if (pendingRest.exchange (false)) engine.panic();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (wasPlaying && ! playing) engine.panic();   // transport stop: rest at the first stopped block
            wasPlaying = playing;
        }

    engine.setSettings (HsParams::read (apvts));

    out.clear();
    events.clear();
    const int last = juce::jmax (0, numSamples - 1);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = juce::jlimit (0, last, meta.samplePosition);
        out.addEvent (m, at);
        if (events.size() >= 4000) continue;
        const auto ch = (std::uint8_t) juce::jlimit (0, 15, m.getChannel() - 1);
        if (m.isNoteOn())
            events.push_back ({ at, hillside::InputEvent::noteOn, ch, (std::uint8_t) m.getNoteNumber() });
        else if (m.isNoteOff())   // includes velocity 0
            events.push_back ({ at, hillside::InputEvent::noteOff, ch, (std::uint8_t) m.getNoteNumber() });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            events.push_back ({ at, hillside::InputEvent::allNotesOff, ch, 0 });
    }

    sink.out = &out;
    sink.last = last;
    engine.render (numSamples, events.data(), (int) events.size(), manualParam->load() >= 0.5f, sink);
    midi.swapWith (out);
    publish();
}

// Bypassed: incoming MIDI passes untouched and the keys are not read. The envelope goes to rest once (0 sent).
void HillsideProcessor::processBypassed (juce::MidiBuffer& midi, int numSamples)
{
    lastCall = clock();
    if (! wasBypassed) engine.panic();
    wasBypassed = true;
    pendingRest = false;
    engine.setSettings (HsParams::read (apvts));
    sink.out = &midi;
    sink.last = juce::jmax (0, numSamples - 1);
    engine.flushOnly (numSamples, sink);
    publish();
}

void HillsideProcessor::publish()
{
    const auto& env = engine.envelope();
    display.linear.store ((float) env.linear());
    display.shaped.store ((float) env.shaped());
    display.progress.store ((float) env.progress());
    display.stage.store ((int) env.stage());
    display.gate.store (engine.gate());
    for (int i = 0; i < hillside::kNumSmoothOutputs; ++i) display.sent[i].store (engine.ccStream().lastSent (i));
    display.blocks.fetch_add (1);
}

void HillsideProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("pluginVersion", JucePlugin_VersionString);
        copyXmlToBinary (*xml, destData);
    }
}

void HillsideProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // The Gate button is a momentary button: a project never opens with it held.
            if (auto* p = apvts.getParameter (HsParams::manualGate)) p->setValueNotifyingHost (0.0f);
        }
}

juce::AudioProcessorEditor* HillsideProcessor::createEditor() { return new HillsideEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HillsideProcessor(); }

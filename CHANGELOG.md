# Changelog

## 0.1.0 (milestone 1: built, waiting for the Logic test on both Macs)

- First build: repo layout, build workflow (engine self-test, mapper test, plugin-layer check with window pictures,
  universal AU + VST3, ad-hoc signing, auval as a MIDI effect, pluginval, package with the shared installer,
  INSTALL.txt and the mapper, private draft release).
- AU MIDI effect (type `aumi`), no audio buses, zero latency. Codes `Hlsd` / `Pdot`.
- The engine (plain C++): one ADSR envelope with exact stage timing at any sample rate and block size; Shape from
  straight to log attack / exponential decay and release; Gate and Trig modes; Sustain 0% = AD, 100% = ASR; the gate
  from notes (any key held; no restart on overlapping notes; back-to-back notes restart; a new note in the release
  starts from where the envelope is); the Gate button; rest on stop, reset, bypass and pauses.
- Two outputs as 14-bit CC on channel 16 (changeable): Envelope (follows Shape) CC 3 + 35, Linear CC 9 + 41, each
  with its own on/off switch. One value per block at most, 6 ms and 0.2% / 100 ms rules, exact corners.
- The window as approved in the draft: envelope display with the moving point, A D S R lights, gate light, level bars,
  four faders, Shape knob, Gate button, Gate | Trig; milestone 2's outputs dimmed and tagged. Resizable 80% to 200%.
- Full control list fixed (16 controls, all version 1), including milestone 2's four gate switches, Trig and Send as.
  Golden control list and a 0.1.0 saved state in `check/golden/`.
- The Hillside Mapper Scripter script (8 lanes: source, Learn target, min, max), with the Gate and Trig sources
  already in place for 0.2.0; works alongside the Ogdoad and Strange Drift mappers.

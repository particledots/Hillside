# Changelog

## 0.2.0 (milestone 2 finished: tested in Logic on the Intel Mac, 2026-11-22)

- Logic test result (Bryn, Intel Mac, macOS 12.7.8, Logic 10.7.8): every step worked as described, no issues: old
  project loads with the gate block live, gate and Trig lights, Send as Note plays the gate notes with the stages,
  Stop / Send as / channel / Gate S switch / bypass while Gate S is on leave no note hanging, the mapper's Gate S lane
  moves a filter cutoff, removing Hillside mid-note leaves no note hanging. CI (run 45045194505): engine test 720
  checks, mapper test 111 checks, plugin-layer check 275 checks, auval succeeded (aumi), pluginval level 5 passed
  (AU and VST3).

- The four stage gates and the combined trigger are live: Gate A, D, S, R (high for the whole stage, at least 20 ms)
  and Trig (20 ms at the start of every stage that runs; closer triggers merge), sent as CC 85-89, notes 40-44
  (E1 to G#1, velocity 100) or both, on the MIDI out channel, chosen by Send as (default CC). Engine: new `Pulses`.
- Their switches and Send as are live on the panel (no longer dimmed, "milestone 2" tag gone); each output has a light;
  the numbers beside them follow Send as.
- Stuck-note safety: Send as, channel and switch changes, stop, reset, bypass, pauses and a new sample rate end every
  gate and gate note at once; a control change never starts one.
- Everything the engine sends in a block now goes out in sample order.
- Control list unchanged (16 controls). Golden control list and saved state for 0.2.0 added; 0.1.0 projects load.
- The mapper is unchanged; its test now feeds the Gate and Trig sources the engine's real gate stream.

## 0.1.0 (milestone 1 finished: tested in Logic on the M4 Mac mini and the Intel Mac, 2026-11-22)

- Logic test result (Bryn, both Macs: M4 Mac mini, macOS Tahoe, Logic 11.2.2; Intel Mac, macOS 12.7.8, Logic 10.7.8):
  every step worked as described, no issues: install and load as a MIDI effect, window as drawn, A D S R with the
  moving point and lights, Trig completes the attack on a tapped key, Gate releases at once, the mapper moves a filter
  cutoff, the Gate button plays without notes, Stop while held leaves nothing stuck, save and reopen, proportional
  resizing. Intel Mac CPU meter about 10% while playing with the mapper. CI (run 45041504730): engine test 493 checks,
  mapper test 76 checks, plugin-layer check 158 checks, auval succeeded (aumi), pluginval level 5 passed (AU and VST3).

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

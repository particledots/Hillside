# Hillside

An original AU MIDI effect for Logic Pro: notes in, an ADSR envelope out as smooth 14-bit MIDI CC, which a Scripter
mapper moves onto any knob on the same track. Inspired by version 1 of the Instruō cèis ADSR envelope generator. It
is not a copy, contains no Instruō code or artwork, and is not affiliated with Instruō. The cèis is an analog module
with no published firmware; its version 1 manual was used only to understand what the controls do. The envelope maths
is our own.

Status: **version 0.1.0, milestone 1 of 2: built, waiting for Bryn's test in Logic on both Macs.**

## What it does (milestone 1)

Put Hillside in a **MIDI FX slot** above any instrument and play. Your notes sound as normal; while a key is held the
envelope runs, the panel shows it moving, and it goes out as MIDI CC. Defaults: Attack 5 ms, Decay 300 ms, Sustain 50%,
Release 400 ms, Shape 100%, Gate mode, MIDI out channel 16, both outputs on.

- **Attack** (3 ms to 10 s), **Decay** (3 ms to 12 s), **Release** (3 ms to 12 s): the faders move the time on a
  logarithmic scale (centre: Attack 173 ms, Decay and Release 190 ms). **Sustain** (0 to 100%) is a level.
- **Shape**: anticlockwise straight lines; clockwise a log-shaped attack (fast rise, slowing near the top) with
  exponential decay and release (quick drop, long tail), as on classic East Coast envelopes. Morphs smoothly in between.
  Shape never changes a stage's length.
- **Gate | Trig**:
  - *Gate*: the stages run while a key is held; letting go starts the release at once, even during the attack or
    decay.
  - *Trig*: the attack always completes, even for a very short note; if the key is already up by then, the release
    follows straight away. A key let go during the decay or sustain starts the release at once, as in Gate mode.
- **The Gate button** (the round button): plays the envelope while you hold it, with or without notes. It glows with
  the envelope, like the module's button. It can be automated; a project never opens with it held.
- **Sustain at the ends**: at 0% the envelope is AD (attack, decay to 0, no sustain and no release afterwards); at 100%
  it is ASR (the decay is skipped and the top is held).
- **Two outputs**: *Envelope* follows the Shape knob (the module's one CV output); *Linear* is always straight,
  whatever Shape is set to. Each has its own on/off switch: switched off it sends 0 once, then nothing.
- **The panel** shows, live: the curve for the current settings (Linear dashed), a moving point on the envelope, the
  gate bar, the four stage lights A D S R, the gate-in light and a level bar for each output.
- **Resizing**: drag the bottom-right corner; 80% to 200% (624 x 376 to 1560 x 940), proportions locked, size saved
  with the project.
- Milestone 2's controls (Gate A, D, S, R, Trig, Send as) are on the panel, dimmed and tagged "milestone 2".

## Notes and the gate

- The gate is high while **any** key is held (any channel), or while the Gate button is held.
- **Overlapping notes**: a new key while another is held does not restart the envelope (on the module a held gate has
  no new edge; restarting is the cèis [2]'s retrigger, which is left out). Letting go of an earlier key while a later
  one is held does nothing; the release starts when the last key is up.
- **Back-to-back notes**: a note that starts at the exact sample the previous one ends restarts the envelope there,
  whichever message comes first, also for the same key played again.
- **A new note during the release** starts the attack from where the envelope is (no drop to zero, no click), at the
  same speed: from part way up it takes proportionally less time.
- A key pressed twice without a release counts once; its first release lets it go (a missing note-off can never hold
  the gate). Velocity 0 is a release. All Notes Off / All Sound Off release that channel's keys.
- A zero-length note (on and off at the same sample) is a one-sample gate: in Trig mode a full attack, in Gate mode
  next to nothing (as a trigger on the module "jumps to the release").
- **Rest**: transport stop, Logic resetting the plugin, bypass, and a pause of more than a second in Logic's calls send
  the envelope straight to 0 (0 is sent on every output), release every key and forget the Gate button until it is let
  go. A key's later release then does nothing.
- **Changes while a note is held**: a new time changes the running stage's speed from that moment, with no jump;
  Shape and Sustain move the level at once; switching an output off sends its 0; changing the MIDI out channel sends 0
  on the old channel, then carries on on the new one. Changing or automating a control never sends a note.
- Zero latency: every incoming message (notes, controllers, pitch bend, aftertouch, program changes, sysex) passes on
  unchanged, at its own sample, in order.

## How the envelope reaches a knob in Logic

Logic has no CV. Each output goes out as a standard 14-bit CC pair (16,384 steps instead of 128) on channel 16:

| Output | Envelope | Linear | Gate A | Gate D | Gate S | Gate R | Trig |
|---|---|---|---|---|---|---|---|
| coarse CC | 3 | 9 | 85 | 86 | 87 | 88 | 89 |
| fine CC | 35 | 41 | | | | | |
| note (Note / Both) | | | 40 (E1) | 41 (F1) | 42 (F♯1) | 43 (G1) | 44 (G♯1) |
| from | 0.1.0 | 0.1.0 | 0.2.0 | 0.2.0 | 0.2.0 | 0.2.0 | 0.2.0 |

CC 3, 9 and 85-90 have no standard job in MIDI and are not used by Ogdoad (channel 16: CC 12-31, 44-63, 102-105,
notes 36-39) or Strange Drift (channel 15: CC 106-119); 35 and 41 are the standard fine partners of 3 and 9
(coarse + 32). Notes 40-44 follow Ogdoad's 36-39. So all three plugins can share a track.

The **Hillside Mapper** (a Scripter script, in the download as `Hillside Mapper (paste into Scripter).txt`, source in
`tools/mapper/Hillside-Mapper.js`) sits in a MIDI FX slot below Hillside. It has 8 lanes; each lane has a Source
(Off, Envelope, Linear, Gate A, Gate D, Gate S, Gate R, Trig), a Target (Logic's *Learn plug-in parameter*), and
Min % / Max %. It moves the learnt knob on any plugin below it on the same channel strip and stops Hillside's CC from
reaching the instrument; everything else passes through, Ogdoad's and Strange Drift's CC included. The Gate and Trig
sources are already in the menu (fixed order), and work once Hillside 0.2.0 sends them, so the mapper will not need
pasting again. `INSTALL.txt` in the download has the steps.

**With Ogdoad and Strange Drift on the same track:** MIDI FX slots in the order Hillside, Ogdoad, Strange Drift, Ogdoad
Mapper, Strange Drift Mapper, Hillside Mapper. Hillside goes first so Ogdoad's trigger notes cannot open its gate.

**Smoothness and CPU.** An output is sent once it has moved at least 0.2% (32 of 16,384 steps) and 6 ms have passed,
or after 100 ms for smaller moves, and at most once per audio block; nothing while it stands still (sustain, rest).
Corners are exact: when a stage reaches its target (the top of the attack, the sustain level, the final 0) that exact
value is sent at that exact sample, so a 3 ms attack still reaches the very top. (If two corners fall in one audio
block, the later one is sent: one value per block is all Logic applies anyway.) At Logic's usual 256-sample buffer,
stages of about **50 ms or longer** move a knob smoothly; 20-50 ms move in a few steps; under about 20 ms the knob
jumps (but lands on the right values). With a 1024-sample buffer the smooth limit is about 200 ms. Playing a 1 s note every
1.5 s sends about 270 MIDI messages a second for both outputs together (measured in the engine test); switch off an
output you do not use to halve that. The engine uses well under 0.1% of one core.

## The manual, and how it was read

1. **"Two envelope reset modes"**: the feature list names them, but the manual never describes a separate control.
   The only two-way switch on the version 1 panel is Gate / Trig, and its section is about exactly how the envelope
   cuts short or completes. Hillside reads them as that switch.
2. **One CV output**: the "LIN ADSR OUT" and "LOG/EXPO ADSR OUT" drawings show the one output at the two ends of the
   Shape knob. Hillside sends that output (Envelope) plus an always-straight one (Linear).
3. **Trig mode, key let go during the decay**: the manual only says a trigger completes the attack and then releases.
   Hillside: the attack is the only stage that always completes; the decay and sustain end when the key is let go.
4. **Sustain at the ends**: 0% = AD (no sustain stage; no release afterwards, the envelope is already at 0);
   100% = ASR (no decay).
5. **Stage times**: Decay always takes its time to reach the sustain level, and Release its time to reach 0, from
   wherever they start. An attack from part way up is not described; Hillside keeps its speed.
6. **The curve and the trigger length** are not published. The Shape curve is our own (see `engine/include/hillside/Curve.h`);
   the trigger (milestone 2) is 20 ms, as Ogdoad's.
7. The times "~3 ms - ~10 s / ~12 s" are approximate on the module; Hillside uses exactly 3 ms to 10 s (Attack) and
   3 ms to 12 s (Decay, Release).

## Left out

- The four CV inputs: use Logic's automation instead. (The mappers cannot move Hillside's own knobs: they only move
  plugins below them.)
- Times below 3 ms (on the module only reachable with negative CV).
- The 3 V gate threshold: notes are the gate.
- Everything that exists only on the cèis [2] (retrigger, hold, logic extraction, slew limiting).
- Pitch Snap and the 28-scale quantizer (they do not apply to an envelope).

## Later (not planned; only built if asked)

- Velocity scaling the envelope's height.
- Restart on every new note (legato off).
- Sustain pedal holding the gate.
- A "notes from channel" filter for the gate.
- An inverted output.

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The engine. Plain C++, no plugin code: the envelope (`Envelope`, `Curve`), the gate from notes (`Keys`), the CC stream (`CcStream`), the MIDI numbers (`Outputs.h`) and the per-sample engine (`Engine`). Reused by the next plugin (cnōc). |
| `plugin/` | The plugin wrapper: what Logic sees (an AU MIDI effect), the list of controls (`Parameters`), and the panel (`PluginEditor`, `Style`). |
| `check/` | `HillsideCheck`: a console program that tests the plugin layer without a host; `golden/` holds the control list and a saved state of each version. |
| `tools/mapper/` | The Scripter mapper script. |
| `tools/install/` | `Install-Unofficial-Plugin.command`, the same installer as the other particledots plugins (Audio Unit only). |
| `.github/workflows/` | The automatic build. |

## Checks run on every push

- **Engine self-test** (Linux): the Shape curve; every stage starting and ending within one sample of the ideal moment
  at 44.1, 48 and 96 kHz, with blocks of 32 to 1024 samples and random sizes, the envelope identical sample for sample
  at every block size and the same curve at every sample rate; Gate and Trig with notes ending in every stage and
  zero-length notes; Sustain at 0% and 100%; Shape bending the right way; a new note during the release; overlapping,
  back-to-back (every order), repeated and stray notes, All Notes Off, events in 0-sample blocks; the Gate button alone,
  with notes and through a stop; rest (stop, reset, bypass); changes while held (times, mode, sustain, channel,
  switches, and random changes of everything); the CC rules over hours of random playing (one value per block, 6 ms,
  coarse before fine, every value equal to the envelope at that sample, corners exact, nothing at rest); a stuck-gate
  fuzz of about 250,000 random events; CPU and MIDI rate.
- **Mapper test**: the real mapper script in a stand-in for Scripter, fed by the engine's real MIDI stream: each lane
  follows its output value for value, Min / Max, upside-down lanes, 14-bit detail, Gate and Trig sources, Off lanes,
  the channel setting, notes and everyone else's CC passing, and the three mappers (Hillside, Ogdoad, Strange Drift)
  chained in all six orders.
- **Plugin-layer check** (`HillsideCheck`): golden control list and saved states of every version; MIDI-effect flags,
  no audio buses, no latency; the MIDI path (everything passes unchanged and in order, channel 16, rest at the start,
  the top exactly at its sample, velocity 0, 0- and 1-sample blocks, another out channel); automation of every control
  with and without a held note (never more than one value per output per block, never a note); transport stop, reset,
  releaseResources, prepareToPlay, All Notes Off, bypass and a pause in Logic's calls while held; two stuck-gate fuzzes
  through the real plugin (one with bypass); the panel (design size, every control moves only its own parameter, the
  Gate button plays, milestone 2 dimmed, resizing limits, saved size). The run uploads pictures of the window
  (artifact **Hillside-window-pictures**).
- **macOS build**: universal binary (Apple Silicon + Intel), ad-hoc signing, Apple's `auval -v aumi Hlsd Pdot`,
  pluginval at strictness 5 (AU and VST3), and the package (installer identical to the shared one, INSTALL.txt with
  the mapper steps, the mapper).

## Getting the plugin

1. Open the **Actions** tab of this repo and click the latest **Build Hillside** run.
2. At the bottom, under **Artifacts**, download **Hillside-macOS** and unzip it (twice: the download is a zip
   containing `Hillside-macOS.zip`).
3. Inside the `Hillside-macOS` folder are `Hillside.component`, `Hillside.vst3`, `INSTALL.txt`, `auval.log`, the
   pluginval logs, `Hillside Mapper (paste into Scripter).txt` and `Install-Unofficial-Plugin.command`.
4. In Terminal, once, clear the download block: `xattr -dr com.apple.quarantine ~/Downloads/Hillside-macOS/*`
   (adjust the path if you unzipped elsewhere).
5. **Easy install (Logic, Audio Unit):** double-click `Install-Unofficial-Plugin.command`. It asks for your Mac
   password once, installs the AU for all users and restarts the AU registrar.
6. In Logic it is a MIDI effect: MIDI FX slot of a Software Instrument track, **Audio Units > particledots > Hillside**.

## Fixed names

Plugin code `Hlsd`, manufacturer `Pdot`, bundle id `com.particledots.hillside`, AU type MIDI effect (`aumi`), target
`Hillside`, engine library `hillside_engine`. Parameter ids, in order: `attack`, `decay`, `sustain`, `release`, `shape`,
`gateMode`, `manualGate`, `outChannel`, `outEnvOn`, `outLinOn`, `gateAOn`, `gateDOn`, `gateSOn`, `gateROn`, `trigOn`,
`sendAs`. Saved with the project besides the parameters: `editorWidth`. The CC and note numbers above and the mapper's
Source menu order are fixed too. These never change, so saved projects keep finding the plugin and its settings.

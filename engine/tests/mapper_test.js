// Runs the real Hillside Mapper script with a stand-in for Logic's Scripter, fed by the engine's real MIDI stream
// (from hillside_ccdump). Also chains it with copies of the Ogdoad Mapper and the Strange Drift Mapper
// (tests/fixtures), in every order, to show the three plugins can share a track.
// Usage: HILLSIDE_CCDUMP=<path to hillside_ccdump> node mapper_test.js <Hillside-Mapper.js> <Ogdoad-Mapper.js> <StrangeDrift-Mapper.js>

"use strict";
const fs = require("fs");
const vm = require("vm");
const { execFileSync } = require("child_process");

let checks = 0, failures = 0;
function expect(ok, what) { checks++; if (!ok) { failures++; console.log("FAIL: " + what); } }

const scriptText = fs.readFileSync(process.argv[2], "utf8");
const ogdoadText = fs.readFileSync(process.argv[3], "utf8");
const driftText = fs.readFileSync(process.argv[4], "utf8");
const dumpExe = process.env.HILLSIDE_CCDUMP;

function readDump(seconds, channel, sendAs) {
    const lines = execFileSync(dumpExe, [String(seconds), String(channel), String(sendAs || 0)], { maxBuffer: 1 << 28 }).toString().trim().split("\n");
    const d = { events: [], notes: [], finals: [], pulsesOn: -1 };
    for (const line of lines) {
        const p = line.split(" ");
        if (p[0] === "final") d.finals[+p[1]] = +p[2];
        else if (p[0] === "pulses-on") d.pulsesOn = +p[1];
        else if (p[0] === "note") d.notes.push({ sample: +p[1], channel: +p[2], pitch: +p[3], velocity: +p[4] });
        else d.events.push({ sample: +p[0], channel: +p[1], cc: +p[2], value: +p[3] });
    }
    return d;
}
const dump = readDump(60, 16);
const events = dump.events, finals = dump.finals;
expect(events.length > 2000, "the engine produced a CC stream (" + events.length + " messages)");
const OUR_CC = [3, 35, 9, 41, 85, 86, 87, 88, 89];
expect(events.every(e => e.channel === 16 && OUR_CC.includes(e.cc)), "the stream uses only CC 3, 35, 9, 41 and 85-89 on channel 16");
expect(dump.notes.length === 0, "Send as CC (the default): no notes are sent");
expect(finals[0] === 0 && finals[1] === 0, "both envelope outputs end at 0");
expect(dump.pulsesOn === 0, "no gate or trigger left on at the end");

// The stage gates and Trig in the real stream (0.2.0): 127 / 0 only, strictly alternating, ending at 0; every pulse
// at least 20 ms (960 samples at 48 kHz).
const GATE_CC = [85, 86, 87, 88, 89];
const pulsesOf = n => events.filter(e => e.cc === n);
for (const n of GATE_CC) {
    const p = pulsesOf(n);
    const name = n === 89 ? "Trig" : "Gate " + "ADSR"[n - 85];
    expect(p.length >= 20 && p.every((e, i) => e.value === (i % 2 === 0 ? 127 : 0)), name + ": CC " + n + " alternates 127 / 0 (" + p.length + " messages)");
    expect(p.length % 2 === 0, name + ": ends at 0");
    let shortest = Infinity;
    for (let i = 1; i < p.length; i += 2) shortest = Math.min(shortest, p[i].sample - p[i - 1].sample);
    expect(shortest >= 960, name + ": every pulse at least 20 ms (shortest " + shortest + " samples)");
}

// What the engine meant at each fine message (coarse * 128 + fine), per output.
function decode(evs) {
    const coarse = {}, values = { 35: [], 41: [] };
    for (const e of evs) {
        if (e.cc === 3 || e.cc === 9) coarse[e.cc] = e.value;
        if (e.cc === 35 || e.cc === 41) values[e.cc].push(((coarse[e.cc - 32] || 0) * 128 + e.value) / 16383);
    }
    return values;
}
const truth = decode(events);
expect(truth[35].length > 500 && truth[41].length > 500, "both outputs move (" + truth[35].length + ", " + truth[41].length + " values)");
expect(Math.max(...truth[35]) === 1 && Math.max(...truth[41]) === 1, "the stream reaches the very top");

// ---- A stand-in for Scripter ---------------------------------------------------------------------------------
function makeScripter(text, settings, callParameterChanged, passTo) {
    const out = { passed: [], targets: [] };
    class Event { send() { out.passed.push(this); if (passTo) passTo(this); } trace() {} }
    class ControlChange extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, number: 0, value: 0 }, o); } }
    class NoteOn extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, pitch: 60, velocity: 100 }, o); } }
    class NoteOff extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, pitch: 60, velocity: 0 }, o); } }
    class PitchBend extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, value: 0 }, o); } }
    class TargetEvent { constructor() { this.target = ""; this.value = 0; } send() { out.targets.push({ target: this.target, value: this.value }); } }
    const values = {};
    const sandbox = {
        Event, ControlChange, NoteOn, NoteOff, PitchBend, TargetEvent, Math,
        GetParameter: (name) => { if (!(name in values)) throw new Error("GetParameter unknown name " + name); return values[name]; },
        Trace: () => {},
    };
    vm.createContext(sandbox);
    vm.runInContext(text, sandbox);
    const params = vm.runInContext("PluginParameters", sandbox);
    params.forEach(p => { if (p.type !== "target") values[p.name] = p.defaultValue !== undefined ? p.defaultValue : 0; });
    for (const [name, v] of Object.entries(settings)) {
        if (!(name in values)) throw new Error("test uses unknown setting " + name);
        values[name] = v;
        if (callParameterChanged) sandbox.ParameterChanged(params.findIndex(p => p.name === name), v);
    }
    return { sandbox, params, out, values };
}
const near = (a, b) => Math.abs(a - b) < 1e-9;
const lane = (s, n) => s.out.targets.filter(t => t.target === "Lane " + n + " target").map(t => t.value);

// ---- 1. The script itself -----------------------------------------------------------------------------------------
{
    const { params } = makeScripter(scriptText, {}, false);
    const names = params.map(p => p.name);
    expect(params.length === 33, "33 settings (channel + 8 lanes x 4)");
    expect(new Set(names).size === names.length, "setting names are unique");
    expect(params.filter(p => p.type === "target").length === 8, "8 Learn targets");
    expect(params.every(p => ["lin", "menu", "target"].includes(p.type)), "only setting types Scripter knows");
    expect(params[0].name === "Hillside MIDI channel" && params[0].defaultValue === 16, "channel setting, 16 by default");
    const src = params.find(p => p.name === "Lane 1 source").valueStrings;
    expect(src.join() === "Off,Envelope,Linear,Gate A,Gate D,Gate S,Gate R,Trig", "Source menu in its fixed order");
    expect(src.every(x => /^[ -~]+$/.test(x)) && /^[\x09\x0a\x0d -~]*$/.test(scriptText), "plain characters only (safe to paste)");
    expect(!/\.trace\(\)/.test(scriptText), "no trace printing in the mapper");
    const ogNames = makeScripter(ogdoadText, {}, false).params.map(p => p.name);
    const sdNames = makeScripter(driftText, {}, false).params.map(p => p.name);
    expect(ogNames[0] !== names[0] && sdNames[0] !== names[0], "its channel setting has its own name");
}

// ---- 2. Nothing set up: Hillside's CC stops, everything else passes -----------------------------------------------
{
    const s = makeScripter(scriptText, {}, false);
    const { ControlChange, NoteOn, NoteOff, PitchBend } = s.sandbox;
    for (const e of events) s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: e.cc, value: e.value }));
    expect(s.out.passed.length === 0 && s.out.targets.length === 0, "the whole stream stops at the mapper, no knob moves");
    for (const n of [85, 86, 87, 88, 89]) s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: n, value: 127 }));
    expect(s.out.passed.length === 0, "gate and trig CC (from 0.2.0) stop too");
    s.sandbox.HandleMIDI(new NoteOn({ pitch: 64 }));
    s.sandbox.HandleMIDI(new NoteOff({ pitch: 64 }));
    s.sandbox.HandleMIDI(new NoteOn({ channel: 16, pitch: 40 }));           // a gate note (from 0.2.0) plays the instrument
    s.sandbox.HandleMIDI(new PitchBend({ value: 1000 }));
    s.sandbox.HandleMIDI(new ControlChange({ channel: 1, number: 1, value: 90 }));
    s.sandbox.HandleMIDI(new ControlChange({ channel: 1, number: 3, value: 90 }));    // CC 3 on another channel
    s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: 20, value: 5 }));   // Ogdoad LFO 1
    s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: 102, value: 127 })); // Ogdoad trig
    s.sandbox.HandleMIDI(new ControlChange({ channel: 15, number: 106, value: 5 }));  // Strange Drift
    s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: 7, value: 100 }));  // volume on 16
    expect(s.out.passed.length === 10, "notes, pitch bend, other CC, Ogdoad's and Strange Drift's CC pass through (" + s.out.passed.length + ")");
    expect(s.out.passed[0].pitch === 64 && s.out.passed[0] instanceof NoteOn, "notes pass unchanged");
}

// ---- 3. Lanes set up, real stream ----------------------------------------------------------------------------------
for (const viaParameterChanged of [true, false]) {
    const how = viaParameterChanged ? " (settings changed in the panel)" : " (settings restored with a project)";
    const settings = {
        "Lane 1 source": 1,
        "Lane 2 source": 2,
        "Lane 3 source": 1, "Lane 3 min %": 20, "Lane 3 max %": 80,
        "Lane 4 source": 2, "Lane 4 min %": 100, "Lane 4 max %": 0,
        "Lane 5 source": 0,
    };
    const s = makeScripter(scriptText, settings, viaParameterChanged);
    const { ControlChange, NoteOn } = s.sandbox;
    let notesIn = 0;
    const t0 = process.hrtime.bigint();
    events.forEach((e, k) => {
        s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: e.cc, value: e.value }));
        if (k % 300 === 0) { s.sandbox.HandleMIDI(new NoteOn({ pitch: 40 + (k % 30) })); notesIn++; }
    });
    const usPerEvent = Number(process.hrtime.bigint() - t0) / 1000 / events.length;
    const l1 = lane(s, 1), l2 = lane(s, 2), l3 = lane(s, 3), l4 = lane(s, 4), l5 = lane(s, 5);
    // Each lane moves exactly through the engine's values (repeats of the same value are skipped by the mapper).
    const dedupe = a => a.filter((v, i) => i === 0 || v !== a[i - 1]);
    const eq = (a, b) => a.length === b.length && a.every((v, i) => near(v, b[i]));
    expect(eq(l1, dedupe(truth[35])), "lane 1 follows Envelope value for value" + how);
    expect(eq(l2, dedupe(truth[41])), "lane 2 follows Linear value for value" + how);
    expect(eq(l3, dedupe(truth[35].map(v => 0.2 + 0.6 * v))), "lane 3: Envelope scaled to 20..80%" + how);
    expect(eq(l4, dedupe(truth[41].map(v => 1 - v))), "lane 4: Linear upside down" + how);
    expect(l5.length === 0, "a lane set to Off never moves" + how);
    expect(near(l1[l1.length - 1], 0) && near(l3[l3.length - 1], 0.2) && near(l4[l4.length - 1], 1), "lanes end at rest (Min)" + how);
    expect(Math.max(...l1) === 1 && Math.min(...l1) === 0, "Envelope sweeps the whole range, top and bottom exactly" + how);
    const distinct = new Set(l1.map(v => Math.round(v * 16383))).size;
    expect(distinct > 300, "14-bit detail reaches the target (" + distinct + " distinct values)" + how);
    const notCoarse = l1.filter(v => Math.round(v * 16383) % 128 !== 0).length > l1.length / 2;
    expect(notCoarse, "values arrive in 14-bit steps, not 128 coarse steps" + how);
    expect(s.out.passed.length === notesIn, "notes still pass with lanes in use" + how);
    console.log("mapper: " + usPerEvent.toFixed(2) + " microseconds per CC message in Node" + how);
}

// ---- 4. Gate and Trig sources, fed the engine's real gate stream (0.2.0) -----------------------------------------------
{
    const settings = {
        "Lane 1 source": 3, "Lane 1 min %": 10, "Lane 1 max %": 90,
        "Lane 2 source": 4, "Lane 3 source": 5, "Lane 4 source": 6,
        "Lane 5 source": 7, "Lane 5 min %": 100, "Lane 5 max %": 0,
        "Lane 6 source": 1,
    };
    const dedupe = a => a.filter((v, i) => i === 0 || v !== a[i - 1]);
    const eq2 = (a, b) => a.length === b.length && a.every((v, i) => near(v, b[i]));
    // What each gate lane should do: Max while the stream's CC is 127, Min when 0, in the stream's order.
    const want = (n, min, max) => dedupe(pulsesOf(n).map(e => e.value === 127 ? max : min));
    for (const viaParameterChanged of [true, false]) {
        const how = viaParameterChanged ? " (settings changed in the panel)" : " (settings restored with a project)";
        const s = makeScripter(scriptText, settings, viaParameterChanged);
        const { ControlChange } = s.sandbox;
        for (const e of events) s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: e.cc, value: e.value }));
        expect(eq2(lane(s, 1), want(85, 0.1, 0.9)) && lane(s, 1).length >= 20, "Gate A lane follows the real Gate A stream, 10..90%" + how);
        expect(eq2(lane(s, 2), want(86, 0, 1)), "Gate D lane follows the real Gate D stream" + how);
        expect(eq2(lane(s, 3), want(87, 0, 1)), "Gate S lane follows the real Gate S stream" + how);
        expect(eq2(lane(s, 4), want(88, 0, 1)), "Gate R lane follows the real Gate R stream" + how);
        expect(eq2(lane(s, 5), want(89, 1, 0)), "Trig lane follows the real Trig stream, upside down" + how);
        expect(eq2(lane(s, 6), dedupe(truth[35])), "an Envelope lane alongside them is unaffected" + how);
        const ends = [1, 2, 3, 4, 5].map(n => { const l = lane(s, n); return l[l.length - 1]; });
        expect(near(ends[0], 0.1) && near(ends[1], 0) && near(ends[4], 1), "gate lanes end at rest (Min)" + how);
        expect(s.out.passed.length === 0, "gate and trig CC stop at the mapper" + how);
    }
    // The gates line up with the envelope: nothing moves on the Envelope output while Gate S is high.
    const envAt = []; { let c = 0; for (const e of events) { if (e.cc === 3) c = e.value; if (e.cc === 35) envAt.push({ sample: e.sample, v: (c * 128 + e.value) / 16383 }); } }
    const sOn = pulsesOf(87);
    let moving = 0;
    for (let i = 0; i + 1 < sOn.length; i += 2) {
        // ignore the first 20 ms (a short sustain is stretched into the release)
        if (sOn[i + 1].sample - sOn[i].sample <= 960) continue;   // a short sustain is stretched into the release
        moving += envAt.filter(x => x.sample > sOn[i].sample && x.sample < sOn[i + 1].sample).length;
    }
    expect(moving === 0, "Envelope stands still while Gate S is high (" + moving + " moves)");
}

// ---- 4b. Send as Note and Both: the gate notes reach the instrument; the CC (Both) still moves the lanes --------------
{
    const nd = readDump(20, 16, 1);
    expect(nd.events.every(e => [3, 35, 9, 41].includes(e.cc)), "Note: no gate CC is sent");
    expect(nd.notes.length > 50 && nd.notes.every(n => n.channel === 16 && n.pitch >= 40 && n.pitch <= 44), "Note: gate notes 40-44 on channel 16 (" + nd.notes.length + ")");
    const open = {};
    let balanced = true;
    for (const n of nd.notes) {
        if (n.velocity > 0) { balanced = balanced && !open[n.pitch] && n.velocity === 100; open[n.pitch] = true; }
        else { balanced = balanced && !!open[n.pitch]; open[n.pitch] = false; }
    }
    expect(balanced && Object.values(open).every(v => !v) && nd.pulsesOn === 0, "Note: every gate note is released once, nothing left on");
    const s = makeScripter(scriptText, { "Lane 1 source": 3, "Lane 2 source": 7 }, true);
    const { NoteOn, NoteOff, ControlChange } = s.sandbox;
    for (const n of nd.notes) s.sandbox.HandleMIDI(n.velocity > 0 ? new NoteOn({ channel: 16, pitch: n.pitch, velocity: n.velocity }) : new NoteOff({ channel: 16, pitch: n.pitch }));
    expect(s.out.passed.length === nd.notes.length && lane(s, 1).length === 0 && lane(s, 2).length === 0,
           "Note: the gate notes pass the mapper to the instrument unchanged; gate lanes stay put");

    const bd = readDump(20, 16, 2);
    const bCc = bd.events.filter(e => e.cc === 85), bNotes = bd.notes.filter(n => n.pitch === 40);
    expect(bCc.length > 10 && bCc.length === bNotes.length && bCc.every((e, i) => e.sample === bNotes[i].sample && (e.value === 127) === (bNotes[i].velocity > 0)),
           "Both: Gate A as CC and as a note, at the same samples");
    const b = makeScripter(scriptText, { "Lane 1 source": 3 }, true);
    for (const e of bd.events) b.sandbox.HandleMIDI(new b.sandbox.ControlChange({ channel: 16, number: e.cc, value: e.value }));
    for (const n of bd.notes) b.sandbox.HandleMIDI(new b.sandbox.NoteOn({ channel: 16, pitch: n.pitch, velocity: n.velocity }));
    const l1 = lane(b, 1);
    expect(l1.length === bCc.length && near(l1[l1.length - 1], 0) && b.out.passed.length === bd.notes.length, "Both: the lane follows the CC, the notes pass");
}

// ---- 5. A different channel ------------------------------------------------------------------------------------------
{
    const d = readDump(10, 7);
    expect(d.events.every(e => e.channel === 7) && d.events.length > 100, "out channel 7: the engine sends on channel 7");
    const s = makeScripter(scriptText, { "Hillside MIDI channel": 7, "Lane 1 source": 1 }, true);
    const { ControlChange } = s.sandbox;
    for (const e of events.slice(0, 400)) s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: e.cc, value: e.value }));
    expect(s.out.targets.length === 0 && s.out.passed.length === 400, "channel 7 set: channel 16 CC is left alone");
    for (const e of d.events) s.sandbox.HandleMIDI(new ControlChange({ channel: e.channel, number: e.cc, value: e.value }));
    expect(s.out.targets.length > 50 && s.out.passed.length === 400, "channel 7 set: channel 7 CC is mapped and stopped");
}

// ---- 6. Sharing a track with Ogdoad and Strange Drift: the three mappers chained, in all six orders ---------------------
{
    // Hillside's real stream (channel 16), with stand-in Ogdoad (channel 16: LFO 1 = CC 20 + 52, trig = CC 102) and
    // Strange Drift (channel 15: Ripple X = CC 106 + 113) streams mixed in.
    const mixed = [];
    let k = 0;
    for (const e of events) {
        mixed.push({ channel: 16, number: e.cc, value: e.value });
        if (k++ % 2 === 0) {
            const v = (k * 37) % 16384, w = (k * 91) % 16384;
            mixed.push({ channel: 16, number: 20, value: v >> 7 }, { channel: 16, number: 52, value: v & 127 });
            mixed.push({ channel: 15, number: 106, value: w >> 7 }, { channel: 15, number: 113, value: w & 127 });
            if (k % 301 === 1) mixed.push({ channel: 16, number: 102, value: 127 }, { channel: 16, number: 102, value: 0 });
        }
    }
    const lastOf = (ch, fine) => { for (let i = mixed.length - 1; i >= 0; i--) if (mixed[i].channel === ch && mixed[i].number === fine) return (mixed[i - 1].value * 128 + mixed[i].value) / 16383; };
    const lastOg = lastOf(16, 52), lastSd = lastOf(15, 113);
    const carry = (to, ev) => new to.sandbox[ev.constructor.name](Object.assign({}, ev));
    const kinds = {
        H: { text: scriptText, settings: { "Lane 1 source": 1, "Lane 2 source": 2 } },
        O: { text: ogdoadText, settings: { "Lane 1 source": 1, "Lane 2 source": 17 } },
        S: { text: driftText,  settings: { "Lane 1 source": 1 } },
    };
    for (const order of ["HOS", "HSO", "OHS", "OSH", "SHO", "SOH"]) {
        const instrument = [];
        const made = {};
        let next = ev => instrument.push(ev);
        for (const letter of order.split("").reverse()) {
            const to = next;
            const m = makeScripter(kinds[letter].text, kinds[letter].settings, true, to);
            made[letter] = m;
            next = ev => m.sandbox.HandleMIDI(carry(m, ev));
        }
        const first = made[order[0]];
        for (const m of mixed) first.sandbox.HandleMIDI(new first.sandbox.ControlChange(m));
        first.sandbox.HandleMIDI(new first.sandbox.NoteOn({ pitch: 50 }));
        const h1 = lane(made.H, 1), h2 = lane(made.H, 2), o1 = lane(made.O, 1), o2 = lane(made.O, 2), s1 = lane(made.S, 1);
        const dedupe = a => a.filter((v, i) => i === 0 || v !== a[i - 1]);
        const tag = " (order " + order.split("").map(c => ({ H: "Hillside", O: "Ogdoad", S: "Strange Drift" })[c]).join(", ") + " Mapper)";
        expect(h1.length === dedupe(truth[35]).length && h1.every((v, i) => near(v, dedupe(truth[35])[i])), "Hillside lane follows Envelope exactly" + tag);
        expect(h2.length === dedupe(truth[41]).length && near(h2[h2.length - 1], 0), "Hillside lane follows Linear" + tag);
        expect(near(o1[o1.length - 1], lastOg) && o1.length > 100 && o2.length > 4, "Ogdoad lanes follow Ogdoad's stream" + tag);
        expect(near(s1[s1.length - 1], lastSd) && s1.length > 100, "Strange Drift lane follows Strange Drift's stream" + tag);
        expect(instrument.length === 1 && instrument[0].pitch === 50, "only the note reaches the instrument: each plugin's CC stops at its own mapper" + tag);
    }
}

if (failures === 0) console.log("mapper tests: " + checks + " checks passed");
else console.log("mapper tests: " + failures + " of " + checks + " checks FAILED");
process.exit(failures === 0 ? 0 : 1);

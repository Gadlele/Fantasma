// a simulated FM-1 on Web MIDI for the installer page (after web/test_web.mjs FakeFM1), driven by window.__fm1cfg
(() => {
  const pack7 = (data) => { const out = []; let acc = 0, nb = 0; for (const b of data) { acc |= b << nb; nb += 8; while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; } } if (nb) out.push(acc & 0x7F); return out; };
  const unpack7 = (s) => { const out = []; let acc = 0, nb = 0; for (const b of s) { acc |= (b & 0x7F) << nb; nb += 7; while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; } } return out; };
  const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7], UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
  const cfg = Object.assign({ identity: "FM-1_015", name: "FM-1", engines: null, unplugAfter: Infinity, product: "FM-1_910" }, window.__fm1cfg || {});
  const st = window.__fm1 = { served: 0, bad: 0, log: [], identity: cfg.identity };
  const access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
  let input, output, waiting = null, queue = [];
  function boot(identity, name, engines) {
    st.identity = identity; st.log.push("boot " + identity); waiting = null; queue = []; cfg.engines = engines;
    for (const m of [access.inputs, access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {}, close: async () => {} };
    output = { id: "o" + id, name, state: "connected", open: async () => {}, close: async () => {}, send: (d) => {
      if (output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => rx(Array.from(d)), 1); } };
    access.inputs.set(input.id, input); access.outputs.set(output.id, output);
  }
  const tx = (bytes) => { const i = input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); };
  function info() {
    const s = (t) => [...new TextEncoder().encode(t), 0];
    const ext = [16, 0x55, 1, 3, 0x4d, 1, 64, 1, 0x42, 1, 3];
    return [...s("FELUCCA v1.0"), cfg.engines.length, 115, 27, 64, 107, ...cfg.engines.flatMap(s), 4, ...ext];
  }
  function rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(st.identity)];
      tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
    } else if (d[0] === 240 && d[1] === 125 && d[2] === 70 && d[3] === 76) {      // the editor protocol: INFO only
      if (cfg.engines && d[4] === 1) tx([240, 125, 70, 76, 1, ...info(), 247]);
    } else if (eq(d, UPGRADE)) {
      queue = st.identity.startsWith("ota-") ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
                                              : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      next();
    } else if (waiting) {
      const u = unpack7(d.slice(1, -1)), [addr, len] = waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(window.__img.subarray(addr, addr + len));
      if (!eq(got, want)) st.bad++;
      waiting = null; st.served++;
      if (st.served >= cfg.unplugAfter) { input.state = output.state = "disconnected"; st.log.push("unplugged"); return; }
      if (addr === 0xE0000000) setTimeout(() => boot("ota-FM-1_900", "Felucca Update", null), 300);
      else if (addr === 0xF0000000) setTimeout(() => boot((window.__fm1cfg && window.__fm1cfg.product) || cfg.product, "Felucca", cfg.after || ["ANALOG"]), 300);
      else next();
    }
  }
  function next() {
    const r = queue.shift(); if (!r) return; waiting = r; const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0; for (let i = 6; i < 14; i++) s += u[i]; u.push(~s & 0xFF);
    tx([0xF0, ...pack7(u), 0xF7]);
  }
  boot(cfg.identity, cfg.name, cfg.engines);
  if (cfg.noDevice) { for (const m of [access.inputs, access.outputs]) m.clear(); }
  // the image the page will serve: fetched like the page does (logicalImage is the identity for these addresses' checks)
  window.__img = new Uint8Array(0);
  navigator.requestMIDIAccess = async (o) => { if (cfg.deny) throw new DOMException("denied", "SecurityError"); st.sysex = !!(o && o.sysex); return access; };
})();

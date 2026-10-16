// The installer page against a simulated FM-1 (fake_fm1.js) in Chromium:
//   node web/site/test_install.mjs http://localhost:8000/ NAME-VER.fwsc NAME     (the site from make_pages.py, served)
// Needs Playwright (npm i -g playwright; PLAYWRIGHT=path/to/playwright/index.mjs if it is not found).
const { chromium } = await import(process.env.PLAYWRIGHT || "playwright");
import fs from "fs";
const [, , base, fw, name] = process.argv;
const fake = fs.readFileSync(new URL("./fake_fm1.js", import.meta.url), "utf8");
const b = await chromium.launch();
let fails = 0;
const ok = (c, m) => { console.log((c ? "ok   " : "FAIL ") + m); if (!c) fails++; };
async function page(cfg) {
  const ctx = await b.newContext();
  await ctx.addInitScript(`window.__fm1cfg = ${JSON.stringify(cfg)};\n${fake}`);
  const p = await ctx.newPage();
  p.on("pageerror", (e) => { console.log("PAGEERROR", e.message); fails++; });
  p.on("dialog", (d) => d.accept());
  await p.goto(base + "install/");
  await p.waitForFunction(() => /Ready/.test(document.getElementById("build").textContent), null, { timeout: 15000 });
  const id = (await p.textContent("#build")).match(/identity (FM-1_\d+)/)[1];   // the package's: what the FM-1 reports after
  await p.evaluate((id) => { window.__fm1cfg = Object.assign(window.__fm1cfg || {}, { product: id }); }, id);
  await p.evaluate(async (fw) => {   // the logical image, for the fake to check what it is served
    const f = new Uint8Array(await (await fetch(fw)).arrayBuffer()), out = new Uint8Array(f.length - 20);
    for (let i = 0; i < 20; i++) out.set(f.subarray(i * 0x30, i * 0x30 + 0x2F), i * 0x2F);
    out.set(f.subarray(20 * 0x30), 20 * 0x2F); window.__img = out;
  }, "../firmware/" + fw);
  return { p, ctx };
}
const tickAll = async (p) => { for (const c of await p.$$(".pre")) await c.check(); };
const status = (p) => p.textContent("#status");
const waitStatus = (p, re, ms = 60000) => p.waitForFunction((r) => new RegExp(r).test(document.getElementById("status").textContent), re.source, { timeout: ms });
const unit = (p) => p.textContent("#unit");
const waitUnit = (p, re) => p.waitForFunction((r) => new RegExp(r).test(document.getElementById("unit").textContent), re.source, { timeout: 20000 });

{ // official firmware -> check, backup refused, gate, install
  const { p, ctx } = await page({ identity: "FM-1_015", name: "FM-1", product: "FM-1_910", after: name === "Golana" ? ["ANALOG", "GOLANA"] : ["ANALOG", "FM6", "AMBI", "BELL"] });
  ok(await p.isDisabled("#go"), "install off until the checklist is ticked");
  await p.click("#check"); await waitUnit(p, /official/); ok(/official M-VAVE firmware \(FM-1_015\)/.test(await unit(p)), "check: official firmware named");
  await p.click("#backup"); await waitUnit(p, /cannot make a complete backup/); ok(true, "backup: refused on the official firmware");
  const cs = await p.$$(".pre"); for (const c of cs.slice(0, 3)) await c.check();
  ok(await p.isDisabled("#go"), "install still off with three of four boxes");
  await cs[3].check(); ok(!(await p.isDisabled("#go")), "install on with all four");
  await p.click("#go");
  await waitStatus(p, /^Done|^Error|lost|^Stopped|not found|another/);
  const st = await p.evaluate(() => window.__fm1);
  ok(/^Done\. The FM-1 runs /.test(await status(p)), "install: Done (" + (await status(p)) + ")");
  ok(st.bad === 0 && st.served >= 10 && st.sysex && st.identity === (await p.evaluate(() => window.__fm1cfg.product)), `install: every read served right (${st.served} reads, ${st.bad} bad), SysEx asked, the FM-1 reports ${st.identity}`);
  ok(+(await p.getAttribute("#bar", "value")) === 100 || (await p.evaluate(() => document.getElementById("bar").value)) === 100, "progress at 100 %");
  await p.click("#check"); await waitUnit(p, /runs/); ok(new RegExp(name).test(await unit(p)), "check after the install: " + (await unit(p)));
  await ctx.close();
}
{ // the other fork and Felucca are named
  for (const [eng, want] of [[["ANALOG", "FM6", "PHASE", "SAMPLE", "GOLANA"], "Golana v1.0"], [["ANALOG", "FM6", "AMBI", "BELL"], "Fantasma"], [["ANALOG", "FM6", "LOFI", "SLICE"], "Felucca v1.0"]]) {
    const { p, ctx } = await page({ identity: "FM-1_910", name: "Felucca", engines: eng });
    await p.click("#check"); await waitUnit(p, /runs/); ok((await unit(p)).includes(want), `check: ${want} recognised`);
    await ctx.close();
  }
}
{ // rescue
  const { p, ctx } = await page({ identity: "FM-1_000", name: "FM-1" });
  await p.click("#check"); await waitUnit(p, /runs/); ok(/USB rescue/.test(await unit(p)), "check: USB rescue recognised");
  await ctx.close();
}
{ // update mode: resume
  const { p, ctx } = await page({ identity: "ota-FM-1_900", name: "Felucca Update", product: "FM-1_910", after: ["ANALOG"] });
  await p.click("#check"); await waitUnit(p, /update mode/); ok(true, "check: update mode recognised");
  await tickAll(p); await p.click("#go"); await waitStatus(p, /^Done|^Error|lost|^Stopped|not found|another/);
  ok(/^Done/.test(await status(p)), "update mode: Install resumes and finishes (" + (await status(p)) + ")");
  await ctx.close();
}
{ // unplugged in step 1
  const { p, ctx } = await page({ identity: "FM-1_015", name: "FM-1", unplugAfter: 2 });
  await tickAll(p); await p.click("#go"); await waitStatus(p, /lost|Error|Stopped/);
  ok(/connection was lost/.test(await status(p)), "unplugged: " + (await status(p)));
  ok(!(await p.isDisabled("#go")), "unplugged: Install can be pressed again");
  await ctx.close();
}
{ // no FM-1, MIDI refused
  let { p, ctx } = await page({ noDevice: true });
  await tickAll(p); await p.click("#go"); await waitStatus(p, /not found/); ok(true, "no FM-1: told to check the cable");
  await ctx.close();
  ({ p, ctx } = await page({ deny: true }));
  await tickAll(p); await p.click("#go"); await waitStatus(p, /not allowed/); ok(true, "MIDI refused: told how to allow it");
  await ctx.close();
}
{ // a damaged package is refused
  const ctx = await b.newContext(); const p = await ctx.newPage();
  await p.route("**/*.fwsc", async (r) => { const res = await r.fetch(); const body = Buffer.from(await res.body()); body[5000] ^= 1; r.fulfill({ response: res, body }); });
  await p.goto(base + "install/"); await p.waitForFunction(() => !/Loading/.test(document.getElementById("build").textContent));
  ok(/did not pass/.test(await p.textContent("#build")) && await p.isDisabled("#go"), "a damaged package: refused, Install stays off");
  await ctx.close();
}
await b.close();
console.log(fails ? `${fails} FAILED` : "installer tests passed");
process.exit(fails ? 1 : 0);

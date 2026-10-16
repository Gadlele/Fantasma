#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the project's site (GitHub Pages, branch gh-pages):

  index.html                 the project page (web/site/landing.html, project.json)
  install/index.html         the web installer (web/site/install.html with Felucca's fm1pkg.js, fm1ota.js,
                             fm1backup.js inlined), the package's identity, size and SHA-256 checked in the page
  try/                       the browser emulator (a build of web/emu)
  firmware/NAME-VER.fwsc     the package, with SHA256SUMS, LICENSE, LICENSING.md and LICENSES/
  img/                       web/site/img

  "What's new" and the list of versions come from CHANGELOG.md (web/site/changelog.py); its newest entry must
  be project.json's version.

  web/site/make_pages.py PACKAGE.fwsc EMU_DIR_OR_PAGE OUT_DIR

The package must be a release build of tools/build.py (Felucca's loader, no vendor files)."""
import hashlib
import html
import importlib.util
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WEB = HERE.parent
ROOT = WEB.parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def product_of(raw):
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def fill(page, cfg):
    for k, v in cfg.items():
        page = page.replace("{{" + k + "}}", v)
    left = re.findall(r"\{\{[A-Z_0-9]+\}\}", page)
    if left:
        raise SystemExit(f"unfilled: {sorted(set(left))}")
    return page


def main(pkg, emu, out):
    pkg, emu, out = Path(pkg), Path(emu), Path(out)
    p = json.loads((HERE / "project.json").read_text(encoding="utf-8"))
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product) or b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: not a release package of this tree (identity {product!r})")
    sha = hashlib.sha256(raw).hexdigest()
    ver = p["version"]
    spec = importlib.util.spec_from_file_location("changelog", HERE / "changelog.py")
    cl = importlib.util.module_from_spec(spec); spec.loader.exec_module(cl)
    log = cl.entries(ROOT / "CHANGELOG.md")
    if not log or log[0][0] != ver:
        raise SystemExit(f"CHANGELOG.md: the newest entry must be {ver} (project.json's version)")
    fw_name = f"{p['name']}-{ver}.fwsc"
    if out.exists():
        for child in out.iterdir():                 # (a git checkout of gh-pages: keep .git)
            if child.name != ".git":
                shutil.rmtree(child) if child.is_dir() else child.unlink()
    out.mkdir(parents=True, exist_ok=True)

    common = {"NAME": p["name"], "VERSION": ver, "TAGLINE": html.escape(p["tagline"]), "REPO": p["repo"],
              "REPO_SHORT": p["repo"].replace("https://", ""), "FONTS": p["fonts"], "THEME": p["theme"]}

    # the installer
    lib = "\n".join(strip_module((WEB / f).read_text(encoding="utf-8")) for f in ("fm1pkg.js", "fm1ota.js", "fm1backup.js"))
    meta = json.dumps({"name": p["name"], "version": ver, "product": product, "size": len(raw), "sha256": sha,
                       "pkg": "../firmware/" + fw_name})
    page = fill((HERE / "install.html").read_text(encoding="utf-8"), {**common, "RESCUE": p["rescue"]})
    for mark in ("/*LIB*/", "/*META*/"):
        assert page.count(mark) == 1, mark
    page = page.replace("/*LIB*/", lib).replace("/*META*/", meta)
    (out / "install").mkdir()
    (out / "install" / "index.html").write_text(page, encoding="utf-8")

    # the project page
    feats = "\n".join(
        f'      <div class="feature" style="--c:{f.get("c", "var(--accent)")}"><h3>{html.escape(f["t"])}</h3><p>{f["p"]}</p></div>'
        for f in p["features"])
    gal = "\n".join(
        f'      <figure><img src="img/{g["img"]}" alt="{html.escape(g["t"])}" loading="lazy" width="240" height="240">'
        f'<figcaption><b>{html.escape(g["t"])}</b> · {g["p"]}</figcaption></figure>' for g in p["gallery"])
    page = fill((HERE / "landing.html").read_text(encoding="utf-8"), {
        **common, "LEAD": p["lead"], "H1_TRACK": p["h1_track"], "H1_EXTRA": p["h1_extra"], "STRIPES": p["stripes"],
        "DOT_RADIUS": p["dot_radius"], "DEVICE_TITLE": p["device_title"], "DEVICE_SUB": p["device_sub"],
        "FEATURES_SUB": p["features_sub"], "FEATURES": feats, "GALLERY_SUB": p["gallery_sub"], "GALLERY": gal,
        "CREDITS": p["credits"], "NEWS": cl.html(log[0][2]), "NEWS_DATE": html.escape(log[0][1]),
        "VERSIONS": "\n".join(
            f'      <li><a href="{p["repo"]}/releases/tag/{p["name"].lower()}-v{v}"><b>{v}</b></a>'
            f'<span>{html.escape(d)}</span>{" <em>latest</em>" if i == 0 else ""}</li>'
            for i, (v, d, _) in enumerate(log))})
    (out / "index.html").write_text(page, encoding="utf-8")
    shutil.copytree(HERE / "img", out / "img")

    # the emulator: a directory (index.html, felucca.wasm, worklet.js, fonts/) or one self-contained page
    try_dir = out / "try"
    if emu.is_dir():
        shutil.copytree(emu, try_dir)
        idx = try_dir / "index.html"
    else:
        try_dir.mkdir()
        idx = try_dir / "index.html"
        shutil.copy(emu, idx)
    s = idx.read_text(encoding="utf-8")
    if not re.match(r"\s*<!doctype", s, re.I):          # (a page made for the Artifact host: give it a document)
        s = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
             '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
             f'<link rel="icon" href="../img/icon.png">\n</head>\n<body>\n{s}\n</body>\n</html>\n')
    for a, b in p.get("emu_replace", []):
        s = s.replace(a, b)
    left = [a for a, _ in p.get("emu_replace", []) if a in s]
    assert not left, left
    idx.write_text(s, encoding="utf-8")

    # the package and its licences
    fw = out / "firmware"
    (fw / "LICENSES").mkdir(parents=True)
    shutil.copy(pkg, fw / fw_name)
    (fw / "SHA256SUMS").write_text(f"{sha}  {fw_name}\n", encoding="utf-8")
    names = sorted(f.name for f in (ROOT / "LICENSES").glob("*.txt"))
    for n in names:
        shutil.copy(ROOT / "LICENSES" / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(
        f'<!doctype html><meta charset="utf-8"><title>{p["name"]} licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(ROOT / doc, fw / doc)
    (out / ".nojekyll").write_text("", encoding="utf-8")
    print(f"site: {out}: {p['name']} {ver}, {product}, {len(raw)} B, sha256 {sha[:16]}…")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CHANGELOG.md, read: its versions ("## X.Y — date" headings, newest first) and each one's notes.

  web/site/changelog.py VERSION          the notes of VERSION as Markdown (the GitHub Release's text)
  web/site/changelog.py --check VERSION  exit 1 unless CHANGELOG.md has VERSION as its newest entry

make_pages.py uses entries() and html() for the site's "What's new" and its list of versions."""
import html as _html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEAD = re.compile(r"^## (\d+(?:\.\d+){1,3}(?:-[A-Za-z0-9]+)?)\s*(?:[—-]\s*(.+?))?\s*$")


def entries(path=ROOT / "CHANGELOG.md"):
    """[(version, date, markdown)], newest first"""
    out, cur = [], None
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        m = HEAD.match(line)
        if m:
            cur = [m.group(1), m.group(2) or "", []]
            out.append(cur)
        elif line.startswith("## "):
            cur = None
        elif cur is not None:
            cur[2].append(line)
    return [(v, d, "\n".join(b).strip()) for v, d, b in out]


def _inline(s):
    s = _html.escape(s, quote=False)
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    s = re.sub(r"`(.+?)`", r"<code>\1</code>", s)
    return re.sub(r"\[(.+?)\]\((https?://[^)\s]+)\)", r'<a href="\2">\1</a>', s)


def html(md):
    """the Markdown CHANGELOG uses (paragraphs, "- " lists with indented continuation lines) as HTML"""
    blocks, para, items = [], [], []

    def flush():
        if para:
            blocks.append("<p>" + _inline(" ".join(para)) + "</p>")
            para.clear()
        if items:
            blocks.append("<ul>" + "".join(f"<li>{_inline(i)}</li>" for i in items) + "</ul>")
            items.clear()
    for line in md.splitlines():
        if not line.strip():
            flush()
        elif line.startswith("- "):
            if para:
                flush()
            items.append(line[2:].strip())
        elif line.startswith("  ") and items:
            items[-1] += " " + line.strip()
        else:
            para.append(line.strip())
    flush()
    return "\n".join(blocks)


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--check":
        e = entries()
        sys.exit(0 if e and e[0][0] == a[1] else f"CHANGELOG.md: the newest entry is {e[0][0] if e else 'none'}, not {a[1]}")
    if len(a) != 1:
        sys.exit(__doc__)
    for v, d, md in entries():
        if v == a[0]:
            print(md)
            break
    else:
        sys.exit(f"CHANGELOG.md has no entry for {a[0]}")

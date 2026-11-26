#!/usr/bin/env python3
"""Fetch the newest Facet Web screen from the YDA site and write web/facet.html.

The YDA page carries <script src="https://cdn.shopify.com/s/files/1/0691/7098/7296/files/yda-facet-01-X.Y.Z.js?v=N">.
That file is (function(){var h="<html>...";document.write(h)})() - the whole screen as one JSON string.
Same checks as the app's own updater. Prints the version. Exit 1 when nothing usable was found.
"""
import json, re, sys, urllib.request, pathlib

PAGE = "https://ydajewelry.com/pages/yda-facet?preview_theme_id=193809809696"
PREFIX = "https://cdn.shopify.com/s/files/1/0691/7098/7296/files/yda-facet-01-"
OUT = pathlib.Path(__file__).resolve().parent.parent / "web" / "facet.html"

def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "yda-facet-build"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode("utf-8")

def vkey(v):
    return tuple(int(x) for x in v.split("."))

def main():
    found = {}
    pat = re.escape(PREFIX) + r"(\d+\.\d+\.\d+)\.js\?v=\d+"
    try:
        for m in re.finditer(pat, get(PAGE).replace("&amp;", "&")):
            found[m.group(1)] = m.group(0)
    except Exception as e:
        print("YDA page not reachable:", e, file=sys.stderr)
    # fallback: the last known file (web/last-screen.txt), so a build never depends on the page alone
    last = OUT.parent / "last-screen.txt"
    if last.exists():
        for m in re.finditer(pat, last.read_text()):
            found.setdefault(m.group(1), m.group(0))
    if not found:
        sys.exit("no screen found on the YDA page")
    ver = max(found, key=vkey)
    js = get(found[ver])
    if f"Yda Facet 01 v{ver} " not in js[:200]:
        sys.exit("unexpected file header")
    a, b = js.index("var h="), js.rindex(";document.write(h)")
    html = json.loads(js[a + 6:b])
    if "Yda Facet 01" not in html or len(html) < 100000:
        sys.exit("file does not look like the Facet screen")
    if 'name="facet-version"' not in html[:4000]:
        h = html.lower().index("<head>") + 6
        html = html[:h] + f'\n<meta name="facet-version" content="{ver}">' + html[h:]
    if not html.lstrip().lower().startswith("<!doctype"):
        html = "<!doctype html>" + html
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(html, encoding="utf-8")
    print(ver)

if __name__ == "__main__":
    main()

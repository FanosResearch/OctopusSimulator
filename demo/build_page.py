#!/usr/bin/env python3
"""build_page.py -- inline the three acts' Octopus data into the demo page so it is a single
self-contained HTML file (no server, no network) for the booth.

Reads demo/data/act{1,2,3}.jsonl (+ .manifest.json) written by demo/run_acts.sh and writes them
into the <script id="octopus-data"> block of octopus-tracking-demo.html (in place by default).
The page also works without this step when served over HTTP (it fetches demo/data/) or by
dropping the files onto it.

Usage: python demo/build_page.py [-o out.html]
"""
import argparse, json, os, re, datetime

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(); ap.add_argument("-o", "--out"); ap.add_argument("--data", default=os.path.join(here, "data"))
    a = ap.parse_args()
    page = os.path.join(here, "octopus-tracking-demo.html"); out = a.out or page
    d = {"built": datetime.date.today().isoformat(), "manifest": {}}
    for act in ("act1", "act2", "act3"):
        with open(os.path.join(a.data, act + ".jsonl"), encoding="utf-8") as f:
            d[act] = [json.loads(l) for l in f if l.strip()]
        mp = os.path.join(a.data, act + ".manifest.json")
        d["manifest"][act] = json.load(open(mp, encoding="utf-8")) if os.path.exists(mp) else {}
    blob = json.dumps(d, separators=(",", ":")).replace("</", "<\\/")
    html = open(page, encoding="utf-8").read()
    new, n = re.subn(r'(<script id="octopus-data" type="application/json">)(.*?)(</script>)', lambda m: m.group(1) + blob + m.group(3), html, flags=re.S)
    if n != 1: raise SystemExit("data block not found in the page")
    open(out, "w", encoding="utf-8", newline="\n").write(new)
    print(f"{out}: embedded {sum(len(d[a]) for a in ('act1','act2','act3'))} job records ({len(blob)/1024:.0f} KB)")

if __name__ == "__main__":
    main()

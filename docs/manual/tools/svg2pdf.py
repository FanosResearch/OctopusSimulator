#!/usr/bin/env python3
"""Render every figures/svg/*.svg to figures/pdf/<name>.pdf with headless Chrome.

Chrome prints an HTML wrapper whose @page size equals the SVG's viewBox in CSS px, so
the PDF page is exactly the drawing; pdfcrop (MiKTeX/TeX Live) then trims the margins.
A PDF is only rebuilt when its SVG is newer. If Chrome is not found and every PDF
exists, the script succeeds silently -- the rendered PDFs are committed on purpose, so
the LaTeX build never depends on a browser.

    python tools/svg2pdf.py            # incremental
    python tools/svg2pdf.py --force    # rebuild all
"""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SVG_DIR = os.path.join(ROOT, "figures", "svg")
PDF_DIR = os.path.join(ROOT, "figures", "pdf")

CHROME_CANDIDATES = [
    os.environ.get("CHROME", ""),
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
]


def find_chrome():
    for c in CHROME_CANDIDATES:
        if c and os.path.exists(c):
            return c
    for name in ("google-chrome", "chromium", "chromium-browser", "chrome"):
        p = shutil.which(name)
        if p:
            return p
    return None


def viewbox(svg_path):
    head = open(svg_path, encoding="utf-8").read(4000)
    m = re.search(r'viewBox="\s*0\s+0\s+([\d.]+)\s+([\d.]+)"', head)
    if not m:
        raise SystemExit("no viewBox in " + svg_path)
    return float(m.group(1)), float(m.group(2))


def render(chrome, svg, pdf):
    w, h = viewbox(svg)
    with tempfile.TemporaryDirectory() as td:
        html = os.path.join(td, "page.html")
        # Pass the SVG by file URL; the page is sized to the drawing so there is one page.
        svg_url = "file:///" + os.path.abspath(svg).replace("\\", "/")
        open(html, "w", encoding="utf-8").write(
            "<!doctype html><html><head><meta charset='utf-8'><style>"
            "@page{size:%dpx %dpx;margin:0}html,body{margin:0;padding:0}"
            "img{display:block;width:%dpx;height:%dpx}</style></head>"
            "<body><img src='%s'></body></html>" % (w, h, w, h, svg_url))
        raw = os.path.join(td, "raw.pdf")
        cmd = [chrome, "--headless", "--disable-gpu", "--no-pdf-header-footer",
               "--print-to-pdf=" + raw, "file:///" + html.replace("\\", "/")]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if shutil.which("pdfcrop"):
            subprocess.run(["pdfcrop", "--margins", "2", raw, pdf], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            shutil.copy(raw, pdf)


def main():
    force = "--force" in sys.argv
    os.makedirs(PDF_DIR, exist_ok=True)
    svgs = sorted(f for f in os.listdir(SVG_DIR) if f.endswith(".svg"))
    todo = []
    for f in svgs:
        s = os.path.join(SVG_DIR, f); p = os.path.join(PDF_DIR, f[:-4] + ".pdf")
        if force or not os.path.exists(p) or os.path.getmtime(s) > os.path.getmtime(p):
            todo.append((s, p))
    if not todo:
        print("svg2pdf: all %d figures up to date" % len(svgs)); return
    chrome = find_chrome()
    if not chrome:
        missing = [p for _, p in todo if not os.path.exists(p)]
        if missing:
            raise SystemExit("svg2pdf: Chrome not found and these PDFs are missing:\n  " + "\n  ".join(missing))
        print("svg2pdf: Chrome not found; keeping the %d committed PDFs" % len(todo)); return
    for s, p in todo:
        render(chrome, s, p)
        print("svg2pdf: %s -> %s" % (os.path.basename(s), os.path.relpath(p, ROOT)))


if __name__ == "__main__":
    main()

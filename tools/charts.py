#!/usr/bin/env python3
"""Render the charts of index.html from data/results.json.

    python3 tools/charts.py          rewrite the blocks between <!--chart:NAME--> and <!--/chart-->

The charts are inline SVG that take their colours from the CSS variables of style.css, so they
follow the light/dark theme. Every number comes from data/results.json, which records where it
was measured; nothing is drawn that is not in that file. Standard library only."""
import json, re, sys
from html import escape

ROOT = __file__.rsplit('/', 2)[0] if '/' in __file__ else '.'
data = json.load(open(f'{ROOT}/data/results.json'))
W = 720


def fmt(v):
    return f'{v:g}' if abs(v) < 100 else f'{v:.0f}'


def svg(h, label, body):
    return (f'<svg class="chart" viewBox="0 0 {W} {h}" role="img" aria-label="{escape(label)}">'
            f'{body}</svg>')


def accuracy():
    b = data['benchmark']
    lim, old, new = b['limits'], b['share_baseline'], b['share_now']
    dev_o = [round(o - l, 1) for o, l in zip(old, lim)]
    dev_n = [round(n - l, 1) for n, l in zip(new, lim)]
    top, bot, left, right = 28, 40, 56, 16
    h = 250
    lo, hi = -4.0, 4.0
    y = lambda v: top + (hi - v) / (hi - lo) * (h - top - bot)
    gw = (W - left - right) / len(lim)
    s = []
    for t in (-4, -2, 0, 2, 4):
        s.append(f'<line class="grid" x1="{left}" x2="{W-right}" y1="{y(t):.1f}" y2="{y(t):.1f}"/>'
                 f'<text class="ax" x="{left-8}" y="{y(t)+4:.1f}" text-anchor="end">{('0' if t == 0 else f'{t:+d}')}</text>')
    s.append(f'<line class="zero" x1="{left}" x2="{W-right}" y1="{y(0):.1f}" y2="{y(0):.1f}"/>')
    bw = 26
    for i, l in enumerate(lim):
        cx = left + gw * (i + .5)
        for k, (v, cls) in enumerate(((dev_o[i], 'old'), (dev_n[i], 'new'))):
            x = cx + (k - 1) * bw + (0 if k else -2) + (2 if k else 0)
            x = cx - bw - 2 + k * (bw + 4)
            y0, y1 = y(0), y(v)
            s.append(f'<rect class="bar {cls}" x="{x:.1f}" y="{min(y0,y1):.1f}" width="{bw}" height="{max(abs(y1-y0),1):.1f}"/>'
                     f'<text class="val" x="{x+bw/2:.1f}" y="{(y1-5 if v>=0 else y1+13):.1f}" text-anchor="middle">{v:+.1f}</text>')
        s.append(f'<text class="ax" x="{cx:.1f}" y="{h-bot+20}" text-anchor="middle">-l {l}</text>')
    s.append(f'<text class="ax" x="{left}" y="14">achieved minus requested, percentage points of one CPU</text>')
    s.append(legend(W - 250, 14))
    return svg(h, 'Achieved CPU share minus requested limit for five limits, baseline and this version', ''.join(s))


def legend(x, y):
    return (f'<rect class="bar old" x="{x}" y="{y-9}" width="10" height="10"/><text class="ax" x="{x+15}" y="{y}">upstream 0.3.2</text>'
            f'<rect class="bar new" x="{x+130}" y="{y-9}" width="10" height="10"/><text class="ax" x="{x+145}" y="{y}">this fork</text>')


def overhead():
    rows = data['benchmark']['overhead']
    rowh, top, left = 62, 34, 16
    h = top + rowh * len(rows) + 6
    bw_max = W - left - 150
    s = [legend(W - 250, 14)]
    for i, r in enumerate(rows):
        y0 = top + i * rowh
        m = max(r['baseline'], r['now'])
        s.append(f'<text class="lab" x="{left}" y="{y0+2}">{escape(r["label"])}</text>')
        for k, (key, cls) in enumerate((('baseline', 'old'), ('now', 'new'))):
            w = r[key] / m * bw_max
            yy = y0 + 10 + k * 18
            s.append(f'<rect class="bar {cls}" x="{left}" y="{yy}" width="{max(w,1):.1f}" height="14"/>'
                     f'<text class="val" x="{left+w+6:.1f}" y="{yy+11}">{fmt(r[key])} {escape(r["unit"])}</text>')
        pct = (1 - r['now'] / r['baseline']) * 100
        s.append(f'<text class="ax" x="{W-left}" y="{y0+2}" text-anchor="end">{pct:.0f}% less</text>')
    return svg(h, 'Limiter overhead, upstream versus this fork, three measurements', ''.join(s))


def coverage():
    c = data['coverage']
    files = c['files']
    rowh, top, left = 26, 34, 130
    h = top + rowh * (len(files) + 1) + 24
    plot = W - left - 120
    x = lambda v: left + v / 100 * plot
    s = [f'<text class="ax" x="{left}" y="14">line coverage, % (bar) and branch coverage, % (tick)</text>']
    rows = files + [{'name': 'total', 'lines': c['total_lines'], 'branches': c['total_branches']}]
    for i, f in enumerate(rows):
        y = top + i * rowh
        tot = f['name'] == 'total'
        s.append(f'<text class="lab{" strong" if tot else ""}" x="{left-8}" y="{y+13}" text-anchor="end">{escape(f["name"])}</text>')
        cls = 'new' if f['lines'] >= c['gate_lines'] else 'warn'
        s.append(f'<rect class="bar {cls}" x="{left}" y="{y}" width="{f["lines"]/100*plot:.1f}" height="18"/>')
        s.append(f'<line class="tick" x1="{x(f["branches"]):.1f}" x2="{x(f["branches"]):.1f}" y1="{y-2}" y2="{y+20}"/>')
        s.append(f'<text class="val" x="{left+plot+8}" y="{y+13}">{f["lines"]:.1f} / {f["branches"]:.1f}</text>')
    gx = x(c['gate_lines'])
    s.append(f'<line class="gate" x1="{gx:.1f}" x2="{gx:.1f}" y1="{top-8}" y2="{top+rowh*len(rows)}"/>'
             f'<text class="ax" x="{gx:.1f}" y="{top+rowh*len(rows)+14}" text-anchor="middle">CI gate: {c["gate_lines"]}% lines</text>')
    return svg(h, 'Line and branch coverage per source file, with the 85 percent line gate', ''.join(s))


CHARTS = {'accuracy': accuracy, 'overhead': overhead, 'coverage': coverage}

def main():
    path = f'{ROOT}/index.html'
    html = open(path).read()
    for name, fn in CHARTS.items():
        pat = re.compile(rf'(<!--chart:{name}-->)(.*?)(<!--/chart-->)', re.S)
        if not pat.search(html):
            sys.exit(f'marker for chart "{name}" not found in index.html')
        html = pat.sub(lambda m: m.group(1) + fn() + m.group(3), html, count=1)
    open(path, 'w').write(html)
    print('charts written:', ', '.join(CHARTS))

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Build the documentation site from content/*.html.

    python3 tools/build.py          write *.html, sitemap.xml and search.json in the repository root
    python3 tools/build.py --check  build into memory and fail if a file on disk differs, or a link breaks

content/NAME.html is the body of NAME.html. Its first line is a comment with the page's metadata:
    <!--meta title="Install" desc="One sentence." section="Start" order="10"-->
A page without `section` (index) uses the landing layout. The shared header, sidebar, footer, the
version (site.json) and the charts (tools/charts.py, data/results.json) are added here, so a page
never repeats them. Standard library only; the output is plain static files for GitHub Pages."""
import json
import os
import re
import sys
from html import escape

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import charts  # noqa: E402

SITE = json.load(open(os.path.join(ROOT, 'site.json')))
SITE['coverage_source'] = charts.data['coverage']['source']
BASE = SITE['url'].rstrip('/')
SECTIONS = ['Start', 'Reference', 'Project']
META = re.compile(r'^<!--meta (.*?)-->\s*\n', re.S)
ATTR = re.compile(r'(\w+)="(.*?)"')


def load_pages():
    pages = []
    cdir = os.path.join(ROOT, 'content')
    for fn in sorted(os.listdir(cdir)):
        if not fn.endswith('.html'):
            continue
        raw = open(os.path.join(cdir, fn)).read()
        m = META.match(raw)
        if not m:
            sys.exit(f'content/{fn}: first line must be <!--meta title="..." desc="..." -->')
        meta = dict(ATTR.findall(m.group(1)))
        for key in ('title', 'desc'):
            if key not in meta:
                sys.exit(f'content/{fn}: meta has no {key}')
        meta['slug'] = fn[:-5]
        meta['body'] = raw[m.end():]
        meta['order'] = int(meta.get('order', 100))
        pages.append(meta)
    return pages


def fill(body, page):
    """charts, version and date placeholders"""
    for name, fn in charts.CHARTS.items():
        body = re.sub(rf'<!--chart:{name}-->.*?<!--/chart-->|<!--chart:{name}-->',
                      lambda m: fn(), body, count=1, flags=re.S)
    for key, val in SITE.items():
        body = body.replace('{{' + key + '}}', str(val))
    if '{{' in body:
        sys.exit(f'{page["slug"]}: unresolved placeholder {re.search(r"{{.*?}}", body).group(0)}')
    return body


def headings(body):
    return [(m.group(1), re.sub(r'<.*?>', '', m.group(2)))
            for m in re.finditer(r'<h2 id="([^"]+)">(.*?)</h2>', body, re.S)]


def href(slug):
    return './' if slug == 'index' else f'{slug}.html'


def sidebar(pages, cur):
    out = ['<nav class="side" aria-label="Documentation">']
    for sec in SECTIONS:
        items = sorted((p for p in pages if p.get('section') == sec), key=lambda p: p['order'])
        if not items:
            continue
        out.append(f'<h2>{sec}</h2><ul>')
        for p in items:
            cls = ' aria-current="page"' if p['slug'] == cur['slug'] else ''
            out.append(f'<li><a href="{href(p["slug"])}"{cls}>{escape(p["title"])}</a></li>')
        out.append('</ul>')
    out.append('</nav>')
    return ''.join(out)


def toc(body):
    hs = headings(body)
    if len(hs) < 3:
        return ''
    return ('<nav class="toc" aria-label="On this page"><strong>On this page</strong><ul>'
            + ''.join(f'<li><a href="#{i}">{escape(t)}</a></li>' for i, t in hs) + '</ul></nav>')


def pager(pages, cur):
    ordered = sorted((p for p in pages if p.get('section')),
                     key=lambda p: (SECTIONS.index(p['section']), p['order']))
    i = [p['slug'] for p in ordered].index(cur['slug'])
    prev = ordered[i - 1] if i > 0 else None
    nxt = ordered[i + 1] if i + 1 < len(ordered) else None
    s = '<nav class="pager" aria-label="Previous and next page">'
    s += f'<a rel="prev" href="{href(prev["slug"])}"><small>Previous</small>{escape(prev["title"])}</a>' if prev else '<span></span>'
    s += f'<a rel="next" href="{href(nxt["slug"])}"><small>Next</small>{escape(nxt["title"])}</a>' if nxt else '<span></span>'
    return s + '</nav>'


def page_html(p, pages):
    body = fill(p['body'], p)
    home = 'section' not in p
    title = SITE['name'] if home else f'{p["title"]} · {SITE["name"]}'
    canon = f'{BASE}/' if home else f'{BASE}/{p["slug"]}.html'
    head = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{escape(title)}</title>
<meta name="description" content="{escape(p['desc'], quote=True)}">
<link rel="icon" href="favicon.svg" type="image/svg+xml">
<link rel="canonical" href="{canon}">
<link rel="stylesheet" href="style.css">
<script>try{{var t=localStorage.getItem('theme');if(t)document.documentElement.dataset.theme=t}}catch(e){{}}</script>
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="top"><div class="wrap">
  <a class="brand" href="./">{SITE['name']}</a>
  <nav aria-label="Main"><a href="install.html">Install</a><a href="usage.html">Usage</a><a href="faq.html">FAQ</a><a href="releases.html">Releases</a><a href="{SITE['repo']}">GitHub</a></nav>
  <div class="search"><input id="q" type="search" placeholder="Search the docs" aria-label="Search the docs" autocomplete="off"><ul id="hits" hidden></ul></div>
  <button id="theme" type="button" aria-label="Toggle colour theme">theme</button>
</div></header>
'''
    if home:
        main = f'<main id="main">{body}</main>\n'
    else:
        main = (f'<div class="wrap docs">{sidebar(pages, p)}'
                f'<main id="main"><article class="doc"><h1>{escape(p["title"])}</h1>{body}'
                f'{pager(pages, p)}</article>{toc(body)}</main></div>\n')
    foot = f'''<footer><div class="wrap">
  <p>{SITE['name']} {SITE['version']} · GPL-2.0-or-later · <a href="{SITE['repo']}/blob/master/CHANGELOG.md">changelog</a> · <a href="{SITE['repo']}/issues">report a problem</a>. Page <a href="{SITE['repo']}/blob/gh-pages/content/{p['slug']}.html">source</a>.</p>
  <p>Originally by Angelo Marletta; this fork is maintained at <a href="{SITE['repo']}">denji/cpulimit</a>. Contributors: <a href="{SITE['repo']}/blob/master/AUTHORS">AUTHORS</a>. The site is built from <code>content/</code>, <code>site.json</code> and <code>data/results.json</code> by <code>tools/build.py</code>.</p>
</div></footer>
<script src="site.js" defer></script>
</body>
</html>
'''
    return head + main + foot


def search_index(pages):
    out = []
    for p in pages:
        body = fill(p['body'], p)
        body = re.sub(r'<svg.*?</svg>', ' ', body, flags=re.S)
        text = re.sub(r'\s+', ' ', re.sub(r'<.*?>', ' ', body))
        out.append({'url': href(p['slug']), 'title': p['title'], 'text': text[:6000],
                    'heads': [{'id': i, 'title': t} for i, t in headings(body)]})
    return json.dumps(out, separators=(',', ':'))


def sitemap(pages):
    urls = [f'{BASE}/' if p['slug'] == 'index' else f'{BASE}/{p["slug"]}.html' for p in pages]
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
            + ''.join(f'<url><loc>{u}</loc><lastmod>{SITE["date"]}</lastmod></url>\n' for u in urls) + '</urlset>\n')


def check_links(outputs):
    names = set(outputs) | {'favicon.svg', 'style.css', 'site.js', 'robots.txt'}
    ids = {n: set(re.findall(r'\bid="([^"]+)"', t)) for n, t in outputs.items() if n.endswith('.html')}
    bad = []
    for n, t in outputs.items():
        if not n.endswith('.html'):
            continue
        for m in re.finditer(r'<a [^>]*href="([^"]+)"', t):
            h = m.group(1)
            if re.match(r'(https?:|mailto:)', h):
                continue
            path, _, frag = h.partition('#')
            path = n if path == '' else ('index.html' if path == './' else path)
            if path not in names:
                bad.append(f'{n}: {h}: no such file')
            elif frag and path.endswith('.html') and frag not in ids.get(path, ()):
                bad.append(f'{n}: {h}: no such anchor')
    return bad


def main():
    check = '--check' in sys.argv
    pages = load_pages()
    outputs = {f'{p["slug"]}.html': page_html(p, pages) for p in pages}
    outputs['sitemap.xml'] = sitemap(pages)
    outputs['search.json'] = search_index(pages)
    bad = check_links(outputs)
    if bad:
        sys.exit('broken links:\n  ' + '\n  '.join(bad))
    if check:
        stale = [n for n, t in outputs.items()
                 if not os.path.exists(os.path.join(ROOT, n)) or open(os.path.join(ROOT, n)).read() != t]
        if stale:
            sys.exit('out of date (run python3 tools/build.py): ' + ', '.join(stale))
        print('site is up to date;', len(outputs), 'files, links ok')
        return
    for n, t in outputs.items():
        with open(os.path.join(ROOT, n), 'w') as f:
            f.write(t)
    print('wrote', ', '.join(sorted(outputs)))


if __name__ == '__main__':
    main()

/* theme toggle, copy buttons and search; no dependencies */
(function () {
  document.querySelectorAll('.cp').forEach(function (box) {
    var pre = box.querySelector('pre'), b = document.createElement('button');
    b.type = 'button'; b.textContent = 'copy';
    b.addEventListener('click', function () {
      var txt = pre.innerText;
      (navigator.clipboard ? navigator.clipboard.writeText(txt) : Promise.reject()).then(
        function () { b.textContent = 'copied'; }, function () { b.textContent = 'select + copy'; });
      setTimeout(function () { b.textContent = 'copy'; }, 1500);
    });
    box.appendChild(b);
  });
  var t = document.getElementById('theme');
  t.addEventListener('click', function () {
    var cur = document.documentElement.dataset.theme || (matchMedia('(prefers-color-scheme:dark)').matches ? 'dark' : 'light');
    var next = cur === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('theme', next); } catch (e) {}
  });

  var q = document.getElementById('q'), hits = document.getElementById('hits'), index = null;
  function load() {
    if (index) return Promise.resolve(index);
    return fetch('search.json').then(function (r) { return r.json(); }).then(function (j) { return (index = j); });
  }
  function run() {
    var words = q.value.toLowerCase().split(/\s+/).filter(Boolean);
    hits.textContent = '';
    if (!words.length) { hits.hidden = true; return; }
    load().then(function (pages) {
      var found = [];
      pages.forEach(function (p) {
        var hay = (p.title + ' ' + p.text).toLowerCase();
        if (!words.every(function (w) { return hay.indexOf(w) >= 0; })) return;
        var at = hay.indexOf(words[0]), start = Math.max(0, at - 40);
        var score = words.reduce(function (s, w) { return s + (p.title.toLowerCase().indexOf(w) >= 0 ? 10 : 0); }, 0);
        found.push({ p: p, snip: (p.title + ' ' + p.text).slice(start + (p.title.length + 1 > start ? 0 : 0), start + 110), score: score });
      });
      found.sort(function (a, b) { return b.score - a.score; });
      found.slice(0, 8).forEach(function (f) {
        var li = document.createElement('li'), a = document.createElement('a'), s = document.createElement('small');
        a.href = f.p.url; a.textContent = f.p.title; s.textContent = f.snip; a.appendChild(s); li.appendChild(a); hits.appendChild(li);
      });
      if (!found.length) { var li = document.createElement('li'); li.textContent = 'No match'; li.style.padding = '.4rem .6rem'; hits.appendChild(li); }
      hits.hidden = false;
    });
  }
  q.addEventListener('input', run);
  q.addEventListener('keydown', function (e) { if (e.key === 'Escape') { q.value = ''; run(); q.blur(); } });
  document.addEventListener('click', function (e) { if (!e.target.closest('.search')) hits.hidden = true; });
  document.addEventListener('keydown', function (e) {
    if (e.key === '/' && document.activeElement.tagName !== 'INPUT') { e.preventDefault(); q.focus(); }
  });
})();

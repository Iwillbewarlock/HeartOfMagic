/* Shared helpers for the three explainer pages: palette, school glyphs, easing, caption bar.
   Every page renders a frame as a pure function of t through window.__setTime(t). */
var W = 960, H = 540;
/* Skyrim-menu look: near-black smoke, off-white text, thin pale lines, muted school tints. */
var BG = '#0b0b0c', PANEL = '#000000', LINE = '#8f8a7e', MUTED = '#cbc5b6', TEXT = '#e8e4d8', INK = '#0b0b0c';
var BRIGHT = '#fffbef', DIM = '#a29c8f', RULE = '#cfc9b8';
var FONT = "'Bahnschrift Condensed', Bahnschrift, 'Arial Narrow', sans-serif";

/* Muted, desaturated school tints (Okabe-Ito hues kept); each school also has its own glyph. */
var SCHOOL = {
  alt: { name: 'Alteration', color: '#93acbf' },
  con: { name: 'Conjuration', color: '#b89cae' },
  des: { name: 'Destruction', color: '#cc9c76' },
  ill: { name: 'Illusion', color: '#c6bb8c' },
  res: { name: 'Restoration', color: '#90b4a2' }
};
var SCHOOL_ORDER = ['alt', 'con', 'des', 'ill', 'res'];
var LEVELS = [
  { lv: 1, tier: 'Novice' }, { lv: 25, tier: 'Apprentice' }, { lv: 50, tier: 'Adept' },
  { lv: 75, tier: 'Expert' }, { lv: 100, tier: 'Master' }
];

function clamp(x, a, b) { return x < a ? a : (x > b ? b : x); }
function lerp(a, b, k) { return a + (b - a) * k; }
function ease(k) { k = clamp(k, 0, 1); return k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2; }
function easeOutBack(k) { k = clamp(k, 0, 1); var c1 = 1.4, c3 = c1 + 1; return 1 + c3 * Math.pow(k - 1, 3) + c1 * Math.pow(k - 1, 2); }
/* 0 before a, 1 after b, eased in between */
function seg(t, a, b) { return ease((t - a) / (b - a)); }
/* fade in over [a, a+f], hold, fade out over [b-f, b] */
function window01(t, a, b, f) { return Math.min(seg(t, a, a + f), 1 - seg(t, b - f, b)); }
function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
function f1(x) { return Math.round(x * 10) / 10; }

function txt(x, y, s, size, fill, opts) {
  opts = opts || {};
  if (opts.upper) s = String(s).toUpperCase();
  return '<text x="' + f1(x) + '" y="' + f1(y) + '" font-size="' + size + '" fill="' + fill + '"' +
    ' font-weight="' + (opts.weight || 600) + '" text-anchor="' + (opts.anchor || 'start') + '"' +
    (opts.opacity !== undefined ? ' opacity="' + f1(opts.opacity * 100) / 100 + '"' : '') +
    (opts.fit ? ' data-fit="' + opts.fit + '"' : '') +
    (opts.ls ? ' letter-spacing="' + opts.ls + '"' : '') +
    ' dominant-baseline="central">' + esc(s) + '</text>';
}
function rect(x, y, w, h, r, fill, opts) {
  opts = opts || {};
  return '<rect x="' + f1(x) + '" y="' + f1(y) + '" width="' + f1(w) + '" height="' + f1(h) + '" rx="' + r + '"' +
    ' fill="' + (fill || 'none') + '"' +
    (opts.stroke ? ' stroke="' + opts.stroke + '" stroke-width="' + (opts.sw || 3) + '"' : '') +
    (opts.dash ? ' stroke-dasharray="' + opts.dash + '"' : '') +
    (opts.opacity !== undefined ? ' opacity="' + f1(opts.opacity * 100) / 100 + '"' : '') + '/>';
}

/* School glyph centred on (cx, cy), half-size s. hole = colour for cut-outs (the eye's pupil). */
function glyph(key, cx, cy, s, fill, hole) {
  var p, i, a, out = '';
  if (key === 'alt') {
    p = [[0, -s], [0.95 * s, 0.72 * s], [-0.95 * s, 0.72 * s]];
    out = '<polygon points="' + p.map(function (q) { return f1(cx + q[0]) + ',' + f1(cy + q[1] + 0.08 * s); }).join(' ') + '" fill="' + fill + '"/>';
  } else if (key === 'con') {
    p = [];
    for (i = 0; i < 10; i++) {
      a = -Math.PI / 2 + i * Math.PI / 5;
      var r = i % 2 ? 0.44 * s : 1.05 * s;
      p.push(f1(cx + r * Math.cos(a)) + ',' + f1(cy + 0.08 * s + r * Math.sin(a)));
    }
    out = '<polygon points="' + p.join(' ') + '" fill="' + fill + '"/>';
  } else if (key === 'des') {
    /* flame: a tall tongue on the right, a short one on the left */
    var P = function (x, y) { return f1(cx + x * s) + ',' + f1(cy + y * s); };
    out = '<path d="M' + P(0.15, -1.05) +
      ' C' + P(0.75, -0.45) + ' ' + P(0.95, 0.05) + ' ' + P(0.75, 0.5) +
      ' C' + P(0.55, 0.95) + ' ' + P(-0.55, 0.95) + ' ' + P(-0.75, 0.5) +
      ' C' + P(-0.9, 0.15) + ' ' + P(-0.75, -0.2) + ' ' + P(-0.5, -0.5) +
      ' C' + P(-0.45, -0.2) + ' ' + P(-0.3, -0.05) + ' ' + P(-0.12, 0.0) +
      ' C' + P(-0.25, -0.45) + ' ' + P(-0.1, -0.8) + ' ' + P(0.15, -1.05) + 'Z" fill="' + fill + '"/>';
  } else if (key === 'ill') {
    out = '<path d="M' + f1(cx - 1.05 * s) + ',' + f1(cy) + ' Q' + f1(cx) + ',' + f1(cy - 0.95 * s) + ' ' + f1(cx + 1.05 * s) + ',' + f1(cy) +
      ' Q' + f1(cx) + ',' + f1(cy + 0.95 * s) + ' ' + f1(cx - 1.05 * s) + ',' + f1(cy) + 'Z" fill="' + fill + '"/>' +
      '<circle cx="' + f1(cx) + '" cy="' + f1(cy) + '" r="' + f1(0.3 * s) + '" fill="' + (hole || BG) + '"/>';
  } else if (key === 'res') {
    var k = 0.34 * s;
    out = '<rect x="' + f1(cx - k) + '" y="' + f1(cy - s) + '" width="' + f1(2 * k) + '" height="' + f1(2 * s) + '" rx="' + f1(k * 0.4) + '" fill="' + fill + '"/>' +
      '<rect x="' + f1(cx - s) + '" y="' + f1(cy - k) + '" width="' + f1(2 * s) + '" height="' + f1(2 * k) + '" rx="' + f1(k * 0.4) + '" fill="' + fill + '"/>';
  }
  return out;
}

/* Small diamond (end caps, corner pins). */
function diamond(cx, cy, r, fill, o) {
  return '<path d="M' + f1(cx) + ',' + f1(cy - r) + ' L' + f1(cx + r) + ',' + f1(cy) + ' L' + f1(cx) + ',' + f1(cy + r) + ' L' + f1(cx - r) + ',' + f1(cy) + 'Z" fill="' + fill + '"' +
    (o !== undefined ? ' opacity="' + f1(o * 100) / 100 + '"' : '') + '/>';
}
/* Hairline rule with diamond end caps. */
function hairline(x1, x2, y, col, o) {
  col = col || RULE; o = o === undefined ? 0.7 : o;
  return '<g opacity="' + f1(o * 100) / 100 + '"><line x1="' + f1(x1 + 6) + '" y1="' + f1(y) + '" x2="' + f1(x2 - 6) + '" y2="' + f1(y) + '" stroke="' + col + '" stroke-width="1.5"/>' +
    diamond(x1, y, 4, col) + diamond(x2, y, 4, col) + '</g>';
}
/* Knot-like corner ornament: a double L and two linked diamond loops (over-under gaps), pinned by a diamond.
   dx, dy = +1 / -1, pointing into the box. */
function knotCorner(x, y, dx, dy, col, o) {
  var L = 26, P = function (a, b) { return f1(x + dx * a) + ',' + f1(y + dy * b); };
  var loop = function (c, r) { return 'M' + P(c, c - r) + ' L' + P(c + r, c) + ' L' + P(c, c + r) + ' L' + P(c - r, c) + 'Z'; };
  return '<g fill="none" stroke="' + col + '" stroke-width="1.6" opacity="' + f1(o * 100) / 100 + '">' +
    '<path d="M' + P(0, L) + ' L' + P(0, 0) + ' L' + P(L, 0) + '"/>' +
    '<path d="M' + P(5, L - 8) + ' L' + P(5, 5) + ' L' + P(L - 8, 5) + '"/>' +
    '<path d="' + loop(11, 6) + '"/><path d="' + loop(18, 4.5) + '"/>' +
    '</g>' + diamond(x, y, 3.5, col, o);
}
/* Thin plate: smoky dark fill, 1.25 px outline, optional knot corners. */
function plate(x, y, w, h, opts) {
  opts = opts || {};
  var col = opts.stroke || LINE, o = opts.opacity === undefined ? 1 : opts.opacity;
  var out = '<g opacity="' + f1(o * 100) / 100 + '">' +
    '<rect x="' + f1(x) + '" y="' + f1(y) + '" width="' + f1(w) + '" height="' + f1(h) + '" rx="' + (opts.r || 2) + '"' +
    ' fill="' + (opts.fill || '#000000') + '" fill-opacity="' + (opts.fo === undefined ? 0.45 : opts.fo) + '"' +
    ' stroke="' + col + '" stroke-width="' + (opts.sw || 1.4) + '"' + (opts.dash ? ' stroke-dasharray="' + opts.dash + '"' : '') + '/>';
  if (opts.corners) {
    var c = opts.cornerCol || RULE, k = -3;
    out += knotCorner(x + k, y + k, 1, 1, c, 0.9) + knotCorner(x + w - k, y + k, -1, 1, c, 0.9) +
      knotCorner(x + k, y + h - k, 1, -1, c, 0.9) + knotCorner(x + w - k, y + h - k, -1, -1, c, 0.9);
  }
  return out + '</g>';
}
/* Selection: thin corner brackets round a box. */
function brackets(x, y, w, h, col, o, len) {
  len = len || 12; col = col || BRIGHT;
  var p = function (ax, ay, bx, by, cx, cy) { return 'M' + f1(ax) + ',' + f1(ay) + ' L' + f1(bx) + ',' + f1(by) + ' L' + f1(cx) + ',' + f1(cy); };
  var x2 = x + w, y2 = y + h;
  return '<path d="' + p(x, y + len, x, y, x + len, y) + ' ' + p(x2 - len, y, x2, y, x2, y + len) + ' ' +
    p(x, y2 - len, x, y2, x + len, y2) + ' ' + p(x2 - len, y2, x2, y2, x2, y2 - len) +
    '" fill="none" stroke="' + col + '" stroke-width="2" opacity="' + f1(o * 100) / 100 + '"/>';
}

/* Title at the top left: uppercase, wide spacing, a hairline under it. */
function title(s) {
  return txt(30, 38, s, 34, TEXT, { weight: 600, upper: true, ls: 4, fit: 900 }) + hairline(30, W - 30, 64, RULE, 0.55);
}

/* Caption: a ruled notification line at the bottom. steps = [{a, b, text}], b exclusive; f = fade time. */
function captionBar(t, steps, f) {
  var out = '<rect x="0" y="474" width="' + W + '" height="66" fill="#000000" opacity="0.5"/>' + hairline(40, W - 40, 476, RULE, 0.6);
  for (var i = 0; i < steps.length; i++) {
    var st = steps[i];
    var o = window01(t, st.a, st.b, f);
    if (st.a <= 0) o = Math.min(1, 1 - seg(t, st.b - f, st.b));
    if (st.b >= 1e8) o = seg(t, st.a, st.a + f);
    if (o > 0.004) out += txt(W / 2, 508, st.text, 30, TEXT, { weight: 400, anchor: 'middle', opacity: o, fit: 900, ls: 0.5 });
  }
  return out;
}

/* Smoky backdrop: static fog noise and a vignette (static, so loops stay seamless and the GIF small). */
function backdrop() {
  return '<defs><filter id="fog" x="0" y="0" width="100%" height="100%">' +
    '<feTurbulence type="fractalNoise" baseFrequency="0.0045 0.009" numOctaves="2" seed="7"/>' +
    '<feColorMatrix values="0 0 0 0 0.86  0 0 0 0 0.84  0 0 0 0 0.78  0.6 0 0 0 -0.22"/></filter>' +
    '<radialGradient id="vig" cx="50%" cy="45%" r="75%"><stop offset="0" stop-color="#25231f"/>' +
    '<stop offset="0.55" stop-color="#121211"/><stop offset="1" stop-color="#000000"/></radialGradient></defs>' +
    '<rect width="' + W + '" height="' + H + '" fill="url(#vig)"/>' +
    '<rect width="' + W + '" height="' + H + '" filter="url(#fog)" opacity="0.14"/>';
}

/* Shrink any text marked data-fit until it is at most that many px wide. */
function fitTexts(svg) {
  var list = svg.querySelectorAll('text[data-fit]');
  for (var i = 0; i < list.length; i++) {
    var el = list[i], max = Number(el.getAttribute('data-fit'));
    var size = Number(el.getAttribute('font-size'));
    while (el.getComputedTextLength() > max && size > 14) {
      size -= 1;
      el.setAttribute('font-size', size);
    }
  }
}

function mount(render) {
  document.body.style.cssText = 'margin:0;background:' + BG + ';overflow:hidden';
  var holder = document.createElement('div');
  document.body.appendChild(holder);
  window.__setTime = function (t) {
    holder.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="' + W + '" height="' + H + '" viewBox="0 0 ' + W + ' ' + H + '"' +
      ' font-family="' + FONT.replace(/"/g, "'") + '" style="display:block;font-stretch:condensed">' +
      '<rect width="' + W + '" height="' + H + '" fill="' + BG + '"/>' + backdrop() + render(t) + '</svg>';
    fitTexts(holder.firstChild);
  };
  /* live preview when opened by hand: ?t=3.2 freezes, otherwise loops */
  var m = /[?&]t=([\d.]+)/.exec(location.search);
  if (m) { window.__setTime(Number(m[1])); return; }
  if (/[?&]play/.test(location.search)) {
    var t0 = performance.now();
    (function tick() { window.__setTime(((performance.now() - t0) / 1000) % window.__duration); requestAnimationFrame(tick); })();
  } else {
    window.__setTime(0);
  }
}

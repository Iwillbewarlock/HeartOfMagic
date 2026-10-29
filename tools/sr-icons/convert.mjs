// Converts Spell Research Prisma's archetype icons (tools/icons/skald-icons.js,
// SVG markup on a 24-unit grid) into polyline data for the AS2 drawing API:
// plugins/SL_MagicMenu_SpellResearch/src/ArchetypeIcons.h.
//
//   node tools/sr-icons/convert.mjs <skald-icons.js copy> <source commit> [out.h]
//
// No npm dependencies. The icon file runs in a node:vm context that holds only
// an empty `window` object (no require, process or globals of this script);
// node:vm is not a security boundary, so only feed it the SR Prisma file.
// Anything in the SVG that this converter does not understand stops it with an
// error naming the icon, the element and the attribute.

import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath, pathToFileURL } from 'node:url';

export const TOLERANCE = 0.05;        // max distance, grid units, flattened polyline vs true curve
export const GRID = 24;
export const STROKE_WIDTH = 1.3;
export const DECIMALS = 2;
// Archetypes 2-35 elements, 41-44 targeting, 45-59 techniques. 0-1 casting and
// 36-40 schools (PNG sigils traced from UESP) are not converted.
export const CONVERTED = [...range(2, 35), ...range(41, 59)];
export const ICON_COUNT = 60;

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_OUT = path.resolve(HERE, '../../plugins/SL_MagicMenu_SpellResearch/src/ArchetypeIcons.h');

function range(a, b) { const r = []; for (let i = a; i <= b; i++) { r.push(i); } return r; }

class ConvertError extends Error {}
function fail(ctx, msg) { throw new ConvertError(`${ctx}: ${msg}`); }

// ---------------------------------------------------------------- loading

/** Runs skald-icons.js in a bare vm context and returns its SKALD_ICONS object. */
export function loadIcons(jsPath) {
  const code = fs.readFileSync(jsPath, 'utf8');
  const sandbox = { window: {} };
  vm.runInNewContext(code, sandbox, { filename: path.basename(jsPath), timeout: 1000 });
  const icons = sandbox.window.SKALD_ICONS;
  if (!icons || typeof icons.archetype !== 'function' || icons.count !== ICON_COUNT) {
    throw new ConvertError(`${jsPath}: expected window.SKALD_ICONS with archetype() and count ${ICON_COUNT}, got ${icons ? icons.count : 'nothing'}`);
  }
  return icons;
}

/** The SVG markup atlas.html draws for archetype i (frame + glyph). */
export function iconSvg(icons, i, px = GRID) { return String(icons.archetype(i, px)); }

// ---------------------------------------------------------------- SVG parsing

const ELEMENT_ATTRS = {
  svg: ['xmlns', 'viewBox', 'width', 'height', 'fill', 'stroke', 'stroke-width', 'stroke-linecap', 'stroke-linejoin'],
  path: ['d'],
  circle: ['cx', 'cy', 'r'],
  ellipse: ['cx', 'cy', 'rx', 'ry'],
  rect: ['x', 'y', 'width', 'height', 'rx', 'ry'],
  line: ['x1', 'y1', 'x2', 'y2'],
  polyline: ['points'],
  polygon: ['points'],
};
const STYLE_ATTRS = ['fill', 'stroke', 'stroke-dasharray', 'transform', 'stroke-width', 'stroke-linecap', 'stroke-linejoin'];

/** Tokenizes the flat markup skald-icons.js produces into {name, attrs} elements. */
export function parseSvg(markup, ctx) {
  const tagRe = /<\s*(\/?)([a-zA-Z][\w-]*)((?:\s+[\w:-]+\s*=\s*"[^"]*")*)\s*(\/?)>/g;
  const out = [];
  let pos = 0;
  let m;
  while ((m = tagRe.exec(markup))) {
    if (markup.slice(pos, m.index).trim()) { fail(ctx, `unexpected text "${markup.slice(pos, m.index).trim().slice(0, 40)}"`); }
    pos = tagRe.lastIndex;
    const [, closing, name, attrText] = m;
    if (closing) { if (name !== 'svg') { fail(ctx, `unexpected </${name}>`); } continue; }
    const allowed = ELEMENT_ATTRS[name];
    if (!allowed) { fail(ctx, `unsupported element <${name}>`); }
    const attrs = {};
    for (const a of attrText.matchAll(/([\w:-]+)\s*=\s*"([^"]*)"/g)) {
      if (!allowed.includes(a[1]) && !STYLE_ATTRS.includes(a[1])) { fail(ctx, `unsupported attribute ${a[1]} on <${name}>`); }
      attrs[a[1]] = a[2];
    }
    out.push({ name, attrs });
  }
  if (markup.slice(pos).trim()) { fail(ctx, `unexpected trailing text`); }
  if (!out.length || out[0].name !== 'svg') { fail(ctx, 'markup does not start with <svg>'); }
  return out;
}

function num(v, ctx, what) {
  const n = Number(v);
  if (v === undefined || v === '' || !Number.isFinite(n)) { fail(ctx, `${what} is not a number: "${v}"`); }
  return n;
}

function parseNumberList(s, ctx) {
  const toks = s.match(NUM_RE_G) || [];
  if (s.replace(NUM_RE_G, '').replace(/[\s,]/g, '')) { fail(ctx, `bad number list "${s}"`); }
  return toks.map(Number);
}
const NUM_SRC = '[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?';
const NUM_RE_G = new RegExp(NUM_SRC, 'g');

/** transform attribute -> affine [a b c d e f] (x' = a x + c y + e). */
function parseTransform(s, ctx) {
  let mtx = [1, 0, 0, 1, 0, 0];
  const re = /\s*(rotate|translate|scale|matrix)\s*\(([^)]*)\)\s*,?/gy;
  let m;
  let consumed = 0;
  while ((m = re.exec(s))) {
    consumed = re.lastIndex;
    const a = parseNumberList(m[2], ctx);
    let t;
    if (m[1] === 'rotate' && (a.length === 1 || a.length === 3)) {
      const r = a[0] * Math.PI / 180, c = Math.cos(r), sn = Math.sin(r);
      const [cx, cy] = a.length === 3 ? [a[1], a[2]] : [0, 0];
      t = [c, sn, -sn, c, cx - c * cx + sn * cy, cy - sn * cx - c * cy];
    } else if (m[1] === 'translate' && (a.length === 1 || a.length === 2)) {
      t = [1, 0, 0, 1, a[0], a[1] || 0];
    } else if (m[1] === 'scale' && (a.length === 1 || a.length === 2)) {
      t = [a[0], 0, 0, a.length === 2 ? a[1] : a[0], 0, 0];
    } else if (m[1] === 'matrix' && a.length === 6) {
      t = a;
    } else {
      fail(ctx, `unsupported transform "${m[0].trim()}"`);
    }
    mtx = mul(mtx, t);
  }
  if (s.slice(consumed).trim()) { fail(ctx, `unsupported transform "${s}"`); }
  return mtx;
}
function mul(p, q) {
  return [p[0] * q[0] + p[2] * q[1], p[1] * q[0] + p[3] * q[1], p[0] * q[2] + p[2] * q[3], p[1] * q[2] + p[3] * q[3],
    p[0] * q[4] + p[2] * q[5] + p[4], p[1] * q[4] + p[3] * q[5] + p[5]];
}
function apply(m, [x, y]) { return [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]; }

// ---------------------------------------------------------------- geometry

// A "segment" is a parametric piece of an element's outline: {kind:'line'|'cubic'|'quad'|'arc', ...}.
// A "contour" is {closed, segs[]} in local (pre-transform) coordinates.

function evalSeg(s, t) {
  if (s.kind === 'line') { return [s.p0[0] + (s.p1[0] - s.p0[0]) * t, s.p0[1] + (s.p1[1] - s.p0[1]) * t]; }
  if (s.kind === 'quad') {
    const u = 1 - t;
    return [u * u * s.p0[0] + 2 * u * t * s.c[0] + t * t * s.p1[0], u * u * s.p0[1] + 2 * u * t * s.c[1] + t * t * s.p1[1]];
  }
  if (s.kind === 'cubic') {
    const u = 1 - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    return [a * s.p0[0] + b * s.c1[0] + c * s.c2[0] + d * s.p1[0], a * s.p0[1] + b * s.c1[1] + c * s.c2[1] + d * s.p1[1]];
  }
  // arc: centre, radii, x-axis rotation phi, start angle th0, sweep dth
  const th = s.th0 + s.dth * t, ct = Math.cos(th), st = Math.sin(th), cp = Math.cos(s.phi), sp = Math.sin(s.phi);
  return [s.cx + s.rx * ct * cp - s.ry * st * sp, s.cy + s.rx * ct * sp + s.ry * st * cp];
}

function distToSeg(p, a, b) {
  const dx = b[0] - a[0], dy = b[1] - a[1], l2 = dx * dx + dy * dy;
  let t = l2 ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2 : 0;
  t = Math.max(0, Math.min(1, t));
  return Math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy);
}

/** Flattens one segment to points (excluding its start point), deviation <= tol. */
function flattenSeg(s, tol) {
  if (s.kind === 'line') { return [s.p1]; }
  const out = [];
  if (s.kind === 'arc') {
    // Sagitta of a chord spanning dθ on a circle of radius R is R(1-cos(dθ/2)); R = max radius bounds the ellipse.
    const R = Math.max(s.rx, s.ry);
    const step = tol >= R ? Math.PI / 2 : 2 * Math.acos(1 - tol / R);
    const n = Math.max(2, Math.ceil(Math.abs(s.dth) / step));
    for (let i = 1; i <= n; i++) { out.push(evalSeg(s, i / n)); }
    return out;
  }
  // Béziers: recursive split until the control polygon lies within tol of the chord
  // (the curve is inside the control hull, so this bounds the deviation).
  const ctrl = s.kind === 'quad' ? [s.p0, s.c, s.p1] : [s.p0, s.c1, s.c2, s.p1];
  const rec = (pts, depth) => {
    const a = pts[0], b = pts[pts.length - 1];
    const flat = pts.slice(1, -1).every((p) => distToSeg(p, a, b) <= tol);
    if (flat || depth > 16) { out.push(b); return; }
    const [l, r] = splitBezier(pts);
    rec(l, depth + 1);
    rec(r, depth + 1);
  };
  rec(ctrl, 0);
  return out;
}
function splitBezier(pts) {
  const left = [pts[0]], right = [pts[pts.length - 1]];
  let cur = pts;
  while (cur.length > 1) {
    const nxt = [];
    for (let i = 0; i < cur.length - 1; i++) { nxt.push([(cur[i][0] + cur[i + 1][0]) / 2, (cur[i][1] + cur[i + 1][1]) / 2]); }
    left.push(nxt[0]);
    right.unshift(nxt[nxt.length - 1]);
    cur = nxt;
  }
  return [left, right];
}

/** SVG arc endpoint parameterisation -> centre form (SVG 1.1 appendix F.6.5/F.6.6). */
function arcSeg(p0, rx, ry, phiDeg, large, sweep, p1, ctx) {
  if (p0[0] === p1[0] && p0[1] === p1[1]) { return null; }
  rx = Math.abs(rx); ry = Math.abs(ry);
  if (!rx || !ry) { return { kind: 'line', p0, p1 }; }
  const phi = phiDeg * Math.PI / 180, cp = Math.cos(phi), sp = Math.sin(phi);
  const dx = (p0[0] - p1[0]) / 2, dy = (p0[1] - p1[1]) / 2;
  const x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
  const lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
  if (lam > 1) { const k = Math.sqrt(lam); rx *= k; ry *= k; }
  const num_ = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  const den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  let co = Math.sqrt(Math.max(0, num_ / den));
  if (large === sweep) { co = -co; }
  const cxp = co * rx * y1 / ry, cyp = -co * ry * x1 / rx;
  const cx = cp * cxp - sp * cyp + (p0[0] + p1[0]) / 2, cy = sp * cxp + cp * cyp + (p0[1] + p1[1]) / 2;
  const ang = (ux, uy, vx, vy) => Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  const th0 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
  let dth = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
  if (!sweep && dth > 0) { dth -= 2 * Math.PI; }
  if (sweep && dth < 0) { dth += 2 * Math.PI; }
  if (!Number.isFinite(cx + cy + th0 + dth)) { fail(ctx, 'arc could not be solved'); }
  return { kind: 'arc', p0, p1, cx, cy, rx, ry, phi, th0, dth };
}

/** Path data -> contours. Supports M L H V C S Q T A Z, absolute and relative. */
export function parsePathData(d, ctx) {
  const toks = [];
  const re = new RegExp(`([MmLlHhVvCcSsQqTtAaZz])|(${NUM_SRC})|([\\s,]+)|(.)`, 'g');
  for (const m of d.matchAll(re)) {
    if (m[4] !== undefined) { fail(ctx, `unsupported path data character "${m[4]}" in "${d}"`); }
    if (m[1]) { toks.push(m[1]); } else if (m[2]) { toks.push(m[2]); }
  }
  const contours = [];
  let cur = null;
  let p = [0, 0], start = [0, 0], lastC = null, lastQ = null, cmd = null, i = 0;
  const isCmd = (t) => /^[A-Za-z]$/.test(t);
  const take = () => {
    if (i >= toks.length || isCmd(toks[i])) { fail(ctx, `missing number after ${cmd} in "${d}"`); }
    return Number(toks[i++]);
  };
  const flag = () => {
    // Arc flags may be written packed ("011"); split one digit off a token if so.
    if (i >= toks.length || isCmd(toks[i])) { fail(ctx, `missing arc flag in "${d}"`); }
    const t = toks[i];
    if (t === '0' || t === '1') { i++; return Number(t); }
    if (/^[01]\d/.test(t)) { toks[i] = t.slice(1); return Number(t[0]); }
    fail(ctx, `bad arc flag "${t}" in "${d}"`);
  };
  const push = (seg) => { if (seg) { cur.segs.push(seg); } };
  const ensure = () => { if (!cur) { cur = { closed: false, segs: [] }; contours.push(cur); start = p.slice(); } };
  while (i < toks.length) {
    if (isCmd(toks[i])) { cmd = toks[i++]; } else if (!cmd) { fail(ctx, `path data does not start with a command: "${d}"`); }
    const rel = cmd === cmd.toLowerCase() && cmd !== 'z';
    const C = cmd.toUpperCase();
    const pt = () => { const x = take(), y = take(); return rel ? [p[0] + x, p[1] + y] : [x, y]; };
    let nextC = null, nextQ = null;
    if (C === 'M') {
      p = pt();
      cur = { closed: false, segs: [] }; contours.push(cur); start = p.slice();
      cmd = rel ? 'l' : 'L';
    } else if (C === 'Z') {
      if (cur) { if (p[0] !== start[0] || p[1] !== start[1]) { push({ kind: 'line', p0: p, p1: start.slice() }); } cur.closed = true; }
      p = start.slice(); cur = null; cmd = null;
      continue;
    } else {
      ensure();
      if (C === 'L') { const q = pt(); push({ kind: 'line', p0: p, p1: q }); p = q; }
      else if (C === 'H') { const x = take(); const q = [rel ? p[0] + x : x, p[1]]; push({ kind: 'line', p0: p, p1: q }); p = q; }
      else if (C === 'V') { const y = take(); const q = [p[0], rel ? p[1] + y : y]; push({ kind: 'line', p0: p, p1: q }); p = q; }
      else if (C === 'C') { const c1 = pt(), c2 = pt(), q = pt(); push({ kind: 'cubic', p0: p, c1, c2, p1: q }); nextC = c2; p = q; }
      else if (C === 'S') {
        const c1 = lastC ? [2 * p[0] - lastC[0], 2 * p[1] - lastC[1]] : p.slice();
        const c2 = pt(), q = pt(); push({ kind: 'cubic', p0: p, c1, c2, p1: q }); nextC = c2; p = q;
      } else if (C === 'Q') { const c = pt(), q = pt(); push({ kind: 'quad', p0: p, c, p1: q }); nextQ = c; p = q; }
      else if (C === 'T') {
        const c = lastQ ? [2 * p[0] - lastQ[0], 2 * p[1] - lastQ[1]] : p.slice();
        const q = pt(); push({ kind: 'quad', p0: p, c, p1: q }); nextQ = c; p = q;
      } else if (C === 'A') {
        const rx = take(), ry = take(), phi = take(), la = flag(), sw = flag(), q = pt();
        push(arcSeg(p, rx, ry, phi, la, sw, q, ctx)); p = q;
      } else { fail(ctx, `unsupported path command ${cmd}`); }
    }
    lastC = nextC; lastQ = nextQ;
  }
  return contours;
}

function ellipseContour(cx, cy, rx, ry) {
  // SVG's ellipse starts at (cx+rx, cy) and runs toward (cx, cy+ry); dashes depend on this.
  return { closed: true, segs: [{ kind: 'arc', p0: [cx + rx, cy], p1: [cx + rx, cy], cx, cy, rx, ry, phi: 0, th0: 0, dth: 2 * Math.PI }] };
}

function rectContour(x, y, w, h, rx, ry) {
  const L = (a, b) => ({ kind: 'line', p0: a, p1: b });
  if (!rx || !ry) {
    return { closed: true, segs: [L([x, y], [x + w, y]), L([x + w, y], [x + w, y + h]), L([x + w, y + h], [x, y + h]), L([x, y + h], [x, y])] };
  }
  const A = (cx, cy, th0) => ({ kind: 'arc', cx, cy, rx, ry, phi: 0, th0, dth: Math.PI / 2 });
  // Starts at (x+rx, y) clockwise in screen space, like SVG's rect.
  return { closed: true, segs: [
    L([x + rx, y], [x + w - rx, y]), A(x + w - rx, y + ry, -Math.PI / 2),
    L([x + w, y + ry], [x + w, y + h - ry]), A(x + w - rx, y + h - ry, 0),
    L([x + w - rx, y + h], [x + rx, y + h]), A(x + rx, y + h - ry, Math.PI / 2),
    L([x, y + h - ry], [x, y + ry]), A(x + rx, y + ry, Math.PI),
  ] };
}

/** One element -> contours in local coordinates. */
function elementContours(el, ctx) {
  const a = el.attrs, n = (k) => num(a[k], ctx, `<${el.name}> ${k}`), opt = (k) => (a[k] === undefined ? 0 : n(k));
  switch (el.name) {
    case 'path': return parsePathData(a.d || '', ctx);
    case 'circle': return [ellipseContour(opt('cx'), opt('cy'), n('r'), n('r'))];
    case 'ellipse': return [ellipseContour(opt('cx'), opt('cy'), n('rx'), n('ry'))];
    case 'rect': {
      const w = n('width'), h = n('height');
      let rx = a.rx === undefined ? undefined : n('rx'), ry = a.ry === undefined ? undefined : n('ry');
      if (rx === undefined) { rx = ry; } if (ry === undefined) { ry = rx; }
      rx = Math.min(rx || 0, w / 2); ry = Math.min(ry || 0, h / 2);
      return [rectContour(opt('x'), opt('y'), w, h, rx, ry)];
    }
    case 'line': return [{ closed: false, segs: [{ kind: 'line', p0: [opt('x1'), opt('y1')], p1: [opt('x2'), opt('y2')] }] }];
    case 'polyline':
    case 'polygon': {
      const v = parseNumberList(a.points || '', ctx);
      if (v.length < 4 || v.length % 2) { fail(ctx, `<${el.name}> points has ${v.length} numbers`); }
      const segs = [];
      for (let k = 2; k < v.length; k += 2) { segs.push({ kind: 'line', p0: [v[k - 2], v[k - 1]], p1: [v[k], v[k + 1]] }); }
      return [{ closed: el.name === 'polygon', segs }];
    }
    default: return fail(ctx, `unsupported element <${el.name}>`);
  }
}

function contourStart(c) { return c.segs.length ? evalSeg(c.segs[0], 0) : null; }

/** Contour -> polyline points (closed contours do not repeat the first point). */
function flattenContour(c, tol) {
  const s = contourStart(c);
  if (!s) { return []; }
  const pts = [s];
  for (const seg of c.segs) { pts.push(...flattenSeg(seg, tol)); }
  if (c.closed && pts.length > 1 && Math.hypot(pts[0][0] - pts.at(-1)[0], pts[0][1] - pts.at(-1)[1]) < 1e-9) { pts.pop(); }
  return pts;
}

/** Splits a polyline into dashes (SVG: the pattern restarts at each subpath). */
function dashPolyline(pts, closed, pattern) {
  const path_ = closed ? [...pts, pts[0]] : pts;
  const dashes = [];
  let k = 0, left = pattern[0], on = true, cur = [path_[0]];
  for (let i = 1; i < path_.length; i++) {
    let a = path_[i - 1];
    const b = path_[i];
    let segLen = Math.hypot(b[0] - a[0], b[1] - a[1]);
    while (segLen > 1e-12) {
      if (segLen <= left) {
        left -= segLen; if (on) { cur.push(b); } segLen = 0;
      } else {
        const t = left / segLen, q = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t];
        if (on) { cur.push(q); dashes.push(cur); cur = null; } else { cur = [q]; }
        segLen -= left; a = q; on = !on; k = (k + 1) % pattern.length; left = pattern[k];
      }
    }
  }
  if (on && cur && cur.length > 1) { dashes.push(cur); }
  return dashes;
}

// ---------------------------------------------------------------- conversion

function round(v) { const f = 10 ** DECIMALS; const r = Math.round(v * f) / f; return Object.is(r, -0) ? 0 : r; }

function roundPoints(pts, closed) {
  const out = [];
  for (const p of pts) {
    const q = [round(p[0]), round(p[1])];
    if (!out.length || q[0] !== out.at(-1)[0] || q[1] !== out.at(-1)[1]) { out.push(q); }
  }
  if (closed && out.length > 2 && out[0][0] === out.at(-1)[0] && out[0][1] === out.at(-1)[1]) { out.pop(); }
  if (out.length === 1) { out.push(out[0].slice()); }   // zero-length dot: keep two points so the round cap draws
  return out;
}

/**
 * Converts one archetype. Returns { subpaths: [{kind, closed, points}], exact: [[x,y]...] per subpath }
 * where `exact` is the same outline sampled at 1/100 of the tolerance, for the deviation check.
 */
export function convertIcon(icons, i, tol = TOLERANCE) {
  const ctx0 = `archetype ${i}`;
  const els = parseSvg(iconSvg(icons, i), ctx0);
  const root = els[0].attrs;
  if (root.viewBox !== `0 0 ${GRID} ${GRID}`) { fail(ctx0, `viewBox is "${root.viewBox}", expected 0 0 ${GRID} ${GRID}`); }
  if (Number(root['stroke-width']) !== STROKE_WIDTH) { fail(ctx0, `stroke-width is ${root['stroke-width']}, expected ${STROKE_WIDTH}`); }
  if (root['stroke-linecap'] !== 'round' || root['stroke-linejoin'] !== 'round') { fail(ctx0, 'expected round caps and joins'); }
  const subpaths = [], exact = [];
  els.slice(1).forEach((el, ei) => {
    const ctx = `${ctx0} element ${ei} <${el.name}>`;
    const a = el.attrs;
    for (const k of ['stroke-width', 'stroke-linecap', 'stroke-linejoin']) { if (a[k] !== undefined) { fail(ctx, `per-element ${k} is not supported`); } }
    const fill = a.fill ?? root.fill, stroke = a.stroke ?? root.stroke;
    for (const [k, v] of [['fill', fill], ['stroke', stroke]]) {
      if (v !== 'none' && v !== 'currentColor') { fail(ctx, `${k}="${v}" (only none/currentColor; the icon set is one colour)`); }
    }
    const mtx = a.transform ? parseTransform(a.transform, ctx) : null;
    const tf = (pts) => (mtx ? pts.map((p) => apply(mtx, p)) : pts);
    let dash = null;
    if (a['stroke-dasharray'] !== undefined && a['stroke-dasharray'] !== 'none') {
      dash = parseNumberList(a['stroke-dasharray'], ctx);
      if (dash.length % 2) { dash = [...dash, ...dash]; }
      if (!dash.length || dash.some((v) => v < 0) || dash.every((v) => v === 0)) { fail(ctx, `bad stroke-dasharray "${a['stroke-dasharray']}"`); }
      if (mtx) { fail(ctx, 'stroke-dasharray together with transform is not supported'); }
    }
    for (const c of elementContours(el, ctx)) {
      const pts = tf(flattenContour(c, tol));
      const ex = tf(flattenContour(c, tol / 100));
      if (!pts.length) { continue; }
      if (fill !== 'none') { subpaths.push({ kind: 'fill', closed: true, points: roundPoints(pts, true) }); exact.push(ex); }
      if (stroke !== 'none') {
        if (dash) {
          // Each dash is checked against the whole exact outline (a dash's ends move along the path with
          // the flattening, which is not a shape error).
          const exFull = c.closed ? [...ex, ex[0]] : ex;
          for (const d of dashPolyline(pts, c.closed, dash)) { subpaths.push({ kind: 'stroke', closed: false, points: roundPoints(d, false), dashed: true }); exact.push(exFull); }
        } else {
          subpaths.push({ kind: 'stroke', closed: c.closed, points: roundPoints(pts, c.closed) }); exact.push(ex);
        }
      }
    }
  });
  return { subpaths, exact };
}

/** Max distance from each exact sample to the converted polyline, and back (vertices to the exact outline). */
export function deviation(sub, ex) {
  const poly = sub.closed ? [...sub.points, sub.points[0]] : sub.points;
  const exPoly = sub.closed ? [...ex, ex[0]] : ex;
  const toPoly = (p, pl) => { let d = Infinity; for (let k = 1; k < pl.length; k++) { d = Math.min(d, distToSeg(p, pl[k - 1], pl[k])); } return pl.length === 1 ? Math.hypot(p[0] - pl[0][0], p[1] - pl[0][1]) : d; };
  let m = 0;
  if (!sub.dashed) { for (const p of ex) { m = Math.max(m, toPoly(p, poly)); } }
  // Back direction: samples along every converted segment, not only its vertices.
  const SAMPLES = 16;
  for (let k = 1; k < poly.length; k++) {
    for (let j = 0; j <= SAMPLES; j++) {
      const t = j / SAMPLES, a = poly[k - 1], b = poly[k];
      m = Math.max(m, toPoly([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t], exPoly));
    }
  }
  return m;
}

// ---------------------------------------------------------------- header output

function lit(v) {
  const s = String(round(v));
  return s.includes('.') ? `${s}f` : s;
}

export function buildHeader(results, rev) {
  const points = [], subs = [], entries = [], shared = new Map();
  for (let i = 0; i < ICON_COUNT; i++) {
    const r = results.get(i);
    entries.push({ first: subs.length, count: r ? r.subpaths.length : 0, i });
    if (!r) { continue; }
    for (const s of r.subpaths) {
      // Identical point runs (the frames) are stored once and shared by every subpath that uses them.
      const key = JSON.stringify(s.points);
      let first = shared.get(key);
      if (first === undefined) { first = points.length; shared.set(key, first); points.push(...s.points); }
      subs.push({ first, count: s.points.length, closed: s.closed, fill: s.kind === 'fill', i });
    }
  }
  if (points.length > 0xffff || subs.length > 0xffff) { throw new ConvertError(`too many points (${points.length}) or subpaths (${subs.length}) for uint16 indices`); }
  const L = [];
  L.push('#pragma once');
  L.push('');
  L.push('// GENERATED FILE - DO NOT EDIT.');
  L.push('// Generated by tools/sr-icons/convert.mjs from SpellResearchPrismaUI HEAD');
  L.push(`// ${rev} tools/icons/skald-icons.js (Spell Research Prisma's own archetype icons).`);
  L.push(`// Archetypes 2-35 (elements), 41-44 (targeting) and 45-59 (techniques), frame included;`);
  L.push('// 0-1 (casting) and 36-40 (school sigils, PNG images) are empty entries.');
  L.push(`// ${GRID}-unit grid, y down, one colour. Curves are flattened to polylines within ${TOLERANCE} units,`);
  L.push(`// coordinates rounded to ${DECIMALS} decimals; dashed strokes are pre-split into one open subpath per dash.`);
  L.push('// Draw: scale = size / kIconGrid; stroke subpaths with lineStyle(kIconStroke * scale), round caps/joins');
  L.push('// (moveTo first point, lineTo the rest, lineTo the first again when closed); fill subpaths with');
  L.push('// beginFill / endFill and no line. A subpath of two equal points is a dot (zero-length stroke).');
  L.push('// Subpaths with identical points (the group frames) share one run in kPoints.');
  L.push('');
  L.push('#include <array>');
  L.push('#include <cstddef>');
  L.push('#include <cstdint>');
  L.push('');
  L.push('namespace ArchetypeIcons');
  L.push('{');
  L.push(`    inline constexpr float kIconGrid = ${GRID}.0f;`);
  L.push(`    inline constexpr float kIconStroke = ${STROKE_WIDTH}f;`);
  L.push(`    inline constexpr std::size_t kIconCount = ${ICON_COUNT};`);
  L.push('');
  L.push('    struct Point { float x, y; };');
  L.push('    struct Subpath { std::uint16_t first, count; bool closed, fill; };');
  L.push('    struct Icon { std::uint16_t first, count; };  // range in kSubpaths; count 0 = no icon');
  L.push('');
  L.push(`    inline constexpr std::array<Point, ${points.length}> kPoints{ {`);
  for (let k = 0; k < points.length; k += 10) {
    L.push('        ' + points.slice(k, k + 10).map((p) => `{${lit(p[0])},${lit(p[1])}}`).join(',') + ',');
  }
  L.push('    } };');
  L.push('');
  L.push(`    inline constexpr std::array<Subpath, ${subs.length}> kSubpaths{ {`);
  let curIcon = -1, line = [];
  const flush = () => { if (line.length) { L.push(`        ${line.join(',')},  // ${curIcon}`); line = []; } };
  for (const s of subs) {
    if (s.i !== curIcon || line.length >= 8) { flush(); curIcon = s.i; }
    line.push(`{${s.first},${s.count},${s.closed},${s.fill}}`);
  }
  flush();
  L.push('    } };');
  L.push('');
  L.push(`    inline constexpr std::array<Icon, kIconCount> kIcons{ {`);
  for (let k = 0; k < entries.length; k += 10) {
    L.push('        ' + entries.slice(k, k + 10).map((e) => `{${e.first},${e.count}}`).join(',') + `,  // ${k}-${Math.min(k + 9, ICON_COUNT - 1)}`);
  }
  L.push('    } };');
  L.push('}');
  L.push('');
  return { text: L.join('\n'), pointCount: points.length, subpathCount: subs.length, pointRefs: subs.reduce((n, s) => n + s.count, 0) };
}

// ---------------------------------------------------------------- main

function main(argv) {
  const [jsPath, rev, outArg] = argv;
  if (!jsPath || !rev || !/^[0-9a-f]{7,40}$/.test(rev)) {
    console.error('usage: node tools/sr-icons/convert.mjs <copy of skald-icons.js> <SpellResearchPrismaUI commit hash> [out.h]');
    process.exit(2);
  }
  const icons = loadIcons(jsPath);
  const results = new Map();
  const rows = [];
  let worst = 0;
  for (const i of CONVERTED) {
    const r = convertIcon(icons, i);
    results.set(i, r);
    const dev = Math.max(0, ...r.subpaths.map((s, k) => deviation(s, r.exact[k])));
    worst = Math.max(worst, dev);
    const fills = r.subpaths.filter((s) => s.kind === 'fill').length;
    rows.push(`${String(i).padStart(2)}  subpaths ${String(r.subpaths.length).padStart(3)}  fill ${fills}  points ${String(r.subpaths.reduce((n, s) => n + s.points.length, 0)).padStart(4)}  max dev ${dev.toFixed(4)}`);
  }
  const out = path.resolve(outArg || DEFAULT_OUT);
  const hdr = buildHeader(results, rev);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, hdr.text);
  console.log(rows.join('\n'));
  console.log(`icons ${results.size}, subpaths ${hdr.subpathCount}, points stored ${hdr.pointCount} (${hdr.pointRefs} drawn; identical runs shared), worst deviation ${worst.toFixed(4)} (tolerance ${TOLERANCE} + rounding)`);
  console.log(`wrote ${out} (${Buffer.byteLength(hdr.text)} bytes)`);
  if (worst > TOLERANCE + 0.5 * 10 ** -DECIMALS * Math.SQRT2 + 1e-9) {
    console.error(`deviation ${worst} exceeds tolerance plus rounding`);
    process.exit(1);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try { main(process.argv.slice(2)); } catch (e) {
    if (e instanceof ConvertError) { console.error(`convert failed: ${e.message}`); process.exit(1); }
    throw e;
  }
}

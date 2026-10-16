/* Nordic knotwork for the Skald pictures: strands as sampled polylines, woven over-under.
   Each crossing is found numerically; along every strand the crossings alternate over/under
   (true for the trefoil and a two-strand plait, both alternating), and the over strand is
   redrawn with a page-coloured edge so the under strand shows a gap. */
var KNOT_PAGE = '#0b0b0c';

function kPath(pts) {
  var d = 'M' + pts[0][0].toFixed(2) + ',' + pts[0][1].toFixed(2);
  for (var i = 1; i < pts.length; i++) d += ' L' + pts[i][0].toFixed(2) + ',' + pts[i][1].toFixed(2);
  return d;
}

/* Trefoil knot centred on (cx, cy), outer radius about r, turned by rot radians. */
function trefoil(cx, cy, r, rot) {
  var pts = [], n = 360, k = r / 3.05;
  for (var i = 0; i <= n; i++) {
    var t = i / n * Math.PI * 2;
    var x = Math.sin(t) + 2 * Math.sin(2 * t), y = Math.cos(t) - 2 * Math.cos(2 * t);
    var c = Math.cos(rot || 0), s = Math.sin(rot || 0);
    pts.push([cx + k * (x * c - y * s), cy + k * (x * s + y * c)]);
  }
  return { pts: pts, closed: true };
}

/* Two-strand plait from (x1, y1) to (x2, y2): sine strands half a turn apart. */
function plait(x1, y1, x2, y2, amp, wave) {
  var len = Math.hypot(x2 - x1, y2 - y1), ux = (x2 - x1) / len, uy = (y2 - y1) / len;
  var out = [], ph;
  for (ph = 0; ph < 2; ph++) {
    var pts = [], n = Math.max(40, Math.round(len * 1.5));
    for (var i = 0; i <= n; i++) {
      var a = i / n * len, o = amp * Math.sin(a / wave * Math.PI * 2 + ph * Math.PI);
      pts.push([x1 + ux * a - uy * o, y1 + uy * a + ux * o]);
    }
    out.push({ pts: pts, closed: false });
  }
  return out;
}

function segHit(p, q, r, s) {
  var d = (q[0] - p[0]) * (s[1] - r[1]) - (q[1] - p[1]) * (s[0] - r[0]);
  if (Math.abs(d) < 1e-9) return null;
  var t = ((r[0] - p[0]) * (s[1] - r[1]) - (r[1] - p[1]) * (s[0] - r[0])) / d;
  var u = ((r[0] - p[0]) * (q[1] - p[1]) - (r[1] - p[1]) * (q[0] - p[0])) / d;
  return (t >= 0 && t < 1 && u >= 0 && u < 1) ? [t, u] : null;
}

/* Woven SVG for a set of strands: stroke colour, width, gap (page edge each side of an over strand). */
function weave(strands, col, w, gap, opacity) {
  var visits = [], crossings = [], si, sj, i, j;
  for (si = 0; si < strands.length; si++) visits.push([]);
  for (si = 0; si < strands.length; si++) for (sj = si; sj < strands.length; sj++) {
    var A = strands[si].pts, B = strands[sj].pts;
    for (i = 0; i < A.length - 1; i++) for (j = (si === sj ? i + 2 : 0); j < B.length - 1; j++) {
      if (si === sj && strands[si].closed && i === 0 && j === B.length - 2) continue;
      var h = segHit(A[i], A[i + 1], B[j], B[j + 1]);
      if (!h) continue;
      var c = { a: { s: si, at: i + h[0] }, b: { s: sj, at: j + h[1] } };
      crossings.push(c);
      visits[si].push({ c: c, at: c.a.at, side: 'a' });
      visits[sj].push({ c: c, at: c.b.at, side: 'b' });
    }
  }
  for (si = 0; si < strands.length; si++) {
    visits[si].sort(function (x, y) { return x.at - y.at; });
    for (i = 0; i < visits[si].length; i++) if (i % 2 === 0) visits[si][i].c.over = visits[si][i].side;
  }
  var out = '<g fill="none" stroke-linecap="round" stroke-linejoin="round" opacity="' + (opacity === undefined ? 1 : opacity) + '">';
  for (si = 0; si < strands.length; si++) out += '<path d="' + kPath(strands[si].pts) + '" stroke="' + col + '" stroke-width="' + w + '"/>';
  for (i = 0; i < crossings.length; i++) {
    var v = crossings[i][crossings[i].over || 'a'], P = strands[v.s].pts, piece = [], span = 3 * w + 2 * gap;
    /* the over strand's piece round the crossing, about span long on each side */
    var acc = 0, k = Math.floor(v.at);
    for (j = k; j > 0 && acc < span; j--) { acc += Math.hypot(P[j][0] - P[j - 1][0], P[j][1] - P[j - 1][1]); }
    var from = j; acc = 0;
    for (j = k + 1; j < P.length - 1 && acc < span; j++) { acc += Math.hypot(P[j + 1][0] - P[j][0], P[j + 1][1] - P[j][1]); }
    piece = P.slice(from, j + 1);
    out += '<path d="' + kPath(piece) + '" stroke="' + KNOT_PAGE + '" stroke-width="' + (w + 2 * gap) + '" stroke-linecap="butt"/>';
    out += '<path d="' + kPath(piece) + '" stroke="' + col + '" stroke-width="' + w + '"/>';
  }
  return out + '</g>';
}

/* A small open ring, the end cap of a rule (in place of the menu's diamond). */
function ringCap(cx, cy, r, col, o) {
  return '<circle cx="' + cx + '" cy="' + cy + '" r="' + r + '" fill="none" stroke="' + col + '" stroke-width="1.5" opacity="' + (o === undefined ? 1 : o) + '"/>';
}

/**
 * NodeBatch - draws many plain spells with a few paint calls.
 *
 * Drawing each spell on its own (save, translate, rotate, scale, fill, dash,
 * stroke, restore) cost three to five paint calls a spell, and a tree has well
 * over a thousand. Most of them look like many others (locked, undiscovered,
 * plainly known), so they are collected here instead: one path per look
 * (fill, outline, width, alpha, dashed), filled and stroked once. Spells with
 * anything of their own (XP rings, selection, hover, learning) are still drawn
 * one by one, after the batch. Spells with a lock look (the grey shell of a
 * locked spell with hard prerequisites, the grey ring round a known one) are
 * batched too: in the game's trees nearly every spell has one, so leaving them
 * out left almost the whole tree to be drawn spell by spell.
 *
 * The shapes are the school shapes of the tree (CanvasRenderer builds its
 * Path2D cache from SHAPES), turned the same way: flat edge toward the centre.
 *
 * Usage (world coordinates, inside the tree's transform):
 *   NodeBatch.begin();
 *   NodeBatch.addHalo(x, y, radius, color, alpha);   // TreeStyle.drawHalo, under everything
 *   NodeBatch.addShape(school, x, y, size, fill, stroke, alpha, dashed, width, layer, bare);
 *   NodeBatch.addMark(x, y, color, alpha);            // the "?" on undiscovered spells
 *   NodeBatch.flush(ctx, rotationDeg);
 */

var NodeBatch = {

    // Unit shapes, first point first (the dash pattern starts there)
    SHAPES: {
        Destruction: [[0, -1], [1, 0], [0, 1], [-1, 0]],                       // diamond
        Alteration: [[0, -1], [0.9, -0.5], [0.9, 0.5], [0, 1], [-0.9, 0.5], [-0.9, -0.5]],  // hexagon
        Conjuration: (function() {                                             // pentagon
            var pts = [];
            for (var i = 0; i < 5; i++) {
                var a = (i * 72 - 90) * Math.PI / 180;
                pts.push([Math.cos(a), Math.sin(a)]);
            }
            return pts;
        })(),
        Illusion: [[0, 1], [-0.85, -0.6], [0.85, -0.6]]                         // triangle, tip inward
    },

    // Added to the angle toward the centre so a flat edge faces it (circles need none)
    ROTATION: {
        Destruction: Math.PI / 4,
        Alteration: Math.PI / 6,
        Conjuration: Math.PI / 2,
        Illusion: Math.PI / 2
    },

    DASH: [0.5, 0.4],          // locked / undiscovered outline, in shape units
    MARK_FONT: '10px sans-serif',

    LAYERS: 3,                 // 0 = under the body (lock shell, lock ring), 1 = spell bodies,
                               // 2 = what sits on them (the known-spell centre, the lock hole)

    _halos: null,              // [x, y, radius, color, alpha, ...]
    _shapes: null,             // key -> {fill, stroke, alpha, width, dash, path}
    _shapeOrder: null,         // per layer, in the order the looks came
    _marks: null,              // key -> {color, alpha, pts}
    _markOrder: null,

    _unturned: false,          // this batch: shapes not turned toward the centre (simple level of detail)

    /** The rotation a school's shape gets at (x, y): 0 for circles. */
    rotationAt: function(school, x, y) {
        if (this._unturned) return 0;
        var off = this.ROTATION[school];
        return off ? Math.atan2(y, x) + off : 0;
    },

    /** A Path2D of the school's unit shape (the circle for unknown schools). */
    unitPath: function(school) {
        var p = new Path2D();
        var pts = this.SHAPES[school];
        if (!pts) {
            p.arc(0, 0, 1, 0, Math.PI * 2);
            return p;
        }
        p.moveTo(pts[0][0], pts[0][1]);
        for (var i = 1; i < pts.length; i++) p.lineTo(pts[i][0], pts[i][1]);
        p.closePath();
        return p;
    },

    /** unturned: the shapes stay as drawn (CanvasRenderer's simple level of detail does not turn them). */
    begin: function(unturned) {
        this._unturned = unturned === true;
        this._halos = [];
        this._shapes = {};
        this._shapeOrder = [];
        for (var i = 0; i < this.LAYERS; i++) this._shapeOrder.push([]);
        this._marks = {};
        this._markOrder = [];
    },

    /** Queue a glow sprite (TreeStyle.drawHalo) centred on (x, y). */
    addHalo: function(x, y, radius, color, alpha) {
        if (alpha > 0) this._halos.push(x, y, radius, color, alpha);
    },

    /**
     * Queue one spell's shape; spells with the same look share one path.
     * stroke null = fill only; width is the outline in world units (default 1);
     * layer 0-2 (default 1, see LAYERS), each drawn over all of the one below.
     * A see-through shape in layer 0 or 1 is first filled with the backdrop
     * (see flush) unless `bare` (the lock ring: the lines show through it).
     */
    addShape: function(school, x, y, size, fill, stroke, alpha, dashed, width, layer, bare) {
        width = width || 1;
        if (layer === undefined) layer = 1;
        var key = layer + '|' + fill + '|' + stroke + '|' + width + '|' + alpha + '|' + (dashed ? size : '') + (bare ? '|b' : '');
        var b = this._shapes[key];
        if (!b) {
            b = this._shapes[key] = {
                fill: fill, stroke: stroke, alpha: alpha, width: width, bare: !!bare,
                dash: dashed ? [this.DASH[0] * size, this.DASH[1] * size] : null,
                path: new Path2D()
            };
            this._shapeOrder[layer].push(b);
        }
        var path = b.path;
        var pts = this.SHAPES[school];
        if (!pts) {
            path.moveTo(x + size, y);
            path.arc(x, y, size, 0, Math.PI * 2);
            return;
        }
        var rot = this.rotationAt(school, x, y);
        var c = Math.cos(rot) * size, s = Math.sin(rot) * size;
        path.moveTo(x + pts[0][0] * c - pts[0][1] * s, y + pts[0][0] * s + pts[0][1] * c);
        for (var i = 1; i < pts.length; i++) {
            path.lineTo(x + pts[i][0] * c - pts[i][1] * s, y + pts[i][0] * s + pts[i][1] * c);
        }
        path.closePath();
    },

    /** Queue a screen-upright "?" at (x, y). */
    addMark: function(x, y, color, alpha) {
        var key = color + '|' + alpha;
        var m = this._marks[key];
        if (!m) {
            m = this._marks[key] = { color: color, alpha: alpha, pts: [] };
            this._markOrder.push(m);
        }
        m.pts.push(x, y);
    },

    /**
     * Draw everything queued: halos, shapes by layer, then marks. rotationDeg is
     * the tree's turn (marks stay upright). backdrop: a see-through body is first
     * filled with this, opaque, so the lines under it do not show through.
     */
    flush: function(ctx, rotationDeg, backdrop) {
        if (!this._shapeOrder) return;
        var i, j;
        ctx.save();
        for (i = 0; i < this._halos.length; i += 5) {
            var hx = this._halos[i], hy = this._halos[i + 1];
            ctx.translate(hx, hy);
            TreeStyle.drawHalo(ctx, this._halos[i + 2], this._halos[i + 3], this._halos[i + 4]);
            ctx.translate(-hx, -hy);
        }
        var dashed = false;
        for (var l = 0; l < this._shapeOrder.length; l++) {
            var order = this._shapeOrder[l];
            for (i = 0; i < order.length; i++) {
                var b = order[i];
                if (backdrop && l < 2 && !b.bare && b.alpha < 1) {
                    ctx.globalAlpha = 1;
                    ctx.fillStyle = backdrop;
                    ctx.fill(b.path);
                }
                ctx.globalAlpha = b.alpha;
                ctx.fillStyle = b.fill;
                ctx.fill(b.path);
                if (!b.stroke) continue;
                ctx.strokeStyle = b.stroke;
                ctx.lineWidth = b.width;
                if (b.dash || dashed) ctx.setLineDash(b.dash || []);
                dashed = !!b.dash;
                ctx.stroke(b.path);
            }
        }
        if (dashed) ctx.setLineDash([]);

        if (this._markOrder.length) {
            // Turned back by the tree's rotation: a point p is drawn at R(rot)p
            var rot = (rotationDeg || 0) * Math.PI / 180;
            var cos = Math.cos(rot), sin = Math.sin(rot);
            ctx.rotate(-rot);
            ctx.font = this.MARK_FONT;
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            for (i = 0; i < this._markOrder.length; i++) {
                var m = this._markOrder[i];
                ctx.globalAlpha = m.alpha;
                ctx.fillStyle = m.color;
                for (j = 0; j < m.pts.length; j += 2) {
                    var x = m.pts[j], y = m.pts[j + 1];
                    ctx.fillText('?', x * cos - y * sin, x * sin + y * cos);
                }
            }
        }
        ctx.restore();
        this._halos = this._shapes = this._shapeOrder = this._marks = this._markOrder = null;
        this._unturned = false;
    }
};

window.NodeBatch = NodeBatch;

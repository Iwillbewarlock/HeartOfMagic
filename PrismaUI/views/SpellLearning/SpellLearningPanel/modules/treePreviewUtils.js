/**
 * Tree Preview — Shared Utilities
 *
 * Provides reusable UI components for tree preview settings panels:
 * - Drag-to-slide number inputs with +/- buttons
 * - Setting row HTML generation
 * - Canvas size and fit shared by the previews (boxSize, fitCanvas,
 *   contentExtent, fitScale) and where a school's name goes (schoolLabelPlace)
 *
 * Must load BEFORE treePreviewSun.js and treePreviewFlat.js.
 */

var TreePreviewUtils = {

    /**
     * Generate HTML for a setting row with drag input
     * @param {string} label - Display label
     * @param {string} id - Input element ID
     * @param {number} min
     * @param {number} max
     * @param {number} step
     * @param {number} value - Current value
     * @param {string} [suffix] - Optional suffix like '%' or '°'
     */
    settingHTML: function(label, id, min, max, step, value, suffix) {
        return '<div class="tree-preview-setting">' +
            '<label class="tree-preview-label">' + label + '</label>' +
            '<div class="tree-preview-drag-input">' +
                '<button class="drag-input-btn drag-input-minus" data-for="' + id + '">\u2212</button>' +
                '<input type="number" class="drag-input-field" id="' + id + '" ' +
                    'min="' + min + '" max="' + max + '" step="' + (step || 1) + '" value="' + value + '">' +
                '<button class="drag-input-btn drag-input-plus" data-for="' + id + '">+</button>' +
                (suffix ? '<span class="drag-input-suffix">' + suffix + '</span>' : '') +
            '</div>' +
        '</div>';
    },

    /**
     * Bind a drag input: +/- buttons, direct typing, and drag-to-slide
     * @param {string} id - Input element ID
     * @param {function} onChange - Called with (newValue) on change
     */
    bindInput: function(id, onChange) {
        var field = document.getElementById(id);
        if (!field) return;
        var min = parseFloat(field.min) || 0;
        var max = parseFloat(field.max) || 100;
        var step = parseFloat(field.step) || 1;
        var sensitivity = step * 0.3;

        function clamp(v) {
            return Math.max(min, Math.min(max, v));
        }

        function snap(v) {
            return Math.round((v - min) / step) * step + min;
        }

        function apply(v) {
            v = clamp(snap(v));
            field.value = v;
            onChange(v);
        }

        // Direct keyboard input
        field.addEventListener('change', function() {
            apply(parseFloat(this.value) || min);
        });

        // +/- buttons
        var parent = field.parentElement;
        var minusBtn = parent.querySelector('.drag-input-minus');
        var plusBtn = parent.querySelector('.drag-input-plus');

        if (minusBtn) {
            minusBtn.addEventListener('click', function(e) {
                e.preventDefault();
                apply(parseFloat(field.value) - step);
            });
        }
        if (plusBtn) {
            plusBtn.addEventListener('click', function(e) {
                e.preventDefault();
                apply(parseFloat(field.value) + step);
            });
        }

        // Drag-to-slide on the number field
        var dragging = false;
        var didDrag = false;
        var dragStartX = 0;
        var dragStartValue = 0;

        field.addEventListener('mousedown', function(e) {
            if (e.button !== 0) return;
            dragging = true;
            didDrag = false;
            dragStartX = e.clientX;
            dragStartValue = parseFloat(field.value) || min;
            e.preventDefault();
        });

        document.addEventListener('mousemove', function(e) {
            if (!dragging) return;
            var dx = e.clientX - dragStartX;
            if (Math.abs(dx) < 3 && !didDrag) return;
            didDrag = true;
            field.style.cursor = 'ew-resize';
            apply(dragStartValue + dx * sensitivity);
        });

        document.addEventListener('mouseup', function() {
            if (!dragging) return;
            dragging = false;
            field.style.cursor = '';
            if (!didDrag) {
                // Was a click, not a drag — focus for typing
                field.focus();
                field.select();
            }
        });
    },

    // =========================================================================
    // SHARED PADDED GRID RENDERER
    // =========================================================================

    /**
     * Render a grid function into a padded offscreen canvas (cached).
     *
     * All preview modes (SUN, FLAT, future) should use this instead of
     * managing their own offscreen canvases. The padded area ensures the
     * grid extends well beyond the visible viewport, so pan/zoom never
     * reveals blank space.
     *
     * @param {CanvasRenderingContext2D} ctx - Target context to blit into
     * @param {number} cx  - Grid center X in the target coordinate space
     * @param {number} cy  - Grid center Y in the target coordinate space
     * @param {number} w   - Logical canvas width
     * @param {number} h   - Logical canvas height
     * @param {function} renderFn - function(offCtx, padCx, padCy, opts)
     * @param {string} cacheKey - Settings-based key (no w/h needed)
     * @param {object} cacheStore - Object to store _gridCanvas/_gridCacheKey on
     */
    renderGridCached: function(ctx, cx, cy, w, h, renderFn, cacheKey, cacheStore) {
        var pad = 4;
        var padW = Math.ceil(w * pad);
        var padH = Math.ceil(h * pad);
        var offX = Math.floor((padW - w) / 2);
        var offY = Math.floor((padH - h) / 2);
        var padCx = cx + offX;
        var padCy = cy + offY;

        var fullKey = cacheKey + '|' + padW + '|' + padH;

        if (cacheStore._gridCanvas && cacheStore._gridCacheKey === fullKey) {
            // Cache hit
            ctx.drawImage(cacheStore._gridCanvas, -offX, -offY);
            return;
        }

        // Cache miss — create/resize offscreen canvas
        if (!cacheStore._gridCanvas ||
            cacheStore._gridCanvas.width !== padW ||
            cacheStore._gridCanvas.height !== padH) {
            cacheStore._gridCanvas = document.createElement('canvas');
            cacheStore._gridCanvas.width = padW;
            cacheStore._gridCanvas.height = padH;
        }

        var offCtx = cacheStore._gridCanvas.getContext('2d');
        offCtx.clearRect(0, 0, padW, padH);

        // Render grid into the padded canvas (centered at padCx, padCy)
        renderFn(offCtx, padCx, padCy);

        cacheStore._gridCacheKey = fullKey;
        ctx.drawImage(cacheStore._gridCanvas, -offX, -offY);
    },

    // =========================================================================
    // CANVAS SIZE AND FIT (TreePreview, TreeGrowth, the Pre Req Master preview)
    // =========================================================================

    FIT_MARGIN: 12,     // css px kept clear round a preview's content
    FIT_DECOR: 45,      // css px past the root ring for arrows and names, when the mode gives no extent
    LABEL_SIDE: 0.3,    // |cos| / |sin| of the label's angle past which it hangs off that side

    /**
     * A preview box's size in whole css px, or null while it is not laid out
     * (its page or tab hidden). A canvas measured then used to take a made-up
     * 300 or 400 px and keep it until something resized the box.
     * @returns {{w:number, h:number}|null}
     */
    boxSize: function(el) {
        if (!el || !el.getBoundingClientRect) return null;
        var r = el.getBoundingClientRect();
        var w = Math.floor(r.width), h = Math.floor(r.height);
        return (w > 0 && h > 0) ? { w: w, h: h } : null;
    },

    /**
     * Size a preview canvas to its box. Returns the new size, or null when the
     * box is not laid out or the canvas already has that size (setting a
     * canvas's width clears it, so it is only set on a change).
     * @param {HTMLCanvasElement} canvas
     * @param {CanvasRenderingContext2D} ctx
     * @param {Element} box
     * @param {number} curW - the size the caller draws at now
     * @param {number} curH
     * @returns {{w:number, h:number}|null}
     */
    fitCanvas: function(canvas, ctx, box, curW, curH) {
        var size = this.boxSize(box);
        if (!size || !canvas || !ctx) return null;
        var dpr = window.devicePixelRatio || 1;
        var bw = Math.round(size.w * dpr), bh = Math.round(size.h * dpr);
        if (size.w === curW && size.h === curH && canvas.width === bw && canvas.height === bh) return null;
        canvas.width = bw;
        canvas.height = bh;
        canvas.style.width = size.w + 'px';
        canvas.style.height = size.h + 'px';
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.scale(dpr, dpr);
        return size;
    },

    /**
     * How far a preview's content reaches round its centre, {x, y} css px at
     * scale 1: the mode's own extent (roots, arrows and school names) when it
     * gives one, else the root ring plus decor.
     */
    contentExtent: function(data, decor) {
        if (!data) return null;
        if (data.extent && data.extent.x > 0 && data.extent.y > 0) return data.extent;
        var radius = 0;
        var roots = data.rootNodes || [];
        for (var i = 0; i < roots.length; i++) {
            var d = Math.sqrt(roots[i].x * roots[i].x + roots[i].y * roots[i].y);
            if (d > radius) radius = d;
        }
        if (!radius && data.ringRadius) radius = data.ringRadius;
        if (!(radius > 0)) return null;
        var r = radius + (typeof decor === 'number' ? decor : this.FIT_DECOR);
        return { x: r, y: r };
    },

    /** The scale (1 at most) that fits extent into a w x h preview with FIT_MARGIN round it. */
    fitScale: function(extent, w, h) {
        if (!extent || !(extent.x > 0) || !(extent.y > 0)) return 1;
        var s = Math.min(1, (w / 2 - this.FIT_MARGIN) / extent.x, (h / 2 - this.FIT_MARGIN) / extent.y);
        return s > 0 ? s : 1;
    },

    /**
     * A school's name past its roots, upright: dist from the centre along
     * angle, hanging off the side it points to (left-aligned on the right,
     * under the point at the bottom ...), so it never lies over its own roots.
     * @returns {{x:number, y:number, align:string, baseline:string, reachX:number, reachY:number}}
     *          x, y relative to the centre; reach: how far the text goes from it
     */
    schoolLabelPlace: function(angle, dist, textW, textH) {
        var c = Math.cos(angle), s = Math.sin(angle);
        var x = c * dist, y = s * dist;
        var align = c > this.LABEL_SIDE ? 'left' : (c < -this.LABEL_SIDE ? 'right' : 'center');
        var baseline = s > this.LABEL_SIDE ? 'top' : (s < -this.LABEL_SIDE ? 'bottom' : 'middle');
        return {
            x: x, y: y, align: align, baseline: baseline,
            reachX: Math.abs(x) + (align === 'center' ? textW / 2 : textW),
            reachY: Math.abs(y) + (baseline === 'middle' ? textH / 2 : textH)
        };
    }
};

console.log('[TreePreviewUtils] Loaded');

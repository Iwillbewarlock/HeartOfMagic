/**
 * TreePreviewHidden - the root preview's data (TreePreview.getOutput) without
 * the preview on screen.
 *
 * A Classic build places its spells on the preview's grid: ClassicBuildRequest
 * sends its size as grid_hint and ClassicLayout puts each spell on one of its
 * points. getOutput() has nothing until the active mode's render() has run once,
 * and that only happens in the preview's render loop, which does not run while
 * the panel is closed. The build at game start (AutoBuild) runs then, so it
 * calls ensure() first: render() once with a context that draws nothing, at
 * the size the preview would have.
 *
 * The size: the preview canvas's own when it has one, otherwise FALLBACK_SIZE
 * square - the preview box's CSS height (.tree-preview-split, 400px) and
 * TreePreview._updateCanvasSize's fallback for a hidden box. The Sun grid's
 * outer points reach to 3x the canvas diagonal (treePreviewSun.js maxExtent),
 * so a build at another canvas size can place far spells a little differently.
 * The Sun mode keeps the points of its first render for the session (its cache
 * key leaves the size out), so ensure() drops that cache once it has the output:
 * the preview's first frame on screen works the points out at its own size.
 *
 * Depends on: treePreview.js (TreePreview, its modes and schoolData)
 */

var TreePreviewHidden = {

    FALLBACK_SIZE: 400,

    /** The CanvasRenderingContext2D methods the preview modes and grids call. */
    _CONTEXT_METHODS: ['arc', 'beginPath', 'closePath', 'drawImage', 'fill', 'fillRect', 'fillText',
        'lineTo', 'moveTo', 'rect', 'restore', 'rotate', 'save', 'scale', 'setLineDash',
        'setTransform', 'stroke', 'strokeRect', 'strokeText', 'translate', 'clearRect'],

    /**
     * A 2D context that accepts every call the preview makes and draws nothing.
     * Style properties (fillStyle, font...) are plain fields.
     * @returns {Object}
     */
    nullContext: function() {
        var ctx = {};
        var noop = function() {};
        for (var i = 0; i < this._CONTEXT_METHODS.length; i++) ctx[this._CONTEXT_METHODS[i]] = noop;
        ctx.measureText = function() { return { width: 0 }; };
        var gradient = function() { return { addColorStop: noop }; };
        ctx.createLinearGradient = gradient;
        ctx.createRadialGradient = gradient;
        return ctx;
    },

    /**
     * The preview's output, worked out now when the preview has not drawn yet.
     * @param {Object} [preview] - TreePreview (tests pass their own)
     * @returns {Object|null} what preview.getOutput() returns, null when there is no mode to ask
     */
    ensure: function(preview) {
        preview = preview || (typeof TreePreview !== 'undefined' ? TreePreview : null);
        if (!preview || typeof preview.getOutput !== 'function') return null;
        var out = preview.getOutput();
        if (out) return out;

        var mode = preview.modes ? preview.modes[preview.activeMode] : null;
        if (!mode || typeof mode.render !== 'function') return null;
        var w = preview._width > 0 ? preview._width : this.FALLBACK_SIZE;
        var h = preview._height > 0 ? preview._height : this.FALLBACK_SIZE;
        mode.render(this.nullContext(), w, h, preview.schoolData || {});
        out = preview.getOutput();
        if ('_gridPointCache' in mode) {
            mode._gridPointCache = null;
            mode._gridPointCacheKey = '';
        }
        console.log('[TreePreviewHidden] Preview data worked out off screen (' + w + 'x' + h + '): ' +
            (out ? out.rootNodes.length + ' roots, ' + out.gridPoints.length + ' grid points' : 'none'));
        return out;
    }
};

if (typeof window !== 'undefined') window.TreePreviewHidden = TreePreviewHidden;

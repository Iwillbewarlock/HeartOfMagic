/**
 * Tree Growth status line - #tgStatus under the scan page's Build, Apply and
 * Clear buttons, and its copy on the Easy page (#easyStatus).
 *
 * Both spans sit inside language strings (scanner.complexStatusWrap,
 * easyMode.statusWrap, marked data-i18n-html), so the start and every language
 * switch rebuild them as "Waiting for scan...". A line written with its
 * language key is written again in the new language after a switch
 * (relabelStatus, called by languageSetting.js); one written without a key
 * (an add-on module's) goes back as it was written.
 *
 * Loaded right after treeGrowth.js: the methods below become TreeGrowth's
 * (TreeGrowth.setStatusText, TreeGrowth.STATUS_COLORS, ...).
 *
 * Depends on: treeGrowth.js (TreeGrowth: _nodeCount, _totalPool), i18n.js (t),
 *             easyMode.js (_syncEasyStatus, looked up when called)
 */

var TreeGrowthStatus = {

    /**
     * Colours of the line, by tone: the status tokens every UI theme and design
     * sets (styles-skyrim.css :root, the design stylesheets and presets), kept
     * apart from each other and from the idle line's text; the hex is the
     * fallback when a theme leaves the variable out. The idle line takes its
     * colour from the stylesheet (#tgStatus, #easyStatus in styles-skyrim.css).
     * easyMode.js mirrors the line's colour as is.
     */
    STATUS_COLORS: {
        idle: 'var(--status-idle, rgba(184, 168, 120, 0.5))',
        working: 'var(--status-working, #f59e0b)',
        done: 'var(--status-done, #22c55e)',
        error: 'var(--status-error, #ef4444)'
    },

    /** The built line is two strings ("Tree built - 10/20 nodes placed") under this key */
    BUILT_KEY: 'treeGrowth.treeBuilt',

    /** [span id, the language string that holds it] */
    IDLE_LINES: [['tgStatus', 'scanner.complexStatusWrap'], ['easyStatus', 'easyMode.statusWrap']],
    IDLE_FALLBACK: 'Waiting for scan...',

    /**
     * @param {string} text
     * @param {string} [tone] - 'idle', 'working', 'done' or 'error' (STATUS_COLORS). A
     *   growth module written before the tones passes a CSS colour here instead
     *   (MODULE_CONTRACTS documented one); that is used as it is.
     * @param {string} [key] - the text's language key, so a language switch writes it again
     * @param {Object} [params] - the key's {{variables}}
     */
    setStatusText: function(text, tone, key, params) {
        this._status = { text: text, tone: tone, key: key || null, params: params || null };
        var el = document.getElementById('tgStatus');
        if (el) {
            el.textContent = text;
            var color = tone && (this.STATUS_COLORS.hasOwnProperty(tone) ? this.STATUS_COLORS[tone] : tone);
            if (color && typeof color === 'string') el.style.color = color;
        }
        // Looked up afresh each time: a language switch rebuilds both spans
        if (typeof _syncEasyStatus === 'function') _syncEasyStatus();
    },

    /** "Tree built - N/M nodes placed" for the tree TreeGrowth holds */
    showBuilt: function() {
        this.setStatusText(this._builtLabel(), 'done', this.BUILT_KEY);
    },

    _builtLabel: function() {
        var label = t(this.BUILT_KEY);
        if (this._nodeCount > 0) {
            label += ' — ' + t('treeGrowth.nodesPlaced', {placed: this._nodeCount, total: this._totalPool || this._nodeCount});
        }
        return label;
    },

    /** After a language switch: the line says what it said, in the new language when it has a key. */
    relabelStatus: function() {
        var s = this._status;
        if (!s) {
            if (typeof _syncEasyStatus === 'function') _syncEasyStatus();
            return;
        }
        var text = s.text;
        if (s.key === this.BUILT_KEY) text = this._builtLabel();
        else if (s.key) text = t(s.key, s.params);
        this.setStatusText(text, s.tone, s.key, s.params);
    },

    /** Back to the idle "Waiting for scan..." in the current language (a cleared tree). */
    resetStatus: function() {
        this._status = null;
        for (var i = 0; i < this.IDLE_LINES.length; i++) {
            var el = document.getElementById(this.IDLE_LINES[i][0]);
            if (!el) continue;
            var key = this.IDLE_LINES[i][1];
            var wrap = el.parentNode;
            var html = (typeof t === 'function') ? t(key) : key;
            if (wrap && wrap.getAttribute && wrap.getAttribute('data-i18n-html') === key && html !== key) {
                wrap.innerHTML = html;   // the span made again, as the language string has it
            } else {
                el.textContent = this.IDLE_FALLBACK;
                el.style.color = '';
            }
        }
    },

    /** Give an object (TreeGrowth, or a test's stand-in) these methods. */
    mixInto: function(target) {
        for (var name in this) {
            if (this.hasOwnProperty(name) && name !== 'mixInto' && name !== '_status') target[name] = this[name];
        }
        return target;
    }
};

if (typeof TreeGrowth !== 'undefined') TreeGrowthStatus.mixInto(TreeGrowth);

if (typeof window !== 'undefined') window.TreeGrowthStatus = TreeGrowthStatus;

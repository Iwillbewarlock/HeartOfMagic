/**
 * WheelScroll - the mouse wheel scrolls the panel's lists and pages faster.
 *
 * The game's browser scrolls a short way per wheel notch, so the settings page
 * and the long lists took many turns to get through. Every wheel event that
 * nothing else took (the tree and the previews zoom with the wheel and call
 * preventDefault) scrolls the nearest scrollable box under the cursor by SPEED
 * times what the browser would have, and the browser's own scroll is stopped.
 *
 * A box is scrollable when its overflow-y is auto or scroll and it has more to
 * show in the wheel's direction; otherwise its parents are tried, so a list
 * scrolled to its end hands the wheel to the page around it, as before.
 * Form fields keep the wheel (a number field or a list box may use it).
 *
 * Depends on: nothing
 */

var WheelScroll = {

    SPEED: 3,              // times the browser's own scroll per notch
    LINE_PX: 16,           // deltaMode 1 (lines): pixels per line
    // deltaMode 2 (pages) scrolls this share of the box's height per page
    PAGE_SHARE: 0.9,

    init: function() {
        var self = this;
        document.addEventListener('wheel', function(e) { self.onWheel(e); }, { passive: false });
    },

    onWheel: function(e) {
        if (e.defaultPrevented || e.ctrlKey || !e.deltaY) return;
        var target = e.target;
        if (!target || this._keepsWheel(target)) return;
        var box = this._scrollableFrom(target, e.deltaY);
        if (!box) return;
        var dy = e.deltaY;
        if (e.deltaMode === 1) dy *= this.LINE_PX;
        else if (e.deltaMode === 2) dy *= box.clientHeight * this.PAGE_SHARE;
        e.preventDefault();
        box.scrollTop += dy * this.SPEED;
    },

    /** Elements that use the wheel themselves. */
    _keepsWheel: function(el) {
        var tag = el.tagName ? el.tagName.toUpperCase() : '';
        return tag === 'CANVAS' || tag === 'SVG' || tag === 'SELECT' || tag === 'TEXTAREA' ||
               (tag === 'INPUT' && (el.type === 'number' || el.type === 'range'));
    },

    /** The nearest box, from el outwards, that can still scroll toward dy. Without Element.closest. */
    _scrollableFrom: function(el, dy) {
        while (el && el.nodeType === 1 && el !== document.documentElement) {
            if (el.scrollHeight > el.clientHeight + 1) {
                var oy = window.getComputedStyle(el).overflowY;
                if (oy === 'auto' || oy === 'scroll') {
                    var room = dy > 0 ? el.scrollHeight - el.clientHeight - el.scrollTop : el.scrollTop;
                    if (room > 0) return el;
                }
            }
            el = el.parentNode;
        }
        return null;
    }
};

window.WheelScroll = WheelScroll;
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function() { WheelScroll.init(); });
} else {
    WheelScroll.init();
}

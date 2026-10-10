/**
 * HomIcons - the panel's drawn icons. Every icon spot is an empty
 * <span class="hom-icon" data-icon="<id>">; this paints its background from the
 * active design's icon folder (design.icons, e.g. "themes/arcane/icons/"), one
 * 48px PNG per id. A design without icons, or with a path that does not end in
 * "/", gets the line set. DesignPresets.apply calls setPath.
 *
 * Limit: one file per icon (19 per set) instead of an atlas, because
 * background-position on a sprite is unproven in Ultralight. Upgrade path: an
 * atlas once that is measured in game.
 *
 * Depends on: nothing.
 */
var HomIcons = {
    IDS: ['scan', 'build', 'apply', 'clear', 'replay', 'save', 'reset', 'import', 'fullscreen', 'close',
          'hotkey', 'tree-view', 'display', 'debug', 'progression', 'early', 'tome', 'tree-gen', 'prereq'],
    DEFAULT_SET: 'themes/icons-line/',
    _set: 'themes/icons-line/',

    setPath: function(path) {
        var ok = typeof path === 'string' && path.length > 1 && path.charAt(path.length - 1) === '/';
        this._set = ok ? path : this.DEFAULT_SET;
        if (typeof document !== 'undefined' && document) this.paint(document);
    },

    paint: function(root) {
        if (!root || !root.querySelectorAll) return;
        var slots = root.querySelectorAll('.hom-icon');
        for (var i = 0; i < slots.length; i++) {
            var id = slots[i].getAttribute('data-icon');
            if (this.IDS.indexOf(id) === -1) continue;
            slots[i].style.backgroundImage = 'url("' + this._set + id + '.png")';
        }
    },

    slot: function(id) {
        return '<span class="hom-icon" data-icon="' + id + '" aria-hidden="true" style="background-image:url(&quot;' +
            this._set + id + '.png&quot;)"></span>';
    },

    setLabel: function(button, text) {
        if (!button) return;
        var label = button.querySelector ? button.querySelector('.hom-label') : null;
        if (label) label.textContent = text;
        else button.textContent = text;
    }
};

if (typeof window !== 'undefined') window.HomIcons = HomIcons;
if (typeof global !== 'undefined') global.HomIcons = HomIcons;

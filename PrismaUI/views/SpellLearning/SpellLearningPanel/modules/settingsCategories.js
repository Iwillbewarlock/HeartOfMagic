/**
 * One-level Settings navigation. Moves the existing controls, so their IDs,
 * listeners, preset updates and save/reset behaviour remain unchanged.
 * Selection and each category's scroll position live only for this panel session.
 * Load after i18n.js and before main.js; init also supports a late/manual call.
 */
var SettingsCategories = {
    DEFAULT_CATEGORY: 'learning',
    NARROW_WIDTH: 760,
    CATEGORIES: [
        { id: 'display', text: 'Display' },
        { id: 'learning', text: 'Learning' },
        { id: 'tomes', text: 'Spell Tomes' },
        { id: 'advanced', text: 'Advanced' }
    ],
    BLOCKS: [
        ['settings.ui.title', 'display'],
        ['settings.hotkey.title', 'display'],
        ['settings.ui.treeTitle', 'display'],
        ['settings.progression.title', 'learning'],
        ['settings.earlyLearning.title', 'learning'],
        ['settings.tomeLearning.title', 'tomes'],
        ['settings.devDebug.title', 'advanced'],
        ['settings.treeGen.title', 'advanced']
    ],
    selected: 'learning',
    scrollPositions: {},
    panels: {},
    buttons: {},
    viewport: null,

    _label: function(el, key, fallback) {
        el.setAttribute('data-i18n', key);
        el.textContent = typeof tOr === 'function' ? tOr(key, null, fallback) : fallback;
    },

    /** Keep the actual nodes: recreating innerHTML would discard bound handlers. */
    _moveBlock: function(root, key, category) {
        var title = root.querySelector('[data-i18n="' + key + '"]');
        var block = title;
        while (block && !block.classList.contains('settings-block')) block = block.parentNode;
        if (block) this.panels[category].appendChild(block);
    },

    select: function(category) {
        if (!this.panels[category] || !this.viewport) return false;
        if (this.selected && this.panels[this.selected]) {
            this.scrollPositions[this.selected] = this.viewport.scrollTop;
        }
        this.selected = category;
        for (var i = 0; i < this.CATEGORIES.length; i++) {
            var id = this.CATEGORIES[i].id;
            var active = id === category;
            this.panels[id].hidden = !active;
            this.panels[id].setAttribute('aria-hidden', active ? 'false' : 'true');
            this.buttons[id].setAttribute('aria-selected', active ? 'true' : 'false');
        }
        this.viewport.scrollTop = this.scrollPositions[category] || 0;
        return true;
    },

    init: function() {
        var root = document.getElementById('contentSettings');
        if (!root || this.viewport) return;
        var self = this;
        var shell = document.createElement('div');
        shell.className = 'settings-categories';
        var nav = document.createElement('div');
        nav.className = 'settings-category-nav';
        nav.setAttribute('role', 'tablist');
        nav.setAttribute('aria-labelledby', 'settings-categories-label');
        var label = document.createElement('span');
        label.id = 'settings-categories-label';
        label.className = 'settings-category-nav-label';
        this._label(label, 'settings.categories.label', 'Settings categories');
        nav.appendChild(label);
        var viewport = document.createElement('div');
        viewport.className = 'settings-category-viewport';
        this.viewport = viewport;
        for (var i = 0; i < this.CATEGORIES.length; i++) {
            (function(category) {
                var button = document.createElement('button');
                button.type = 'button';
                button.id = 'settings-category-' + category.id;
                button.className = 'settings-category-button';
                button.setAttribute('role', 'tab');
                button.setAttribute('aria-controls', 'settings-page-' + category.id);
                self._label(button, 'settings.categories.' + category.id, category.text);
                button.addEventListener('click', function() { self.select(category.id); });
                nav.appendChild(button);
                self.buttons[category.id] = button;
                var panel = document.createElement('div');
                panel.id = 'settings-page-' + category.id;
                panel.className = 'settings-category-page';
                panel.setAttribute('role', 'tabpanel');
                panel.setAttribute('aria-labelledby', button.id);
                panel.hidden = true;
                self.panels[category.id] = panel;
                viewport.appendChild(panel);
            })(this.CATEGORIES[i]);
        }
        for (var b = 0; b < this.BLOCKS.length; b++) {
            this._moveBlock(root, this.BLOCKS[b][0], this.BLOCKS[b][1]);
        }
        var modded = document.getElementById('moddedXPSourcesSection');
        if (modded) this.panels.advanced.appendChild(modded);
        // The old two-column wrappers now contain no controls.
        var oldSplit = root.querySelector('.settings-split');
        if (oldSplit) oldSplit.style.display = 'none';
        shell.appendChild(nav);
        shell.appendChild(viewport);
        var footer = root.querySelector('.settings-actions-sticky');
        var version = document.getElementById('settingsVersion');
        root.insertBefore(shell, version || footer || null);
        root.classList.add('has-settings-categories');
        this.select(this.DEFAULT_CATEGORY);
        // Resizing the game panel need not resize the browser window.
        function fitWidth() {
            if (root.clientWidth > 0) {
                root.classList.toggle('settings-categories-narrow', root.clientWidth < self.NARROW_WIDTH);
            }
        }
        if (typeof ResizeObserver !== 'undefined') {
            this._resizeObserver = new ResizeObserver(fitWidth);
            this._resizeObserver.observe(root);
        } else {
            document.addEventListener('mouseup', fitWidth);
            if (typeof window !== 'undefined' && window.addEventListener) {
                window.addEventListener('resize', fitWidth);
            }
        }
        fitWidth();
    }
};

if (typeof document !== 'undefined' && document.addEventListener) {
    document.addEventListener('DOMContentLoaded', function() { SettingsCategories.init(); });
}

/**
 * NotificationSetting - Settings > Early Learning > "Show Notifications" and
 * "Weakened Spell Alerts".
 *
 * Show Notifications (notifications.enabled) turns every corner (HUD) message of
 * Heart of Magic off: C++ HudNotify, the Papyrus SpellLearning.NotificationText
 * native (ISL patch study messages, BookXP) and the BookXP addon read it. The
 * weakened spell alerts (and how often they come) are a part of it, so their
 * controls are greyed out while it is off. The messages' text follows the panel's
 * language (lang/*.json "hud.*" keys, docs/TRANSLATING.md).
 *
 * Saved as config.notifications = { enabled, weakenedSpellNotifications,
 * weakenedSpellInterval }. A settings preset carries the alert options, never
 * Show Notifications (a preference, not a play style): settingsPresets.js skips it.
 *
 * Depends on: settings, autoSaveSettings (optional), updateSliderFillGlobal (optional)
 */

var NotificationSetting = {

    DEFAULTS: {
        enabled: true,
        weakenedSpellNotifications: true,
        weakenedSpellInterval: 10
    },
    MIN_INTERVAL: 5,
    MAX_INTERVAL: 60,

    /** settings.notifications, made with the defaults when missing. */
    _notif: function() {
        if (!settings.notifications || typeof settings.notifications !== 'object') {
            settings.notifications = {};
        }
        var n = settings.notifications;
        for (var k in this.DEFAULTS) {
            if (this.DEFAULTS.hasOwnProperty(k) && n[k] === undefined) n[k] = this.DEFAULTS[k];
        }
        return n;
    },

    init: function() {
        var self = this;
        var master = document.getElementById('showNotificationsToggle');
        var weakened = document.getElementById('weakenedNotificationsToggle');
        var interval = document.getElementById('notificationIntervalSlider');

        if (master) {
            master.addEventListener('change', function() {
                self._notif().enabled = this.checked;
                self.sync();
                self._save();
            });
        }
        if (weakened) {
            weakened.addEventListener('change', function() {
                self._notif().weakenedSpellNotifications = this.checked;
                self.sync();
                self._save();
            });
        }
        if (interval) {
            interval.addEventListener('input', function() {
                self._notif().weakenedSpellInterval = parseInt(this.value, 10);
                self._showInterval();
                self._save();
            });
        }
        this.sync();
    },

    /** Put the saved values on the controls. */
    sync: function() {
        var n = this._notif();
        var master = document.getElementById('showNotificationsToggle');
        var weakened = document.getElementById('weakenedNotificationsToggle');
        var interval = document.getElementById('notificationIntervalSlider');
        var intervalRow = document.getElementById('notificationIntervalRow');
        var weakenedRow = document.getElementById('weakenedNotificationsRow');

        if (master) master.checked = n.enabled !== false;
        if (weakened) {
            weakened.checked = n.weakenedSpellNotifications !== false;
            weakened.disabled = n.enabled === false;
        }
        if (interval) {
            interval.value = n.weakenedSpellInterval;
            interval.disabled = n.enabled === false;
        }
        this._showInterval();
        // The interval means nothing with the alerts off
        if (intervalRow) intervalRow.style.display = n.weakenedSpellNotifications !== false ? 'flex' : 'none';
        // Show Notifications off: the alerts are off too, whatever they say
        if (weakenedRow && weakenedRow.classList) weakenedRow.classList.toggle('disabled-controls', n.enabled === false);
    },

    /** Read the saved settings from a config, keeping the defaults for what is missing or wrong. */
    loadFrom: function(data) {
        var d = this.DEFAULTS;
        var src = (data && data.notifications && typeof data.notifications === 'object') ? data.notifications : {};
        var v = src.weakenedSpellInterval;
        settings.notifications = {
            enabled: src.enabled !== false,
            weakenedSpellNotifications: src.weakenedSpellNotifications !== false,
            weakenedSpellInterval: (typeof v === 'number' && isFinite(v) && v >= this.MIN_INTERVAL && v <= this.MAX_INTERVAL)
                ? v : d.weakenedSpellInterval
        };
        this.sync();
    },

    /** Write the settings into a config. */
    saveTo: function(config) {
        var n = this._notif();
        config.notifications = {
            enabled: n.enabled !== false,
            weakenedSpellNotifications: n.weakenedSpellNotifications !== false,
            weakenedSpellInterval: n.weakenedSpellInterval
        };
        return config;
    },

    reset: function() {
        settings.notifications = JSON.parse(JSON.stringify(this.DEFAULTS));
        this.sync();
    },

    _showInterval: function() {
        var interval = document.getElementById('notificationIntervalSlider');
        var out = document.getElementById('notificationIntervalValue');
        var v = this._notif().weakenedSpellInterval;
        if (out) out.textContent = v + 's';
        if (interval && typeof updateSliderFillGlobal === 'function') updateSliderFillGlobal(interval);
    },

    _save: function() {
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
    }
};

window.NotificationSetting = NotificationSetting;

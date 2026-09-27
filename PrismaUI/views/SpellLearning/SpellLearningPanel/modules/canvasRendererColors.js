/**
 * CanvasRenderer - colours: school colours, the colours a design and the player
 * both set, and small colour helpers.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer._getSchoolColor() and so on. Load after canvasRendererV2.js
 * (which holds _defaultSchoolColors and PLAYER_COLOR_DEFAULTS).
 *
 * Depends on: CanvasRenderer, TreeStyle (ink, tokens), settings,
 * getOrAssignSchoolColor and hexToRgba (colorUtils.js, optional)
 */

(function() {
    var methods = {
        /**
         * Get school color - checks settings first, then defaults
         * Replaces TREE_CONFIG.getSchoolColor()
         */
        _getSchoolColor: function(school) {
            var color;
            if (typeof settings !== 'undefined' && settings.schoolColors && settings.schoolColors[school]) {
                color = settings.schoolColors[school];
            } else if (typeof getOrAssignSchoolColor === 'function') {
                color = getOrAssignSchoolColor(school);
            } else {
                color = this._defaultSchoolColors[school] || '#888888';
            }
            // As the design preset's ink (unchanged unless the preset asks)
            return TreeStyle.ink(color);
        },

        /**
         * Colours both a design and the player set (learning path, heart, globe):
         * the player's once they changed it from a shipped default, else the design's
         * token, else the player's. A design like Arcane re-colours them for its page,
         * but a colour the player picked is kept.
         */
        _designOrPlayer: function(tokenName, settingName, playerValue) {
            var token = TreeStyle.tokens[tokenName];
            if (!token) return playerValue;
            var defaults = this.PLAYER_COLOR_DEFAULTS[settingName] || [];
            var own = playerValue ? String(playerValue).toLowerCase() : '';
            if (own && defaults.indexOf(own) < 0) return playerValue;
            return token;
        },

        /** Learning colour: see _designOrPlayer. */
        _learningColor: function() {
            return this._designOrPlayer('learningColor', 'learningPathColor', this._learningPathColor) || '#00ffff';
        },

        _heartRing: function() {
            return this._designOrPlayer('hubRing', 'heartRingColor', this._heartRingColor) || '#b8a878';
        },

        _hexToRgb: function(hex) {
            hex = hex.replace('#', '');
            return {
                r: parseInt(hex.substring(0, 2), 16),
                g: parseInt(hex.substring(2, 4), 16),
                b: parseInt(hex.substring(4, 6), 16)
            };
        },

        _hexToRgba: function(hex, alpha) { return hexToRgba(hex, alpha); },

        // =========================================================================
        // COLOR UTILITIES
        // =========================================================================

        dimColor: function(color, factor) {
            var rgb = this.parseColor(color);
            if (!rgb) return color;
            return 'rgb(' + Math.round(rgb.r * factor) + ',' + 
                            Math.round(rgb.g * factor) + ',' + 
                            Math.round(rgb.b * factor) + ')';
        },

        brightenColor: function(color, factor) {
            var rgb = this.parseColor(color);
            if (!rgb) return color;
            return 'rgb(' + Math.min(255, Math.round(rgb.r + (255 - rgb.r) * (factor - 1))) + ',' + 
                            Math.min(255, Math.round(rgb.g + (255 - rgb.g) * (factor - 1))) + ',' + 
                            Math.min(255, Math.round(rgb.b + (255 - rgb.b) * (factor - 1))) + ')';
        },

        blendColors: function(color1, color2, t) {
            var rgb1 = this.parseColor(color1);
            var rgb2 = this.parseColor(color2);
            if (!rgb1 || !rgb2) return color1;
            return 'rgb(' + Math.round(rgb1.r + (rgb2.r - rgb1.r) * t) + ',' + 
                            Math.round(rgb1.g + (rgb2.g - rgb1.g) * t) + ',' + 
                            Math.round(rgb1.b + (rgb2.b - rgb1.b) * t) + ')';
        },

        getInnerAccentColor: function(color) {
            var cache = this._innerAccentCache || (this._innerAccentCache = {});
            if (cache.hasOwnProperty(color)) return cache[color];
            var rgb = this.parseColor(color);
            var accent = !rgb ? '#1a1a2e' : 'rgb(' + Math.round(rgb.r * 0.35) + ',' +
                                                     Math.round(rgb.g * 0.3) + ',' +
                                                     Math.round(rgb.b * 0.4) + ')';
            cache[color] = accent;
            return accent;
        },

        parseColor: function(color) {
            if (!color) return null;
            if (color.startsWith('#')) {
                return {
                    r: parseInt(color.slice(1, 3), 16),
                    g: parseInt(color.slice(3, 5), 16),
                    b: parseInt(color.slice(5, 7), 16)
                };
            }
            var match = color.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)/);
            if (match) {
                return {
                    r: parseInt(match[1]),
                    g: parseInt(match[2]),
                    b: parseInt(match[3])
                };
            }
            return null;
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();

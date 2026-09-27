/**
 * CanvasRenderer - the lines between the schools (their gradients kept until a
 * setting changes; a design may draw them as book rules instead) and the debug
 * grid of candidate positions.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.toggleDebugGrid() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, settings, state (globe position), TreeStyle
 */

(function() {
    var methods = {
        /** School dividers (world coordinates, rotated context). */
        renderSchoolDividers: function(ctx) {
            // Check if dividers are enabled
            if (!settings.showSchoolDividers) return;

            // LOD: Skip dividers entirely in MINIMAL tier
            if (this._lodTier === 'minimal') return;

            var schoolNames = Object.keys(this.schools);
            if (schoolNames.length < 2) return;

            var length = settings.dividerLength !== undefined ? settings.dividerLength : 800;
            var fade = (settings.dividerFade !== undefined ? settings.dividerFade : 50) / 100;  // Convert to 0-1
            var lineWidth = settings.dividerSpacing !== undefined ? settings.dividerSpacing : 3;
            var colorMode = settings.dividerColorMode || 'school';
            var customColor = settings.dividerCustomColor || '#ffffff';
            var gd = (state.treeData && state.treeData.globe) || { x: 0, y: 0 };

            // A design preset can draw them as book rules instead
            if (TreeStyle.renderOrnamentDividers(ctx, this, length, gd)) return;

            // Build cache key from all settings that affect gradients
            var cacheKey = length + '|' + fade + '|' + lineWidth + '|' + colorMode + '|' + customColor + '|' + gd.x + '|' + gd.y + '|' + schoolNames.length;
            for (var ci = 0; ci < schoolNames.length; ci++) {
                var sch = this.schools[schoolNames[ci]];
                cacheKey += '|' + (sch.startAngle || 0);
                if (colorMode === 'school') cacheKey += '|' + this._getSchoolColor(schoolNames[ci]);
            }

            // Rebuild gradient cache if settings changed
            if (this._dividerCacheKey !== cacheKey || !this._cachedDividerGradients) {
                this._dividerCacheKey = cacheKey;
                this._cachedDividerGradients = [];
                var startAlpha = 0.8;
                var endAlpha = startAlpha * (1 - fade);

                for (var i = 0; i < schoolNames.length; i++) {
                    var schoolName = schoolNames[i];
                    var school = this.schools[schoolName];
                    var angle = school.startAngle !== undefined ? school.startAngle : (i * (360 / schoolNames.length) - 90);
                    var rad = angle * Math.PI / 180;

                    var color;
                    if (colorMode === 'custom') {
                        color = customColor;
                    } else {
                        color = this._getSchoolColor(schoolName) || '#ffffff';
                    }

                    var endX = gd.x + Math.cos(rad) * length;
                    var endY = gd.y + Math.sin(rad) * length;
                    var gradient = ctx.createLinearGradient(gd.x, gd.y, endX, endY);

                    var rgb = this._hexToRgb(color);
                    gradient.addColorStop(0, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + startAlpha + ')');
                    gradient.addColorStop(0.5, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + (startAlpha * 0.7 + endAlpha * 0.3) + ')');
                    gradient.addColorStop(1, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + endAlpha + ')');

                    this._cachedDividerGradients.push({
                        gradient: gradient,
                        startX: gd.x, startY: gd.y,
                        endX: endX, endY: endY
                    });
                }
            }

            ctx.lineWidth = lineWidth;
            ctx.lineCap = 'round';

            // Draw using cached gradients
            for (var i = 0; i < this._cachedDividerGradients.length; i++) {
                var cached = this._cachedDividerGradients[i];
                ctx.strokeStyle = cached.gradient;
                ctx.beginPath();
                ctx.moveTo(cached.startX, cached.startY);
                ctx.lineTo(cached.endX, cached.endY);
                ctx.stroke();
            }
        },

        /**
         * Render debug grid showing all candidate positions
         * Called within the rotated context
         * Uses ACTUAL school data for alignment
         */
        renderDebugGrid: function(ctx) {
            if (!this.showDebugGrid) return;

            var spacing = 50;
            var extent = 1300;

            ctx.fillStyle = 'rgba(184, 168, 120, 0.35)';
            for (var gx = -extent; gx <= extent; gx += spacing) {
                for (var gy = -extent; gy <= extent; gy += spacing) {
                    ctx.beginPath();
                    ctx.arc(gx, gy, 3, 0, Math.PI * 2);
                    ctx.fill();
                }
            }
        },

        /**
         * Toggle debug grid visibility
         */
        toggleDebugGrid: function() {
            this.showDebugGrid = !this.showDebugGrid;
            this._needsRender = true;
            console.log('[CanvasRenderer] Debug grid:', this.showDebugGrid ? 'ON' : 'OFF');
            return this.showDebugGrid;
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();

var TreeTrunk = {

    /**
     * Compute trunk corridors for all schools.
     *
     * @param {string} mode - 'sun' or 'flat'
     * @param {Array} schools - [{name, color, arcStart, arcSize, rootLine}] (SUN)
     *                          or [{name, color, segStart, segSize, rootLine}] (FLAT)
     * @param {Array} rootNodes - [{x, y, school, color, dir}]
     * @param {Object} grid - {tierSpacing, ringRadius, ringTier, spokes, tiers} (SUN)
     *                        or {spacing, lineLength, direction} (FLAT)
     * @param {number} trunkThicknessPct - 1..100, % of school's root line width
     * @returns {Array} corridors - one per school, shape depends on mode
     */
    computeCorridors: function(mode, schools, rootNodes, grid, trunkThicknessPct) {
        var corridors = [];
        var pct = Math.max(1, Math.min(100, trunkThicknessPct)) / 100;

        for (var i = 0; i < schools.length; i++) {
            var school = schools[i];
            var schoolRoots = [];
            for (var ri = 0; ri < rootNodes.length; ri++) {
                if (rootNodes[ri].school === school.name) {
                    schoolRoots.push(rootNodes[ri]);
                }
            }
            if (schoolRoots.length === 0) continue;

            if (mode === 'sun') {
                corridors.push(this._sunCorridor(school, schoolRoots, grid, pct));
            } else if (mode === 'flat') {
                corridors.push(this._flatCorridor(school, schoolRoots, grid, pct));
            }
        }

        return corridors;
    },

    /**
     * SUN mode corridor: a straight rectangular trunk with parallel edges.
     * Two points on the ring (trunk width), extended in a single growth
     * direction so edges stay parallel (not radial/diverging).
     * Returns { x1,y1, x2,y2, x3,y3, x4,y4, color, school, growDirX, growDirY }
     */
    _sunCorridor: function(school, roots, grid, pct) {
        var arcSize = school.arcSize;
        var trunkArc = arcSize * pct;

        // Average root angle
        var sumSin = 0, sumCos = 0;
        for (var i = 0; i < roots.length; i++) {
            var a = Math.atan2(roots[i].y, roots[i].x);
            sumSin += Math.sin(a);
            sumCos += Math.cos(a);
        }
        var centerAngle = Math.atan2(sumSin / roots.length, sumCos / roots.length);

        // Growth direction: outward or inward from ring
        // Use dot product instead of angle comparison (immune to ±PI wrapping)
        var root0 = roots[0];
        var posAngle = Math.atan2(root0.y, root0.x);
        var dirVx = Math.cos(root0.dir);
        var dirVy = Math.sin(root0.dir);
        var outVx = Math.cos(posAngle);
        var outVy = Math.sin(posAngle);
        var growsOutward = (dirVx * outVx + dirVy * outVy) > 0;

        var ringRadius = grid.ringRadius || 150;
        var tierSpacing = grid.tierSpacing || 30;
        var maxTiers = grid.tiers || 10;

        // Two points on the ring at trunk edges
        var halfArc = trunkArc / 2;
        var a1 = centerAngle - halfArc;
        var a2 = centerAngle + halfArc;
        var p1x = Math.cos(a1) * ringRadius;
        var p1y = Math.sin(a1) * ringRadius;
        var p4x = Math.cos(a2) * ringRadius;
        var p4y = Math.sin(a2) * ringRadius;

        // Single growth direction vector (parallel, not radial)
        var gdx = Math.cos(centerAngle);
        var gdy = Math.sin(centerAngle);
        if (!growsOutward) { gdx = -gdx; gdy = -gdy; }

        // Corridor length — kept moderate so tree isn't excessively elongated.
        // getGridPointsInCorridor extends dynamically if more room is needed.
        var maxExtent = Math.max((maxTiers + 2) * tierSpacing, ringRadius * 5);

        // Extend both ring points in the SAME direction → parallel edges
        return {
            x1: p1x,                        y1: p1y,
            x2: p1x + gdx * maxExtent,      y2: p1y + gdy * maxExtent,
            x3: p4x + gdx * maxExtent,      y3: p4y + gdy * maxExtent,
            x4: p4x,                        y4: p4y,
            color: school.color,
            school: school.name,
            growsOutward: growsOutward,
            growDirX: gdx,
            growDirY: gdy,
            tierSpacing: tierSpacing
        };
    },

    /**
     * FLAT mode corridor: a rectangular band perpendicular to the baseline.
     * Returns { x1,y1, x2,y2, x3,y3, x4,y4, color, school }
     */
    _flatCorridor: function(school, roots, grid, pct) {
        var segSize = school.segSize;
        var trunkWidth = segSize * pct;
        var isHoriz = grid.direction === 'horizontal';
        var spacing = grid.spacing || 30;

        // Center of trunk on the segment
        var avgPos = 0;
        for (var i = 0; i < roots.length; i++) {
            avgPos += isHoriz ? roots[i].x : roots[i].y;
        }
        avgPos /= roots.length;

        var half = trunkWidth / 2;
        var growDir = roots[0].dir;
        var gdx = Math.cos(growDir);
        var gdy = Math.sin(growDir);
        // Extend corridor to cover the full grid (no hard cap — grid points are the real limiter)
        var maxExtent = spacing * 100;

        // Two points on baseline (perpendicular to growth direction)
        // Then extend them in growth direction
        if (isHoriz) {
            // Baseline is horizontal, trunk width along X
            return {
                x1: avgPos - half, y1: 0,
                x2: avgPos - half, y2: gdy * maxExtent,
                x3: avgPos + half, y3: gdy * maxExtent,
                x4: avgPos + half, y4: 0,
                color: school.color,
                school: school.name
            };
        } else {
            // Baseline is vertical, trunk width along Y
            return {
                x1: 0, y1: avgPos - half,
                x2: gdx * maxExtent, y2: avgPos - half,
                x3: gdx * maxExtent, y3: avgPos + half,
                x4: 0, y4: avgPos + half,
                color: school.color,
                school: school.name
            };
        }
    },

    /**
     * Filter actual grid points (from previous stage) that fall inside
     * a trunk corridor. Returns points sorted by distance from the trunk
     * base (layer-by-layer filling from root outward).
     *
     * @param {Object} corridor - from computeCorridors (4-point rectangle)
     * @param {Array} gridPoints - [{x, y, school}] from baseData.gridPoints
     * @param {number} maxPoints - maximum points to return
     * @returns {Array} [{x, y, color}] sorted by distance from trunk base
     */
    getGridPointsInCorridor: function(corridor, gridPoints, maxPoints) {
        // Corridor geometry: growth direction and perpendicular
        var gdx = corridor.growDirX || 0;
        var gdy = corridor.growDirY || 0;

        // If no explicit growth direction (flat mode), compute from corridor shape
        if (gdx === 0 && gdy === 0) {
            var bx = (corridor.x1 + corridor.x4) / 2;
            var by = (corridor.y1 + corridor.y4) / 2;
            var fx = (corridor.x2 + corridor.x3) / 2;
            var fy = (corridor.y2 + corridor.y3) / 2;
            var len = Math.sqrt((fx - bx) * (fx - bx) + (fy - by) * (fy - by));
            if (len > 0) { gdx = (fx - bx) / len; gdy = (fy - by) / len; }
        }

        // Perpendicular (width axis)
        var pdx = -gdy;
        var pdy = gdx;

        // Trunk width from base edge points
        var dx14 = corridor.x4 - corridor.x1;
        var dy14 = corridor.y4 - corridor.y1;
        var halfWidth = Math.sqrt(dx14 * dx14 + dy14 * dy14) / 2;

        // Trunk base midpoint
        var midX = (corridor.x1 + corridor.x4) / 2;
        var midY = (corridor.y1 + corridor.y4) / 2;

        // Trunk length (base to far edge)
        var ex = (corridor.x2 + corridor.x3) / 2;
        var ey = (corridor.y2 + corridor.y3) / 2;
        var maxDist = Math.sqrt((ex - midX) * (ex - midX) + (ey - midY) * (ey - midY));

        // Minimum distance from base — skip the root ring (reserved for root nodes)
        var tierSp = corridor.tierSpacing || 20;
        var minDist = tierSp * 0.5;

        // Generate a regular grid inside the corridor at natural tierSpacing.
        // The corridor grows LONGER to fit more nodes rather than getting denser.
        var corridorWidth = halfWidth * 2;
        var rowSpacing = tierSp;
        var cols = Math.max(1, Math.round(corridorWidth / tierSp));
        if (cols < 2 && halfWidth > tierSp * 0.3) cols = 2;

        // Extend corridor length if needed to fit all requested points
        var neededRows = Math.ceil(maxPoints / cols);
        var neededLength = (neededRows + 2) * rowSpacing;
        var effectiveMaxDist = Math.max(maxDist, minDist + neededLength);

        var inside = [];
        var along = minDist + rowSpacing * 0.5;
        while (along < effectiveMaxDist) {
            for (var c = 0; c < cols; c++) {
                var across;
                if (cols === 1) {
                    across = 0;
                } else {
                    across = -halfWidth + corridorWidth * (c + 0.5) / cols;
                }

                var px = midX + gdx * along + pdx * across;
                var py = midY + gdy * along + pdy * across;

                inside.push({
                    x: px, y: py,
                    color: corridor.color,
                    dist: along
                });
            }
            along += rowSpacing;
        }

        // Sort by distance from base (layer-by-layer filling)
        inside.sort(function(a, b) { return a.dist - b.dist; });

        // Return up to maxPoints
        var result = [];
        var count = Math.min(maxPoints, inside.length);
        for (var j = 0; j < count; j++) {
            result.push({ x: inside[j].x, y: inside[j].y, color: inside[j].color });
        }
        return result;
    },

    /**
     * Fill trunk with ghost nodes layer-by-layer.
     * Biases toward filling each layer completely before going up.
     *
     * @param {Array} gridPoints - from getGridPointsInCorridor (already sorted by layer)
     * @param {number} nodeCount - how many nodes to place
     * @returns {Array} placements [{x, y, color}] up to nodeCount
     */
    fillLayerByLayer: function(gridPoints, nodeCount) {
        var result = [];
        var count = Math.min(nodeCount, gridPoints.length);
        for (var i = 0; i < count; i++) {
            result.push({
                x: gridPoints[i].x,
                y: gridPoints[i].y,
                color: gridPoints[i].color
            });
        }
        return result;
    }
};

console.log('[TreeTrunk] Loaded');

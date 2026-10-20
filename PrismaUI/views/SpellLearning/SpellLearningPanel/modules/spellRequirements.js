/**
 * Player-facing requirements. Mastery, not merely owning a weakened spell,
 * satisfies a prerequisite. Editing retains the original hard/soft controls.
 */
var SpellRequirements = {
    isMet: function(id) {
        var node = _findNodeById(id);
        return !!(node && node.state === 'unlocked');
    },

    group: function(node, locks) {
        var hard = node.hardPrereqs || [];
        var soft = node.softPrereqs || [];
        if (!hard.length && !soft.length) hard = node.prerequisites || [];
        var result = { required: [], optional: [], locks: [], met: [], need: 0 };
        var lockIds = {}, seen = {}, self = this;
        (locks || []).forEach(function(lock) { lockIds[lock.nodeId] = lock; });
        function add(id, kind) {
            if (seen[id]) return;
            seen[id] = true;
            var n = _findNodeById(id);
            var shown = SpellNames.isShown(n);
            if (lockIds[id] && !lockIds[id].revealed) shown = false;
            var entry = {
                id: id, kind: lockIds[id] ? 'lock' : kind,
                name: spellDisplayName(id, n, shown), shown: shown,
                school: lockIds[id] && shown && n ? n.school : ''
            };
            if (self.isMet(id)) result.met.push(entry);
            else result[entry.kind === 'lock' ? 'locks' : kind].push(entry);
        }
        hard.forEach(function(id) { add(id, 'required'); });
        soft.forEach(function(id) { add(id, 'optional'); });
        (locks || []).forEach(function(lock) { add(lock.nodeId, 'locks'); });
        var metSoft = soft.filter(function(id) { return self.isMet(id); }).length;
        result.need = Math.max(0, (node.softNeeded || 0) - metSoft);
        // Once the optional quota is met, the unused choices are not requirements.
        if (!result.need) result.optional = [];
        // Knowing a later spell can waive these gates; the reverse-unlock note
        // explains why. Only actual mastery belongs in the Met row.
        if (node.openedByKnownChild && settings.reverseUnlock !== false && node.state !== 'locked') {
            result.required = [];
            result.optional = [];
            result.locks = [];
            result.need = 0;
        }
        return result;
    },

    chip: function(entry) {
        var chip = document.createElement('button');
        chip.type = 'button';
        chip.className = 'requirement-chip' + (entry.kind === 'lock' || entry.kind === 'locks' ? ' requirement-lock' : '');
        chip.textContent = entry.name;
        if (entry.kind === 'lock' || entry.kind === 'locks') {
            var badge = document.createElement('span');
            badge.className = 'requirement-lock-badge';
            badge.textContent = '\uD83D\uDD12 ' + tOr('details.lockBadge', null, 'LOCK');
            chip.insertBefore(badge, chip.firstChild);
            if (entry.school && typeof schoolText === 'function') {
                var school = document.createElement('span');
                school.textContent = '(' + schoolText(entry.school) + ')';
                chip.appendChild(school);
            }
        }
        chip.disabled = !entry.shown;
        if (entry.shown) chip.onclick = function() { selectNodeById(entry.id); };
        return chip;
    },

    render: function(node, editing) {
        var host = document.getElementById('card-requirements');
        if (!host) return;
        host.innerHTML = '';
        host.classList.toggle('hidden', !!editing);
        if (editing) {
            renderSpellPrereqEditor(node);
            return;
        }
        var locks = typeof PreReqMaster !== 'undefined' && PreReqMaster.getLocksForNode
            ? PreReqMaster.getLocksForNode(node.id) : [];
        var groups = this.group(node, locks);
        var missing = groups.required.length + groups.locks.length + groups.need;
        var label = document.createElement('span');
        label.className = 'requirement-label';
        label.textContent = missing ? tOr('card.stillNeeded', null, 'Still needed:') :
            tOr('card.requirementsMet', null, 'Requirements met');
        host.appendChild(label);
        var self = this;
        groups.required.concat(groups.locks).forEach(function(entry) { host.appendChild(self.chip(entry)); });
        if (groups.need) {
            var optional = document.createElement('span');
            optional.className = 'requirement-options';
            var title = document.createElement('span');
            title.textContent = tOr('details.softNeed', { need: groups.need, total: groups.optional.length },
                'need {{need}} of {{total}}');
            optional.appendChild(title);
            groups.optional.forEach(function(entry) { optional.appendChild(self.chip(entry)); });
            host.appendChild(optional);
        }
        if (groups.met.length) {
            var met = document.createElement('div');
            met.className = 'requirements-met';
            var metLabel = document.createElement('span');
            metLabel.textContent = tOr('card.met', null, 'Met:');
            met.appendChild(metLabel);
            groups.met.forEach(function(entry) { met.appendChild(self.chip(entry)); });
            host.appendChild(met);
        }
    }
};

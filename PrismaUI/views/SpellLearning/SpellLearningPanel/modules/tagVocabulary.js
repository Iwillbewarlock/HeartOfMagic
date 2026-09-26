/**
 * SpellLearning Tag Vocabulary Module
 *
 * The closed list of tags the librarian may assign to a spell.
 * This module has no dependencies and should be loaded early.
 *
 * MIRROR of plugins/spelllearning/include/librarian/TagVocabulary.h.
 * The two lists must stay identical - change one, change the other.
 * librarian-test --check-vocab compares them and fails when they drift.
 *
 * Definitions live in docs/librarian/TAGS.md.
 */

// =============================================================================
// ELEMENTS - what the magic is made of
// =============================================================================
// The 34 from Spell Research, plus four the load order's own keyword
// frameworks distinguish and Spell Research cannot express.

var TAG_ELEMENTS = [
    'acid',
    'air',
    'apparition',
    'arcane',
    'armor',
    'blood',
    'construct',
    'creature',
    'daedra',
    'disease',
    'earth',
    'eldritch',
    'fire',
    'flesh',
    'force',
    'frost',
    'health',
    'holy',
    'human',
    'life',
    'light',
    'magicka',
    'metal',
    'nature',
    'necrotic',
    'poison',
    'resistance',
    'shadow',
    'shield',
    'shock',
    'soul',
    'stamina',
    'sun',
    'time',
    'trap',
    'undead',
    'water',
    'weapon'
];

// =============================================================================
// TECHNIQUES - what the magic does
// =============================================================================
// The 15 from Spell Research, plus three the frameworks distinguish.

var TAG_TECHNIQUES = [
    'cloak',
    'control',
    'courage',
    'curing',
    'curse',
    'dispel',
    'fear',
    'frenzy',
    'infuse',
    'pacify',
    'sacrifice',
    'sense',
    'siphon',
    'strengthen',
    'summoning',
    'telekinesis',
    'teleport',
    'transform'
];

// =============================================================================
// LOOKUP
// =============================================================================

function isTagElement(tag) {
    return TAG_ELEMENTS.indexOf(tag) !== -1;
}

function isTagTechnique(tag) {
    return TAG_TECHNIQUES.indexOf(tag) !== -1;
}

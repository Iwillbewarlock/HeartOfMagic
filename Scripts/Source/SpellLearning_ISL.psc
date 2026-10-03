Scriptname SpellLearning_ISL Hidden
{Native function stubs for SpellLearning ISL integration.
These functions are implemented in SpellLearning.dll}

; Called when a spell tome is read - returns true if we handled it
bool Function OnTomeRead(Book akBook, Spell akSpell, ObjectReference akContainer) global native

; Check if ISL integration is currently active
bool Function IsIntegrationActive() global native

; Get the XP per hour setting
float Function GetXPPerHour() global native

; Get the tome inventory bonus setting (0.0 - 1.0)
float Function GetTomeBonus() global native

; Enable or disable ISL integration
Function SetEnabled(bool enabled) global native

; Called from patched ISL script after each study session
; Grants proportional XP based on hours studied vs total hours to master
Function OnStudyProgress(Spell akSpell, int hoursStudied, float totalStudied, float hoursToMaster) global native

; Called from patched ISL script when study is complete and spell is learned
Function OnStudyComplete(Spell akSpell) global native

; Settings > Spell Tomes > Skill XP from Studying (default on): true only when it is
; off, so a study session does not raise the school's skill (a skill mod - Static Skill
; Leveling... - does it). Asked this way round so that a missing native (None = false)
; keeps ISL's own behaviour: the skill is raised.
bool Function SkipStudySkillXP() global native

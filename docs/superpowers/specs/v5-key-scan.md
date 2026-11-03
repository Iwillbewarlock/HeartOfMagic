# Build 5 key scan (Q1), 2026-10-25

Read-only scan of the installed mods under `D:/TAKEALOOK/mods` (4789 mod folders; active profile `TKL - MUNG ADDON`,
`ModOrganizer.ini selected_profile`). Paths below are relative to `D:/TAKEALOOK/mods/<mod folder>/`.

## Decision

| Key | Code | Was | Why |
|---|---|---|---|
| next learnable (`[Keys] iNext`) | **22 (U)** | 19 (R) | R is Spell Organizer's remove-spell key, live only in the magic menu (`SpellOrganizerMCM.psc:225-226`, installed value 19 in `TAKEALOOK - MCM and INI/MCM/Settings/Spell Organizer.json:12`). Y (first on the list) is registered by Rape Tattoos Continued with no menu check (`rapeTattoos.psc:30`), so not free. U is the first free key of the list Y, U, H, J, K, B, V. |
| set target (`[Keys] iSetTarget`) | **34 (G)** | 34 (G) | Kept. The only other G is OSL Aroused's `DebugActionKey`: registered, but its handler body is commented out (`OSLAroused_Main.psc:271-273`) and the event returns in menu mode first (`:245`). No config file binds 34. |

Both defaults changed in `Settings.h` and in the shipped `plugins/SL_MagicMenu/dist/SL_MagicMenu.ini` (`iNext=22`). Every
on-screen hint prints the configured key's name. The plugin logs the keys at every menu open and the scan summary once at load.

Not readable here: Spell Organizer's value in the save (the MCM json says 19, `LoadSettingsonReload=0`; assumed R), keys built
into SKSE DLLs without a config file, GlobalVariable defaults in plugins (read only from comments and MCM files).

## Candidate order (design: Y, U, H, J, K, B, V)

| Code | Verdict | Deciding line |
|---|---|---|
| 21 Y | taken (dormant) | `Rape Tattoos Continued/scripts/source/rapeTattoos.psc:30` `RegisterForKey(21) ; Y Key` in OnInit; `OnKeyUp` (:80-95) has no menu check, only `if (!menuForm.debugEnabled) return` (debug off by default, `rapeTattoos_mcm.psc:114`) |
| 22 U | **free in the magic menu** | SexLab P+ `rotatescene: 22` (`TAKEALOOK - MCM and INI/SKSE/Plugins/SexLab/SexlabConfig.json:155`) is registered only while a scene runs (`sslThreadController.psc:280-298`) and its `OnKeyDown` returns in menu mode (`:310-311`); SmoothCam presets `modToggleKey: 22` are inactive in every profile |
| 35 H | taken | Horse Whistle `TAKEALOOK - MCM and INI/MCM/Settings/Horse Whistle Key.ini:2` `iWhistleHotkey = 35`; Animated Whistling `simplesthorses_quest.psc:25` (both check menu mode); `keybinds.json:15` spf_slso |
| 36 J | taken | Inventory Reforged `SKSE/Plugins/InventoryReforged.ini:96` `iJunkKey = 36`; IR runs a panel in the magic menu when its `bMagicMenu = 1` (`:18`; this mod ships an override with 0) |
| 37 K | free (not reached) | only SexLab P+ `adjustchange: 37` (`SexlabConfig.json:102`), scene only |
| 48 B | taken | Helmet Toggle 2 `TAKEALOOK - MCM and INI/MCM/Settings/keybinds.json:5` `"keycode": 48`; Paragon `MCM/Config/ParagonPerks/settings.ini:2` `iDualBlockKey = 48` (inactive in MUNG ADDON) |
| 47 V | taken | Copy Paste In Menus `SKSE/Plugins/po3_CopyPasteInMenus.ini:7` `iSecondaryKey = 47` (Ctrl+V in menu text fields) |

## All hits by code

`code | mod folder | file:line | text | live? | menu check?`

### 19 R
- 19 | Spell Organizer - Hide Remove Delete Erase Forget Spells | `Scripts/source/SpellOrganizerMCM.psc:22` | `Int Property RemoveHotkey = -1 Auto` | default -1 | -
- 19 | same | `SpellOrganizerMCM.psc:340-341` | `RemoveHotkey = JMap.getInt(SettingsFile, "RemoveHotkey", -1)` / `RegisterForKey(RemoveHotkey)` (also `:473` from the MCM) | live | `:225-226` `If utility.IsInMenuMode() && ui.IsMenuOpen("MagicMenu")` / `If KeyCode == RemoveHotkey`: acts ONLY in the magic menu (removes the selected spell, confirmation box)
- 19 | TAKEALOOK - MCM and INI | `MCM/Settings/Spell Organizer.json:12` (and `backup/Spell Organizer.json:14`) | `"RemoveHotkey": 19,` | installed setting | -
- 19 | MCM Helper | `MCM/Config/SkyUI_SE/settings.ini:87` | `iFavoriteGroupUseKey=19` | SkyUI favorites group key, passed to the SWF (`SkyUI - Ghost Item Bug Fix/Source/Scripts/SKI_FavoritesManager.psc:134` `int _groupUseKey = 19 ; R`, `:425-426`) | Favorites menu only
- 19 | Photo Mode | `MCM/Config/PhotoMode/settings.ini:14`, `:30` | `iResetKey = 19`, `iGalleryLoadScreenKey = 19` | photo mode UI only | -
- 19 | TAKEALOOK - MCM and INI | `SKSE/Plugins/QuickLootIE/DefaultConfig.json:19` | `"KeybindingTakeAll" : 19,` | loot menu only (whether this file is read is unverified; QuickLootIE MCM `QuickLootIEMCM.psc:73`) | -
- 19 | Shout Organizer | `Scripts/Source/ShoutOrganizerMCM.psc:22, :335, :223-224` | same structure as Spell Organizer, magic menu only | installed value 20 (T), `Shout Organizer.json:12` - not 19 | -
- 19 | HoM-MagicMenu-Proto (this mod, build 4 install) | `SKSE/Plugins/SL_MagicMenu.ini:26` | `iNext=19` | ours | -

### 21 Y
- 21 | Rape Tattoos Continued | `scripts/source/rapeTattoos.psc:30` | `RegisterForKey(21) ; Y Key` | live, debug-gated (`:82`) | none
- No config file binds 21 now (an old note `D:/TAKEALOOK/_키바인딩_현황.md` named Horse Whistle on Y; its ini is 35 now).

### 22 U
- 22 | [NoDelete] 0034 [LED][Custom] SmoothCam Preset | `SKSE/Plugins/SmoothCamPreset6.json:325`, `SmoothCamPreset7.json:325` | `"modToggleKey": 22,` | inactive in all profiles | -
- 22 | TAKEALOOK - MCM and INI | `SKSE/Plugins/SexLab/SexlabConfig.json:155` | `"rotatescene" : 22,` | scene only | `sslThreadController.psc:311` `If (Utility.IsInMenuMode() || _SkipHotkeyEvents)` return

### 34 G
- 34 | OSL Aroused - ChildFix | `Scripts/Source/OSLAroused_Main.psc:33`, `:158` | `int DebugActionKey = 34` / `RegisterForKey(DebugActionKey)` | registered; handler commented out `:271-273` | `:245` `if Utility.IsInMenuMode()` return
- 34 | OSL Aroused - Arousal Reborn | `Scripts/Source/OSLAroused_Main.psc:33, :159, :284-286` | same | same | `:258`
- 34 | HoM-MagicMenu-Proto | `SKSE/Plugins/SL_MagicMenu.ini:27` | `iSetTarget=34` | ours | -
- Other 34s in config files are not keys (`"index": 34`, `"icon": 34`).

### 35 H
- 35 | TAKEALOOK - MCM and INI | `MCM/Settings/Horse Whistle Key.ini:2` | `iWhistleHotkey = 35` | live | `eeeHorseWhistleScript.psc:66` `If !Utility.IsInMenuMode() ...`
- 35 | Animated Whistling | `source/scripts/simplesthorses_quest.psc:25` | `RegisterForKey(SH_Hotkey.GetValue() as int) ; Default H 35` | live (global default unread) | `:68-69` `!Utility.IsInMenuMode()`
- 35 | TAKEALOOK - MCM and INI | `MCM/Settings/keybinds.json:15` | `"keycode": 35,` (spf_slso actionChangeAnim) | SexLab scene | -
- 35 | Theos Render Pipeline | `SKSE/Plugins/TheosRenderPipeline.ini:37` | `ToggleOverlay=0x23` | false hit: a Win32 virtual key (End), inactive | -

### 36 J
- 36 | InventoryReforged 1.5.0 | `SKSE/Plugins/InventoryReforged.ini:96` | `iJunkKey = 36` | live in MUNG ADDON; whether it acts in the magic menu is unverified | -

### 37 K
- 37 | TAKEALOOK - MCM and INI | `SKSE/Plugins/SexLab/SexlabConfig.json:102` | `"adjustchange" : 37,` | scene only | as 22

### 47 V
- 47 | Copy Paste In Menus | `SKSE/Plugins/po3_CopyPasteInMenus.ini:7` | `iSecondaryKey = 47` | Ctrl+V in menus | -
- 47 | TAKEALOOK - MCM and INI | `SKSE/Plugins/po3_ConsolePlusPlus.ini:20` | `iSecondaryKey = 47` | console only | -
- 47 | Dual Wield Parrying SKSE | `SKSE/Plugins/DualWieldParryingNG.ini:4` | `ParryKey = 47` | overridden by `TAKEALOOK - MCM and INI/SKSE/Plugins/DualWieldParryingNG.ini:4` `ParryKey = 260` | -

### 48 B
- 48 | TAKEALOOK - MCM and INI | `MCM/Settings/keybinds.json:5` | `"keycode": 48,` (Helmet Toggle 2, HT_ManageHelmet) | live | -
- 48 | Paragon - A Modern Perk Mod | `MCM/Config/ParagonPerks/settings.ini:2` | `iDualBlockKey = 48` | inactive in MUNG ADDON | -

### 49 N
- 49 | Creature Framework | `scripts/source/CFConfigMenu.psc:40` (`:746` OnDefaultST) | `int property PupTargetKey = 49 auto hidden` | default 49 | -
- 49 | same | `scripts/source/CreatureFramework.psc:179, :214`, `:218-220` | `RegisterForKey(Config.PupTargetKey)` / `event OnKeyUp` / `TargetPuppetSpell.Cast(Player)` | live | **none** (casts in menus too)
- 49 | ZaZ Animation Pack | `scripts/source/zbfconfig.psc:316-317`, `:1104` | `iSelectActorKey = 49` / `RegisterForKey(iSelectActorKey)` | live | `:785-787` `If zbfUtil.IsInMenu()` return
- 49 | SexLab Aroused Creatures SE | `Scripts/Source/slac_Config.psc:4572`, `slac_PlayerScript.psc:383`; `SKSE/Plugins/ArousedCreatures/slacSettingsProfile0.json:225` `"targetkey" : 49,` | live | `:415` `!slacUtility.MenuOpen("ForceAll")`
- 49 | SexLab Inflation Framework SE | `Scripts/Source/SLIF_Menu.psc:467` | `RegisterForKey(SLIF_Config.GetInt("register_key", 49))` | live | `:472` `!Utility.IsInMenuMode()`
- 49 | Rape Tattoos Continued | `scripts/source/rapeTattoos.psc:29` | `RegisterForKey(49) ; N Key` | live, debug-gated | none
- 49 | TAKEALOOK - MCM and INI | `SKSE/Plugins/SexLab/SexlabConfig.json:160` | `"targetactor" : 49,` | SexLab, returns in menu mode (`sslSystemConfig.psc:681-682`) | yes

### Notes
- SkyUI `interface/skyui/config.txt`: one installed copy (`Kome's Inventory Tweaks (KIT)/interface/skyui/config.txt:4-6`):
  search 57 (Space), switchTab 56 (Alt), equipMode 42 (LShift); none of the codes above. SkyUI_SE MCM Helper `[Controls]`
  (`MCM Helper/MCM/Config/SkyUI_SE/settings.ini:79-98`): only `iFavoriteGroupUseKey=19` from the set.
- Scrab's Papyrus Extender defines `RegisterForAllAlphaNumericKeys` (`Source/Scripts/PyramidUtils.psc:80-100`, all codes above);
  no installed .psc calls it (.pex callers not checked).
- .pex only, not read: Bow Rapid Combo (2 folders), Bow Rapid Combo V3, CBBE 3BA (3BBB) - Settings Loader, NL_MCM, MCM Menu Redone.

## Scan method (to re-run)

Papyrus (13,141 `.psc`; Git Bash, cwd `D:/TAKEALOOK/mods`; `rg` there is a shell function, call it directly):
1. `rg --files -g "*.psc" "D:/TAKEALOOK/mods" | wc -l`
2. `rg -n -i --no-heading "RegisterForKey\s*\(" -g "*.psc" "D:/TAKEALOOK/mods"`
3. `rg -n -i --no-heading -g "*.psc" "(key|hotkey|keycode|keyID|kc)\w*\s*(=|:=)\s*(19|34|49|21|22|35|36|37|48|47)\b" .`
4. `rg -n -i --no-heading -g "*.psc" -g "!**/Skyrim Script Extender*/**" "key.*[^0-9.\w](19|34|49|21|22|35|36|37|48|47)\b[^.0-9]|key.*[^0-9.\w](19|34|49|21|22|35|36|37|48|47)$" .`
5. `rg -n -i --no-heading -g "*.psc" -g "!**/Skyrim Script Extender*/**" "^\s*(int|float)\s+property\s+\w*(key|hotkey)\w*\s*(=\s*-?\d+)?" .`
6. the set's numbers inside the 84 .psc files that have RegisterForKey / AddKeyMapOption / OnKeyDown / OnKeyUp / keymap
7. `rg -n --no-heading -P -i -g "*.ini" -g "!**/meta.ini" "^\s*[a-z]?\w*(key|hotkey|keycode|keybind)\w*\s*=\s*(19|34|49|21|22|35|36|37|48|47)\s*(;.*)?$" .`
8. `rg -l -a -i -g "*.pex" "RegisterForKey|RegisterForAllAlphaNumericKeys" .`

Config files (MCM/Settings 61, MCM/Config settings.ini 61, config.json 57 with 87 keymap options, SkyUI config.txt 1,
SKSE/Plugins ini/toml/json/yaml 3214): Python scripts `scan1.py` .. `scan7.py` in the build-5 session scratchpad
(`D:/qtuna/Temp/Temp/claude/C--Users-qtuna-Desktop/1d4c9a74-27ed-4b66-909a-a19dd005ca16/scratchpad/`); rule: a key-like name
(`key|hotkey|button|bind|shortcut|keycode|toggle`) whose value is exactly one of the codes (decimal or 0x hex), plus an
all-names pass for 34 and 37 and the SexLab config.

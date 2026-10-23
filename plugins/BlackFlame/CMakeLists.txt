# ==========================================================================
# HeartOfMagic_BlackFlame -- "흑염 달구기" prototype
# ==========================================================================
# While the player's Stoke buff (HeartOfMagic_BlackFlame.esp) is active, the
# visuals of the player's own fire spells turn grey-black. A separate
# prototype: not part of the Heart of Magic release (not in Distribution.cmake
# or the FOMOD). CommonLibSSE-NG, compiler flags and vcpkg packages come from
# the top-level CMakeLists.txt super-build (same shape as plugins/BookXP).
# ==========================================================================

project(HeartOfMagic_BlackFlame VERSION ${PROJECT_VERSION} DESCRIPTION "Heart of Magic - Black Flame prototype" LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()

# EncodingUtils.cpp: PluginNames (plugin names with non-ASCII bytes in the
# catalog's persistentIds), as plugins/spelllearning SpellScannerFormId.cpp.
set(BLACKFLAME_SOURCES
    src/Main.cpp
    src/Config.cpp
    src/DevAutomation.cpp
    src/Forms.cpp
    src/FireSet.cpp
    src/FormCopies.cpp
    src/FormCopiesFill.cpp
    src/FormCopiesRefresh.cpp
    src/FormCopiesNames.cpp
    src/FormCopiesFillArt.cpp
    src/FormCopiesFillTexture.cpp
    src/FireClosure.cpp
    src/NifDump.cpp
    src/CopyRequest.cpp
    src/CopyRequestData.cpp
    src/CopyRequestDump.cpp
    src/Tester.cpp
    src/CopyFreshness.cpp
    src/Palette.cpp
    src/ThreadTrace.cpp
    src/TrackedMutex.cpp
    src/MainThreadQueue.cpp
    src/Perf.cpp
    src/Watchdog.cpp
    src/BuffState.cpp
    src/EffectEvents.cpp
    src/HitEffectHook.cpp
    src/HitPathObserve.cpp
    src/HitPathAttribution.cpp
    src/HitPathAddress.cpp
    src/CorpseMark.cpp
    src/MagicApplySink.cpp
    src/CorpseGrey.cpp
    src/CorpseColours.cpp
    src/CorpsePalettes.cpp
    src/SreProbe.cpp
    src/GameClock.cpp
    src/HitArt.cpp
    src/CloneWatch.cpp
    src/CopyModelProbe.cpp
    src/CopyModelProbeCache.cpp
    src/HazardHook.cpp
    src/Attribution.cpp
    src/PlayerArt.cpp
    src/ScriptTags.cpp
    src/PapyrusWrap.cpp
    src/GreyMap.cpp
    src/ImpactSwap.cpp
    src/Patch.cpp
    src/CopySwapLaunch.cpp
    src/CopySwapImpact.cpp
    src/CopySwapArt.cpp
    src/HandArtRefresh.cpp
    src/EquipAbilityRefresh.cpp
    src/CopySwapHazard.cpp
    src/HitEvents.cpp
    src/LanternDamage.cpp
    src/LanternPlace.cpp
    src/LanternIed.cpp
    src/HazardSwap.cpp
    src/GreyCheck.cpp
    src/Recolor.cpp
    src/EffectLight.cpp
    src/SehGuard.cpp
    src/CasterArt.cpp
    src/ProjectileHook.cpp
    src/ExplosionHook.cpp
    src/SelfTest.cpp
    src/SelfTestLog.cpp
    src/SelfTestJudge.cpp
    src/SelfTestProbe.cpp
    src/SelfTestSteps.cpp
    src/SelfTestStepsLate.cpp
    src/SelfTestStepsRemote.cpp
    src/SelfTestStepsV2.cpp
    src/SelfTestStepsV2Fire.cpp
    src/SelfTestStepsV2Flames.cpp
    src/SelfTestStepsV2Art.cpp
    src/SelfTestStepsHand.cpp
    src/SelfTestStepsV2Cover.cpp
    src/SelfTestStepsDecal.cpp
    src/SelfTestStepsBound.cpp
    src/SelfTestReload.cpp
    src/SelfTestText.cpp
    src/SelfTestWorld.cpp
    src/SelfTestDispel.cpp
    src/SelfTestAim.cpp
    # Single launch (P2 owns these files; P0 added the entries so no phase edits this list)
    src/FileIndex.cpp
    src/FileIndexInit.cpp
    src/FileIndexGuardCheck.cpp
    src/SlotLedger.cpp
    src/SlotAssign.cpp
    src/SlotTxst.cpp
    src/SelfTestStepsDecalSlots.cpp
    src/FormCopiesFillModel.cpp
    src/CopyGroups.cpp
    src/CopyGroupsNotes.cpp
    src/CopyModelProbeVerify.cpp
    src/MenuWatch.cpp
    src/BuilderLauncher.cpp
    src/BuildProgress.cpp
    src/BuildProgressCopy.cpp
    src/SelfTestStepsSlots.cpp
    # P2 additions: the slot route's kDataLoaded order and its key inputs
    src/SingleLaunch.cpp
    src/OldCopies.cpp
    src/OldCopiesVfs.cpp
    src/SlotKeys.cpp
    src/CopyRequestV5.cpp
    src/DataFiles.cpp
    src/CopyGroupsPlan.cpp
    src/CopyGroupsUpgrade.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/EncodingUtils.cpp
)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "HeartOfMagic"
    USE_ADDRESS_LIBRARY
    MINIMUM_SKSE_VERSION "2.0.12"
    VERSION ${PROJECT_VERSION}
    SOURCES ${BLACKFLAME_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/include
)

target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../PCH.h)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "HeartOfMagic_BlackFlame")

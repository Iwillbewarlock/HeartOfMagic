# ============================================================================
# PROJECT CONFIGURATION
# ============================================================================

# OpenMP support for parallel pairwise similarity loops
string(APPEND CMAKE_CXX_FLAGS " /openmp")

project(
    SpellLearning
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "AI-Generated Spell Learning Tree System"
    LANGUAGES CXX
)

set(PLUGIN_AUTHOR "Iwillbewarlock (original by Dinkel Zombie)")

# ============================================================================
# Build Configuration
# ============================================================================

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()


# Create SKSE plugin using CommonLibSSE function
# (CommonLib target and macro provided by top-level CMakeLists.txt)
add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "${PLUGIN_AUTHOR}"
    USE_ADDRESS_LIBRARY
    MINIMUM_SKSE_VERSION "2.0.12"
    VERSION ${PROJECT_VERSION}
    SOURCES
    src/Main.cpp
    src/RisaMenu.cpp
    src/uimanager/UIManagerCore.cpp
    src/uimanager/UIManagerNotify.cpp
    src/uimanager/UIManagerScanner.cpp
    src/uimanager/UIManagerTree.cpp
    src/uimanager/UIManagerAutoBuild.cpp
    src/uimanager/UIManagerPreReqMaster.cpp
    src/uimanager/UIManagerDeclutter.cpp
    src/uimanager/UIManagerProgression.cpp
    src/uimanager/UIManagerConfig.cpp
    src/uimanager/UIManagerConfigSave.cpp
    src/uimanager/UIManagerLocale.cpp
    src/uimanager/UIManagerIO.cpp
    src/EncodingUtils.cpp
    src/spellscanner/SpellScannerHelpers.cpp
    src/spellscanner/SpellScannerFormId.cpp
    src/spellscanner/SpellScannerJson.cpp
    src/spellscanner/SpellScannerEvidence.cpp
    src/spellscanner/SpellScannerChips.cpp
    src/spellscanner/SpellScannerCard.cpp
    src/spellscanner/SpellScannerCasters.cpp
    src/spellscanner/SpellScannerCopies.cpp
    src/spellscanner/SpellScannerGate.cpp
    src/spellscanner/SpellScannerDuplicates.cpp
    src/spellscanner/SpellScannerScan.cpp
    src/tomereach/TomeReachRecords.cpp
    src/tomereach/TomeReachConfig.cpp
    src/tomereach/TomeReachPass.cpp
    src/tomereach/TomeReach.cpp
    src/librarian/LibrarianRules.cpp
    src/librarian/LibrarianClassify.cpp
    src/librarian/LibrarianCatalog.cpp
    src/librarian/LibrarianTraits.cpp
    src/librarian/LibrarianAdapters.cpp
    src/librarian/LibrarianAdapterMatch.cpp
    src/librarian/LibrarianAdapterDamage.cpp
    src/librarian/LibrarianPerkAdapterPatch.cpp
    src/environment/EnvironmentTwins.cpp
    src/environment/EnvironmentSelfTest.cpp
    src/environment/EnvironmentSelfTestChecks.cpp
    src/HudNotify.cpp
    src/librarian/LibrarianLoadOrderSource.cpp
    src/librarian/LibrarianSpellResearch.cpp
    src/librarian/LibrarianSpellResearchPatch.cpp
    src/SpellCastHandler.cpp
    src/SpellCastImmediateHook.cpp
    src/progressionmanager/ProgressionManagerCore.cpp
    src/progressionmanager/ProgressionManagerTargets.cpp
    src/progressionmanager/ProgressionManagerCopies.cpp
    src/progressionmanager/ProgressionManagerXP.cpp
    src/progressionmanager/ProgressionManagerStudy.cpp
    src/progressionmanager/ProgressionManagerAPI.cpp
    src/progressionmanager/ProgressionManagerSerialization.cpp
    src/ISLIntegration.cpp
    src/SpellCastXPSource.cpp
    src/PassiveLearningSource.cpp
    src/spelleffectiveness/SpellEffectivenessHookCore.cpp
    src/spelleffectiveness/SpellEffectivenessHookDisplay.cpp
    src/spelleffectiveness/SpellEffectivenessHookGrant.cpp
    src/SpellTomeHook.cpp
    src/SpellTomeHookGate.cpp
    src/SpellTomeHookInventory.cpp
    src/SpellTomeHookSave.cpp
    src/SpellTomes.cpp
    src/CoSave.cpp
    src/PapyrusAPI.cpp
    src/treebuilder/TreeNLP.cpp
    src/treebuilder/TreeBuilderCore.cpp
    src/treebuilder/TreeBuilderLinks.cpp
    src/treebuilder/TreeBuilderPlugins.cpp
    src/treebuilder/TreeBuilderReachability.cpp
    src/treebuilder/TreeBuilderSimilarity.cpp
    src/treebuilder/TreeBuilderThemes.cpp
    src/treebuilder/TreeBuilderBridges.cpp
    src/treebuilder/TreeBuilderClassic.cpp
    src/treebuilder/TreeBuilderTypeGrouping.cpp
    src/treebuilder/LayoutDeclutter.cpp
    src/treebuilder/LayoutLineClear.cpp
    src/treebuilder/LayoutLineClearCost.cpp
    src/treebuilder/LayoutLineGrid.cpp
    src/treebuilder/LayoutMath.cpp
    src/treebuilder/SimdKernels.cpp
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../PCH.h
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    xbyak::xbyak
    hwy::hwy
    hwy::hwy_contrib
    ZLIB::ZLIB
)

# Highway foreach_target dispatch must not be merged into unity build groups
set_source_files_properties(src/treebuilder/SimdKernels.cpp PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
    SKIP_PRECOMPILE_HEADERS ON
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}"
)

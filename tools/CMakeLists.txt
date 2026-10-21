project(
    treebuilder-test
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "Standalone test harness for TreeBuilder algorithms"
    LANGUAGES CXX
)

set(SL_SRC_DIR "${CMAKE_SOURCE_DIR}/plugins/spelllearning/src")
set(SL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/plugins/spelllearning/include")

include_directories(${SL_INCLUDE_DIR})

add_executable(${PROJECT_NAME}
    treebuilder-test.cpp
    ${SL_SRC_DIR}/treebuilder/TreeNLP.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderCore.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderLinks.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderPlugins.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderReachability.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderSimilarity.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderClassic.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderTypeGrouping.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderThemes.cpp
    ${SL_SRC_DIR}/treebuilder/TreeBuilderBridges.cpp
    ${SL_SRC_DIR}/treebuilder/SimdKernels.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

# Stub headers (PCH.h, Common.h) MUST come first so they shadow the real
# plugin headers that pull in RE/Skyrim.h and SKSE/SKSE.h.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    hwy::hwy
    hwy::hwy_contrib
)

# Highway foreach_target dispatch must not be merged into unity build groups
set_source_files_properties(${SL_SRC_DIR}/treebuilder/SimdKernels.cpp PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
)

# =============================================================================
# librarian-test  -  offline harness for the tag librarian
# =============================================================================
# Compiles the same classifier the plugin uses, so the coverage and precision
# it reports are the plugin's own numbers, not a second implementation's.

add_executable(librarian-test
    librarian-test.cpp
    librarian-score.cpp
    librarian-vocab-check.cpp
    librarian-adapters.cpp
    librarian-spellresearch.cpp
    ${SL_SRC_DIR}/librarian/LibrarianRules.cpp
    ${SL_SRC_DIR}/librarian/LibrarianAdapters.cpp
    ${SL_SRC_DIR}/librarian/LibrarianSpellResearch.cpp
    ${SL_SRC_DIR}/librarian/LibrarianClassify.cpp
    ${SL_SRC_DIR}/librarian/LibrarianCatalog.cpp
    ${SL_SRC_DIR}/librarian/LibrarianTraits.cpp
)

target_compile_features(librarian-test PRIVATE cxx_std_23)

# Stub headers first, same reason as above.
target_include_directories(librarian-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(librarian-test PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# =============================================================================
# tome-reach-test  -  offline harness for the tome reach pass
# =============================================================================
# The plugin's own TomeReach sources: which tree spells have tomes nothing in
# the load order hands out (inputs from lab/tome-reach/make_inputs.py).

add_executable(tome-reach-test
    tome-reach-test.cpp
    tome-reach-selftest.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachRecords.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachConfig.cpp
    ${SL_SRC_DIR}/tomereach/TomeReachPass.cpp
)

target_compile_features(tome-reach-test PRIVATE cxx_std_23)

target_include_directories(tome-reach-test PRIVATE
    ${SL_SRC_DIR}
)

target_link_libraries(tome-reach-test PRIVATE
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)

# =============================================================================
# declutter-test  -  offline harness for the native tree declutter pass
# =============================================================================
# The plugin's own LayoutDeclutter sources, so a tree run here comes out as it
# does for the panel; compare with modules/layoutDeclutter.js on the same tree.

add_executable(declutter-test
    declutter-test.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutDeclutter.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineClear.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineClearCost.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutLineGrid.cpp
    ${SL_SRC_DIR}/treebuilder/LayoutMath.cpp
)

target_compile_features(declutter-test PRIVATE cxx_std_23)

# Stub headers first, same reason as above.
target_include_directories(declutter-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SL_INCLUDE_DIR}
    ${SL_SRC_DIR}
)

target_link_libraries(declutter-test PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# =============================================================================
# tree-save-check  -  offline check of SaveSpellTree's no-spells guard
# =============================================================================
# The plugin's own TreeSaveCheck.h (header only, no game types): --self-test on
# made-up trees, or tree files checked against a full parse.

add_executable(tree-save-check
    tree-save-check.cpp
)

target_compile_features(tree-save-check PRIVATE cxx_std_23)

target_include_directories(tree-save-check PRIVATE
    ${SL_INCLUDE_DIR}
)

target_link_libraries(tree-save-check PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# blackflame-greybuild  -  grey duplicates of shared-particle fire meshes
# =============================================================================
# Black Flame (plugins/BlackFlame/README.md, "Grey duplicates"): grey copies of
# master particle system NIFs and impact / hazard NIFs whose AddOnNodes point at
# the grey ADDN indices. nifly (GPL-3.0) is linked here only - never into a DLL.

find_package(nifly CONFIG REQUIRED)
# DirectXTex (MIT): the texture copies (GreyTexture.cpp) decode, recolour and encode DDS files; linked here only. Its
# dx11 feature (vcpkg.json) adds the DirectCompute BC6H/BC7 encoder the builder uses for BC7 targets (GreyGpuEncoder.h)
find_package(directxtex CONFIG REQUIRED)

# One folder per configuration: a Debug and a Release build at the same time each embed their own identity
set(BLACKFLAME_IDENTITY_DIR "${CMAKE_CURRENT_BINARY_DIR}/blackflame-greybuild-identity/$<CONFIG>")

add_executable(blackflame-greybuild
    blackflame-greybuild/main.cpp
    blackflame-greybuild/GreyMap.cpp
    blackflame-greybuild/GreyNif.cpp
    blackflame-greybuild/GreyDump.cpp
    blackflame-greybuild/GreyRoundTrip.cpp
    blackflame-greybuild/GreyRun.cpp
    blackflame-greybuild/GreyRequest.cpp
    blackflame-greybuild/GreyEsl.cpp
    blackflame-greybuild/GreySelfTest.cpp
    blackflame-greybuild/GreySelfTestPlan.cpp
    blackflame-greybuild/GreySelfTestForms.cpp
    blackflame-greybuild/GreySelfTestGame.cpp
    blackflame-greybuild/GreyFormsRequest.cpp
    blackflame-greybuild/GreyFormsMap.cpp
    blackflame-greybuild/GreyFormsTxst.cpp
    blackflame-greybuild/GreyFormsTextureSets.cpp
    blackflame-greybuild/GreyFormsEsl.cpp
    blackflame-greybuild/GreyFormsRun.cpp
    blackflame-greybuild/GreyFromGame.cpp
    blackflame-greybuild/GreyTexture.cpp
    blackflame-greybuild/GreyTextureNif.cpp
    blackflame-greybuild/GreySelfTestTexture.cpp
    blackflame-greybuild/GreySelfTestStats.cpp
    blackflame-greybuild/GreySelfTestPaths.cpp
    blackflame-greybuild/GreyShapes.cpp
    blackflame-greybuild/GreyTexStats.cpp
    blackflame-greybuild/GreyRisk.cpp
    blackflame-greybuild/GreyGlow.cpp
    blackflame-greybuild/GreyPalette.cpp
    blackflame-greybuild/GreyMarble.cpp
    blackflame-greybuild/GreyParticleGraph.cpp
    blackflame-greybuild/GreyHandFx.cpp
    blackflame-greybuild/GreySelfTestRisk.cpp
    blackflame-greybuild/GreySelfTestDecal.cpp
    # Single launch (P1 owns these files; P0 added the entries)
    blackflame-greybuild/GreySlotPool.cpp
    blackflame-greybuild/GreyKeyedRun.cpp
    blackflame-greybuild/GreyProgress.cpp
    blackflame-greybuild/GreyBudget.cpp
    blackflame-greybuild/GreyKeyedRequest.cpp
    blackflame-greybuild/GreyKeyedTextures.cpp
    blackflame-greybuild/GreyKeyedEfsh.cpp
    blackflame-greybuild/GreySelfTestEfsh.cpp
    # Black Flame v2.1: the effect shader base textures (contract 8)
    blackflame-greybuild/GreyKeyedBases.cpp
    blackflame-greybuild/GreySelfTestBases.cpp
    blackflame-greybuild/GreySelfTestSlots.cpp
    blackflame-greybuild/GreySelfTestKeyed.cpp
    blackflame-greybuild/GreySelfTestHands.cpp
    blackflame-greybuild/GreySelfTestMarble.cpp
    blackflame-greybuild/GreySelfTestMarbleOrder.cpp
    blackflame-greybuild/GreySelfTestHandFx.cpp
    blackflame-greybuild/GreySelfTestKeyedRuns.cpp
    # Output identity (CopyContractKeys.h kBuilderOutputVersion) and its golden-output test
    blackflame-greybuild/GreySelfTestGolden.cpp
    blackflame-greybuild/GreySelfTestGoldenConstants.cpp
    blackflame-greybuild/GreyGoldenFixtures.cpp
    # BC7 on the GPU (DirectXTex's dx11 feature) with its guards, and their self-test
    blackflame-greybuild/GreyGpuEncoder.cpp
    blackflame-greybuild/GreySelfTestGpu.cpp
    # The Black Flame lantern's meshes and soot texture (--write-lantern) and their self-test
    blackflame-greybuild/GreyLantern.cpp
    blackflame-greybuild/GreySelfTestLantern.cpp
    ${BLACKFLAME_IDENTITY_DIR}/GreyIdentity.rc
)

# The output identity ("<n>" or "<n>+dev<digest>", GreyIdentity.cmake): written at build time, not at configure, so a
# source edit or a Debug build is seen by the next build; the script rewrites its two files only when the text changes.
# GreyIdentityText.h is the builder's compiled copy, GreyIdentity.rc the RCDATA resource the DLL reads (CopyContractIdentity.h)
find_program(BLACKFLAME_GIT git)
add_custom_target(blackflame-greybuild-identity
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -DOUT_DIR=${BLACKFLAME_IDENTITY_DIR} -DCONFIG=$<CONFIG>
            -DGIT=${BLACKFLAME_GIT} -P ${CMAKE_CURRENT_SOURCE_DIR}/blackflame-greybuild/GreyIdentity.cmake
    BYPRODUCTS ${BLACKFLAME_IDENTITY_DIR}/GreyIdentityText.h ${BLACKFLAME_IDENTITY_DIR}/GreyIdentity.rc
    COMMENT "Black Flame builder: output identity"
    VERBATIM
)
add_dependencies(blackflame-greybuild blackflame-greybuild-identity)
target_include_directories(blackflame-greybuild PRIVATE ${BLACKFLAME_IDENTITY_DIR})

# The golden test's committed inputs (GreySelfTestGolden.cpp), and the library versions its constant digest records:
# DirectXTex's DIRECTX_TEX_VERSION is read from its header; nifly has no version macro, so its vcpkg port version
# ("<version>#<port-version>") comes from the installed package's SPDX file ("unknown" when that cannot be read)
set(BLACKFLAME_NIFLY_PORT "unknown")
set(BLACKFLAME_DIRECTXTEX_PORT "unknown")
foreach(port nifly directxtex)
    if(EXISTS "${${port}_DIR}/vcpkg.spdx.json")
        file(READ "${${port}_DIR}/vcpkg.spdx.json" spdx_text)
        string(JSON spdx_version ERROR_VARIABLE spdx_error GET "${spdx_text}" packages 0 versionInfo)
        if(NOT spdx_error)
            # cl.exe reads a '#' in a /D value as '=': "<version>#<n>" goes in as "<version> port <n>"
            string(REPLACE "#" " port " spdx_version "${spdx_version}")
            string(TOUPPER "${port}" port_upper)
            set(BLACKFLAME_${port_upper}_PORT "${spdx_version}")
        endif()
    endif()
endforeach()
# No fixture folder is compiled in (a developer's absolute path would ship in the exe): the POST_BUILD gate and the package
# script pass --golden-dir
target_compile_definitions(blackflame-greybuild PRIVATE
    BLACKFLAME_NIFLY_PORT="${BLACKFLAME_NIFLY_PORT}"
    BLACKFLAME_DIRECTXTEX_PORT="${BLACKFLAME_DIRECTXTEX_PORT}"
)

# C++17 (std::format via fmt): nifly's Matrix3/Matrix4 declare a non-const operator==, which
# C++20's reversed comparison candidates make ambiguous for MSVC (error C2666 in Object3d.hpp)
find_package(fmt CONFIG REQUIRED)
set_target_properties(blackflame-greybuild PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# shell32 / ole32: SHGetKnownFolderPath and CoTaskMemFree (--from-game finds Documents); d3d11 / dxgi: the GPU BC7
# encoder's device and adapter (GreyGpuEncoder.cpp). Windows SDK, no new package; the builder only, never a DLL.
# OpenMP: the builder's own #pragma omp (ComputeStats, GreyTexStats.cpp) and omp_set_num_threads (GreyBudget.cpp), linked
# here by name rather than through DirectXTex's target (which links it too, so nothing new is loaded: vcomp140.dll)
find_package(OpenMP REQUIRED COMPONENTS CXX)
target_link_libraries(blackflame-greybuild PRIVATE
    shell32
    ole32
    d3d11
    dxgi
    nifly
    Microsoft::DirectXTex
    OpenMP::OpenMP_CXX
    fmt::fmt
    nlohmann_json::nlohmann_json
)

# The whole --self-test after every link, the golden-output test included (its fixtures required): a failure fails the
# build, so an output change without a kBuilderOutputVersion bump cannot be built past. Its output goes to a log; the
# build prints the summary, the time and the FAIL lines (GreySelfTestGate.cmake)
add_custom_command(TARGET blackflame-greybuild POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:blackflame-greybuild>
            -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/blackflame-greybuild/golden
            -DLOG=$<TARGET_FILE_DIR:blackflame-greybuild>/blackflame-greybuild-selftest.log
            -P ${CMAKE_CURRENT_SOURCE_DIR}/blackflame-greybuild/GreySelfTestGate.cmake
    COMMENT "Black Flame builder: --self-test (golden output gate)"
    VERBATIM
)

# =============================================================================
# blackflame-cosave-check  -  offline check of Black Flame's BFSL co-save record
# =============================================================================
# plugins/BlackFlame/src/SlotCoSave.h (header only, no game types) on byte buffers and the copy contract's shared test
# table (CopyContractTest.h): --self-test. C++17 like the builder, so the contract headers are checked at the
# standard both sides build them with. Entry frozen by P0 of the single-launch design; P2 owns the sources.

add_executable(blackflame-cosave-check
    blackflame-cosave-check/main.cpp
    blackflame-cosave-check/SkseFile.cpp
)

set_target_properties(blackflame-cosave-check PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_include_directories(blackflame-cosave-check PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/BlackFlame/src
)

target_link_libraries(blackflame-cosave-check PRIVATE
    nlohmann_json::nlohmann_json
)

# IdentityTests.h reads the output identity of the blackflame-greybuild.exe built beside it (the DLL's read): built first
add_dependencies(blackflame-cosave-check blackflame-greybuild)

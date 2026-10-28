#pragma once

// tree-remap-check's case types, shared by its two files: tree-remap-check.cpp (the
// runner and the load order cases) and tree-remap-check-rematch.cpp (the editor id
// rematch cases, EditorIdRematch.h).

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TreeRemapCheck
{
    /// A loaded spell with an editor id (powerofthree's Tweaks)
    struct FakeSpell
    {
        std::string editorId;
        std::uint32_t formId = 0;
        std::string school;
        std::string persistentId;
    };

    /// The current load order as the judge sees it
    struct FakeGame
    {
        std::map<std::string, std::string> resolves;  ///< persistentId -> formId now (absent: plugin or record gone)
        std::set<std::string> loadedPlugins;          ///< plugins whose missing ids are a missing record, not plugin
        std::set<std::string> legacyValid;            ///< formIds a node without a persistentId still finds
        std::set<std::string> notPlayerSpells;        ///< formIds that resolve but are no player's spell
        std::vector<FakeSpell> spells;                ///< loaded spells with an editor id
    };

    struct Expect
    {
        std::vector<std::string> kept;  ///< spell names left in the tree
        int resolved = 0;
        int invalid = 0;
        int collisions = 0;
        int followed = 0;
        std::vector<std::string> missingPlugins;
        std::vector<std::string> missingRecords;
        std::map<std::string, std::string> formIds;  ///< "school/name" -> formId the node must have now
        int rematched = 0;                           ///< kept nodes found again by editor id
        int rematchClashes = 0;                      ///< rematched nodes dropped for a spell their school has
        std::map<std::string, std::string> persistentIds;  ///< "school/name" -> persistentId the node must have now
    };

    struct Case
    {
        const char* name;
        const char* tree;  ///< the saved tree, as JSON text
        FakeGame game;
        Expect expect;
    };

    // tree-remap-check-rematch.cpp
    /// The tree cases of a node found again by editor id (run by tree-remap-check.cpp's RunCase)
    std::vector<Case> RematchCases();
    /// EditorIdRematch.h on its own: the one candidate in the node's school, else none; 0 = passed
    int RunRematchCase();
}

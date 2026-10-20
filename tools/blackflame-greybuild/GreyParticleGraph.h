#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace nifly { class NiHeader; class NiObject; class NiRef; }
namespace GreyBuild::HandGraph
{
    using Remap = std::map<std::uint32_t, std::uint32_t>;
    void ChildRefs(nifly::NiObject& block, std::set<nifly::NiRef*>& refs);
    std::uint32_t CloneGraph(nifly::NiHeader& dst, nifly::NiHeader& src,
        std::uint32_t id, std::uint32_t root, Remap& map);
    bool DropShaderController(nifly::NiHeader& hdr, std::uint32_t id);
    void RemapPointers(nifly::NiHeader& hdr, const Remap& map);
    void ScaleFloat(nifly::NiHeader& hdr, std::uint32_t id, float scale, std::set<std::uint32_t>& done);
    void CloneSequenceLinks(nifly::NiHeader& dst, nifly::NiHeader& src, std::uint32_t root,
        Remap& map, const std::string& oldName, const std::string& newName, bool shaderOnly = false);
    void Attach(nifly::NiHeader& dst, nifly::NiHeader& src, std::uint32_t root,
        std::uint32_t added, const std::string& name);
}

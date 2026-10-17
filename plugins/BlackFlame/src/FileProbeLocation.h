#pragma once

#include "Common.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// =============================================================================
// FILE PROBE LOCATION - test B of the dev file visibility probe: a BSResource::Location serving files from memory
// =============================================================================
// One Location subclass (RE/L/Location.h: DoCreateStream 03, DoTraversePrefix 05, DoGetInfo1 06, DoGetInfo2 07, DoGetName
// 09) handing out a Stream subclass (RE/S/Stream.h) over bytes held in memory, registered with BSResource::RegisterLocation
// (RE/B/BSResource.h, 68476 / 69829) at the probe's time. Paths are compared lower case with '\' separators, a leading
// "data\" dropped. Every call into the location is counted; the first calls (any path, to see the form the engine uses)
// and every call that names the probe's virtual folder are logged with their raw path.
// Lifetime: the engine keeps the location's pointer for the rest of the process and CommonLib has no unregister, so the
// location is allocated once on the game heap (Location's TES_HEAP_REDEFINE_NEW) and never freed. Its streams are counted
// at the engine's own Stream count offset (FileProbeEngine.h), never through CommonLib's BSTSmartPointer: the engine
// releases them, and at zero deletes them through their virtual destructor (our class's operator delete: the game heap).

namespace BlackFlame::FileProbe::VirtualFiles
{
    /// The virtual folder the probe serves under meshes\ and textures\ (lower case, '\' separators)
    inline constexpr const char* kFolder = "heartofmagic\\blackflame\\vfsprobe\\";

    struct File
    {
        std::string key;  // "meshes\\heartofmagic\\..." / "textures\\heartofmagic\\...", lower case
        std::shared_ptr<const std::vector<std::byte>> bytes;
    };

    /// Main thread, once: the location made over a_files and registered at a_priority. False (a_note says why) when the
    /// engine's stream count is unknown, it was registered already, or the call faulted
    [[nodiscard]] bool Register(std::vector<File> a_files, std::uint32_t a_priority, std::string& a_note);

    /// What the engine asked the location so far (any thread)
    [[nodiscard]] std::string CountersText();
}

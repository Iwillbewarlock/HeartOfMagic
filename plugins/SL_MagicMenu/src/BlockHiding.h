#pragma once

#include <string>
#include <vector>

// =============================================================================
// BLOCK HIDING - the game side of HiddenBlocks.h: [Blocks] sHidden read from
// SL_MagicMenu.ini (the path LangGame.h reads) at plugin load and at each magic
// menu open, kept for the widget (applied to that menu) and the settings tab (its
// first look). Read on the game thread; Setting() from any thread.
// =============================================================================

namespace BlockHiding
{
    /// Reads [Blocks] sHidden (HiddenBlocks::Parse) and keeps it; logged when it differs from the last read. a_when: "plugin
    /// load", "menu open" (the log line)
    void Read(const char* a_when);

    /// The ids of the last Read as written (unknown ones too), in their order
    std::vector<std::string> Setting();
}

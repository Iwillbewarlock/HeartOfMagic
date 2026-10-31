#include "Common.h"

#include "BlockHiding.h"

#include "HiddenBlocks.h"
#include "LangGame.h"

#include <mutex>
#include <optional>

namespace BlockHiding
{
    namespace
    {
        std::mutex g_lock;
        std::optional<std::vector<std::string>> g_setting;  // nullopt until the first Read
    }

    void Read(const char* a_when)
    {
        const auto value = LangGame::IniString(HiddenBlocks::kSection, HiddenBlocks::kKey, static_cast<std::uint32_t>(HiddenBlocks::kMaxChars));
        auto ids = HiddenBlocks::Parse(value);
        const std::scoped_lock lock(g_lock);
        if (g_setting && *g_setting == ids) return;
        logger::info("Blocks: [Blocks] sHidden at {} = '{}' - {}", a_when, HiddenBlocks::Join(ids),
            ids.empty() ? "every block shown" : std::format("{} id(s) to hide (applied at a magic menu open)", ids.size()));
        g_setting = std::move(ids);
    }

    std::vector<std::string> Setting()
    {
        const std::scoped_lock lock(g_lock);
        return g_setting.value_or(std::vector<std::string>{});
    }
}

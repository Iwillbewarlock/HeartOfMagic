#pragma once

// Spell Research - Prisma UI: public SKSE message interface.
//
// Lets another SKSE plugin (the MagicMenuRewrite add-on first) open a Spell
// Research window. Copy this header into your plugin; it has no dependencies.
//
//   Open:   SKSE::GetMessagingInterface()->Dispatch(SRPrismaAPI::kMessageOpen,
//               &msg, sizeof(msg), SRPrismaAPI::kPluginName);
//   Reply:  register a listener for SRPrismaAPI::kPluginName at kPostLoad;
//           replies arrive as kMessageReply, addressed to your plugin.
//
// The sender may have the MagicMenu open: the request waits (up to 2 s) for
// it to close - closing a TweenMenu it drops back to - then runs the same
// checks the Spell Research hotkey makes.
// Journal and Alembic reply kAccepted when the flow was started - they are
// script flows, and "opened" is not knowable at that point. The experience
// window replies kOpened once it is visible (also when it already was).

#include <cstdint>

namespace SRPrismaAPI
{
	inline constexpr const char* kPluginName = "SpellResearchPrismaUI";

	// Written as hex, not 'SRPO'/'SRPR': multi-character literals are
	// implementation-defined and warn on GCC/Clang. Same values MSVC gives.
	inline constexpr std::uint32_t kMessageOpen = 0x5352504F;   // 'SRPO'
	inline constexpr std::uint32_t kMessageReply = 0x53525052;  // 'SRPR'

	inline constexpr std::uint32_t kOpenVersion = 1;   // layout of OpenMessage
	inline constexpr std::uint32_t kReplyVersion = 1;  // layout of ReplyMessage

	enum class What : std::uint32_t
	{
		kExperience = 1,  // the archetype experience window
		kJournal = 2,     // Research Journal (needs the journal carried)
		kAlembic = 3      // Alembic (needs the alembic carried)
	};

	enum class Result : std::uint32_t
	{
		kAccepted = 0,    // journal / alembic flow started
		kBusy = 1,        // a Spell Research panel, flow or blocking menu is up; try later
		kRefused = 2,     // not allowed now: item missing, in combat, setup not done, UI missing
		kBadMessage = 3,  // wrong size, version or what
		kOpened = 4       // experience window is visible
	};

	struct OpenMessage
	{
		std::uint32_t version;    // kOpenVersion
		std::uint32_t requestId;  // yours; echoed in the reply
		std::uint32_t what;       // What
	};
	static_assert(sizeof(OpenMessage) == 12);

	struct ReplyMessage
	{
		std::uint32_t version;    // kReplyVersion
		std::uint32_t requestId;  // from the OpenMessage (0 if it could not be read)
		std::uint32_t what;       // from the OpenMessage (0 if it could not be read)
		std::uint32_t result;     // Result
	};
	static_assert(sizeof(ReplyMessage) == 16);
}

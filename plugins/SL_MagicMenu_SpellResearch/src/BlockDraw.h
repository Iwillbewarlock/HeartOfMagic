#pragma once

#include <cstdint>

// =============================================================================
// BLOCK DRAW - the block "Research Archetypes" (BlockApi v2 drawSelection and
// click), the three links' open requests to SR Prisma ('SRPO') and their replies
// ('SRPR'). Draw and click run on the UI thread (SL_MagicMenu's frame handler).
// Include after Common.h (RE and SKSE types).
// =============================================================================

namespace BlockDraw
{
    /// BlockApi::DrawSelectionFn: the archetypes of the card's spell (a_shown), the links, the reason line
    void Draw(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void* a_user);

    /// BlockApi::ClickFn: a press and release on the same link sends SR Prisma its open request
    void Click(float a_pressX, float a_pressY, float a_releaseX, float a_releaseY, void* a_user);

    /// The magic menu opened: a "does not respond" from the last menu is forgotten, the drawn-id self-test count restarts
    void OnMenuOpen();

    /// The magic menu closed: a notification held while it was open is shown (in an SKSE task, on the game thread)
    void OnMenuClose();

    /// A game is being loaded or started: a held notification belongs to the old one
    void ForgetHeld();

    /// A game is being loaded or started: an open request still waiting for its reply is dropped (SR Prisma answers it with
    /// Busy on the load, which must not show in the loaded game)
    void ForgetPending();

    /// SR Prisma's messages (the listener for "SpellResearchPrismaUI"); any thread
    void OnSrPrismaMessage(SKSE::MessagingInterface::Message* a_msg);
}

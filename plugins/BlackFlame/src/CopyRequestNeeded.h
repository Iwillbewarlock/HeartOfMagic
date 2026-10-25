#pragma once

// =============================================================================
// COPY REQUEST NEEDED - the dump files the builder will read this start (disk trim, 2026-10-26; DiskTrimRules.h)
// =============================================================================
// kDataLoaded, after BuildProgress::ReadAtStart and WritePublished, before CopyRequestV5 dumps (the NIF bytes still held).
// The objects are the request's: every usable slot's model, muzzle and TXST key, every keyed effect shader palette and
// base. A model or muzzle object reads its NIF and every texture that NIF names (NifDump::TexturePathsIn, the plan's own
// rule); a TXST object its eight slot textures; a palette or base its one texture. Our two palettes are always read.

#include "Common.h"

#include "DiskTrimRules.h"
#include "SlotKeys.h"

namespace BlackFlame::CopyRequestNeeded
{
    /// a_startRead: ReadAtStart read progress.jsonl to its end; a_listWritten: WritePublished wrote the list. Either false:
    /// the whole dump (all)
    [[nodiscard]] DiskTrimRules::Needed Compute(const SlotKeys::Inputs& a_inputs, bool a_startRead, bool a_listWritten);
}

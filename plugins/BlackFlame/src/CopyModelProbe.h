#pragma once

#include "Common.h"

// =============================================================================
// COPY MODEL PROBE - does the game load every copy NIF? (v2i, causes B: Flame Cloak, Dragonfire, Incinerate)
// =============================================================================
// Once on the main thread after kDataLoaded (our queue's first frame, the main menu): BSModelDB::Demand for every
// distinct model path of the filled copies - the record's model of each PROJ, EXPL, IPCT, HAZD and ARTO copy and a PROJ
// copy's muzzle flash model - as the engine's own LoadGraphics asks for it (path as the record stores it, default
// ArgsType = RequestModel2's {0, 3, 0x1010001}). A summary (ok, failed, ms), every failure with its ErrorCode, and the
// paths left out when the time budget runs out. Safety net: a copy whose path failed while its SOURCE's same model path
// loaded (the control: a probe that fails for everything takes nothing out) is taken out of CopyOf for the session
// (FormCopies.h DisableCopiesForSession), so that form stays original instead of invisible.

namespace BlackFlame
{
    /// kDataLoaded, after the copies are filled: queues the probe for the main thread
    void QueueCopyModelProbe();
}

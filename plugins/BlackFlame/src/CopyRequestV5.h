#pragma once

// =============================================================================
// COPY REQUEST V5 - request.json for the builder in key mode, and the dump beside it (single launch)
// =============================================================================
// The slot route's request (CopyContract.h Request). Everything version 4 had (CopyRequest.h: game, forms with their
// scalars, excluded, addons, models, nifs, textures, textureCaps, dumpComplete) and:
//   version 5, contractVersion at the top level (CopyContract kContractVersion)
//   options      { tint, tintAdd (QuantizedTint of the config's values, the numbers the key prefix holds), paletteMode,
//                  keyPrefix (KeyPrefixString: the builder rebuilds it with VerifyKeyPrefix) }
//   forms[]      key (the model key16 of the form's slot, when it has one), slot (its local id: information)
//   muzzles[]    { path (the PROJ muzzle as its record stores it), key }
//   addnMps[]    { slot, path (the source ADDN's model), key, index (the source ADDN's AddOnNode index the copies'
//                AddOnNodes are remapped from; contract v4 names it), source (information) }
//   txst[]       { slot, key, dodt }
//   efshPalettes[] contract 6 (CopyContractEfsh.h EfshPalettesJson): { path, role, state, key (state ok) }
//   efshBases[]    contract 8 (CopyContractEfshBase.h EfshBasesJson): the same shape, role membrane-fill / particle-base / both
// The dump (data\meshes, data\textures, dump-manifest.json; CopyRequestDump.h) holds the very bytes the keys hashed (the
// NIFs read at kDataLoaded, the planned textures); it is rewritten only when its fingerprint changed. With the config's
// tint or tintAdd outside 0..2 there is no key prefix (SlotKeys) and no request is written (logged).
// (disk trim, 2026-10-26) The dump holds only the files a_needed names (CopyRequestNeeded.h: the sources of the objects the
// builder will build this start), unless a_needed.all; the request is the same either way.

#include "Common.h"

#include "DiskTrimRules.h"
#include "FireClosure.h"
#include "SlotKeys.h"

#include <set>
#include <string>

namespace BlackFlame::CopyRequestV5
{
    struct Result
    {
        bool written = false;
        bool dumpComplete = false;  // set by the dump; false whenever nothing was written
        std::string dumpState;
        std::size_t forms = 0;     // forms with a key
        std::size_t muzzles = 0;
        std::size_t addnMps = 0;
        std::size_t txst = 0;
        std::size_t efshPalettes = 0;  // listed
        std::size_t efshKeyed = 0;     // of them, keyed (in the dump)
        std::size_t efshUnread = 0;    // of them, unread in textures[] (the game could not read it this start)
        std::size_t efshBases = 0;     // contract 8: base textures listed
        std::size_t efshBasesKeyed = 0;
        long long ms = 0;
    };

    /// kDataLoaded (singleLaunch), after the slots and their keys: the dump, then request.json v5 (temp + rename)
    Result Write(const FireClosure& a_closure, SlotKeys::Inputs& a_inputs, const DiskTrimRules::Needed& a_needed);

    /// kDataLoaded, before the plan (SlotKeys): the base texture paths the last request.json keyed (efshBases with state ok;
    /// the dump plan puts them first in their tier). Empty when there is no such request, it does not parse or is older
    [[nodiscard]] std::set<std::string> LastKeyedBases();
}

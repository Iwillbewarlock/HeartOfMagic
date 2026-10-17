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
// The dump (data\meshes, data\textures, dump-manifest.json; CopyRequestDump.h) holds the very bytes the keys hashed (the
// NIFs read at kDataLoaded, the planned textures); it is rewritten only when its fingerprint changed. With the config's
// tint or tintAdd outside 0..2 there is no key prefix (SlotKeys) and no request is written (logged).

#include "Common.h"

#include "FireClosure.h"
#include "SlotKeys.h"

#include <string>

namespace BlackFlame::CopyRequestV5
{
    struct Result
    {
        bool written = false;
        bool dumpComplete = true;
        std::string dumpState;
        std::size_t forms = 0;     // forms with a key
        std::size_t muzzles = 0;
        std::size_t addnMps = 0;
        std::size_t txst = 0;
        long long ms = 0;
    };

    /// kDataLoaded (singleLaunch), after the slots and their keys: the dump, then request.json v5 (temp + rename)
    Result Write(const FireClosure& a_closure, SlotKeys::Inputs& a_inputs);
}

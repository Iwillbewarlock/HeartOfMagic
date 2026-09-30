# tome-reach

Which spell tomes nothing in a load order hands out (docs/ARCHITECTURE.md, "Tomes nothing hands out").

- `tome_reach.py` - the first measurement (Python, reads an MO2 profile directly). It is the spec the plugin's
  C++ pass (`plugins/spelllearning/src/tomereach/`) was written against.
- `expected-takealook-2026-09-30.txt` - what it found on the TAKEALOOK profile `TKL - MUNG ADDON`: 46 tree
  spells from 11 mods.
- `make_inputs.py <MO2 root> <profile> > inputs.json` - the load order and data folders for
  `build/tools/Release/tome-reach-test.exe -i inputs.json -s spell_scan_output.json`, which runs the plugin's
  own pass and must list the same spells.

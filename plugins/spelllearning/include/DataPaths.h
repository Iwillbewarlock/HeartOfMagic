#pragma once

// =============================================================================
// DATA PATHS - the data files AutoScan, the tree graft, ScanRecord and GetUnifiedConfigPath
// read, named once (the scanner's own output and catalog writers, and the environment
// checks, still spell theirs out).
// The tree and its record go through UIManager::GetTreeFilePath and
// GetTreeLoadOrderFilePath; selftest.json is Environment::SelfTest::kSwitchFile.
// =============================================================================

namespace DataPaths
{
    /// The panel's settings (GetUnifiedConfigPath; AutoScan's setting and the graft's player rules read it)
    inline constexpr const char* kConfig = "Data/SKSE/Plugins/SpellLearning/config.json";
    /// The librarian's catalog of the last full scan (ScanRecord.h, the graft's seen list baseline)
    inline constexpr const char* kCatalog = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";
    /// A full scan's dump (SpellScanner::WriteScanOutput; the graft reads the scan at game start's)
    inline constexpr const char* kScanOutput = "Data/SKSE/Plugins/SpellLearning/spell_scan_output.json";
}

// =============================================================================
// tome-reach-test --self-test  -  the TomeReach reader on small made-up plugins
// =============================================================================
//
// Writes a handful of plugins to a temp folder, runs the plugin's own pass on
// them and checks every way a tome is reached (placed, compressed inventory,
// a script property behind an XXXX size, a distribution config, a light
// plugin's 12-bit ids), the tomes nothing reaches, four kinds of damaged plugin
// (their own and their masters' tomes count as reached, no other plugin's), a
// plugin that cannot be opened or whose header is cut, and the config matching
// rules. Needs no game and no load order.

#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachPass.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    using Bytes = std::vector<unsigned char>;

    constexpr std::uint32_t kCompressedFlag = 0x00040000;
    constexpr std::uint32_t kLightFlag = 0x00000200;
    constexpr std::uint8_t kTeachesSpell = 0x04;
    constexpr std::uint32_t kGroupHeaderSize = 24;  // a GRUP's size counts its own header

    int g_failed = 0;
    int g_passed = 0;

    void Check(bool ok, const char* what)
    {
        (ok ? g_passed : g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }

    void Put32(Bytes& b, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i)));
    }

    void Put16(Bytes& b, std::uint16_t v)
    {
        b.push_back(static_cast<unsigned char>(v));
        b.push_back(static_cast<unsigned char>(v >> 8));
    }

    void PutTag(Bytes& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

    Bytes Sub(const char* tag, const Bytes& data)
    {
        Bytes b;
        PutTag(b, tag);
        Put16(b, static_cast<std::uint16_t>(data.size()));
        b.insert(b.end(), data.begin(), data.end());
        return b;
    }

    // A subrecord written behind an XXXX that carries its real size
    Bytes BigSub(const char* tag, const Bytes& data)
    {
        Bytes size;
        Put32(size, static_cast<std::uint32_t>(data.size()));
        Bytes b = Sub("XXXX", size);
        PutTag(b, tag);
        Put16(b, 0);
        b.insert(b.end(), data.begin(), data.end());
        return b;
    }

    Bytes U32Bytes(std::uint32_t v)
    {
        Bytes b;
        Put32(b, v);
        return b;
    }

    Bytes Str(const std::string& s)
    {
        Bytes b(s.begin(), s.end());
        b.push_back(0);
        return b;
    }

    Bytes Record(const char* type, std::uint32_t flags, std::uint32_t formId, const Bytes& body)
    {
        Bytes stored = body;
        if (flags & kCompressedFlag) {
            uLongf length = compressBound(static_cast<uLong>(body.size()));
            Bytes packed(length);
            compress2(packed.data(), &length, body.data(), static_cast<uLong>(body.size()), Z_BEST_SPEED);
            packed.resize(length);
            stored.clear();
            Put32(stored, static_cast<std::uint32_t>(body.size()));
            stored.insert(stored.end(), packed.begin(), packed.end());
        }
        Bytes b;
        PutTag(b, type);
        Put32(b, static_cast<std::uint32_t>(stored.size()));
        Put32(b, flags);
        Put32(b, formId);
        Put32(b, 0);
        Put32(b, 0);
        b.insert(b.end(), stored.begin(), stored.end());
        return b;
    }

    Bytes Group(const char* label, const Bytes& contents)
    {
        Bytes b;
        PutTag(b, "GRUP");
        Put32(b, static_cast<std::uint32_t>(kGroupHeaderSize + contents.size()));
        PutTag(b, label);
        Put32(b, 0);
        Put32(b, 0);
        Put32(b, 0);
        b.insert(b.end(), contents.begin(), contents.end());
        return b;
    }

    Bytes Plugin(std::uint32_t flags, const std::vector<std::string>& masters, const Bytes& groups)
    {
        Bytes header;
        for (const auto& m : masters) {
            const Bytes mast = Sub("MAST", Str(m));
            header.insert(header.end(), mast.begin(), mast.end());
        }
        Bytes b = Record("TES4", flags, 0, header);
        b.insert(b.end(), groups.begin(), groups.end());
        return b;
    }

    Bytes Book(std::uint32_t formId, const std::string& editorId, std::uint32_t spell)
    {
        Bytes body = Sub("EDID", Str(editorId));
        Bytes data{ kTeachesSpell, 0, 0, 0 };
        Put32(data, spell);
        Put32(data, 0);
        Put32(data, 0);
        const Bytes d = Sub("DATA", data);
        body.insert(body.end(), d.begin(), d.end());
        return Record("BOOK", 0, formId, body);
    }

    void Join(Bytes& into, const Bytes& more) { into.insert(into.end(), more.begin(), more.end()); }

    std::filesystem::path Write(const std::filesystem::path& dir, const std::string& name, const Bytes& data)
    {
        const auto path = dir / name;
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
        return path;
    }

    void ConfigRules()
    {
        using TomeReach::IsDistributionConfig;
        Check(IsDistributionConfig("Data/Foo_DISTR.ini"), "an SPID _DISTR.ini is a distribution config");
        Check(IsDistributionConfig("Data/SKSE/Plugins/SkyPatcher/leveledList/a.ini"), "a SkyPatcher file is one");
        Check(!IsDistributionConfig("Data/SKSE/Plugins/GridInventory_Default.ini"),
            "an inventory layout list is not (it names every book)");
        Check(!IsDistributionConfig("Data/SKSE/Plugins/DynamicStringDistributor/x/a_DISTR.ini"),
            "a translation folder is not");

        const auto dir = std::filesystem::temp_directory_path() / "tome-reach-selftest";
        const auto file = Write(dir, "Rules_DISTR.ini", Bytes{});
        std::ofstream(file) << "Item = 0x1803~Master.esm|NONE\nItem = Master.esm|0x0805\nItem = ConfigTome|NONE\n";
        TomeReach::ConfigText text;
        text.Add(file);
        Check(!text.Names("master.esm", 0x803, ""), "0x1803~Master.esm does not name 0x803");
        Check(text.Names("master.esm", 0x805, ""), "Master.esm|0x0805 names 0x805");
        Check(text.Names("other.esm", 0x999, "ConfigTome"), "an editor id names its tome");
        Check(!text.Names("other.esm", 0x999, "ConfigTom"), "a shorter editor id does not");
    }

    // File and folder names outside the ANSI code page (Thai is outside CP949 and CP1252 alike):
    // path::string() throws on them, so the scan must read names as UTF-8 and keep going.
    void NonAnsiNames(const std::filesystem::path& root)
    {
        namespace fs = std::filesystem;
        const auto data = root / "distr-scan";
        const auto thaiFolder = data / "SKSE" / "Plugins" / fs::path(u8"SkyPatcher") / fs::path(u8"ไทย");
        std::error_code ec;
        fs::create_directories(thaiFolder, ec);
        Write(data, "Plain_DISTR.ini", Bytes{});
        std::ofstream(data / fs::path(u8"ไทย_DISTR.ini")) << "Spell = 0x800~Master.esm\n";
        std::ofstream(data / fs::path(u8"ไทย.txt")) << "not a config\n";
        std::ofstream(thaiFolder / fs::path(u8"a.ini")) << "x\n";

        std::vector<fs::path> found;
        bool threw = false;
        try {
            found = TomeReach::FindDistributionConfigs(data);
        } catch (...) {
            threw = true;
        }
        auto has = [&](const fs::path& p) { return std::ranges::find(found, p) != found.end(); };
        Check(!threw, "a non-ANSI file name does not throw out of the scan");
        Check(has(data / "Plain_DISTR.ini") && has(data / fs::path(u8"ไทย_DISTR.ini")),
            "a non-ANSI _DISTR.ini is found next to a plain one");
        Check(has(thaiFolder / "a.ini"), "a config in a non-ANSI SkyPatcher subfolder is found");
        Check(found.size() == 3, "a non-config with a non-ANSI name is not");
        const std::u8string lowered = u8"ไทย_distr.ini";
        Check(TomeReach::LowerUtf8(fs::path(u8"ไทย_DISTR.INI")) == std::string(lowered.begin(), lowered.end()),
            "LowerUtf8 lowers ASCII and keeps UTF-8 bytes");
    }

    // The top level is read by an OS wildcard per suffix, not a full listing: it must keep exactly the
    // files the full listing kept (every entry through IsDistributionConfig), case and near-misses included
    void WildcardMatchesFullListing(const std::filesystem::path& root)
    {
        namespace fs = std::filesystem;
        const auto data = root / "distr-wildcard";
        std::error_code ec;
        fs::remove_all(data, ec);
        fs::create_directories(data / "Dir_DISTR.ini", ec);  // a folder with a config's name
        for (const char* name : { "A_DISTR.ini", "b_kid.INI", "C_FLM.ini", "D_Swap.ini", "E_CID.ini", "F_CDF.ini",
                 "G_LLI.ini", "H_distr.ini.bak", "I_distr.txt", "j.json", "k.ini", "meta.ini", "LongName_DISTR_x.ini",
                 "Mod_DISTRini", "Some Long Name With Spaces_DISTR.ini" }) {
            std::ofstream(data / name) << "x\n";
        }
        std::ofstream(data / fs::path(u8"ไทย_KID.ini")) << "x\n";

        std::vector<fs::path> full;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file() && TomeReach::IsDistributionConfig(it->path())) full.push_back(it->path());
        }
        auto found = TomeReach::FindDistributionConfigs(data);
        std::ranges::sort(full);
        std::ranges::sort(found);
        Check(full.size() == 9, "the full listing keeps the nine configs (seven suffixes, spaces, Thai)");
        Check(found == full, "the wildcard search keeps the same files as the full listing");

        constexpr std::array kSpid = { std::string_view("_distr.ini") };
        Check(TomeReach::TopLevelConfigs(data, kSpid).size() == 2, "a single suffix keeps only its own files");
    }

    // Under SKSE/Plugins only the files below a direct child folder whose name holds a config folder's
    // name are read (found by wildcard, not a full walk). The answer is a full recursive walk with that
    // rule written out here; decoys: suffix files and config folders nested deeper are not read.
    void PluginsSearchMatchesRule(const std::filesystem::path& root)
    {
        namespace fs = std::filesystem;
        const auto data = root / "distr-plugins";
        const auto plugins = data / "SKSE" / "Plugins";
        std::error_code ec;
        fs::remove_all(data, ec);
        const fs::path files[] = {
            plugins / "SkyPatcher" / "leveledList" / "a.ini",       // kept: nested inside a config folder
            plugins / "SkyPatcher" / "npc" / "deep" / "b.ini",      // kept: deeper still
            plugins / "SkyPatcher" / "readme.txt",                  // not a config extension
            plugins / "SkyPatcher" / "meta.ini",                    // MO2's meta file
            plugins / "My_LLOS_Patch" / "c.json",                   // kept: the name holds "llos"
            plugins / "SkyPatcher_LLOS" / "d.ini",                  // kept once (two names in one)
            plugins / "ContainerDistributionFramework" / "e.yaml",  // kept
            plugins / "x_DISTR.ini",                                // decoy: a suffix file under SKSE/Plugins
            plugins / "Other" / "y.ini",                            // decoy: not a config folder
            plugins / "Other" / "z_KID.ini",                        // decoy: a suffix file deeper down
            plugins / "Other" / "SkyPatcher" / "z.ini",             // decoy: a config folder not a direct child
            plugins / "DynamicStringDistributor" / "SkyPatcher" / "w.ini",  // decoy: translation folder
            data / "Top_DISTR.ini",                                 // kept: the top level
        };
        for (const auto& f : files) {
            fs::create_directories(f.parent_path(), ec);
            std::ofstream(f) << "x\n";
        }
        fs::create_directories(plugins / "LeveledList_folder.ini", ec);  // a folder with a config's name

        constexpr std::array kFolders = { "skypatcher", "containerdistributionframework", "llos", "leveledlist",
            "containeritemdistributor" };
        std::vector<fs::path> want;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file() && TomeReach::IsDistributionConfig(it->path())) want.push_back(it->path());
        }
        for (fs::recursive_directory_iterator it(plugins, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || !TomeReach::IsDistributionConfig(it->path())) continue;
            const auto rel = fs::relative(it->path(), plugins);
            const auto first = TomeReach::LowerUtf8(*rel.begin());
            const bool inFolder = std::distance(rel.begin(), rel.end()) >= 2 &&
                std::ranges::any_of(kFolders, [&](const char* f) { return first.find(f) != std::string::npos; });
            if (inFolder) want.push_back(it->path());
        }
        auto found = TomeReach::FindDistributionConfigs(data);
        std::ranges::sort(want);
        std::ranges::sort(found);
        Check(want.size() == 6, "the rule keeps six files (top level, nested config folders, two names once)");
        Check(found == want, "the SKSE/Plugins folder search keeps the same files as a full walk with the rule");
    }
}

int RunSelfTest()
{
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "tome-reach-selftest";
    fs::create_directories(dir);

    // Master.esm: five tomes (0x800..0x804)
    Bytes books;
    Join(books, Book(0x000800, "PlacedTome", 0x900));
    Join(books, Book(0x000801, "HeldTome", 0x901));
    Join(books, Book(0x000802, "ScriptTome", 0x902));
    Join(books, Book(0x000803, "ConfigTome", 0x903));
    Join(books, Book(0x000804, "LostTome", 0x904));
    Join(books, Book(0x000807, "SU04SpellTomeDebug", 0x907));
    const auto master = Write(dir, "Master.esm", Plugin(0, {}, Group("BOOK", books)));

    // Light.esl: its own ids (index 1 = itself, one master) keep only 12 bits, so
    // 0x01001805 is 0x805; and 0x806, which nothing refers to
    Bytes lightBooks;
    Join(lightBooks, Book(0x01001805, "LightTome", 0x905));
    Join(lightBooks, Book(0x01000806, "LostLightTome", 0x906));
    const auto light = Write(dir, "Light.esl", Plugin(kLightFlag, { "Master.esm" }, Group("BOOK", lightBooks)));

    // Mod.esp: a placed copy, a compressed NPC inventory, a script property behind XXXX,
    // and a leveled list that lists the light plugin's tome
    Bytes records;
    Bytes refrs;
    Join(refrs, Record("REFR", 0, 0x02000001, Sub("NAME", U32Bytes(0x00000800))));
    Join(refrs, Record("REFR", 0, 0x02000005, Sub("NAME", U32Bytes(0x00000807))));  // the debug tome, placed
    Join(records, Group("REFR", refrs));
    Join(records, Group("NPC_", Record("NPC_", kCompressedFlag, 0x02000002, Sub("CNTO", U32Bytes(0x00000801)))));
    Bytes vmad = { 5, 0, 2, 0 };
    Put32(vmad, 0x00000802);
    Join(records, Group("QUST", Record("QUST", 0, 0x02000003, BigSub("VMAD", vmad))));
    Bytes lvlo = { 1, 0, 0, 0 };
    Put32(lvlo, 0x01001805);  // bits above the light plugin's 12 are dropped
    Put32(lvlo, 1);
    Join(records, Group("LVLI", Record("LVLI", 0, 0x02000004, Sub("LVLO", lvlo))));
    const auto mod = Write(dir, "Mod.esp", Plugin(0, { "Master.esm", "Light.esl" }, records));

    const auto cfg = Write(dir, "Mod_DISTR.ini", Bytes{});
    std::ofstream(cfg) << "Item = 0x803~Master.esm|ActorTypeNPC\n";

    const std::vector<TomeReach::PluginFile> order = {
        { "Master.esm", master, {} }, { "Light.esl", light, {} }, { "Mod.esp", mod, {} } };

    const auto run = [&](const std::vector<TomeReach::PluginFile>& loadOrder, TomeReach::NameTable& names,
                         std::unordered_set<TomeReach::Key>& tomes) {
        const auto m = names.Id("Master.esm");
        const auto l = names.Id("Light.esl");
        names.SetLight(l, true);
        for (std::uint32_t id = 0x800; id <= 0x804; ++id) tomes.insert(TomeReach::MakeKey(m, id));
        tomes.insert(TomeReach::MakeKey(l, 0x805));
        tomes.insert(TomeReach::MakeKey(l, 0x806));
        tomes.insert(TomeReach::MakeKey(m, 0x807));
        return TomeReach::FindReachedTomes(loadOrder, names, tomes, { cfg });
    };

    std::printf("TomeReach self-test\n");
    {
        TomeReach::NameTable names;
        std::unordered_set<TomeReach::Key> tomes;
        const auto pass = run(order, names, tomes);
        const auto m = names.Id("Master.esm");
        const auto reached = [&](std::uint32_t id) { return pass.reached.contains(TomeReach::MakeKey(m, id)); };
        Check(pass.unreadable.empty() && pass.damaged.empty(), "every made-up plugin reads whole");
        Check(reached(0x800), "a placed copy (REFR NAME) reaches its tome");
        Check(reached(0x801), "a compressed NPC inventory (CNTO) reaches its tome");
        Check(reached(0x802) && pass.byLooseOnly == 1, "a script property behind XXXX reaches its tome, loosely");
        Check(reached(0x803) && pass.byConfig == 1, "a distribution config reaches its tome");
        Check(!reached(0x804), "a tome nothing refers to is not reached");
        Check(!reached(0x807) && pass.byDebug == 1, "a placed tome with \"debug\" in its editor id is not reached");
        const auto l = names.Id("Light.esl");
        Check(pass.reached.contains(TomeReach::MakeKey(l, 0x805)),
            "a light plugin's tome is reached by its 12-bit id (0x01001805 -> 0x805)");
        Check(!pass.reached.contains(TomeReach::MakeKey(l, 0x806)), "a light plugin's lost tome is not reached");
    }
    {
        // Damaged plugins, each with Master.esm as its only master
        const Bytes refr = Group("REFR", Record("REFR", 0, 0x01000001, Sub("NAME", U32Bytes(0))));
        Bytes cut = Plugin(0, { "Master.esm" }, refr);
        cut.resize(cut.size() - 3);                              // a record runs past the end
        Bytes badZip;                                            // a compressed record that does not inflate
        Put32(badZip, 64);
        badZip.insert(badZip.end(), { 1, 2, 3, 4, 5, 6, 7, 8 });
        Bytes zipRecord;
        PutTag(zipRecord, "NPC_");
        Put32(zipRecord, static_cast<std::uint32_t>(badZip.size()));
        Put32(zipRecord, kCompressedFlag);
        Put32(zipRecord, 0x01000002);
        Put32(zipRecord, 0);
        Put32(zipRecord, 0);
        Join(zipRecord, badZip);
        Bytes tail = Plugin(0, { "Master.esm" }, refr);         // the file ends inside a record header
        tail.insert(tail.end(), 10, 0);
        Bytes cutSub;                                            // a subrecord runs past its record
        PutTag(cutSub, "NAME");
        Put16(cutSub, 8);
        Put32(cutSub, 0x00000800);
        Bytes badHeader = Sub("MAST", Str("Master.esm"));        // the header's master list is cut
        badHeader.resize(badHeader.size() - 4);

        auto withBroken = order;
        withBroken.push_back({ "Broken.esp", Write(dir, "Broken.esp", cut), {} });
        withBroken.push_back({ "BadZip.esp", Write(dir, "BadZip.esp", Plugin(0, { "Master.esm" }, Group("NPC_", zipRecord))), {} });
        withBroken.push_back({ "Tail.esp", Write(dir, "Tail.esp", tail), {} });
        withBroken.push_back({ "CutSub.esp",
            Write(dir, "CutSub.esp", Plugin(0, { "Master.esm" }, Group("REFR", Record("REFR", 0, 0x01000003, cutSub)))), {} });
        withBroken.push_back({ "BadHeader.esp", Write(dir, "BadHeader.esp", Record("TES4", 0, 0, badHeader)), {} });
        withBroken.push_back({ "Missing.esp", dir / "Missing.esp", {} });
        TomeReach::NameTable names;
        std::unordered_set<TomeReach::Key> tomes;
        const auto pass = run(withBroken, names, tomes);
        const auto has = [](const std::vector<std::string>& list, const char* name) {
            return std::find(list.begin(), list.end(), name) != list.end();
        };
        Check(pass.damaged.size() == 4 && has(pass.damaged, "Broken.esp") && has(pass.damaged, "BadZip.esp") &&
                  has(pass.damaged, "Tail.esp") && has(pass.damaged, "CutSub.esp"),
            "a cut record, a bad compressed record, a cut last header and a cut subrecord each mean damaged");
        Check(pass.reached.contains(TomeReach::MakeKey(names.Id("Master.esm"), 0x804)) && pass.byDamaged == 1,
            "a damaged plugin's masters' tomes count as reached");
        Check(!pass.reached.contains(TomeReach::MakeKey(names.Id("Light.esl"), 0x806)),
            "a tome of a plugin that is not a damaged plugin's master keeps its answer");
        Check(pass.unreadable.size() == 2 && has(pass.unreadable, "Missing.esp") && has(pass.unreadable, "BadHeader.esp"),
            "a plugin that cannot be opened, or whose header is cut, is unreadable");
    }
    ConfigRules();
    NonAnsiNames(dir);
    WildcardMatchesFullListing(dir);
    PluginsSearchMatchesRule(dir);

    std::printf("self-test: %d passed, %d failed\n", g_passed, g_failed);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return g_failed ? 1 : 0;
}

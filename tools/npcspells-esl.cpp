#include "core/NpcSpellsEsl.h"
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace NpcSpells;
int main(int argc, char** argv)
{
    const char* path = nullptr;
    for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "-o") == 0) path = argv[i + 1];
    if (!path) { std::fprintf(stderr, "usage: npcspells-esl -o <path>\n"); return 1; }
    const auto& table = SlotTable();
    const auto bytes = BuildEslBytes(table);
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::fprintf(stderr, "cannot open %s\n", path); return 1; }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) { std::fprintf(stderr, "write failed: %s\n", path); return 1; }
    std::printf("wrote %s: %zu LVSP records, local 0x%X..0x%X\n", path, table.size() * kMaxChunks, kFirstLocalFormId, LocalFormId(table.size() - 1, kMaxChunks - 1));
    return 0;
}

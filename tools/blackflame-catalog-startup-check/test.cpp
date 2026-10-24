#include "FakeEngine.h"
#include "CatalogStartup.cpp"
#include "Seam.cpp"
#include <filesystem>
#include <thread>

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "missing";
    int checks = 0, failures = 0, duplicateCallback = 0;
    auto check = [&](bool good, const char* name) { ++checks; failures += !good; std::cout << (good ? "PASS " : "FAIL ") << mode << ": " << name << '\n'; };
    const std::filesystem::path file = BlackFlame::kCatalogPath;
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".part");
    auto write = [&](const char* text) { std::ofstream out(file); out << text; };
    auto ready = [&] { write(R"({"spells":{"Skyrim.esm|0x00012FCD":{"elements":["fire"]}}})"); };
    if (mode == "existing" || mode == "closed-existing") ready();
    if (mode == "empty") write(R"({"spells":{}})");
    if (mode == "invalid") write("{unfinished");
    if (mode == "wrong-spells") write(R"({"spells":[]})");
    if (mode == "wrong-root") write("[]");
    if (mode == "missing-spells") write(R"({"version":1})");
    if (mode == "part-only") { std::ofstream out(file.string() + ".part"); out << R"({"spells":{}})"; }
    if (mode == "closed" || mode == "closed-existing") Probe::publishOpen = false;
    if (mode == "proof-refused" || mode == "proof-pending") Probe::proof = false;
    if (mode == "proof-pending") Probe::proofPending = true;
    if (mode == "queue-failed") Probe::queueAccepts = false;
    if (mode == "no-queue") Probe::queueInstalled = false;
    OnDataLoaded();
    BlackFlame::CatalogStartup::Begin([&] { ++duplicateCallback; });
    const bool immediate = mode == "existing" || mode == "empty";
    if (immediate) {
        check(Probe::fireSets == 1 && Probe::readyFireSets == 1, "existing valid catalog initializes synchronously");
        check(Probe::singleLaunch == 1 && Probe::readyLaunch == 1 && Probe::hooks == 1, "runtime initialized exactly once");
        check(Probe::waiting.empty(), "no polling for an existing catalog");
    } else {
        check(Probe::fireSets == 0 && Probe::closure == 0 && Probe::singleLaunch == 0, "not initialized before catalog readiness");
        if (mode == "close-waiting") Probe::publishOpen = false;
        if (mode == "queue-failed-later") Probe::queueAccepts = false;
        Probe::Drain();
        check(Probe::fireSets == 0 && Probe::singleLaunch == 0, "unready poll does not initialize");
        const bool cancelled = mode == "closed" || mode == "closed-existing" || mode == "close-waiting" ||
            mode == "proof-refused" || mode == "queue-failed" || mode == "queue-failed-later" || mode == "no-queue";
        if (cancelled) {
            check(Probe::waiting.empty(), "cancelled wait does not requeue");
            ready(); Probe::publishOpen = true; Probe::proof = true; Probe::queueAccepts = true;
            BlackFlame::CatalogStartup::Begin([&] { ++duplicateCallback; });
            Probe::Drain();
            check(Probe::fireSets == 0 && Probe::singleLaunch == 0 && Probe::hooks == 0, "late readiness cannot restart a cancelled session");
        } else {
            ready();
            if (mode == "proof-pending") {
                Probe::Drain();
                check(Probe::fireSets == 0, "valid catalog waits for main-thread proof");
                Probe::proof = true; Probe::proofPending = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(510));
            Probe::Drain();
            check(Probe::fireSets == 1 && Probe::readyFireSets == 1, "late valid catalog builds fire sets exactly once");
            check(Probe::singleLaunch == 1 && Probe::readyLaunch == 1 && Probe::closure == 1 && Probe::hooks == 1,
                  "late valid catalog starts copies and hooks exactly once");
        }
    }
    const int fires = Probe::fireSets, starts = Probe::singleLaunch;
    Probe::Drain(); Probe::Drain();
    BlackFlame::CatalogStartup::Begin([&] { ++duplicateCallback; });
    check(Probe::fireSets == fires && Probe::singleLaunch == starts && duplicateCallback == 0, "repeated Begin and wakeups cannot initialize twice");
    std::filesystem::remove(file); std::filesystem::remove(file.string() + ".part");
    std::cout << "RESULT " << mode << " checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}

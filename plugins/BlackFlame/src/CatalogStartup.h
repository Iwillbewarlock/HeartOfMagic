#pragma once

#include <functional>

namespace BlackFlame::CatalogStartup
{
    // DataLoaded: run once with a readable HoM catalog, or wait on the proven main-thread queue.
    // Never resume after the first load/new game closes MenuWatch's publication window.
    void Begin(std::function<void()> a_ready);
}

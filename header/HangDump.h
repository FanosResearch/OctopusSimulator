/*
 * File  :      HangDump.h
 * A registry of state dumpers for wedged runs. A component registers a callable; whoever detects
 * the wedge (a controller's OCTOPUS_HANG_DUMP watchdog, the network's own watchdog, or the
 * network's fatal path) runs them all once. Keeps the controllers free of a dependency on the
 * network classes.
 */
#ifndef _HANG_DUMP_H
#define _HANG_DUMP_H
#include <functional>
#include <vector>

namespace octopus
{
    struct HangDump
    {
        static std::vector<std::function<void()>> &hooks() { static std::vector<std::function<void()>> h; return h; }
        static void add(std::function<void()> f) { hooks().push_back(std::move(f)); }
        static void run()
        {
            static bool done = false;
            if (done) return;
            done = true;
            for (auto &f : hooks()) f();
        }
    };
}
#endif /* _HANG_DUMP_H */

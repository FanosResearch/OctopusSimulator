/*
 * File  :      ScriptedMaster.h
 * Test fixture (compiled into the tests that use it, not into the library): a master that offers
 * a scripted list of beats, so every TL-WAVE golden scenario (test/golden/scenarios.md §1.1) and
 * every TL-NEG mutant (docs/TileLink_Negative_Tests.md §1.1) can be driven exactly.
 *
 * Two kinds of offers, on any channel the master sends on (A, C, E):
 *   - explicit `(cycle, channel, message, beat)`: valid HIGH with that beat in that cycle only; the
 *     master never retries on its own, so a rejected beat is re-listed if the scenario re-offers it,
 *     and retraction or replacement before the first accept is a different entry in the next cycle.
 *     An optional `before` callback edits the message just before the offer (a mutant that changes
 *     a control field mid-burst, M-BURST-2).
 *   - adaptive `send(start, message)`: from `start` on, the message's next beat is offered every
 *     cycle until all its beats are accepted (re-offering a rejected beat); the adaptive messages of
 *     a channel go one after the other, and an explicit offer in a cycle takes precedence.
 * A beat on a channel the link does not have (C or E of a TL-UL/UH link) goes to `missingChannel`
 * (wired to Monitor::offerOnMissingChannel by the test: M-LEVEL-4).
 * Ready on the channels it receives (B, D): HIGH except in the listed cycles, and LOW in reset
 * unless ready_in_reset.
 */
#ifndef _TL_TEST_SCRIPTED_MASTER_H
#define _TL_TEST_SCRIPTED_MASTER_H
#include "tilelink/Kernel.h"
#include <functional>
#include <set>
#include <vector>

namespace octopus { namespace tl { namespace fixture
{
    // A message laid out for a bus of w bytes, mask derived (§4.5), id from the kernel.
    MsgPtr makeMsg(Kernel &k, Channel ch, unsigned opcode, unsigned size, unsigned w, uint64_t address,
                   uint64_t source, DataMode mode = DataMode::None, unsigned param = 0);
    // Sets a beat's mask from a bit pattern (bit l = lane l).
    void setMask(Msg &m, unsigned beat, uint64_t bits);
    // Little-endian value of the active lanes of a beat (bytes = min(2^size, w)), into/out of Full data.
    void setData(Msg &m, unsigned beat, uint64_t value);
    uint64_t getData(const Msg &m, unsigned beat);

    struct ScriptOffer
    {
        uint64_t cycle = 0;
        Channel  ch = Channel::A;
        MsgPtr   msg;
        unsigned beat = 0;
        std::function<void(Msg &)> before;
    };

    // The scripted sender/receiver engine shared by ScriptedMaster and ScriptedSlave.
    class ScriptEngine
    {
    public:
        std::vector<ScriptOffer> offers;                          // explicit
        struct Auto { uint64_t start; Channel ch; MsgPtr msg; unsigned next = 0; bool done = false; };
        std::vector<Auto> autos;                                  // adaptive
        std::set<uint64_t> not_ready[NUM_CHANNELS];               // cycles with ready LOW
        bool ready_in_reset = false;
        std::function<void(Channel, const Msg &, unsigned, uint64_t)> missingChannel;
        std::vector<std::pair<uint64_t, Beat>> received;          // accepted beats on the receive channels

        void at(uint64_t cycle, Channel ch, const MsgPtr &m, unsigned beat = 0, std::function<void(Msg &)> before = nullptr)
        { offers.push_back(ScriptOffer{cycle, ch, m, beat, before}); }
        void send(uint64_t start, Channel ch, const MsgPtr &m) { autos.push_back(Auto{start, ch, m}); }
        void notReady(Channel ch, uint64_t cycle) { not_ready[static_cast<int>(ch)].insert(cycle); }
        bool autosDone() const { for (auto &a : autos) if (!a.done) return false; return true; }

        void doOffer(Kernel &k, Link &l, uint64_t t, bool sends_master_channels);
        void doAccept(Kernel &k, Link &l, uint64_t t, bool sends_master_channels);
        void doCommit(Link &l, uint64_t t, bool sends_master_channels);
    };

    class ScriptedMaster : public Agent, public ScriptEngine
    {
    public:
        explicit ScriptedMaster(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        void offer(uint64_t t) override;
        void accept(uint64_t t) override;
        void commit(uint64_t t) override;
        bool idle() const override { return autosDone(); }
        Link &link() const;
    };
}}}
#endif /* _TL_TEST_SCRIPTED_MASTER_H */

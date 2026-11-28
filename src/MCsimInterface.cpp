/*
 * File  :      MCsimInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On August 4, 2022
 */

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "../header/MCsimInterface.h"
#include "../header/Logger.h"
#include "DramMap.h"
#include "PagePlacer.h"

using namespace std;

using namespace std;

namespace octopus
{
    MCsimInterface::MCsimInterface(CommunicationInterface *lower_interface, int dram_id, int llc_id,
                                   int num_cores, int block_size, uint64_t clk_period,
                                   const std::string &mem_system, uint64_t core_clock_mhz, int bank_shift)
        : ClockedObj(clk_period) // == LLC period: one tick per core cycle (see header). Must be
                                 // non-zero: ClockManager reschedules at current_time+period, so
                                 // period 0 self-reschedules at the same timestamp forever.
    {
        if (clk_period == 0)
        {
            cout << "MCsimInterface: clock period must be non-zero" << endl;
            exit(0);
        }
        m_id = dram_id;
        m_llc_id = llc_id;
        m_clk_cycle = 1;
        m_llc_line_size = block_size;
        m_read_count = 0;
        m_write_count = 0;

        m_lower_interface = lower_interface;
        m_processing_queue = new FRFCFS_Buffer<Message, MCsimInterface>(&MCsimInterface::getRequestState, this);

        /******************** Initialization of MCsim (DDR4-2400U, 8Gb x8, 1 channel/1 rank) ********************/
        std::string sys_init_file = std::string(MCSIM_PATH) + "system/" + mem_system + "/" + mem_system + ".ini";
        m_mcsim = MCsim::getMemorySystemInstance(
            num_cores,
            sys_init_file,
            "DDR4",
            "2400U",
            "8Gb_x8",
            1,
            1);
        m_mcsim->setCPUClockSpeed(core_clock_mhz * 1000000ull);

        // where the loaded .ini's AddressMapping puts the DRAM bank fields, for DRAM bank
        // partitioning (DramMap; fields of MCsim's memOrg: 1 BankGroup, 2 Bank; bit positions in
        // the line address, so + 6 in the byte address)
        {
            unsigned blo = 0, bw = 0, glo = 0, gw = 0;
            bool hb = m_mcsim->dramFieldBits(2, blo, bw), hg = m_mcsim->dramFieldBits(1, glo, gw);
            MCsim::MultiChannelMemorySystem *mc = m_mcsim;
            // mcsim_bank_shift(i) = k: the bank and bank-group fields must be adjacent; they are
            // then read k bits higher in the address, and the k bits above them move down into
            // MCsim's bank positions (rows still decode from the rest), so the banks sit above
            // e.g. an LLC's page-colour bits and the two partitionings are independent
            m_bank_shift = bank_shift;
            if (m_bank_shift < 0)
            {
                cerr << "MCsimInterface: mcsim_bank_shift(i) must be >= 0" << endl;
                exit(2);
            }
            if (m_bank_shift > 0)
            {
                unsigned lo = hb && hg ? std::min(blo, glo) : hb ? blo : glo;
                unsigned w = (hb ? bw : 0) + (hg ? gw : 0);
                bool adjacent = !(hb && hg) || blo + bw == glo || glo + gw == blo;
                if (!(hb || hg) || !adjacent || w == 0)
                {
                    cerr << "MCsimInterface: mcsim_bank_shift(i) needs adjacent bank and bank-group fields in the AddressMapping" << endl;
                    exit(2);
                }
                m_swz_lo = (int)lo + 6; m_swz_w = (int)w;
                if (m_swz_lo + m_swz_w + m_bank_shift > 38)
                {
                    cerr << "MCsimInterface: mcsim_bank_shift(i) " << m_bank_shift << " moves the bank bits past bit 38 (MCsim decodes 32 bits of the line address)" << endl;
                    exit(2);
                }
                cout << "MCsimInterface: DRAM bank/bank-group bits moved from " << m_swz_lo << "-" << m_swz_lo + m_swz_w - 1
                     << " to " << m_swz_lo + m_bank_shift << "-" << m_swz_lo + m_swz_w + m_bank_shift - 1 << " (mcsim_bank_shift " << m_bank_shift << ")" << endl;
            }
            const int k = m_bank_shift;
            if (hb || hg)
                DramMap::instance().set(hb ? (int)blo + 6 + k : 0, hb ? (int)bw : 0, hg ? (int)glo + 6 + k : 0, hg ? (int)gw : 0,
                                        [mc, this](uint64_t a, unsigned &bg, unsigned &b) { mc->dramDecode(toMCsim(a), bg, b); });
        }
        m_num_cores = num_cores;

        MCsim::TransactionCompleteCB *read_cb = new MCsim::MCsimCallback<MCsimInterface, void, unsigned, uint64_t, uint64_t>(this, &MCsimInterface::read_callback);
        MCsim::TransactionCompleteCB *write_cb = new MCsim::MCsimCallback<MCsimInterface, void, unsigned, uint64_t, uint64_t>(this, &MCsimInterface::write_callback);
        m_mcsim->RegisterCallbacks(read_cb, write_cb);
    }

    MCsimInterface::~MCsimInterface()
    {
        delete m_mcsim;
    }

    void MCsimInterface::cycleProcess()
    {
        processLogic();
        m_mcsim->update();

        m_clk_cycle++;

        // OCTOPUS_MEM_PROBE=N: every N cycles, the DRAM-side backlog and the process's resident
        // memory (diagnostic for runs whose memory grows: is it a queue that never drains?)
        static const uint64_t probe = [](){ const char *t = getenv("OCTOPUS_MEM_PROBE"); return t ? strtoull(t, NULL, 0) : 0ULL; }();
        if (probe && m_clk_cycle % probe == 0)
        {
            long rss_pages = 0;
            if (FILE *f = fopen("/proc/self/statm", "r")) { long sz; if (fscanf(f, "%ld %ld", &sz, &rss_pages) != 2) rss_pages = 0; fclose(f); }
            unsigned q = 0, pr = 0, pw = 0; m_mcsim->dramBacklog(q, pr, pw);
            cout << "[memprobe] cyc " << m_clk_cycle << " mcsim_queue " << q << " mcsim_reads " << pr << " mcsim_writes " << pw
                 << " pending_reads " << m_pending_requests.size() << " octopus_pq " << m_processing_queue->size()
                 << " out_buf " << m_output_buffer.size() << " reads " << m_read_count << " writes " << m_write_count
                 << " rss_mb " << rss_pages * 4096 / 1048576 << endl;
        }
    }

    void MCsimInterface::init()
    {
    }

    static uint64_t mcsimTraceAddr()
    {
        static uint64_t v = [](){ const char *t = getenv("OCTOPUS_TRACE_ADDR"); return t ? strtoull(t, NULL, 0) : 0ULL; }();
        return v;
    }
    static bool mcsimTraced(uint64_t addr)
    {
        uint64_t t = mcsimTraceAddr();
        return t == 1 || (t != 0 && (addr & ~63ULL) == (t & ~63ULL));
    }

    void MCsimInterface::processLogic()
    {
        addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        if (m_processing_queue->getFirstReady(&ready_msg))
        {
            if (mcsimTraced(ready_msg.addr))
                cout << "[mcsim] cyc " << m_clk_cycle << " request addr 0x" << std::hex << ready_msg.addr << std::dec << " msg " << ready_msg.msg_id
                     << " owner " << ready_msg.owner << (ready_msg.data == NULL ? " READ" : " WRITE") << " pending " << m_pending_requests.size() << endl;
            if (m_mcsim->addRequest(ready_msg.owner, toMCsim(ready_msg.addr), ready_msg.data == NULL, m_llc_line_size)) // 1 -> Read, 0 -> Write
            {
                // DRAM bank partitioning: a read counts for its requester. A write-back's owner is
                // whoever caused the eviction (the LLC, or the core whose miss evicted the line), not
                // the line's owner, so it counts for the core the page was placed for (none when unplaced)
                DramMap::instance().onAccess(ready_msg.data == NULL && (int)ready_msg.owner < m_num_cores
                                                 ? (int)ready_msg.owner : PagePlacer::instance().frameOwner(ready_msg.addr),
                                             ready_msg.addr);
                // Design B: request has entered DRAM (mirrors MainMemoryController's stamp).
                Logger::getLogger()->event(ready_msg.msg_id, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::ENTER);
                Logger::getLogger()->trace(ready_msg, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::ENTER);
                if (ready_msg.data == NULL) // Add read requests only (writes are fire-and-forget)
                    m_pending_requests.push_back(ready_msg);
            }
            else
            {
                cout << "MCsimInterface: Error failed to add request to MCsim" << endl;
                exit(0);
            }
        }

        if (!m_output_buffer.empty())
        {
            // Design B: DRAM finished servicing this read -> data leaves DRAM.
            Logger::getLogger()->event(m_output_buffer[0].msg_id, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::EXIT);
            Logger::getLogger()->trace(m_output_buffer[0], Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::EXIT);
            if (!m_lower_interface->pushMessage(m_output_buffer[0], m_clk_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "MCsimInterface(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                exit(0);
            }
            m_output_buffer.erase(m_output_buffer.begin());
        }
    }

    void MCsimInterface::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MCsimInterface> &buf)
    {
        Message msg;

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            msg.cycle = m_clk_cycle;
            if (buf.pushBack(msg, FRFCFS_State::Ready))
                m_lower_interface->popFrontMessage();
        }
    }

    FRFCFS_State MCsimInterface::getRequestState(const Message &msg, FRFCFS_State current_state)
    {
        return FRFCFS_State::Ready;
    }

    void MCsimInterface::read_callback(unsigned id, uint64_t address, uint64_t clock_cycle)
    {
        bool found = false;
        if (mcsimTraced(address))
            cout << "[mcsim] cyc " << m_clk_cycle << " read_callback addr 0x" << std::hex << address << std::dec << " id " << id << " pending " << m_pending_requests.size() << endl;
        for (int i = 0; i < (int)m_pending_requests.size(); i++)
        {
            if (toMCsim(m_pending_requests[i].addr) == address)
            {
                uint64_t data = m_read_count;
                m_read_count++;

                Message msg = Message(m_pending_requests[i].msg_id, // Id
                                      m_pending_requests[i].addr,   // Addr
                                      m_clk_cycle,                  // Cycle
                                      0,                            // Complementary_value
                                      m_pending_requests[i].owner); // Owner
                msg.to.push_back(m_pending_requests[i].from ? m_pending_requests[i].from : (uint16_t)m_llc_id);   // To: the LLC bank that asked
                msg.kind = Message::K_FILL;
                // a line-sized payload (the read counter, then zeros): copy() takes a full line,
                // and copying it straight from the 8-byte counter read 56 bytes past it on the stack
                uint8_t line[64] = {0};
                memcpy(line, &data, sizeof(data));
                msg.copy(line);   // 64 B, as before
                m_output_buffer.push_back(msg);

                m_pending_requests.erase(m_pending_requests.begin() + i);
                found = true;
                break;
            }
        }

        if (!found)
        {
            cout << "MCsimInterface: Error read_callback couldn't find the pending request" << endl;
            exit(0);
        }
    }

    uint64_t MCsimInterface::toMCsim(uint64_t a) const
    {
        // MCsim's bank fields [lo, lo + w) get the address's bits [lo + k, lo + k + w); the bits
        // [lo, lo + k) move up to [lo + w, lo + w + k). A bijection on [lo, lo + w + k).
        if (m_bank_shift == 0) return a;
        const int lo = m_swz_lo, w = m_swz_w, k = m_bank_shift;
        const uint64_t span = ((1ull << (w + k)) - 1) << lo;
        const uint64_t bank = (a >> (lo + k)) & ((1ull << w) - 1);
        const uint64_t low = (a >> lo) & ((1ull << k) - 1);
        return (a & ~span) | (bank << lo) | (low << (lo + w));
    }

    void MCsimInterface::write_callback(unsigned id, uint64_t address, uint64_t clock_cycle)
    {
        m_write_count++;
    }
}

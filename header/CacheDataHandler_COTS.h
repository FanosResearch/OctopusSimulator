/*
 * File  :      CacheDataHandler_COTS.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 20, 2022
 */

#ifndef _CacheDataHandler_COTS_H
#define _CacheDataHandler_COTS_H

#include "CacheDataHandler.h"

#include <map>
#include <vector>

namespace octopus
{
    // Finite realistic default for the write-back buffer (PWB) depth when no
    // `pwb_size` is given in config. Set to -1 in config for unbounded.
    #define DEFAULT_PWB_SIZE 8

    class CacheDataHandler_COTS : public CacheDataHandler
    {
    protected:
        std::map<uint64_t, GenericCacheLine> m_miss_status_holding_regs; //MSHR
        std::map<uint64_t, GenericCacheLine> m_pending_write_back_regs; //PWB

        int m_pwb_size; //max pending write-backs; -1 = unbounded

        // Addresses evicted into the PWB whose write-back request has not yet
        // been issued. A queue (not a single flag) so that multiple evictions
        // in one cycle are all eventually written back -- a hard PWB bound would
        // otherwise orphan entries and deadlock.
        std::vector<uint64_t> m_pwb_pending_issue;

        virtual inline void * getLine(uint64_t set, int way) override;
        virtual bool findline(uint64_t address, uint64_t *set, int *way) override;

        int chooseEvictionWay(uint64_t set);
        void moveLine2WB(uint64_t set, int way);

        inline bool checkMSHR(uint64_t address)
        {
            return m_miss_status_holding_regs.find(address) !=
                        m_miss_status_holding_regs.end();
        }

        inline bool checkPWB(uint64_t address)
        {
            return m_pending_write_back_regs.find(address) !=
                        m_pending_write_back_regs.end();
        }

        inline uint64_t mask_offset(uint64_t address)
        {
            return address & ~((uint64_t)m_block_size - 1);
        }

    public:
        // Debug: where does this address physically live? 0=nowhere 1=array 2=MSHR 3=PWB
        int whereIs(uint64_t address)
        {
            uint64_t s; int w;
            if (CacheDataHandler::findline(address, &s, &w)) return 1;
            if (checkMSHR(mask_offset(address))) return 2;
            if (checkPWB(mask_offset(address))) return 3;
            return 0;
        }

        CacheDataHandler_COTS(ParametersMap map, string pname = "",
                              string config_path = string(CONFIGURATION_PATH),
                              string name = STRINGIFY(CacheDataHandler_COTS));
        virtual ~CacheDataHandler_COTS();

        virtual void writeLine2MSHR(uint64_t address, GenericCacheLine *line);
        // Land arrived block data into the MSHR (fill buffer) WITHOUT promoting to a bank
        // way. Receiving a block off the network into the fill buffer is not a data-array
        // access, so it is immediate; only the MSHR->bank promotion is latency-gated.
        // Returns true iff the line is currently in the MSHR (data landed).
        bool fillMSHRData(uint64_t address, const uint8_t *data);
        // True iff the block is buffered in the MSHR AND its data has arrived.
        bool hasMSHRData(uint64_t address);
        // The block's data wherever it currently lives (array way / MSHR / PWB), ignoring
        // array-access readiness. For a forward that must hand on an in-hand block without
        // incurring (or waiting on) a fresh array read. NULL if the line holds no data.
        uint8_t *peekData(uint64_t address);
        // Write a buffered (MSHR-resident) block into a bank way -- the single array
        // write done once the coherence line stabilizes. Uses the block's own buffered
        // data; evicts a victim to the PWB if the set is full. No-op if not buffered or
        // data not yet arrived.
        void promoteFromMSHR(uint64_t address);

        virtual bool updateLineData(uint64_t address, const uint8_t *data) override;
        virtual bool updateLineBits(uint64_t address, GenericCacheLine *line) override;

        virtual bool isReady(uint64_t address) override;

        bool addressOfLinePendingWB(bool clear_flag, uint64_t *address);

        // True if the block is resident in the cache, or already tracked in the
        // MSHR or PWB (i.e., a demand access to it needs no new MSHR entry).
        inline bool isAddressTracked(uint64_t address)
        {
            uint64_t set;
            int way;
            return findline(address, &set, &way);
        }

        // Headroom in the write-back buffer for one more evicted line.
        inline bool pwbHasSpace()
        {
            return m_pwb_size < 0 ||
                   (int)m_pending_write_back_regs.size() < m_pwb_size;
        }

        // True iff installing a returning fill for `address` would evict a
        // victim into an already-full write-back buffer. This mirrors the
        // eviction condition in updateLineData: the line is being filled from
        // the MSHR (not yet resident) into a set with no free way, while the
        // PWB has no headroom. Read-only -- safe to evaluate before the fill is
        // processed, so the response can be stalled without any rollback.
        inline bool fillWouldOverflowPwb(uint64_t address)
        {
            if (pwbHasSpace())
                return false;
            if (!checkMSHR(mask_offset(address)))
                return false;
            uint64_t set;
            int way;
            if (CacheDataHandler::findline(address, &set, &way))
                return false;
            if (findEmptyWay(address) != -1)
                return false;
            return true;
        }
    };
}

#endif /* _CacheDataHandler_COTS_H */

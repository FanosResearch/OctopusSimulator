/*
 * File  :      GenericCacheLine.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 9, 2022
 */

#ifndef _GenericCacheLine_H
#define _GenericCacheLine_H

#include <cstdint>
#include <cstring>
#include <iostream>

namespace octopus
{
    class GenericCacheLine
    {
    public:
        bool valid;

        uint64_t insertCycle;
        uint64_t accessCycle;
        uint64_t accessCounter;

        int64_t tag;
        int state;
        int owner_id;

        // The line's copy is newer than the next level's (docs/TileLink_LLC_Dirty_Writeback_Plan.md s3.1).
        // Not part of copyBits: a protocol writes its new state through copyBits (updateLineBits) from a line
        // it built, which would reset the bit. It moves with the whole line (copy, operator=: MSHR to array,
        // array to write-back buffer) and changes only through CacheDataHandler::setDirty. Maintained by
        // protocol-independent rules (a CPU Store sets it; at the LLC, a fill from memory clears it and any
        // other data sets it; a write of the line's own copy to the next
        // level clears it). Cache-to-cache transfers are protocol-dependent: in an L1 the bit is maintained
        // only by Stores and write-backs (the snoop and directory protocols keep dirtiness in their states).
        bool dirty;

        uint32_t m_block_size;
        uint8_t *m_data;

        GenericCacheLine();
        GenericCacheLine(int state, bool valid, uint64_t tag,
                         int owner_id = -1, uint8_t *data = NULL, uint32_t block_size = 0);
        GenericCacheLine(const GenericCacheLine &line);

        ~GenericCacheLine();

        GenericCacheLine &operator=(const GenericCacheLine &line);
        
        void copy(const GenericCacheLine &line);
        void copyBits(const GenericCacheLine &line);
        void copyData(const uint8_t *data);
        void modifyData(const uint8_t *data, uint16_t offset, uint16_t size);
    };
}

#endif /* _GenericCacheLine_H */

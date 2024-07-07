/*
 * File  :      GenericCacheLine.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 9, 2022
 */

#include "../header/GenericCacheLine.h"

namespace octopus
{
    GenericCacheLine::GenericCacheLine()
    {
        valid = false;

        insertCycle = 0;
        accessCycle = 0;
        accessCounter = 0;

        tag = -1;
        state = 0;
        owner_id = -1;

        m_block_size = 0;
        m_data = NULL;
    }

    GenericCacheLine::GenericCacheLine(int state, bool valid, uint64_t tag,
                                       int owner_id, uint8_t *data, 
                                       uint32_t block_size) : GenericCacheLine()
    {
        this->valid = valid;

        this->tag = tag;
        this->state = state;
        this->owner_id = owner_id;

        this->m_block_size = block_size;
        if (data != NULL)
        {
            m_data = new uint8_t[block_size / 8];       // Convert block_size from bits to bytes
            memcpy(this->m_data, data, block_size / 8); // Convert block_size from bits to bytes
        }
    }

    GenericCacheLine::GenericCacheLine(const GenericCacheLine &line) : GenericCacheLine()
    {
        this->copy(line);
    }

    GenericCacheLine::~GenericCacheLine()
    {
        if (m_data != NULL)
            delete[] m_data;
    }

    GenericCacheLine &GenericCacheLine::operator=(const GenericCacheLine &line)
    {
        // Guard self assignment
        if (this == &line)
            return *this;

        this->copy(line);
        return *this;
    }

    void GenericCacheLine::copy(const GenericCacheLine &line)
    {
        copyBits(line);
        if (line.m_data != NULL)
            copyData(line.m_data);
    }

    void GenericCacheLine::copyBits(const GenericCacheLine &line)
    {
        this->valid = line.valid;

        this->insertCycle = line.insertCycle;
        this->accessCycle = line.accessCycle;
        this->accessCounter = line.accessCounter;

        // this->tag = line.tag;
        this->state = line.state;
        this->owner_id = line.owner_id;

        this->m_block_size = line.m_block_size;
    }

    void GenericCacheLine::copyData(const uint8_t *data)
    {
        if (m_data == NULL)
            m_data = new uint8_t[m_block_size];

        memcpy(m_data, data, m_block_size);
    }

    void GenericCacheLine::modifyData(const uint8_t *data, uint16_t offset, uint16_t size)
    {
        if (m_data == NULL)
        {
            std::cout << "GenericCacheLine: Error modifying unallocated line" << std::endl;
            exit(0); 
        }
        if(offset > m_block_size)
        {
            std::cout << "GenericCacheLine: Error out of boundary access" << std::endl;
            exit(0); 
        }

        memcpy(&m_data[offset], data, size);
    }
}
/*
 * File  :      DramMap.cpp  (docs/LLC_Banking_and_Partitioning_Design.md s6.9)
 */
#include "DramMap.h"

#include <cstdlib>
#include <iostream>

namespace octopus
{
    void DramMap::set(int bank_lo, int bank_bits, int group_lo, int group_bits, Decode decode)
    {
        // several MCsim controllers (one per memory controller) must agree on the layout
        if (m_valid && (bank_lo != m_bank_lo || bank_bits != m_bank_bits || group_lo != m_group_lo || group_bits != m_group_bits))
        {
            std::cerr << "DramMap: two MCsim controllers map DRAM banks differently; DRAM bank partitioning needs one layout" << std::endl;
            exit(2);
        }
        m_valid = true;
        m_bank_lo = bank_lo; m_bank_bits = bank_bits; m_group_lo = group_lo; m_group_bits = group_bits;
        m_decode = std::move(decode);
    }
}

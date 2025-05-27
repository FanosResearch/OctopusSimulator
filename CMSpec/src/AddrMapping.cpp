/*
 * File  :      Logger.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 17, 2021
 */

#include "../header/AddrMapping.h"

using namespace std;
namespace ns3
{

    AddrMapping *AddrMapping::_AddrMapping = NULL;

    AddrMapping::AddrMapping()
    {  
    }

    unsigned long AddrMapping::addr_map(unsigned long addr,unsigned int owner_id,unsigned int llc_restore_bits)
    {
        unsigned long int new_addr;
        unsigned int bank_bits_shift = log2(m_llc_nbnks);
        unsigned int offset_bits_shift = log2(m_cl_size);
        unsigned int total_num_sets = m_bnk_size/m_cl_size/m_nway;
        unsigned int set_bits_shift = log2(total_num_sets);
        unsigned long int orig_info = 0;

        // map to new addr
        new_addr = (addr >> (bank_bits_shift+offset_bits_shift) << offset_bits_shift ) | (addr & (m_cl_size-1)); //exclude bank bit from the list
        if (partition_setup == "SetPartition")
        {
            // change set to new set
            orig_info = (addr >> (offset_bits_shift+bank_bits_shift)) & (total_num_sets-1);
            unsigned long int new_set = 0;
            if (owner_id >=50)
                new_set = llc_restore_bits;
            else
                new_set = get_set_bits(addr, owner_id);
            unsigned long int set_mask = ((unsigned long int)(total_num_sets-1)<<(offset_bits_shift));
            new_addr = (new_addr & ~set_mask) | (new_set << offset_bits_shift);
            new_addr = new_addr | (orig_info << (64-set_bits_shift)); //put new set at MSBs
        }
        else if (partition_setup == "BankPartition")
        {
            orig_info = (addr >> (offset_bits_shift)) & (m_llc_nbnks-1); //LSB is bank
            new_addr = new_addr | (orig_info << (64-bank_bits_shift)); //put new bank at MSBs
        }

        return new_addr;
    }

    unsigned long AddrMapping::addr_map_restore(unsigned long addr,unsigned int current_bnk)
    {
        unsigned int bank_bits_shift = log2(m_llc_nbnks);
        unsigned int offset_bits_shift = log2(m_cl_size);
        unsigned int total_set_per_bnk = m_bnk_size/m_cl_size/m_nway;
        unsigned int set_bits_shift = log2(total_set_per_bnk);
        unsigned long int new_addr = addr;
        
        
        if (partition_setup == "SetPartition")
        {
            // restore original set
            unsigned long int orig_set = addr >> (64-set_bits_shift);
            new_addr = new_addr & ~(((unsigned long int)total_set_per_bnk-1)<<(64-set_bits_shift)); // remove orig set from addr

            new_addr = new_addr&~((unsigned long int)m_cl_size-1); // extract tag step1: remove block
            new_addr = (new_addr&~(((unsigned long int)total_set_per_bnk-1)<<offset_bits_shift)) << bank_bits_shift; // extract tag step2: remove sets and shift
            new_addr = new_addr |
                (orig_set <<(offset_bits_shift+bank_bits_shift)) | /*restore set*/
                (current_bnk << offset_bits_shift) | /*restore bank*/
                (addr & m_cl_size-1); /*restore offset*/
        }
        else
        {
            // restore original bank
            if (partition_setup == "BankPartition")
            {
                current_bnk = addr >> (64-bank_bits_shift);
                new_addr = new_addr & ~(((unsigned long int)m_llc_nbnks-1)<<((64-bank_bits_shift)));
            }
                
            new_addr = ((new_addr&~((unsigned long int)m_cl_size-1)) << bank_bits_shift );
            new_addr = new_addr | 
                (current_bnk << offset_bits_shift) | /*restore bank*/
                (addr & m_cl_size-1); /*restore offset*/
        }
        
        return new_addr;
    }

    void AddrMapping::addr_map(Message *msg)
    {
        if (msg->owner >=50 && partition_setup == "BankPartition")
            msg->bank_id = msg->llc_addr_restore;
        else
            msg->bank_id = get_bnk_bits(msg->addr,msg->owner);

        //printf("AddrMapping: addr_map owner %d: %lx",msg->owner, msg->addr);
        msg->addr = addr_map(msg->addr,msg->owner,msg->llc_addr_restore);
        //printf(" -> %lx, bank %lx\n", msg->addr, msg->bank_id);
    }

    void AddrMapping::addr_map_restore(Message *msg,unsigned int current_bnk)
    {
        //printf("AddrMapping restore: addr_map owner %d: %lx",msg->owner, msg->addr);
        // this is either invalidation or writeback initiate from LLC
        // we want to store current LLC addr so when it do app_map with this addr, it get the correct info.
        if (msg->owner >=50)
            msg->llc_addr_restore = get_llc_restore_bit(msg->addr, msg->owner);
        msg->addr = addr_map_restore(msg->addr,current_bnk);
        //printf(" -> %lx, bank %lx\n", msg->addr, current_bnk);
    }


    unsigned int  AddrMapping::get_bnk_bits (unsigned long addr,unsigned int owner_id)
    {
        unsigned int offset_bits = log2(m_cl_size);
        unsigned int bnk_bits = (addr >> (offset_bits)) & (m_llc_nbnks-1); //LSB is bank
        unsigned int num_bnk_per_core = m_llc_nbnks/m_ncores;
        if (num_bnk_per_core == 0 && m_llc_nbnks > 2/*make sure other core still have banks to use*/)
            num_bnk_per_core = 1;
        if (partition_setup == "BankPartition")
        {
            // Hardcoded, last two cores are ATP (GPU/DPU). Each of them get their own bank.
            // The other core shares the rest banks
            if (owner_id >= m_ncores-2)
            {
                unsigned int tmp_offset = m_ncores - owner_id;
                bnk_bits = m_llc_nbnks - num_bnk_per_core* tmp_offset + bnk_bits % num_bnk_per_core;
            }
            else
                bnk_bits = bnk_bits % (m_llc_nbnks-2*num_bnk_per_core);
        }
        return bnk_bits;
    }
    unsigned int AddrMapping::get_set_bits (unsigned long addr,unsigned int owner_id)
    {
        unsigned int offset_bits = log2(m_cl_size);
        unsigned int total_num_sets = m_bnk_size/m_cl_size/m_nway;
        unsigned int bank_bits = log2(m_llc_nbnks);
        unsigned int set_bits = (addr >> (offset_bits+bank_bits)) & (total_num_sets-1);
        unsigned int num_set_per_core = total_num_sets/m_ncores;
        if (partition_setup == "SetPartition")
        {
            // Hardcoded, last two cores are ATP (GPU/DPU). Each of them get their own sets.
            // The other core shares the rest sets
            if (owner_id >= m_ncores-2)
            {
                unsigned int tmp_offset = m_ncores - owner_id;
                set_bits = total_num_sets - num_set_per_core*tmp_offset + (set_bits % num_set_per_core);
            }
            else
                set_bits = set_bits % (total_num_sets-2*num_set_per_core);
        }
        return set_bits;
    }
    unsigned int AddrMapping::get_llc_restore_bit(unsigned long addr,unsigned int owner_id)
    {
        unsigned int offset_bits_shift = log2(m_cl_size);
        unsigned int total_num_sets = m_bnk_size/m_cl_size/m_nway;
        unsigned int llc_restore_bit = 0;
        if (partition_setup == "SetPartition")
            llc_restore_bit = (addr >> offset_bits_shift) & (total_num_sets-1);
        else if (partition_setup == "BankPartition")
            llc_restore_bit = owner_id - 50;
        return llc_restore_bit;
    }
    void AddrMapping::set_partition_setup (string part)
    {
        partition_setup = part;
    }
    void AddrMapping::set_llc_nbnks (int nbnks)
    {
        m_llc_nbnks = nbnks;
    }
    int  AddrMapping::get_llc_nbnks (void)
    {
        return m_llc_nbnks;
    }
    void AddrMapping::set_cl_size (int off)
    {
        m_cl_size = off;
    }
    int  AddrMapping::get_cl_size (void)
    {
        return m_cl_size;
    }
    void AddrMapping::set_ncores (int cores)
    {
        m_ncores = cores;
    }
    int  AddrMapping::get_ncores (void)
    {
        return m_ncores;
    }
    void AddrMapping::set_bnk_size (int size)
    {
        m_bnk_size = size;
    }
    int  AddrMapping::get_bnk_size (void)
    {
        return m_bnk_size;
    }
    void AddrMapping::set_nway (int way)
    {
        m_nway = way;
    }

}
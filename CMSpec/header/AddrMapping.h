/*
 * File  :      AddrMapping.h
 * Author:      Shorouk Abdelhalim
 * Email :      abdels28@mcmaster.ca
 *
 * Created On Apr 20, 2025
 */

#ifndef ADDRMAPPING_H
#define ADDRMAPPING_H

#include <stdlib.h>
#include <string>
#include <cmath>
#include <iostream>

#include "CommunicationInterface.h"

using namespace std;

namespace ns3
{
    class AddrMapping
    {
    protected:
        int m_llc_nbnks;
        int m_cl_size;
        int m_bnk_size;
        int m_nway;
        int m_ncores;
        unsigned int bnk_bits;
        unsigned int offset_bits;
        unsigned int set_bits;
        unsigned int bits_mask;
        string partition_setup;

        static AddrMapping *_AddrMapping;

        AddrMapping();


    public:
        unsigned long addr_map (unsigned long addr, unsigned int owner_id = 0, unsigned int llc_restore_bits = 0);
        unsigned long addr_map_restore (unsigned long addr, unsigned int current_bnk);
        void addr_map(Message *msg);
        void addr_map_restore(Message *msg,unsigned int current_bnk);
        void set_partition_setup (string part);
        unsigned int  get_bnk_bits (unsigned long addr,unsigned int owner_id = 0); 
        unsigned int  get_set_bits (unsigned long addr,unsigned int owner_id = 0);
        unsigned int  get_llc_restore_bit (unsigned long addr,unsigned int owner_id = 0); 
        void set_llc_nbnks (int nbnks);
        int  get_llc_nbnks (void);
        void set_cl_size (int off);
        int  get_cl_size (void);
        void set_ncores (int cores);
        int  get_ncores (void);
        void set_bnk_size (int size);
        int  get_bnk_size (void); 
        void set_nway (int way);    

        static AddrMapping *getAddrMapping()
        {
            if (AddrMapping::_AddrMapping == NULL)
                AddrMapping::_AddrMapping = new AddrMapping();
            return AddrMapping::_AddrMapping;
        }
    };
}

#endif
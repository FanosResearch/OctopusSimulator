/*
 * File  :      TripleBus.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 16, 2022
 */

#ifndef _TRIPLE_BUS_H
#define _TRIPLE_BUS_H

#include "Bus.h"
#include "TripleBusController.h"
#include "TripleBusInterface.h"

using namespace std;

namespace ns3
{
    class TripleBus : public Bus
    {
    protected:

    public:
        TripleBus(list<CacheXml>& lower_level_caches, list<CacheXml>& upper_level_caches, int buffers_max_size);
        ~TripleBus();
    };
}

#endif /* _TRIPLE_BUS_H */

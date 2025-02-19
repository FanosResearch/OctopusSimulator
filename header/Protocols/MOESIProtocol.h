/*
 * File  :      MOESIProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#ifndef _MOESIProtocol_H
#define _MOESIProtocol_H

#include "MESIProtocol.h"

namespace octopus
{
    class MOESIProtocol : public MESIProtocol
    {
    protected:
        std::vector<int> statesRequireWriteBack() override;

    public:
        MOESIProtocol(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~MOESIProtocol();
    };
}

#endif

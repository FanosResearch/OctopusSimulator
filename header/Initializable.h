/*
 * File  :      Initializable.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 11, 2022
 */

#ifndef _INITIALIZABLE_H
#define _INITIALIZABLE_H

#include <stdint.h>

namespace ns3
{
    class Initializable
    {
    public:
        Initializable() {}
        ~Initializable() {}

        virtual void initialize(uint64_t address, uint8_t* data) = 0;
    };
}

#endif /* _Initializable_H */

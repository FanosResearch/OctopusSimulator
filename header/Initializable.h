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

        virtual void initialize(uint64_t address, const uint8_t* data, int size) = 0;
        virtual void read(uint64_t address, uint8_t* data) = 0; //temporary function
    };
}

#endif /* _Initializable_H */

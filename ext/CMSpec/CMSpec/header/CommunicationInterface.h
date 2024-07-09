/*
 * File  :      CommunicationInterface.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On July 16, 2021
 */

#ifndef _COMMUNICATIONINTERFACE_H
#define _COMMUNICATIONINTERFACE_H

#include <stdint.h>
#include <string.h>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>

enum MessageType
{
    REQUEST,
    DATA_RESPONSE,
    SERVICE_REQUEST,
};

class Counters
{
    static std::map<int, std::ofstream> files;
public:
    static std::map<int, int> num_req;
    static std::map<int, int> num_repl;
    static std::map<int, int> num_miss;
    static std::map<int, int> num_hit;
    static std::map<int, int> num_interference;

    static void inc(std::map<int, int>& counter, int core_id)
    {
        if(counter.find(core_id) != counter.end())
            counter[core_id]++;
        else
        {    
            counter[core_id] = 0;
            files[core_id]= std::ofstream("/home/gem5/gem5_new/.vscode/core" + std::to_string(core_id) + ".txt");
        }
    }

    static void print(int core_id)
    {
        std::cout << "***************** Core " << core_id << " *****************" << std::endl;
        std::cout << "Num of Requests = " << num_req[core_id] << std::endl;
        std::cout << "Num of Hits = " << num_hit[core_id] << std::endl;
        std::cout << "Num of Misses = " << num_miss[core_id] << std::endl;
        std::cout << "Num of Replacments = " << num_repl[core_id] << std::endl;
        std::cout << "Num of Interference = " << num_interference[core_id] << std::endl;
        std::cout << std::endl;
    }

    static void printFile(int core_id)
    {
        if(files.find(core_id) != files.end() && files[core_id].is_open())
        {
            files[core_id] << "*********************************************************" << std::endl;
            files[core_id] << "Num of Requests = " << num_req[core_id] << std::endl;
            files[core_id] << "Num of Hits = " << num_hit[core_id] << std::endl;
            files[core_id] << "Num of Misses = " << num_miss[core_id] << std::endl;
            files[core_id] << "Num of Replacments = " << num_repl[core_id] << std::endl;
            files[core_id] << "Num of Interference = " << num_interference[core_id] << std::endl;
            files[core_id] << "*********************************************************" << std::endl;
            files[core_id] << std::endl;
            files[core_id].flush();
        }
    }
};

class Message
{
public:
    uint64_t msg_id = 0; // Check if this can be removed and replaced by the addr
    uint64_t addr = 0;
    uint64_t cycle = 0;
    uint64_t complementary_value = 0;
    uint16_t owner = 0;

    std::vector<uint16_t> to;

    enum Source
    {
        LOWER_INTERCONNECT = 0,
        UPPER_INTERCONNECT,
        SELF
    } source;

    uint8_t *data = NULL;
    uint16_t data_size = 0;

    Message(uint64_t msg_id = 0, uint64_t addr = 0, uint64_t cycle = 0, uint64_t complementary_value = 0, uint16_t owner = 0)
    {
        this->msg_id = msg_id;
        this->addr = addr;
        this->cycle = cycle;
        this->complementary_value = complementary_value;
        this->owner = owner;

        this->source = LOWER_INTERCONNECT;
        this->to.clear();

        data = NULL;
    }

    Message(const Message &M2)
    {
        this->copy(M2);
    }

    ~Message()
    {
        if (this->data != NULL)
            delete[] this->data;
    }

    void copy(const Message &M2)
    {
        msg_id = M2.msg_id;
        addr = M2.addr;
        cycle = M2.cycle;
        complementary_value = M2.complementary_value;
        owner = M2.owner;
        source = M2.source;
        to = M2.to;
        data_size = M2.data_size;

        if (M2.data != NULL)
            this->copy(M2.data, M2.data_size);
        else
        {
            if (this->data != NULL)
                delete[] this->data;
            this->data = NULL;
        }
    }

    void copy(const uint8_t *data, uint16_t size = 64)
    {
        if (this->data != NULL)
            delete[] this->data;
        this->data = new uint8_t[size]; // TODO: constant 8 should be converted to cache line size
        this->data_size = size;

        memcpy(this->data, data, size);
    }

    Message &operator=(const Message &M2)
    {
        // Guard self assignment
        if (this == &M2)
            return *this;

        this->copy(M2);
        return *this;
    }
};

class CommunicationInterface
{
public:
    const int m_interface_id;

    CommunicationInterface(int id) : m_interface_id(id) {}
    virtual ~CommunicationInterface() {}

    virtual bool peekMessage(Message *out_msg) = 0;
    virtual void popFrontMessage() = 0;
    virtual bool pushMessage(Message &msg, uint64_t cycle = 0, MessageType type = MessageType::REQUEST) = 0;
    virtual bool pushMessage2RX(Message &msg, MessageType type = MessageType::REQUEST) { return false; }

    virtual bool rollback(uint64_t address, uint64_t mask, Message *out_msg) { return false; }
};

#endif
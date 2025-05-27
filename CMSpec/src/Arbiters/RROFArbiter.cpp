/*
 * File  :      RROFArbiter.cpp
 * Author:      Shorouk Abdelhalim
 * Email :      abdels28@mcmaster.ca
 *
 * Created On April 17, 2022
 */

#include "../../header/Arbiters/RROFArbiter.h"

namespace ns3
{
   RROFArbiter::RROFArbiter(vector<int> *candidates_ids, int arbiter_period) : Arbiter(candidates_ids, arbiter_period) 
    {
		RR_order =0;
		Requestors_num=0;
		Requestor_size=0;
		slot=0;
		exitFlag = false;
		candidate_id = 0;
    }

    RROFArbiter::~RROFArbiter()
    {
    }

    bool RROFArbiter::elect(uint64_t cycle_number, vector<vector<Message>*>& buffers, Message *out_msg)
    {
		exitVector.clear();
		exitFlag = false;
		msg_index =-1;
		slot =0;
		unsigned int orig_core;
		Requestors_num = RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRRQueueSize();
		if (Requestors_num > 0)
		{
			for (unsigned int i=0; i<Requestors_num; i++)
			{
				exitVector.push_back(false);
			}
			while (!exitFlag)
			{
				for (unsigned int index =0; index <Requestors_num; index++ )
				{
					RR_order = RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRRCore(index); //coreID of RR order
					Requestor_size = RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequestorSize(RR_order);

					if(Requestor_size > slot)
					{
						candidate_id = RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequest(RR_order, slot, &orig_core);

						for(int i = 0; i < (int)buffers.size(); i++)
						{
							msg_index = findRequest(*buffers[i], candidate_id);
							if(msg_index != -1)
							{
								out_msg->copy(buffers[i]->at(msg_index));
								buffers[i]->erase(buffers[i]->begin() + msg_index);
								//std::cout <<"RROF: msgindex: " << msg_index << std::endl;
								return true;
							}
						}
					}
					else
					{
						exitVector[index] = true;
					}
				}
				slot++;
				exitFlag = true;
				for (unsigned int index =0; index <Requestors_num; index++)
				{
					if (exitVector[index] == false)
					{
						exitFlag = false;
						break;
					}
				}
			}
		}
		//cout<< "TERMINATING "<<endl;
		return false;
    }
	
	int RROFArbiter::findRequest(vector<Message>& buffer, int id) //request id
    {
        for(int i = 0; i < (int)buffer.size(); i++)
        {
            if(buffer[i].msg_id == (uint64_t)id)
                return i;
        }
        return -1;
    }

}
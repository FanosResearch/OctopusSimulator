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
		m_rq_cached = nullptr;
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

    bool RROFArbiter::elect(uint64_t cycle_number, vector<deque<Message>*>& buffers, Message *out_msg)
    {
		// Early exit: if all buffers are empty, nothing to elect
		size_t totalBufMsgs = 0;
		for (int i = 0; i < (int)buffers.size(); i++)
			totalBufMsgs += buffers[i]->size();
		if (totalBufMsgs == 0)
			return false;

		// Build buffer index for O(1) candidate lookup: msg_id -> (buffer_idx, position)
		std::unordered_map<uint64_t, std::pair<int,int>> bufferIndex;
		bufferIndex.reserve(totalBufMsgs);
		for (int i = 0; i < (int)buffers.size(); i++)
			for (int j = 0; j < (int)buffers[i]->size(); j++)
				bufferIndex[(*buffers[i])[j].msg_id] = {i, j};

		exitVector.clear();
		exitFlag = false;
		msg_index =-1;
		slot =0;
		unsigned int orig_core;
		if (!m_rq_cached)
			m_rq_cached = RequestorsQueues::getReqQObj()->getRequestorsQueues();
		Requestors_num = m_rq_cached->getRRQueueSize();
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
					RR_order = m_rq_cached->getRRCore(index); //coreID of RR order
					Requestor_size = m_rq_cached->getRequestorSize(RR_order);

					if(Requestor_size > slot)
					{
						candidate_id = m_rq_cached->getRequest(RR_order, slot, &orig_core);

						auto it = bufferIndex.find((uint64_t)candidate_id);
						if (it != bufferIndex.end())
						{
							int buf_idx = it->second.first;
							int pos = it->second.second;
							out_msg->copy(buffers[buf_idx]->at(pos));
							buffers[buf_idx]->erase(buffers[buf_idx]->begin() + pos);
							return true;
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
		return false;
    }
	
	int RROFArbiter::findRequest(deque<Message>& buffer, int id) //request id
    {
        for(int i = 0; i < (int)buffer.size(); i++)
        {
            if(buffer[i].msg_id == (uint64_t)id)
                return i;
        }
        return -1;
    }

}
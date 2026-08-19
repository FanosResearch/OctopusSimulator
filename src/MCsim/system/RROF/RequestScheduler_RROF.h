#ifndef REQUESTSCHEDULER_RROF_H
#define REQUESTSCHEDULER_RROF_H

#include "../../src/RequestScheduler.h"
#include "../../src/RequestorsQueues.h"
#include <map>

using namespace std;
namespace MCsim
{
	class RequestScheduler_RROF : public RequestScheduler
	{
	private:

		RequestorsQueues * requestorsQueues;
		unsigned int requestorId;
		unsigned int requestId;
		unsigned int requestorsNum;
		unsigned int requestBank;
		bool requestFound;
		unsigned int requestIndex;
		bool loopFlag = true;
		unsigned int slot=0;
		vector<bool> requestorFlag;
		map<unsigned int, bool> bankFlag;
		bool stopFlag = true;
		unsigned int orig_core;



	public:
		RequestScheduler_RROF(std::vector<RequestQueue *> &requestQueues, std::vector<CommandQueue *> &commandQueues, const std::map<unsigned int, bool> &requestorTable, RequestorsQueues & requestorsqueues) : RequestScheduler(requestQueues, commandQueues, requestorTable)
		{
			requestorsQueues = &requestorsqueues;
		}

		void requestSchedule()
		{
			requestorsNum = requestorsQueues-> getRRQueueSize ();
			requestorFlag.clear();
			bankFlag.clear();
			loopFlag = true;
			slot=0;
			orig_core = -1;
			// init status of requestors queues in RequestorsFIFO
			for (unsigned int j=0; j < requestorsNum; j++)
			{
				requestorFlag.push_back(true);
			}
			// flag true for empty command bank queue
			for (unsigned int j=0; j < commandQueue.size(); j++)
			{
				if (commandQueue[j]->getSize(true) == 0)
				{
					bankFlag[j] = true;
				}
				else
				{
					bankFlag[j] =false;
				}	
			}
			
			while(loopFlag)
			{	
				for (unsigned int it=0; it != requestorsNum; it++) //iterate on RR queue, taking the oldest request from each requestor
			 	{
			 		requestorId = requestorsQueues->getRRCore(it);   //requestorID from RR queue
			 		scheduledRequest = NULL;
			 		if  (slot < requestorsQueues->getRequestorSize(requestorId) ) // There is a request in this slot
			 		{
						requestId = requestorsQueues->getRequest(requestorId, slot, &orig_core);
						scheduledRequest = getRequestFromRequestQueues(); //get this request from requestQueue
						if ((requestFound) && (scheduledRequest !=NULL)) // if the request is found in the request queues
						{
							if (bankFlag[scheduledRequest->bank] == true)  // command queue of the bank is empty
							{	
								if (isSchedulable(scheduledRequest, isRowHit(scheduledRequest)))
								{
									updateRowTable(scheduledRequest->addressMap[Rank], scheduledRequest->addressMap[Bank], scheduledRequest->row); // Update the open row table for the device
									updateRowTable(scheduledRequest->rank, scheduledRequest->bank, scheduledRequest->row);
									//requestQueue[requestBank]->removeRequest();
									scheduledRequest->scheduled = true;
									bankFlag[scheduledRequest->bank] = false; // store the new state of the command queue as not empty						
									
								}
								scheduledRequest = NULL;
							// }
							// else
							// { // support preemption if the oldest request reaches DRAM
							// 	if (slot == 0 && commandQueue[scheduledRequest->bank]->getSize(true) !=0)
							// 	{
							// 		BusPacket *temp_cmd = commandQueue[scheduledRequest->bank]->checkLastCommand(true);
						 	// 		if (scheduledRequest->requestSize/temp_cmd->size <=  commandQueue[scheduledRequest->bank]->getSize(true)) //perform pre-emption
						 	// 		{
							// 			std::cout << "Preemption case: bank: "<< scheduledRequest->bank<< " queueSize: "<< commandQueue[scheduledRequest->bank]->getSize(true) <<" reqID: "<< requestId << " coreID: "<< requestorId<< " removedRequest "<<  temp_cmd->reqID <<std::endl;
							// 			requestQueue[0]->resetScheduledFlag (temp_cmd->requestorID, temp_cmd->reqID);
							// 			commandQueue[scheduledRequest->bank]->clearQueue(true);

							// 			if (isSchedulable(scheduledRequest, isRowHit(scheduledRequest)))
							// 			{
							// 				updateRowTable(scheduledRequest->addressMap[Rank], scheduledRequest->addressMap[Bank], scheduledRequest->row); // Update the open row table for the device											
							// 				scheduledRequest->scheduled = true;
							// 				bankFlag[requestBank] = false; // store the new state of the command queue as not empty						
							// 			}					
							// 		}
							// 	}
								scheduledRequest = NULL;
							}
						}	
			 		}else
					{
						requestorFlag[it] = false;
					}
			 	}
				loopFlag  = false;
			 	slot++;
				// check if there are more requests in the queues
				for (unsigned int index=0; index <requestorFlag.size(); index++)
				{
					loopFlag = loopFlag || requestorFlag[index];
				}
			}
		}

		Request* getRequestFromRequestQueues()
		{
			Request *tempRequest;
			requestFound = false;
			unsigned int selected_core = (&orig_core == NULL) ? requestorId : orig_core;
			for(requestBank = 0; requestBank < requestQueue.size(); requestBank++)
			{
				for(requestIndex=0; requestIndex< requestQueue[requestBank]->getRequestorSize(true,selected_core);requestIndex++)
				{
					tempRequest = requestQueue[requestBank]->getRequestorRequest(selected_core, requestIndex);
					if(tempRequest->reqID == requestId && tempRequest->scheduled == false)
					{
						requestFound = true;
						return tempRequest;
					}
					else
					{
						requestFound = false;
						tempRequest = NULL;
					}
				}
			}
			return NULL;
		}
	};
} // namespace MCsim

#endif /* REQUESTSCHEDULER_DIRECT_H */

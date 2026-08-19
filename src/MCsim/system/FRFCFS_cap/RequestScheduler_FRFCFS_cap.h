#ifndef REQUESTSCHEDULER_FRFCFS_cap_H
#define REQUESTSCHEDULER_FRFCFS_cap_H

#include "../../src/RequestScheduler.h"
//#include "../../src/BusPacket.h"

namespace MCsim
{
	class RequestScheduler_FRFCFS_cap : public RequestScheduler
	{
	private:
	map<unsigned int, bool> bankFlag;
	Request *closeReq;
	size_t closeReqIndex;
	unsigned int cap_size = 4;
	int cap_counter[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

	public:
		RequestScheduler_FRFCFS_cap(std::vector<RequestQueue *> &requestQueues, std::vector<CommandQueue *> &commandQueues, const std::map<unsigned int, bool> &requestorTable) : RequestScheduler(requestQueues, commandQueues, requestorTable) {}

		~RequestScheduler_FRFCFS_cap(){
		}
		// Simple FR FCFS scheduler in the Request Queueu structure
		void requestSchedule()
		{
			closeReq = NULL;
			bankFlag.clear();
			// flag true for empty command bank queue
			for (unsigned int j=0; j < commandQueue.size(); j++)
			{
				if (commandQueue[j]->getSize(true) == 0)
				{
					bankFlag [j] = true;
				}
				else
				{
					bankFlag [j] = false;
				}	
			}

			for (size_t index = 0; index < requestQueue[0]->getSize(false, 0); index++)
			{ // Loop over the queueing structure
				//if (requestQueue[0]->getSize(false, 0) > 0)
				//{
					scheduledRequest = requestQueue[0]->getRequestCheck(index);
					if (scheduledRequest != NULL && (scheduledRequest->scheduled != true))
					{
						if (bankFlag[scheduledRequest->bank] == true)
						{						
							if (isRowHit(scheduledRequest) )
							{
								scheduledRequest = requestQueue[0]->getRequest(index);
								if (isSchedulable(scheduledRequest, isRowHit(scheduledRequest))) // Determine if the request target is an open row or not
								{
									updateRowTable(scheduledRequest->rank, scheduledRequest->bank, scheduledRequest->row); // Update the open row table for the device
									bankFlag[scheduledRequest->bank] = false;	
									requestQueue[0]->removeRequest();
									cap_counter[ scheduledRequest->bank]++;
									if(cap_counter[ scheduledRequest->bank] == cap_size){
										updateRowTable(scheduledRequest->rank, scheduledRequest->bank, (1<<20)); // Update the open row table for the device as closed
									}
								}
							}
							//else
							//{
							//	if (closeReq == NULL)
							//	{
							//		closeReq = scheduledRequest;
							//		closeReqIndex = index;
							//	}		
							//}
						}
					}
				//}
				scheduledRequest = NULL;
			}
			for (size_t index = 0; index < requestQueue[0]->getSize(false, 0); index++)
			{ // Loop over the queueing structure
				//if (requestQueue[0]->getSize(false, 0) > 0)
				//{
					scheduledRequest = requestQueue[0]->getRequestCheck(index);
					if (scheduledRequest != NULL)
					{

						if (bankFlag[scheduledRequest->bank] == true)
						{
							scheduledRequest = requestQueue[0]->getRequest(index);
							if (isSchedulable(scheduledRequest, isRowHit(scheduledRequest))) // Determine if the request target is an open row or not
							{
								updateRowTable(scheduledRequest->rank, scheduledRequest->bank, scheduledRequest->row); // Update the open row table for the device
								requestQueue[0]->removeRequest();	
								bankFlag[scheduledRequest->bank] = false;
								cap_counter[ scheduledRequest->bank]=0;																			   // Remove the request that has been choosed
							}
						}
					}
				//}
				scheduledRequest = NULL;
			}
		}
	};
} // namespace MCsim

#endif /* REQUESTSCHEDULER_FRFCFS_cap_H */

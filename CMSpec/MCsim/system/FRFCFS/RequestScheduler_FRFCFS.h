
#ifndef REQUESTSCHEDULER_FRFCFS_H
#define REQUESTSCHEDULER_FRFCFS_H

#include "../../src/RequestScheduler.h"

namespace MCsim
{
	class RequestScheduler_FRFCFS : public RequestScheduler
	{
	private:
	map<unsigned int, bool> bankFlag;

	public:
		RequestScheduler_FRFCFS(std::vector<RequestQueue *> &requestQueues, std::vector<CommandQueue *> &commandQueues, const std::map<unsigned int, bool> &requestorTable) : RequestScheduler(requestQueues, commandQueues, requestorTable) {}
		// Simple FR FCFS scheduler in the Request Queueu structure
		void requestSchedule()
		{
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
					if (scheduledRequest != NULL)
					{

						if (bankFlag[scheduledRequest->bank] == true)
						{
							if (isRowHit(scheduledRequest) )
							{
								scheduledRequest = requestQueue[0]->getRequest(index);
								if (isSchedulable(scheduledRequest, isRowHit(scheduledRequest))) // Determine if the request target is an open row or not
								{
									updateRowTable(scheduledRequest->rank, scheduledRequest->bank, scheduledRequest->row); // Update the open row table for the device
									requestQueue[0]->removeRequest();	
									bankFlag[scheduledRequest->bank] = false;																		   // Remove the request that has been choosed
								}
							}
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
								bankFlag[scheduledRequest->bank] = false;																		   // Remove the request that has been choosed
							}
						}
					}
				//}
				scheduledRequest = NULL;
			}

		}
	};
} // namespace MCsim

#endif /* REQUESTSCHEDULER_FRFCFS_H */


#ifndef COMMANDSCHEDULER_RROF_H
#define COMMANDSCHEDULER_RROF_H

#include "../../src/CommandScheduler.h"
#include "../../src/RequestorsQueues.h"

namespace MCsim
{
	class CommandScheduler_RROF : public CommandScheduler
	{
	private:
		RequestorsQueues * requestorsQueues;

		bool loopFlag = true;
		unsigned int slot=0;
		//const std::vector<RequestQueue *> &requestQueue;
		unsigned int requestorId;
		unsigned int requestID;
		unsigned int requestBank;
		unsigned int numReq;
		std::vector<bool> requestorFlag;
		bool cmdFound = false;
		std::vector<BusPacket*> readyCMD;
		const std::vector<RequestQueue *> &requestQueue;
		unsigned int CASIntra_ready;
		unsigned int CASIntra_found;

	public:
		CommandScheduler_RROF(std::vector<RequestQueue *> &requestQueues,vector<CommandQueue *> &commandQueues, const map<unsigned int, bool> &requestorTable, RequestorsQueues & requestorsqueues) : CommandScheduler(commandQueues, requestorTable), requestQueue(requestQueues)
		{
			requestorsQueues = &requestorsqueues;
		}
		BusPacket *commandSchedule()
		{
			// initialize status of requestors queues in RequestorsFIFO
			requestorFlag.clear();
			readyCMD.clear();
			scheduledCommand = NULL;
			checkCommand = NULL;
			cmdFound = false;
			loopFlag = true;
			slot=0;
			CASIntra_ready =0;
			CASIntra_found =0;
			numReq =requestorsQueues->getRRQueueSize ();
			for (unsigned int i =0; i < numReq;i++)
			{
				requestorFlag.push_back(true);
			}
			
			// loop for commands
			while(loopFlag)
			{
				for (unsigned int it=0; it <numReq; it++) //iterate on RR queue of each requestor, taking the oldest request from each
				{
					requestorId = requestorsQueues->getRRCore (it);   //requestorID from FIFOs
					if  (slot < requestorsQueues->getRequestorSize(requestorId) ) // There is a request in this  slot
					{
						unsigned int orig_core;
						requestID =  requestorsQueues->getRequest(requestorId, slot, &orig_core);
						for(requestBank=0; requestBank < commandQueue.size();requestBank++)
						{ 
							if(commandQueue[requestBank]->getSize(true) > 0)
							{
								checkCommand = commandQueue[requestBank]->checkCommand(true, 0);
								if ((checkCommand != NULL) && (checkCommand->reqID == requestID))
								{
									readyCMD.push_back(checkCommand);
									if (slot == 0 && ( (checkCommand->busPacketType == BusPacketType::RD)|| (checkCommand->busPacketType == BusPacketType::WR)) && (CASIntra_found ==0))
									{
										if(isReady(checkCommand,checkCommand->bank))
										{
											CASIntra_ready = readyCMD.size() -1;
											CASIntra_found = 1;
										}

									}

									cmdFound =true;
									break;
								} 	
							}
						} 
			 		}
					else
					{
						requestorFlag[it] = false;
					}
				}
			 	loopFlag  = false;
			 	slot++;
			 	// check if there are more requests in the queues
			 	for (unsigned int index=0; index <requestorFlag.size(); index ++)
				{
					loopFlag = loopFlag || requestorFlag[index];
				}
			}
			//cout << "SA: readyCMD size: " << readyCMD.size() << endl;

			if ( cmdFound )
			{	// loop for CAS cmd
				for(int i =0 ;i< (int)readyCMD.size();i++)
				{
					if( (readyCMD[i]->busPacketType == BusPacketType::RD)|| (readyCMD[i]->busPacketType == BusPacketType::WR))
					{
						if (isReady(readyCMD[i],readyCMD[i]->bank)) // If the command is intra-ready
						{
							if (CASIntra_found ==1)
							{
								if(CASIntra_ready != (unsigned int)i)
								continue;
							}
								
							if (isIssuable(readyCMD[i])) // If the command is issue able (inter-ready)
							{
								// remove request from requests Queues
								requestQueue[0]->removeRequest(readyCMD[i]->requestorID, readyCMD[i]->reqID);
								// remove request from FIFO, after issuing its CAS command	
								if (readyCMD[i]->busPacketType == BusPacketType::WR)
								{
									requestorsQueues->removeRequest(readyCMD[i]->requestorID, readyCMD[i]->reqID);	
									cout << "CommandScheduler: remove: " << readyCMD[i]->reqID << " core: " << readyCMD[i]->requestorID << " size: " << requestorsQueues->getRequestorSize(readyCMD[i]->requestorID) << endl;
								}									
								scheduledCommand = commandQueue[readyCMD[i]->bank]->getCommand(true);
								sendCommand(scheduledCommand,readyCMD[i]->bank, false);
								return scheduledCommand;
							}
						}
					}
				}

				// loop for ACT cmd
				for(int i =0 ;i< (int)readyCMD.size();i++)
				{
					if(readyCMD[i]->busPacketType == BusPacketType::ACT)
					{
						if (isReady(readyCMD[i],readyCMD[i]->bank)) // If the command is intra-ready
						{
							if (isIssuable(readyCMD[i])) // If the command is issue able (inter-ready)
							{
								scheduledCommand = commandQueue[readyCMD[i]->bank]->getCommand(true);
								sendCommand(scheduledCommand,readyCMD[i]->bank, false);
								return scheduledCommand;
							}
						}
					}
				}

				// loop for PRE cmd
				for(int i =0 ;i< (int)readyCMD.size();i++)
				{
					if(readyCMD[i]->busPacketType == BusPacketType::PRE)
					{
						if (isReady(readyCMD[i],readyCMD[i]->bank)) // If the command is intra-ready
						{
							if (isIssuable(readyCMD[i])) // If the command is issue able (inter-ready)
							{
								scheduledCommand = commandQueue[readyCMD[i]->bank]->getCommand(true);
								sendCommand(scheduledCommand,readyCMD[i]->bank, false);
								return scheduledCommand;
							}
						}
					}
				}									
			}
			return NULL;
		}
	};
} // namespace MCsim

#endif /* COMMANDSCHEDULER_FCFS_H */

#include "RequestorsQueues.h"

using namespace MCsim;

#define DEBUG(str) std::cerr << str << std::endl;



void RequestorsQueues::update()
{
	clock++;
}

void RequestorsQueues::updateClk(uint64_t clk)
{
	clock = clk;
}
void RequestorsQueues::setLoggerPath(string path)
{
	logger_path = path + "/WCL_logger.csv";
	file.open(logger_path,ios::out);
	file << "ReqID,Core,WCLatency,Arrival_Time,Finish_Time" << endl;
}

RequestorsQueues::RequestorsQueues()
{
	clock = 0;
	pr_promote = 1;
}

RequestorsQueues::~RequestorsQueues()
{

	clearRequests();
	RROrder.clear();
}

// insert request in global queues
bool RequestorsQueues::addRequest(unsigned int coreID, unsigned int requestID,unsigned int phase, unsigned int reqAddr)
{
	if(!isCoreExist(coreID))
	{
		younger_requestorsQueues[coreID] = requestorQueue();
		oldest_requestorsQueues[coreID]= requestorQueue();
	}
	if(std::find(RROrder.begin(), RROrder.end(), coreID) == RROrder.end())
	{
		RROrder.push_back(coreID);
	}
	if (isRequestExist(coreID, requestID, NULL, NULL) >= 0)
	{
		DEBUG("Error RequestorsQueues: addRequest: Request is exist" << requestID << " " << coreID );
		//return true;
		exit(0);
	}
	if (oldest_requestorsQueues[coreID].size() > 0)
	{
		younger_requestorsQueues[coreID].push_back(make_pair(requestID,phase));
		requestIndex[requestID] = make_pair(coreID, true); // isYounger=true
	}
	else
	{
		oldest_requestorsQueues[coreID].push_back(make_pair(requestID,phase));
		requestIndex[requestID] = make_pair(coreID, false); // isYounger=false
		WCL_logger[requestID][0] = clock;
	}
	return true;
}

bool RequestorsQueues::add2UnifiedQueue(unsigned int coreID, unsigned int requestID, unsigned int reqAddr)
{
	if(pr_promote)
	{
		unifyQueue.push_back(make_pair(requestID, make_pair(reqAddr, coreID)));
		unifyIterMap[requestID] = std::prev(unifyQueue.end());
		unifyMap[requestID] = make_pair(reqAddr, coreID);
		return true;
	}
	return false;
}


void RequestorsQueues::updateReqLogger(unsigned int requestID, int type)
{
}

// get a request ID in a certain location
unsigned int RequestorsQueues::getRequest(unsigned int coreID, unsigned int slot, unsigned int *orig_coreID)
{
	*orig_coreID = coreID;
	if (slot == 0)
	{
		if (oldest_requestorsQueues[coreID].empty())
		{
			DEBUG("Error RequestorsQueues: getRequest: oldest queue is empty for core " << coreID);
			return 0;
		}
		unsigned int reqID = oldest_requestorsQueues[coreID].begin()->first;
		auto it = unifyMap.find(reqID);
		if (it != unifyMap.end())
		{
			*orig_coreID = it->second.second;
		}
		return reqID;
	}
	else
	{
		if (slot - 1 >= younger_requestorsQueues[coreID].size())
		{
			DEBUG("Error RequestorsQueues: getRequest: slot out of bounds for core " << coreID << " slot " << slot);
			return 0;
		}
		return (younger_requestorsQueues[coreID].begin()+slot-1)->first;
	}
}

// check for a core in the buffers
bool RequestorsQueues::isCoreExist(unsigned int coreID)
{
	if (oldest_requestorsQueues.find(coreID) == oldest_requestorsQueues.end())
	{
		return false;
	}
	else
	{
		return true;
	}
}

int RequestorsQueues::isRequestExist(unsigned int coreID, unsigned int requestID, unsigned int *core, bool *vector) //returns the request slot
{
	// Use requestIndex for O(1) lookup, but preserve original search semantics:
	// Original searches younger[coreID] first, then ALL oldest queues.
	auto idxIt = requestIndex.find(requestID);
	if (idxIt == requestIndex.end())
		return -1;

	unsigned int foundCore = idxIt->second.first;
	bool isYounger = idxIt->second.second;

	// Original only searched younger queue of the GIVEN coreID (not foundCore)
	if (isYounger && foundCore == coreID)
	{
		auto &q = younger_requestorsQueues[coreID];
		for (unsigned int x = 0; x < q.size(); x++)
		{
			if (q[x].first == requestID)
			{
				if (core) *core = coreID;
				if (vector) *vector = 1;
				return x;
			}
		}
	}
	// Original searched ALL oldest queues regardless of coreID
	if (!isYounger)
	{
		auto &q = oldest_requestorsQueues[foundCore];
		for (unsigned int x = 0; x < q.size(); x++)
		{
			if (q[x].first == requestID)
			{
				if (core) *core = foundCore;
				if (vector) *vector = 0;
				return x;
			}
		}
	}
	return -1;
}

// remove requests and requestors queues
void RequestorsQueues::clearRequests()
{
	for (auto it = younger_requestorsQueues.begin(); it != younger_requestorsQueues.end(); it++)
	{
		it->second.clear();
	}
	for (auto it = oldest_requestorsQueues.begin(); it != oldest_requestorsQueues.end(); it++)
	{
		it->second.clear();
	}
	younger_requestorsQueues.clear();
	oldest_requestorsQueues.clear();
	unifyQueue.clear();
	unifyIterMap.clear();
	unifyMap.clear();
	requestIndex.clear();
}

// Remove the served request, completed request, access with core ID
void RequestorsQueues::removeRequest(unsigned int coreID, unsigned int requestID)
{
	bool vector;
	unsigned int core;
	int index = isRequestExist(coreID,requestID, &core, &vector);

	if (vector && index > -1) // vector=1 -> is a young request
	{
		younger_requestorsQueues[core][index].second--;
		if (younger_requestorsQueues[core][index].second == 0)
		{
			younger_requestorsQueues[core].erase(younger_requestorsQueues[core].begin()+index);
			requestIndex.erase(requestID);
			if(pr_promote)
			{
				unifyMap.erase(requestID);
				auto uit = unifyIterMap.find(requestID);
				if (uit != unifyIterMap.end())
				{
					unifyQueue.erase(uit->second);
					unifyIterMap.erase(uit);
				}
			}
		}
	}
	else if (!vector && index > -1)
	{
		oldest_requestorsQueues[core][index].second--;
		if (oldest_requestorsQueues[core][index].second == 0)
		{
			oldest_requestorsQueues[core].erase(oldest_requestorsQueues[core].begin()+index);
			requestIndex.erase(requestID);
			if(pr_promote)
			{
				unifyMap.erase(requestID);
				auto uit = unifyIterMap.find(requestID);
				if (uit != unifyIterMap.end())
				{
					unifyQueue.erase(uit->second);
					unifyIterMap.erase(uit);
				}
			}
			if (oldest_requestorsQueues[core].size() == 0)
			{
				// Always log and erase WCL_logger for the completed request
				if (WCL_logger.find(requestID) != WCL_logger.end())
				{
					printLogger (core, requestID);
					WCL_logger.erase(requestID);
				}
				RROrder.erase(remove(RROrder.begin(), RROrder.end(), core), RROrder.end());
				if (younger_requestorsQueues[core].size() > 0)
				{
					RROrder.push_back(core); //re-add in RR Queue if there are more standing requests
					oldest_requestorsQueues[core].push_back(younger_requestorsQueues[core][0]);
					// Update requestIndex: promoted request is now oldest
					requestIndex[younger_requestorsQueues[core][0].first] = make_pair(core, false);
					younger_requestorsQueues[core].erase(younger_requestorsQueues[core].begin());
					WCL_logger [oldest_requestorsQueues[core][0].first][0] = clock;
				}
			}
		}
	}
	if (index == -1)
	{
		DEBUG("Error RequestorsQueues: removeRequest: Request is not exist " << requestID << " " << coreID );
	}
	
}

void RequestorsQueues::printLogger (unsigned int coreID, unsigned int requestID)
{
	file << requestID << ","<< coreID<< "," << (clock - WCL_logger[requestID][0])<< "," <<WCL_logger[requestID][0] << ","<<clock<< endl;

}

void RequestorsQueues::setRemovalCount(unsigned int coreID, unsigned int requestID, unsigned int count)
{
	bool vector;
	unsigned int core;
	int index = isRequestExist(coreID,requestID, &core, &vector);
	if(vector && index > -1)
	{
		younger_requestorsQueues[core][index].second = count;
	}
	else if (!vector && index > -1)
	{
		oldest_requestorsQueues[core][index].second = count;
	}
	if (index == -1)
	{
		DEBUG("Error RequestorsQueues: setRemovalCount: Request is not exist " << requestID << " " << coreID );
		return;
	}
}


unsigned int RequestorsQueues::getRemovalCount (unsigned int coreID, unsigned int requestID)
{
	bool vector;
	unsigned int core;
	int index = isRequestExist(coreID,requestID, &core, &vector);

	if(vector && index > -1)
	{
		return younger_requestorsQueues[core][index].second;
	}
	else if (!vector && index > -1)
	{
		return oldest_requestorsQueues[core][index].second;
	}
	if (index == -1)
	{
		DEBUG("Error RequestorsQueues: getRemovalCount: Request is not exist " << requestID << " " << coreID );
		return -1;
	}
	return -1;
}

unsigned int RequestorsQueues::getRequestorSize(unsigned int coreID)
{
	if(oldest_requestorsQueues[coreID].size() > 0)
		return younger_requestorsQueues[coreID].size()+1;
	else
		return 0;
}

// Get RROder queue size
unsigned int RequestorsQueues::getRRQueueSize ()
{
	return RROrder.size();
}

// Get CoreID of index in RROder queue
unsigned int RequestorsQueues::getRRCore (unsigned int index)
{
	return RROrder[index];
}	

void RequestorsQueues::ifOldest_promote(unsigned int coreID, unsigned int requestID)
{
	if(pr_promote)
	{
		unsigned int Request_cl = 0;

		if (oldest_requestorsQueues[coreID].size()==1 && oldest_requestorsQueues[coreID][0].first == requestID) // oldest request is stalled
		{
			// O(1) lookup for stalled request's cache line address
			auto mapIt = unifyMap.find(requestID);
			if (mapIt != unifyMap.end())
			{
				Request_cl = mapIt->second.first;
			}

			for (auto it = unifyQueue.begin(); it != unifyQueue.end();it++) //get first request with the same CL but not the same reqID
			{
				unsigned int younger_req_core = it->second.second;
				unsigned int younger_req_id = it->first;
				unsigned int younger_req_cl = it->second.first;
				if (younger_req_id==requestID) break;
				if (younger_req_cl == Request_cl)
				{	bool vector;
					unsigned int core;
					int younger_req_indx = isRequestExist(younger_req_core, younger_req_id, &core, &vector);

					if (vector && younger_req_indx > 0) // this promoted request should be young request
					{// promote younger request to the oldest request priority, in stall case
						unsigned int younger_req_removal = younger_requestorsQueues[younger_req_core][younger_req_indx].second;
						oldest_requestorsQueues[coreID].insert(oldest_requestorsQueues[coreID].end()-1,make_pair(younger_req_id,younger_req_removal));
						younger_requestorsQueues[younger_req_core].erase(younger_requestorsQueues[younger_req_core].begin()+younger_req_indx);
						// Update requestIndex: promoted from younger to oldest (in coreID's queue)
						requestIndex[younger_req_id] = make_pair(coreID, false);
					}
				}
			}
		}
	}
}

#ifndef REQUESTORSQueues_H
#define REQUESTORSQueues_H

#include <vector>
#include <utility>
#include <map>
#include <iostream>
#include <algorithm>

#include <iostream>
#include <fstream>


using namespace std;

namespace MCsim
{
	class RequestorsQueues
	{
	private:

		typedef vector<pair <unsigned int, unsigned int>>  requestorQueue; // Queue of Requests< Request ID, removal count>
		map< unsigned int, requestorQueue > younger_requestorsQueues; // <coreID, Queue of requests>
		map< unsigned int, requestorQueue > oldest_requestorsQueues;
		vector <unsigned int> RROrder; // RR order <cores ID>

		vector<pair<unsigned int, pair<unsigned int,unsigned int>> > unifyQueue; // unified queue for all the cores <reqID, <CL address. CoreID>>
		
		//static int n = 9;// 0arrival_time, 1req_bus, 2resp_bus1, 3LLC_access1, 4LLC_DRAM_bus, 5DRAM_access, 6LLC_access2, 7resp_bus2, 8smax
		typedef map <unsigned int, uint64_t> latencies; 

		map<unsigned int, latencies> WCL_logger;
		fstream file;
		uint64_t clock;
		bool pr_promote;
		string logger_path;

		// check for a core in the buffers
		bool isCoreExist(unsigned int coreID);	

		// check for a request in the buffer
		int isRequestExist(unsigned int coreID, unsigned int requestID, unsigned int *core, bool *vector);

		void printLogger (unsigned int coreID, unsigned int requestID);
		
	public:
		RequestorsQueues();
		
		virtual ~RequestorsQueues();

		void update();
		void updateClk(uint64_t clk);
		void updateReqLogger(unsigned int requestID, int type);

		// insert request in the requestors queues
		bool addRequest(unsigned int coreID, unsigned int requestID,unsigned int phase, unsigned int reqAddr);
		//bool addRequest(unsigned int coreID, unsigned int requestID,unsigned int phase);

		// push request to the unified queue with their appearance order on request bus
		bool add2UnifiedQueue(unsigned int coreID, unsigned int requestID, unsigned int reqAddr);

		// get a request ID from a certain slot
		unsigned int getRequest(unsigned int coreID, unsigned int slot, unsigned int *orig_coreID);

		// Remove the served request, completed request, access with core ID
		void removeRequest(unsigned int coreID, unsigned int requestID);

		void setRemovalCount(unsigned int coreID, unsigned int requestID, unsigned int requestPhase);
		unsigned int getRemovalCount(unsigned int coreID, unsigned int requestID);

		// get the number of requests a requestor queue
		unsigned int getRequestorSize(unsigned int coreID);

		// Get RROder queue size
		unsigned int getRRQueueSize ();

		// Get CoreID of index in RROder queue
		unsigned int getRRCore (unsigned int index);	

		// remove requests and requestors queues
		void clearRequests();

		// if oldest Request promote the younger request
		void ifOldest_promote(unsigned int coreID, unsigned int requestID);

		// set the logger path
		void setLoggerPath (string path);

	};
} // namespace MCsim
#endif

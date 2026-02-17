
#ifndef MCSIM_H
#define MCSIM_H

#include "MCsimCallback.h"
#include <string>
#include <vector>
using std::string;

namespace MCsim
{
	class RequestorsQueues
	{
		public:
			bool addRequest(unsigned int coreID, unsigned int requestID,unsigned int phase, unsigned int reqAddr);
			//bool addRequest(unsigned int coreID, unsigned int requestID,unsigned int phase);
			bool add2UnifiedQueue(unsigned int coreID, unsigned int requestID, unsigned int reqAddr);
			unsigned int getRequest(unsigned int coreID, unsigned int slot, unsigned int *orig_coreID);
			void removeRequest(unsigned int coreID, unsigned int requestID);
			void setRemovalCount(unsigned int coreID, unsigned int requestID, unsigned int requestPhase);
			unsigned int getRemovalCount(unsigned int coreID, unsigned int requestID);
			unsigned int getRequestorSize(unsigned int coreID);
			unsigned int getRRQueueSize ();
			unsigned int getRRCore (unsigned int index);	
			void clearRequests();
			void update();
			void updateClk(u_int64_t clk);
			void updateReqLogger(unsigned int requestID, int type);

			void ifOldest_promote(unsigned int coreID, unsigned int requestID);
			void setLoggerPath(string path);

			enum class Latency_type : int {
			arrival_time = 0,
			req_bus,
			resp_bus1,
			LLC_access1,
			LLC_DRAM_bus,
			DRAM_access,
			LLC_access2,
			resp_bus2,
			max
		} ;
	};
	class MultiChannelMemorySystem
	{
	public:
		bool addRequest(unsigned int requestorID, unsigned long long address, bool R_W, unsigned int size);
		bool addRequest(unsigned int requestorID, unsigned long long address, bool R_W, unsigned int size, unsigned long long reqID);
		void setCPUClockSpeed(uint64_t cpuClkFreqHz);
		void update();
		void printStats(bool finalStats);
		bool willAcceptTransaction();
		bool willAcceptTransaction(uint64_t addr);
		std::ostream &getLogFile();

		void RegisterCallbacks(
			TransactionCompleteCB *readDone,
			TransactionCompleteCB *writeDone);
		int getIniBool(const std::string &field, bool *val);
		int getIniUint(const std::string &field, unsigned int *val);
		int getIniUint64(const std::string &field, uint64_t *val);
		int getIniFloat(const std::string &field, float *val);

		RequestorsQueues *requestorsQueues;
		RequestorsQueues * getRequestorsQueues();
	};

	MultiChannelMemorySystem *getMemorySystemInstance(unsigned int numberRequestors, const string &systemIniFilename_, const string &deviceGene, const string &deviceSpeed, const string &deviceSize, unsigned int channels, unsigned int ranks);

} // namespace MCsim

#endif

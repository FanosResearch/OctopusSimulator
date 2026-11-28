#include "SimulatorObject.h"
#include "Request.h"
#include "MemorySystem.h"
#include "Callback.h"
#include "ClockDomain.h"
#include "RequestorsQueues.h"
#include <string>

using namespace std;

namespace MCsim
{

	class MultiChannelMemorySystem : public SimulatorObject
	{
	public:
		MultiChannelMemorySystem(unsigned int numberRequestors, const string &systemIniFilename_, const string &deviceGene, const string &deviceSpeed, const string &deviceSize, unsigned int channels, unsigned int ranks);
		MultiChannelMemorySystem(unsigned int numberRequestors, const string &systemIniFilename_, const string &deviceGene, const string &deviceSpeed, const string &deviceSize, unsigned int channels, unsigned int ranks, function<void(Request &)> callback);

		virtual ~MultiChannelMemorySystem();
		bool addRequest(unsigned int requestorID, unsigned long long address, bool R_W, unsigned int size);
		bool addRequest(unsigned int requestorID, unsigned long long address, bool R_W, unsigned int size, unsigned long long reqID);
		void update();
		// Octopus (DRAM bank partitioning): the bit layout of a memOrg field in the line address of
		// channel 0, and the bank group / bank MCsim's own decode gives an address.
		bool dramFieldBits(unsigned field, unsigned &lo, unsigned &width);
		void dramDecode(unsigned long long address, unsigned &bank_group, unsigned &bank);
		void printStats(bool finalStats = false);
		ostream &getLogFile();
		void RegisterCallbacks(
			TransactionCompleteCB *readDone,
			TransactionCompleteCB *writeDone);

		void InitOutputFiles(string tracefilename);
		void setCPUClockSpeed(uint64_t cpuClkFreqHz);

		float getClk();
		void displayConfiguration();
		bool isWriteModeFromController();
		unsigned int generalBufferSize();
		// Octopus (OCTOPUS_MEM_PROBE): channel 0's request-queue depth and accepted, uncompleted reads / writes
		void dramBacklog(unsigned &queued, unsigned &reads, unsigned &writes);
		void flushWrite(bool sw);

		//output file
		std::ofstream visDataOut;
		ofstream mcsim_log;

		RequestorsQueues *requestorsQueues;
		RequestorsQueues * getRequestorsQueues();

	private:
		unsigned int numberRequestors;
		string systemIniFilename;
		string deviceGene;
		float tCK;
		string deviceSpeed;
		string deviceSize;
		unsigned int numberChannels;
		unsigned int numberRanks;
		unsigned findChannelNumber(unsigned int requestorID, uint64_t addr);
		void actual_update();
		vector<MemorySystem *> channels;

		unsigned megsOfMemory;
		string deviceIniFilename;
		string traceFilename;
		string pwd;
		string *visFilename;
		ClockDomain::ClockDomainCrosser clockDomainCrosser;
		static void mkdirIfNotExist(string path);
		static bool fileExists(string path);
	};
} // namespace MCsim

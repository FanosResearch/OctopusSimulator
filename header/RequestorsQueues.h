/*
 * File  :      RequestorsQueues.cpp
 * Author:      Shorouk Abdelhalim
 * Email :      abdels28@mcmaster.ca
 *
 * Created On April 05, 2022
 */

#ifndef RequestorsQueues_H
#define RequestorsQueues_H

#include "MCsim/MCsim.h"


//using namespace std;

namespace ns3
{
	class RequestorsQueues
	{
	private:

	protected:

		static RequestorsQueues *_RequestorsQueues;
        static MCsim::RequestorsQueues *m_requestors_queues;

	public:
		RequestorsQueues(){}

		static MCsim::RequestorsQueues *getRequestorsQueues()
		{
			if (RequestorsQueues::m_requestors_queues == NULL)
				//std::cout << "pointer is NULL" << std::endl;
				return NULL;
			//else
		    	return RequestorsQueues::m_requestors_queues;
		}
        
        void setRequestorsQueues(MCsim::RequestorsQueues * queues)
        {
            RequestorsQueues::m_requestors_queues = queues;
        }

        static RequestorsQueues *getReqQObj()
		{
			if (RequestorsQueues::_RequestorsQueues == NULL)
				RequestorsQueues::_RequestorsQueues = new RequestorsQueues();
		    return RequestorsQueues::_RequestorsQueues;
		}

	};

    //RequestorsQueues *RequestorsQueues::_RequestorsQueues = NULL;
    //MCsim::RequestorsQueues *RequestorsQueues::m_requestors_queues = NULL;
} // namespace ns3
#endif

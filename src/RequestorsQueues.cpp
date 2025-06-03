/*
 * File  :      RequestorsQueues.cpp
 * Author:      Shorouk Abdelhalim
 * Email :      abdels28@mcmaster.ca
 *
 * Created On April 05, 2022
 */


#include "../header/RequestorsQueues.h"

namespace ns3
{

    RequestorsQueues *RequestorsQueues::_RequestorsQueues = NULL;
    MCsim::RequestorsQueues *RequestorsQueues::m_requestors_queues = NULL;

} // namespace ns3

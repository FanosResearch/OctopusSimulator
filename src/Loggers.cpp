/*
 * File  :      Loggers.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 24, 2024
 */

#include "Logger.h"
#include "LoggerPredictable.h"

namespace ns3
{
    Logger* Logger::getLogger()
    {
        if (Logger::_logger == NULL)
        {
            // Logger::_logger = new Logger();
            Logger::_logger = new LoggerPredictable();
        }
        return Logger::_logger;
    }    
}
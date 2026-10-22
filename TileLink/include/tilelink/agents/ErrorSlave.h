/*
 * File  :      ErrorSlave.h
 * The slave that denies everything (plan §3.5, error-path tests): every request is answered with
 * its normal response type (AccessAck, AccessAckData, HintAck; Tables 12, 13) with denied HIGH, no
 * effect, and every beat of a data response corrupt (§4.4). All beats are always sent (§4.4).
 * Timing, queue depth and the other parameters are RamSlave's (it is a RamSlave with deny_all).
 */
#ifndef _TL_AGENTS_ERRORSLAVE_H
#define _TL_AGENTS_ERRORSLAVE_H
#include "tilelink/agents/RamSlave.h"

namespace octopus { namespace tl
{
    class ErrorSlave : public RamSlave
    {
    public:
        ErrorSlave(const std::string &name, const Config &params);
    };
}}
#endif /* _TL_AGENTS_ERRORSLAVE_H */

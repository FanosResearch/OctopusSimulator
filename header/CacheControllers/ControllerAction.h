/*
 * File  :      ControllerAction.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 14, 2023
 */

#ifndef _ControllerAction_H
#define _ControllerAction_H

namespace octopus
{   
    struct ControllerAction
    {
        enum Type
        {
            REMOVE_PENDING = 0,
            HIT_Action,
            ADD_PENDING,
            SEND_BUS_MSG,
            WRITE_BACK,
            UPDATE_CACHE_LINE,
            WRITE_CACHE_LINE_DATA,
            MODIFY_DATA,
            SAVE_REQ_FOR_WRITE_BACK,
            NO_ACTION,
            REMOVE_SAVED_REQ,
            TIMER_ACTION,
            ADD_SHARER,
            REMOVE_SHARER,
            SEND_INV_MSG,
            STALL,
            ADD_DATA2MSG,
            SET_ACK_NUMBER,
            DECREMENT_ACK,
            INCREMENT_SHARERS,
            DECREMENT_SHARERS,
            SET_SHARERS,
            CLEAR_SHARERS,
            SEND_FWD_MESSAGE,
            REMOVE_PENDING_NORESPONSE,
            // TileLink (docs/TileLink_Plan.md s5.3 I-A): a home's D response to a request, fully built
            // by the protocol (TLC_LLC's Grant[Data]); CacheController::tlRespond
            TL_RESPOND,
            // TileLink: a denied Grant (TLC_L1's Error): the line's parked CPU access is replayed through the
            // processing queue for a new Acquire, the CPU stays unanswered (CacheController::tlRetryPending,
            // docs/TileLink_Denied_Grant_Retry_Plan.md)
            TL_RETRY_PENDING,

            MAX_ACTIONS_NUM
        } type;
    
        void* data;
    };
}

#endif
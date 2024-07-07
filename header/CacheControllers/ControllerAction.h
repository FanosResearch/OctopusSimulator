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

            MAX_ACTIONS_NUM
        } type;
    
        void* data;
    };
}

#endif
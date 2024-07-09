/**
 * DO NOT EDIT THIS FILE!
 * File automatically generated by
 *   /home/gem5/gem5_new/src/mem/slicc/symbols/Type.py:643
 */

#ifndef __DMA_Event_HH__
#define __DMA_Event_HH__

#include <iostream>
#include <string>

namespace gem5
{

namespace ruby
{


// Class definition
/** \enum DMA_Event
 *  \brief DMA events
 */
enum DMA_Event {
    DMA_Event_FIRST,
    DMA_Event_ReadRequest = DMA_Event_FIRST, /**< A new read request */
    DMA_Event_WriteRequest, /**< A new write request */
    DMA_Event_Data, /**< Data from a DMA memory read */
    DMA_Event_Ack, /**< DMA write to memory completed */
    DMA_Event_NUM
};

// Code to convert from a string to the enumeration
DMA_Event string_to_DMA_Event(const ::std::string& str);

// Code to convert state to a string
::std::string DMA_Event_to_string(const DMA_Event& obj);

// Code to increment an enumeration type
DMA_Event &operator++(DMA_Event &e);

::std::ostream&
operator<<(::std::ostream& out, const DMA_Event& obj);

} // namespace ruby
} // namespace gem5
#endif // __DMA_Event_HH__

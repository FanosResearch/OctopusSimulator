/*
 * File  :      RequestType.h
 *
 * The request kinds a core model hands to the memory hierarchy. Kept in its
 * own header so that an embedder (the gem5 bridge) can include ExternalCPU.h
 * without pulling in the trace CPU and its configuration machinery.
 */

#ifndef _REQUEST_TYPE_H
#define _REQUEST_TYPE_H

namespace octopus
{
    enum RequestType
    {
        READ = 0,
        WRITE = 1,
        SETUP_WRITE = 2,
        SETUP_READ = 3,
    };
}

#endif /* _REQUEST_TYPE_H */

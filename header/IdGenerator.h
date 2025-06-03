/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 */

#ifndef _IDGENERATOR_H
#define	_IDGENERATOR_H

class IdGenerator {
private:
    static int reqId;

public:
    static int nextReqId() {
        return reqId++;
    }


};

#endif	/* _IDGENERATOR_H */


/*
 * File  :      CLParser.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 11, 2023
 */

#ifndef _CLParser_H
#define _CLParser_H

#include <string>
#include <vector>
#include <iostream>

using namespace std;
namespace octopus
{
    class CLParser
    {
    private:
        vector<string> lines;

    public:
        CLParser(int argc, char *argv[]);
        vector<string> getParam(string identifier);
    };
}

#endif /* _CLParser_H */

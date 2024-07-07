/*
 * File  :      CLParser.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 11, 2023
 */

#include "../header/CLParser.h"

namespace octopus
{   
    CLParser::CLParser(int argc, char *argv[])
    {
        vector<string> vec(argv, argv + argc);

        if (vec.size () > 0)
        {
            vec.erase(vec.begin());  // discard the program name
            
            //Rearrange lines
            for (int i = 0; i < vec.size(); i++)
            {
                if(vec[i].size() < 3)   //if the string is just '-' + a character
                {    
                    lines.push_back(vec[i] + " " + vec[i + 1]);
                    i++;
                }
                else
                    lines.push_back(vec[i]);
            }
            
            for(auto line : lines)
                cout << line << endl;
        }
    }

    vector<string> CLParser::getParam(string identifier)
    {
        vector<string> out;
        
        for(auto line : lines)
        {
            int pos;
            if((pos = line.find(identifier)) == string::npos) //find parameter identifier
                continue;
            
            line = line.substr(pos + 2); //remove parameter identifier

            //Trim leading spaces
            pos = 0;
            for(auto c : line)
            {
                if(c != ' ')
                    break;
                pos++;
            }
            line = line.substr(pos);

            //Trim extra spaces at the end
            for(pos = line.size() - 1; pos > 0; pos--)
            {
                if(line[pos] != ' ')
                    break;
            }
            line = line.substr(0, pos + 1);

            out.push_back(line);
        }
        
        return out;
    }
}
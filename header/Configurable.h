/*
 * File  :      Configurable.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 17, 2023
 */

#ifndef _CONFIGURABLE_H
#define _CONFIGURABLE_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <map>
#include <variant>

#define STRINGIFY(x) #x
#define MAX_LINE_SIZE   4096
#define MAX_NAME_SIZE   100
#define MAX_TYPE_SIZE   10
#define MAX_STRING_SIZE 100

#define EXTENDS_KEYWORD "Extends"
#define WILDCARD        "*"
#define COMMENT_CHAR    '#'

namespace octopus
{
    class Parameter
    {
    public:
        enum Type
        {
            Integer = 0,
            Double,
            String,
            Vector_Integer,
            Vector_Double,
            Vector_String
        };
        typedef std::variant<int, double, std::string, 
                             std::vector<int>, std::vector<double>, std::vector<std::string>> param;

        const std::string name;
        const Type type;
        const param value;
        
        Parameter(std::string name, Type type, param value) : name(name), type(type), value(value){}
    };

    class Configurable
    {
    public:
        typedef std::map<std::string, std::map<std::string, Parameter>> SubordinateMap; //key is the subordinate name and the value is a sub-map of parameters
        typedef std::map<std::string, Parameter> ParametersMap;                         //key is the parameter name and the value is the parameter itself

    protected:
        std::string name;
        std::string parent_name;

        ParametersMap parameters;
        SubordinateMap subordinate_param;

        ParametersMap getSubMap(std::string comp_name, int instance_number = -1);
        void printConfig();
    
    private:
        template<typename T>
        std::vector<T> parseVector(const char* line);
        Parameter parseLine(const char* line);
        Parameter parseCLparam(std::string line);
        void parseFile(std::string config_path, std::string name);
        void addParameter2Map(Parameter p);
        bool checkKeywords(const char* line, std::string* out_path);
        bool checkComment(const char* line); //This function returns true if the line has only a comment or a blank line (contains ',' and spaces only)

    public:
        Configurable(std::string config_path, std::string name, std::string pname = "", bool skip_print = false);
        Configurable(ParametersMap map, std::string config_path, std::string name, std::string pname = "");
        Configurable(std::vector<std::string> cl_params, std::string config_path, std::string name, std::string pname = "");

        static bool print_config_global;
    };
}

#endif /* _CONFIGURABLE_H */

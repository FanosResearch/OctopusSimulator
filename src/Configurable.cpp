/*
 * File  :      Configurable.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 17, 2023
 */

#include "../header/Configurable.h"

using namespace std;

namespace octopus
{   
    bool Configurable::print_config_global = false;

    Configurable::Configurable(string config_path, string name, string pname, bool skip_print) : name(name), parent_name(pname)
    { 
        parseFile(config_path, name);

        if(!skip_print)
            printConfig();
    }

    Configurable::Configurable(ParametersMap map, string config_path, string name, string pname) :
                    Configurable(config_path, name, pname, true)
    {
        for(auto kv : map)
            addParameter2Map(kv.second);

        printConfig();
    }

    Configurable::Configurable(vector<string> cl_params, string config_path, string name, string pname) :
                    Configurable(config_path, name, pname, true)
    {
        for(auto line : cl_params)
        {
            Parameter p = parseCLparam(line.c_str());
            addParameter2Map(p);
        }

        printConfig();
    }

    template<typename T>
    vector<T> Configurable::parseVector(const char* line)
    {
        T value;
        vector<T> vect;
        int ret;
        string sline(line);

        do
        {
            int position = sline.find(",");
            if(position == string::npos)
                break;
            else if(checkComment(sline.substr(position).c_str()))
                break;

            if constexpr(is_same<T, int>::value)
                ret = sscanf(sline.substr(position).c_str(), ",%d", &value);
            else if constexpr(is_same<T, double>::value)
                ret = sscanf(sline.substr(position).c_str(), ",%lf", &value);
            else if constexpr(is_same<T, string>::value)
            {    
                char s_value[MAX_STRING_SIZE];
                ret = sscanf(sline.substr(position).c_str(), ",%[^,\n\r]", &s_value);
                value = string(s_value);
            }
            else
            {
                cout << "Error occur while parsing a vector Parameter" << endl;
                exit(0);
            }

            if(ret == 1)
                vect.push_back(value);
            sline = sline.substr(position + 1);
        }while(!sline.empty());
        
        return vect;
    }

    Parameter Configurable::parseLine(const char* line)
    {
        char param_name[MAX_NAME_SIZE] = {0}, param_indicator[MAX_TYPE_SIZE] = {0};

        sscanf(line, "%[^(]", param_name); //reads from the beginning to '('
        sscanf(line, "%*[^(](%[^)]", param_indicator); //ignores from the beginning to '(' then reads from '(' to ')'

        if(string(param_indicator) == "i")
        {
            int i_value = 0;
            sscanf(line, "%*[^,],%d", &i_value); //ignores from the beginning to ',' then reads the integer value
            return Parameter(param_name, Parameter::Type::Integer, i_value);
        }
        else if(string(param_indicator) == "d")
        {
            double d_value = 0.0;
            sscanf(line, "%*[^,],%lf", &d_value); //ignores from the beginning to ',' then reads the double value
            return Parameter(param_name, Parameter::Type::Double, d_value);
        }
        else if(string(param_indicator) == "s")
        {
            char s_value[MAX_STRING_SIZE] = {0};
            sscanf(line, "%*[^,],%[^,\r\n]", s_value); //ignores from the beginning to ',' then reads all characters till ',', '\r', or '\n'
            return Parameter(param_name, Parameter::Type::String, string(s_value));
        }
        else if(string(param_indicator) == "vi")
        {
            vector<int> vect = parseVector<int>(line);
            return Parameter(param_name, Parameter::Type::Vector_Integer, vect);
        }
        else if(string(param_indicator) == "vd")
        {
            vector<double> vect = parseVector<double>(line);
            return Parameter(param_name, Parameter::Type::Vector_Double, vect);
        }
        else if(string(param_indicator) == "vs")
        {
            vector<string> vect = parseVector<string>(line);
            return Parameter(param_name, Parameter::Type::Vector_String, vect);
        }
        else
        {
            cout << "Wrong Parameter type for " << param_name << "(" << param_indicator << ")\n";
            exit(0);
            return Parameter("Error", Parameter::Type::Integer, -1);
        }
    }

    Parameter Configurable::parseCLparam(string line)
    {
        for(int i = 0; line[i] != '\0'; i++)
        {
            if(line[i] == '=')
            {
                line[i] = ',';
                break;
            }
        }
        return parseLine(line.c_str());
    }

    void Configurable::parseFile(std::string config_path, string name)
    {
        FILE* file = fopen((config_path + "/" + name + ".csv").c_str(), "r");
        if(file == NULL)
        {
            cout << "Failed to open configuration file: " << config_path << endl;
            exit(0);
        }

        char* line = new char[MAX_LINE_SIZE];
        string extension_path;

        fscanf(file, "%[^\n]\n", line);
        if(checkKeywords(line, &extension_path))
        {
            parseFile(config_path, extension_path);
            if(fscanf(file, "%[^\n]\n", line) == EOF)
            {
                delete[] line;
                return;
            }
        }
        
        do
        {
            if(checkComment(line))
                continue;
            Parameter p = parseLine(line);
            addParameter2Map(p);
        }while(fscanf(file, "%[^\n]\n", line) != EOF);

        delete[] line;
    }

    void Configurable::addParameter2Map(Parameter p)
    {
        if(p.name.find(".") == string::npos)             //if there is no dot in the name
        {
            auto ret = parameters.emplace(p.name, p);   //add the parameter to the main map
            if(ret.second == false) //Parameter was added before with an old value
            {
                parameters.erase(ret.first);
                parameters.emplace(p.name, p);
            }
        }
        else                                             //else, add it to subordinate parameters
        {
            string sub_name = p.name.substr(0, p.name.find("."));
            string param_name = p.name.substr(p.name.find(".") + 1);
            auto ret = subordinate_param[sub_name].emplace(param_name, 
                                                           Parameter(param_name, p.type, p.value)); //add the parameter to the main map
            if(ret.second == false) //Parameter was added before with an old value
            {
                subordinate_param[sub_name].erase(ret.first);
                subordinate_param[sub_name].emplace(param_name, Parameter(param_name, p.type, p.value));
            }
        }
    }

    bool Configurable::checkKeywords(const char* line, string* out_path)
    {
        char keyword[MAX_NAME_SIZE];

        sscanf(line, "%[^,]", keyword);

        if(string(keyword) == string(EXTENDS_KEYWORD))
        {
            char* path = new char[MAX_LINE_SIZE];

            sscanf(line, "%*[^,], %[^,\r\n]", path);
            out_path->append(path);

            delete[] path;
            return true;
        }
        
        return false;
    }

    Configurable::ParametersMap Configurable::getSubMap(string comp_name, int instance_number)
    {
        string name = comp_name;
        string wild_name = comp_name + "[" + WILDCARD + "]";
        if(instance_number != -1)
            name += "[" + std::to_string(instance_number) + "]";

        if(subordinate_param.find(name) != subordinate_param.end())
        {
            ParametersMap m = subordinate_param[name];
            if(subordinate_param.find(wild_name) != subordinate_param.end())
            {
                ParametersMap sub_map = subordinate_param[wild_name];
                m.insert(sub_map.begin(), sub_map.end()); //This should add new items from sub_map to m map and ignore replicated parameters. 
            }
            return m;
        }
        else if(subordinate_param.find(wild_name) != subordinate_param.end())
            return subordinate_param[wild_name];
        else
            return ParametersMap();
    }

    bool Configurable::checkComment(const char* line)
    {
        int pos = 0;
        string str = string(line);
        //Trim leading spaces
        for(auto c : str)
        {
            if(c != ' ')
                break;
            pos++;
        }
        str = str.substr(pos);

        //remove leading ','
        pos = 0;
        for(auto c : str)
        {
            if(c != ',' && c != ' ' && c != '\n' && c != '\r')
                break;
            pos++;
        }
        str = str.substr(pos);

        if(str[0] == COMMENT_CHAR || str.empty())
            return true;
        else
            return false;
    }

    void Configurable::printConfig()
    {
        if(!Configurable::print_config_global)
            return;

        printf("--------------------------------------------\n");
        for(auto [key, param] : parameters)
        {
            if(!parent_name.empty())
                printf("\033[1;34m%s.", parent_name.c_str());
            printf("\033[1;32m%s\033[0m.%s = ", name.c_str(), param.name.c_str());
            switch(param.type)
            {
                case Parameter::Type::Integer:
                    printf("%d\n", std::get<int>(param.value));
                    break;
                case Parameter::Type::Double:
                    printf("%lf\n", std::get<double>(param.value));
                    break;
                case Parameter::Type::String:
                    printf("%s\n", std::get<string>(param.value).c_str());
                    break;
                case Parameter::Type::Vector_Integer:
                {
                    auto vec = std::get<vector<int>>(param.value);
                    printf("[");
                    for(int i = 0; i < vec.size(); i++)
                    {
                        if(i < (vec.size() - 1))
                            printf("%d, ", vec[i]);
                        else
                            printf("%d", vec[i]);
                    }
                    printf("]\n");
                    break;
                }
                case Parameter::Type::Vector_Double:
                {
                    auto vec = std::get<vector<double>>(param.value);
                    printf("[");
                    for(int i = 0; i < vec.size(); i++)
                    {
                        if(i < (vec.size() - 1))
                            printf("%lf, ", vec[i]);
                        else
                            printf("%lf", vec[i]);
                    }
                    printf("]\n");
                    break;
                }
                case Parameter::Type::Vector_String:
                {
                    auto vec = std::get<vector<string>>(param.value);
                    printf("[");
                    for(int i = 0; i < vec.size(); i++)
                    {
                        if(i < (vec.size() - 1))
                            printf("%s, ", vec[i].c_str());
                        else
                            printf("%s", vec[i].c_str());
                    }
                    printf("]\n");
                    break;
                }
            }
        }
        printf("--------------------------------------------\n");
    }
} 
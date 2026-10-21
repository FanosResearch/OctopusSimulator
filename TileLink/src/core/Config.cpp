/*
 * File  :      Config.cpp
 * The Octopus CSV parameter format (src/Configurable.cpp), re-read here with no Octopus
 * dependency. The accepted syntax is in Config.h.
 */
#include "tilelink/Config.h"
#include "tilelink/Types.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace octopus { namespace tl
{
    static std::string trim(const std::string &s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    // Configurable::checkComment: after leading spaces and commas, '#' or nothing.
    bool Config::isComment(const std::string &line)
    {
        size_t p = line.find_first_not_of(" ,\t\r\n");
        return p == std::string::npos || line[p] == '#';
    }

    // The cells after the name cell, in order; stops at a comment cell; empty cells skipped
    // (parseVector's behaviour).
    static std::vector<std::string> cells(const std::string &rest)
    {
        std::vector<std::string> out;
        std::string s = rest;
        while (!s.empty())
        {
            if (Config::isComment(s)) break;
            size_t p = s.find(',');
            std::string cell = trim(p == std::string::npos ? s : s.substr(0, p));
            if (!cell.empty()) out.push_back(cell);
            if (p == std::string::npos) break;
            s = s.substr(p + 1);
        }
        return out;
    }

    Param Config::parseLine(const std::string &line0)
    {
        std::string line = line0;
        size_t lp = line.find('('), rp = line.find(')', lp == std::string::npos ? 0 : lp);
        if (lp == std::string::npos || rp == std::string::npos || lp == 0)
            throw ConfigError("bad parameter line (expected name(type),value): " + trim(line0));
        Param p;
        p.name = trim(line.substr(0, lp));
        std::string type = line.substr(lp + 1, rp - lp - 1);
        std::string rest = line.substr(rp + 1);
        if (!rest.empty() && rest[0] == ',') rest = rest.substr(1);
        else if (!rest.empty() && rest[0] != '\r' && rest[0] != '\n')
            throw ConfigError("bad parameter line (expected ',' after the type): " + trim(line0));

        auto first = [&]() -> std::string
        {
            // parseLine's "%[^,\r\n]": the first cell, up to the next comma
            size_t p = rest.find_first_of(",\r\n");
            return p == std::string::npos ? rest : rest.substr(0, p);
        };
        auto toInt = [&](const std::string &s) -> int
        {
            char *end = nullptr;
            long v = std::strtol(s.c_str(), &end, 0);
            if (end == s.c_str()) throw ConfigError("bad integer '" + s + "' for " + p.name);
            return int(v);
        };
        auto toDouble = [&](const std::string &s) -> double
        {
            char *end = nullptr;
            double v = std::strtod(s.c_str(), &end);
            if (end == s.c_str()) throw ConfigError("bad number '" + s + "' for " + p.name);
            return v;
        };
        if (type == "i") { p.type = Param::Integer; p.value = toInt(trim(first())); }
        else if (type == "d") { p.type = Param::Double; p.value = toDouble(trim(first())); }
        else if (type == "s") { p.type = Param::String; p.value = first(); }
        else if (type == "vi")
        {
            std::vector<int> v;
            for (auto &c : cells(rest)) v.push_back(toInt(c));
            p.type = Param::VecInteger; p.value = v;
        }
        else if (type == "vd")
        {
            std::vector<double> v;
            for (auto &c : cells(rest)) v.push_back(toDouble(c));
            p.type = Param::VecDouble; p.value = v;
        }
        else if (type == "vs") { p.type = Param::VecString; p.value = cells(rest); }
        else throw ConfigError("wrong parameter type for " + p.name + "(" + type + ")");
        return p;
    }

    void Config::set(const std::string &line0)
    {
        // parseCLparam: the first '=' stands for the first ','
        std::string line = line0;
        size_t eq = line.find('=');
        if (eq != std::string::npos && (line.find(',') == std::string::npos || eq < line.find(','))) line[eq] = ',';
        add(parseLine(line));
    }

    void Config::add(const Param &p)
    {
        // addParameter2Map: no dot = top level; else the text before the first dot names the
        // sub-map. A later entry replaces an earlier one.
        size_t dot = p.name.find('.');
        if (dot == std::string::npos) { m_params[p.name] = p; return; }
        Param q = p;
        q.name = p.name.substr(dot + 1);
        m_subs[p.name.substr(0, dot)][q.name] = q;
    }

    bool Config::load(const std::string &path, std::string *err) { return loadInto(path, err, 0); }

    bool Config::loadInto(const std::string &path, std::string *err, int depth)
    {
        std::ifstream in(path);
        if (!in) { if (err) *err = "cannot open configuration file: " + path; return false; }
        if (depth > 8) { if (err) *err = "Extends nesting too deep at " + path; return false; }
        std::string line;
        bool first = true;
        int lineno = 0;
        try
        {
            while (std::getline(in, line))
            {
                lineno++;
                if (first)
                {
                    first = false;
                    // Extends,<file>: resolved against this file's directory, ".csv" appended
                    // if the name has no extension (Configurable::parseFile).
                    if (line.compare(0, 8, "Extends,") == 0)
                    {
                        std::string base = trim(line.substr(8));
                        size_t c = base.find(',');
                        if (c != std::string::npos) base = trim(base.substr(0, c));
                        if (base.find('.') == std::string::npos) base += ".csv";
                        std::string dir = path.find('/') == std::string::npos ? "" : path.substr(0, path.rfind('/') + 1);
                        if (!base.empty() && base[0] != '/') base = dir + base;
                        if (!loadInto(base, err, depth + 1)) return false;
                        continue;
                    }
                }
                if (isComment(line)) continue;
                add(parseLine(line));
            }
        }
        catch (ConfigError &e)
        {
            if (err) *err = path + ":" + std::to_string(lineno) + ": " + e.what();
            return false;
        }
        return true;
    }

    const Param &Config::param(const std::string &key) const
    {
        auto it = m_params.find(key);
        if (it == m_params.end()) throw ConfigError("missing configuration key " + key);
        return it->second;
    }

    template <typename T> T Config::get(const std::string &key) const
    {
        const Param &p = param(key);
        if (!std::holds_alternative<T>(p.value)) throw ConfigError("configuration key " + key + " has another type");
        return std::get<T>(p.value);
    }
    template int Config::get<int>(const std::string &) const;
    template double Config::get<double>(const std::string &) const;
    template std::string Config::get<std::string>(const std::string &) const;
    template std::vector<int> Config::get<std::vector<int>>(const std::string &) const;
    template std::vector<double> Config::get<std::vector<double>>(const std::string &) const;
    template std::vector<std::string> Config::get<std::vector<std::string>>(const std::string &) const;

    bool Config::hasSub(const std::string &name, int instance) const
    {
        if (instance < 0) return m_subs.count(name) > 0;
        return m_subs.count(name + "[" + std::to_string(instance) + "]") > 0 || m_subs.count(name + "[*]") > 0;
    }

    Config Config::sub(const std::string &name, int instance) const
    {
        // getSubMap: the instance's entries win over the wildcard's.
        // The stripped keys keep their remaining dots (m_data_handler.cache_size), as Octopus's
        // sub-maps do, so they go straight into the map and are not split again.
        Config c;
        std::string key = instance < 0 ? name : name + "[" + std::to_string(instance) + "]";
        auto wild = m_subs.find(name + "[*]");
        if (instance >= 0 && wild != m_subs.end())
            for (auto &kv : wild->second) c.m_params[kv.first] = kv.second;
        auto it = m_subs.find(key);
        if (it != m_subs.end())
            for (auto &kv : it->second) c.m_params[kv.first] = kv.second;
        return c;
    }

    std::vector<int> Config::instances(const std::string &name) const
    {
        std::vector<int> v;
        for (auto &kv : m_subs)
        {
            const std::string &k = kv.first;
            if (k.size() <= name.size() + 2 || k.compare(0, name.size(), name) != 0 || k[name.size()] != '[' || k.back() != ']') continue;
            std::string idx = k.substr(name.size() + 1, k.size() - name.size() - 2);
            if (idx == "*" || idx.empty() || idx.find_first_not_of("0123456789") != std::string::npos) continue;
            v.push_back(std::atoi(idx.c_str()));
        }
        std::sort(v.begin(), v.end());
        return v;
    }

    std::vector<std::string> Config::subNames() const
    {
        std::vector<std::string> v;
        for (auto &kv : m_subs) v.push_back(kv.first);
        return v;
    }

    static std::string valueText(const Param &p)
    {
        std::ostringstream o;
        switch (p.type)
        {
            case Param::Integer: o << std::get<int>(p.value); break;
            case Param::Double: o << std::get<double>(p.value); break;
            case Param::String: o << std::get<std::string>(p.value); break;
            case Param::VecInteger: { o << "["; bool f = true; for (int x : std::get<std::vector<int>>(p.value)) { o << (f ? "" : ",") << x; f = false; } o << "]"; break; }
            case Param::VecDouble: { o << "["; bool f = true; for (double x : std::get<std::vector<double>>(p.value)) { o << (f ? "" : ",") << x; f = false; } o << "]"; break; }
            case Param::VecString: { o << "["; bool f = true; for (auto &x : std::get<std::vector<std::string>>(p.value)) { o << (f ? "" : ",") << x; f = false; } o << "]"; break; }
        }
        return o.str();
    }

    std::string Config::describe() const
    {
        std::string s;
        for (auto &kv : m_params) s += kv.first + " = " + valueText(kv.second) + "\n";
        for (auto &sub : m_subs)
            for (auto &kv : sub.second) s += sub.first + "." + kv.first + " = " + valueText(kv.second) + "\n";
        return s;
    }
}}

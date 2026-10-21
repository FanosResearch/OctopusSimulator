/*
 * File  :      Config.h
 * A reader of the Octopus CSV parameter format (header/Configurable.h), so that Octopus
 * parameter maps can be passed through later (plan §4). Syntax, matched to Configurable.cpp:
 *   name(type),value[,value...][,,,#comment]      type in i (int), d (double), s (string),
 *                                                 vi, vd, vs (vectors; empty cells skipped,
 *                                                 a cell starting with # ends the vector)
 *   sub.name(type),value                          a sub-map entry: sub = text before the first
 *                                                 dot, e.g. agent[0].type(s),FuzzMaster
 *   name[*].key(type),value                       a wildcard instance, merged under name[k]
 *   Extends,<file>                                first line only: include that file first
 *   # ...   or blank / commas only                a comment
 * A command-line override uses `=` for the first comma: name(type)=value (set()).
 * Later entries replace earlier ones, as addParameter2Map does.
 */
#ifndef _TL_CONFIG_H
#define _TL_CONFIG_H
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace octopus { namespace tl
{
    struct Param
    {
        enum Type { Integer = 0, Double, String, VecInteger, VecDouble, VecString };
        using Value = std::variant<int, double, std::string, std::vector<int>, std::vector<double>, std::vector<std::string>>;
        std::string name;
        Type        type = Integer;
        Value       value;
    };

    class Config
    {
    public:
        using Map = std::map<std::string, Param>;

        // Reads a file; returns false with *err set on a syntax error or a missing file.
        bool load(const std::string &path, std::string *err = nullptr);
        // One line in the file syntax, or name(type)=value. Throws ConfigError on a bad line.
        void set(const std::string &line);
        void add(const Param &p);

        bool has(const std::string &key) const { return m_params.count(key) > 0; }
        // Typed access; T in int, double, std::string, std::vector<int>, std::vector<double>,
        // std::vector<std::string>. Throws ConfigError when the key is missing or of another type.
        template <typename T> T get(const std::string &key) const;
        template <typename T> T get(const std::string &key, const T &def) const { return has(key) ? get<T>(key) : def; }
        const Param &param(const std::string &key) const;
        const Map &params() const { return m_params; }

        // The sub-map of `name` (or `name[k]`, merged with `name[*]`, the instance winning), with
        // the prefix stripped: Configurable::getSubMap.
        Config sub(const std::string &name, int instance = -1) const;
        bool   hasSub(const std::string &name, int instance = -1) const;
        // The instance numbers k present as name[k] (wildcards excluded), ascending.
        std::vector<int> instances(const std::string &name) const;
        std::vector<std::string> subNames() const;

        std::string describe() const;   // one "key = value" per line, sorted

        // A comment or blank line: after leading spaces and commas, '#' or nothing.
        static bool isComment(const std::string &line);

    private:
        static Param parseLine(const std::string &line);
        bool loadInto(const std::string &path, std::string *err, int depth);

        Map m_params;                             // keys without a dot
        std::map<std::string, Map> m_subs;        // sub name -> its map
    };
}}
#endif /* _TL_CONFIG_H */

#ifndef STRING_UTILS_H
#define STRING_UTILS_H
#include <string>
#include <vector>
#include <sstream>
class StringUtils {
public:
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::string trim(const std::string& str);
    static std::string to_lower(const std::string& str);
    static std::string to_upper(const std::string& str);
    static bool starts_with(const std::string& str, const std::string& prefix);
    static bool ends_with(const std::string& str, const std::string& suffix);
    static std::string replace(const std::string& str, const std::string& from, const std::string& to);
    static std::string join(const std::vector<std::string>& vec, const std::string& delimiter);
};
#endif

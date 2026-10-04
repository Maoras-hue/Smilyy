#ifndef REGISTRY_UTILS_H
#define REGISTRY_UTILS_H
#include <string>
#include <windows.h>
#include <vector>
class RegistryUtils {
public:
    static bool set_registry_value(HKEY root, const std::string& key, const std::string& name, const std::string& value);
    static bool get_registry_value(HKEY root, const std::string& key, const std::string& name, std::string& value);
    static bool delete_registry_value(HKEY root, const std::string& key, const std::string& name);
    static bool delete_registry_key(HKEY root, const std::string& key);
    static bool create_registry_key(HKEY root, const std::string& key);
    static bool key_exists(HKEY root, const std::string& key);
    static std::vector<std::string> get_subkeys(HKEY root, const std::string& key);
    static std::vector<std::string> get_values(HKEY root, const std::string& key);
};
#endif

#include "registry_utils.h"
#include "logger.h"
#include <sstream>

bool RegistryUtils::set_registry_value(HKEY root, const std::string& key, const std::string& name, const std::string& value) {
    HKEY hKey;
    if (RegCreateKeyExA(root, key.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        Logger::getInstance().log("Failed to create registry key: " + key);
        return false;
    }
    DWORD result = RegSetValueExA(hKey, name.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(), value.length() + 1);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool RegistryUtils::get_registry_value(HKEY root, const std::string& key, const std::string& name, std::string& value) {
    HKEY hKey;
    if (RegOpenKeyExA(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    char buffer[4096];
    DWORD size = sizeof(buffer);
    DWORD type;
    if (RegQueryValueExA(hKey, name.c_str(), NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            value = std::string(buffer);
            RegCloseKey(hKey);
            return true;
        }
    }
    RegCloseKey(hKey);
    return false;
}

bool RegistryUtils::delete_registry_value(HKEY root, const std::string& key, const std::string& name) {
    HKEY hKey;
    if (RegOpenKeyExA(root, key.c_str(), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD result = RegDeleteValueA(hKey, name.c_str());
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool RegistryUtils::delete_registry_key(HKEY root, const std::string& key) {
    return RegDeleteKeyA(root, key.c_str()) == ERROR_SUCCESS;
}

bool RegistryUtils::create_registry_key(HKEY root, const std::string& key) {
    HKEY hKey;
    if (RegCreateKeyExA(root, key.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool RegistryUtils::key_exists(HKEY root, const std::string& key) {
    HKEY hKey;
    if (RegOpenKeyExA(root, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

std::vector<std::string> RegistryUtils::get_subkeys(HKEY root, const std::string& key) {
    std::vector<std::string> result;
    HKEY hKey;
    if (RegOpenKeyExA(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return result;
    }
    DWORD index = 0;
    char name[256];
    DWORD size = sizeof(name);
    while (RegEnumKeyExA(hKey, index++, name, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        result.push_back(std::string(name));
        size = sizeof(name);
    }
    RegCloseKey(hKey);
    return result;
}

std::vector<std::string> RegistryUtils::get_values(HKEY root, const std::string& key) {
    std::vector<std::string> result;
    HKEY hKey;
    if (RegOpenKeyExA(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return result;
    }
    DWORD index = 0;
    char name[256];
    DWORD size = sizeof(name);
    while (RegEnumValueA(hKey, index++, name, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        result.push_back(std::string(name));
        size = sizeof(name);
    }
    RegCloseKey(hKey);
    return result;
}

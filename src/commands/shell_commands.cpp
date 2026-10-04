#include "shell_commands.h"
#include "../utils/logger.h"
#include <windows.h>
#include <cstdio>
#include <memory>
#include <array>
#include <string>

std::string ShellCommands::execute_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::string full_cmd = "cmd /c " + cmd;
    FILE* pipe = _popen(full_cmd.c_str(), "r");
    if (!pipe) {
        Logger::getInstance().log("Failed to execute command: " + cmd);
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    _pclose(pipe);
    Logger::getInstance().log("Executed shell command: " + cmd);
    return result;
}

std::string ShellCommands::execute_powershell(const std::string& cmd) {
    std::string full_cmd = "powershell -Command \"" + cmd + "\"";
    return execute_command(full_cmd);
}

std::string ShellCommands::execute_cmd(const std::string& cmd) {
    return execute_command(cmd);
}

bool ShellCommands::is_administrator() {
    BOOL result = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &result);
        FreeSid(adminGroup);
    }
    return result != FALSE;
}

std::string ShellCommands::get_current_user() {
    char username[256];
    DWORD size = sizeof(username);
    if (GetUserNameA(username, &size)) {
        return std::string(username);
    }
    return "Unknown";
}

std::string ShellCommands::get_computer_name() {
    char name[256];
    DWORD size = sizeof(name);
    if (GetComputerNameA(name, &size)) {
        return std::string(name);
    }
    return "Unknown";
}

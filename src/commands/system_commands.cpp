#include "system_commands.h"
#include "../utils/logger.h"
#include <windows.h>
#include <tlhelp32.h>
#include <sstream>
#include <iphlpapi.h>
#include <vector>  // Added for vector

#pragma comment(lib, "iphlpapi.lib")

std::string SystemCommands::get_system_info() {
    std::stringstream ss;
    OSVERSIONINFOA osvi = { sizeof(OSVERSIONINFOA) };
    GetVersionExA(&osvi);
    ss << "OS: " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion;
    ss << " Build: " << osvi.dwBuildNumber;
    
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    ss << " CPUs: " << si.dwNumberOfProcessors;
    
    MEMORYSTATUSEX mem = { sizeof(MEMORYSTATUSEX) };
    GlobalMemoryStatusEx(&mem);
    ss << " RAM: " << mem.ullTotalPhys / (1024*1024) << "MB";
    
    return ss.str();
}

std::string SystemCommands::get_process_list() {
    std::stringstream ss;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return "Error getting processes";
    
    PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };
    if (Process32First(snapshot, &pe)) {
        do {
            ss << pe.th32ProcessID << " - " << pe.szExeFile << "\n";
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return ss.str();
}

bool SystemCommands::kill_process(uint32_t pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProcess) return false;
    bool result = TerminateProcess(hProcess, 0);
    CloseHandle(hProcess);
    Logger::getInstance().log("Killed process: " + std::to_string(pid));
    return result;
}

bool SystemCommands::execute_program(const std::string& path, const std::string& args) {
    std::string cmd = path + " " + args;
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        Logger::getInstance().log("Executed: " + cmd);
        return true;
    }
    return false;
}

bool SystemCommands::shutdown_system(int delay) {
    std::string cmd = "shutdown /s /f /t " + std::to_string(delay);
    system(cmd.c_str());
    Logger::getInstance().log("Shutdown initiated with " + std::to_string(delay) + "s delay");
    return true;
}

bool SystemCommands::restart_system(int delay) {
    std::string cmd = "shutdown /r /f /t " + std::to_string(delay);
    system(cmd.c_str());
    Logger::getInstance().log("Restart initiated with " + std::to_string(delay) + "s delay");
    return true;
}

bool SystemCommands::logoff_system() {
    system("shutdown /l");
    Logger::getInstance().log("Logoff initiated");
    return true;
}

std::string SystemCommands::get_network_info() {
    std::stringstream ss;
    ULONG size = 0;
    GetAdaptersInfo(NULL, &size);
    if (size == 0) return "No network adapters found";
    
    std::vector<BYTE> buffer(size);
    PIP_ADAPTER_INFO adapter = (PIP_ADAPTER_INFO)buffer.data();
    if (GetAdaptersInfo(adapter, &size) == NO_ERROR) {
        while (adapter) {
            ss << "Adapter: " << adapter->Description << "\n";
            ss << "IP: " << adapter->IpAddressList.IpAddress.String << "\n";
            ss << "MAC: " << adapter->Address << "\n\n";
            adapter = adapter->Next;
        }
    }
    return ss.str();
}

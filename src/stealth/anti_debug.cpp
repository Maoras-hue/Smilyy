#include "anti_debug.h"
#include "../utils/logger.h"

bool AntiDebug::is_debugger_present() {
    return IsDebuggerPresent() != 0;
}

bool AntiDebug::check_remote_debugger() {
    HANDLE hProcess = GetCurrentProcess();
    BOOL flags = FALSE;  // Changed from DWORD to BOOL
    if (CheckRemoteDebuggerPresent(hProcess, &flags)) {
        return flags != 0;
    }
    return false;
}

void AntiDebug::prevent_debugging() {
    // Disable error reporting
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    // Remove debug privileges
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken)) {
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = 0;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        }
        CloseHandle(hToken);
    }
    Logger::getInstance().log("Anti-debugging measures applied.");
}

void AntiDebug::check_for_debugger_and_exit() {
    if (is_debugger_present() || check_remote_debugger()) {
        Logger::getInstance().log("Debugger detected! Exiting.");
        exit(0);
    }
}

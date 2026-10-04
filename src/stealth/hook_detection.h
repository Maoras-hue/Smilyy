#ifndef HOOK_DETECTION_H
#define HOOK_DETECTION_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>

struct HookInfo {
    std::string module_name;
    std::string function_name;
    LPVOID original_address;
    LPVOID hooked_address;
    size_t hook_size;
    bool is_hooked;
};

struct SystemHook {
    enum Type { KEYBOARD, MOUSE, MESSAGE, CBT, DEBUG, CALLWNDPROC, SHELL };
    Type type;
    HHOOK handle;
    HMODULE module;
    std::string module_path;
    LPVOID hook_proc;
    DWORD thread_id;
    bool is_active;
};

class HookDetection {
public:
    HookDetection();
    ~HookDetection();

    std::vector<HookInfo> DetectIATHooks(const std::string& module_name);
    std::vector<HookInfo> DetectInlineHooks(const std::string& module_name);
    std::vector<HookInfo> DetectDetours(const std::string& module_name);

    std::vector<SystemHook> EnumerateSystemHooks();
    bool RemoveSystemHook(HHOOK hook_handle);
    bool RemoveAllHooks();
    
    bool DetectUserModeHooks();
    bool DetectKernelModeHooks();
    
    std::vector<BYTE> GetOriginalBytes(const std::string& module_name, const std::string& function_name);
    bool RestoreOriginalFunction(const std::string& module_name, const std::string& function_name);
    
    bool BypassHook(const std::string& function_name);
    bool RedirectToOriginal(const std::string& function_name);
    
    bool IsSSDTHooked();
    bool IsIDTHooked();
    bool IsIRPHooked();
    bool IsImportHooked(const std::string& module_name);
    
    enum HookType { NONE, JMP, CALL, PUSH_RET, SYSENTER, INT2E };
    HookType IdentifyHookType(LPVOID address);

private:
    std::map<std::string, std::vector<HookInfo>> detected_hooks;
    std::vector<SystemHook> system_hooks;
    mutable CRITICAL_SECTION cs;

    bool ReadMemory(LPVOID address, BYTE* buffer, SIZE_T size);
    bool WriteMemory(LPVOID address, BYTE* buffer, SIZE_T size);
    bool IsJMPInstruction(BYTE* bytes);
    bool IsCALLInstruction(BYTE* bytes);
    bool IsPUSHRETInstruction(BYTE* bytes);
    bool IsSYSENTERInstruction(BYTE* bytes);
    
    LPVOID GetModuleBase(const std::string& module_name);
    PIMAGE_DOS_HEADER GetDOSHeader(LPVOID base);
    PIMAGE_NT_HEADERS GetNTHeaders(LPVOID base);
    PIMAGE_IMPORT_DESCRIPTOR GetImportDescriptors(LPVOID base);
    PIMAGE_EXPORT_DIRECTORY GetExportDirectory(LPVOID base);
    
    std::string GetModuleNameFromAddress(LPVOID address);
    HMODULE GetModuleFromAddress(LPVOID address);
    
    static const BYTE JMP_PATTERN[5];
    static const BYTE CALL_PATTERN[5];
    static const BYTE PUSH_RET_PATTERN[6];
    static const BYTE SYSENTER_PATTERN[2];
};

#endif // HOOK_DETECTION_H

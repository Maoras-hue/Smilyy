#include "hook_detection.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <dbghelp.h>
#include <cstdint>
#include <algorithm>
#include <exception>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

const BYTE HookDetection::JMP_PATTERN[5] = {0xE9, 0x00, 0x00, 0x00, 0x00};
const BYTE HookDetection::CALL_PATTERN[5] = {0xE8, 0x00, 0x00, 0x00, 0x00};
const BYTE HookDetection::PUSH_RET_PATTERN[6] = {0x68, 0x00, 0x00, 0x00, 0x00, 0xC3};
const BYTE HookDetection::SYSENTER_PATTERN[2] = {0x0F, 0x34};

HookDetection::HookDetection() {
    InitializeCriticalSection(&cs);
}

HookDetection::~HookDetection() {
    DeleteCriticalSection(&cs);
}

std::vector<HookInfo> HookDetection::DetectIATHooks(const std::string& module_name) {
    std::vector<HookInfo> hooks;
    
    HMODULE module = (HMODULE)GetModuleBase(module_name);
    if (!module) return hooks;
    
    PIMAGE_DOS_HEADER dos = GetDOSHeader(module);
    if (!dos) return hooks;
    
    PIMAGE_NT_HEADERS nt = GetNTHeaders(module);
    if (!nt) return hooks;
    
    PIMAGE_IMPORT_DESCRIPTOR imports = GetImportDescriptors(module);
    if (!imports) return hooks;
    
    while (imports->Name) {
        std::string dll_name = (char*)((BYTE*)module + imports->Name);
        
        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((BYTE*)module + imports->OriginalFirstThunk);
        PIMAGE_THUNK_DATA func = (PIMAGE_THUNK_DATA)((BYTE*)module + imports->FirstThunk);
        
        while (thunk->u1.Function) {
            HookInfo info;
            info.module_name = dll_name;
            
            if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                info.function_name = "Ordinal_" + std::to_string(thunk->u1.Ordinal & 0xFFFF);
            } else {
                PIMAGE_IMPORT_BY_NAME name = (PIMAGE_IMPORT_BY_NAME)((BYTE*)module + thunk->u1.AddressOfData);
                info.function_name = (char*)name->Name;
            }
            
            info.original_address = (LPVOID)thunk->u1.Function;
            info.hooked_address = (LPVOID)func->u1.Function;
            
            if (info.original_address != info.hooked_address) {
                info.is_hooked = true;
                info.hook_size = 5;
                hooks.push_back(info);
            }
            
            thunk++;
            func++;
        }
        imports++;
    }
    
    EnterCriticalSection(&cs);
    detected_hooks[module_name] = hooks;
    LeaveCriticalSection(&cs);
    
    return hooks;
}

std::vector<HookInfo> HookDetection::DetectInlineHooks(const std::string& module_name) {
    std::vector<HookInfo> hooks;
    
    HMODULE module = (HMODULE)GetModuleBase(module_name);
    if (!module) return hooks;
    
    PIMAGE_DOS_HEADER dos = GetDOSHeader(module);
    if (!dos) return hooks;
    
    PIMAGE_NT_HEADERS nt = GetNTHeaders(module);
    if (!nt) return hooks;
    
    PIMAGE_EXPORT_DIRECTORY exports = GetExportDirectory(module);
    if (!exports) return hooks;
    
    DWORD* functions = (DWORD*)((BYTE*)module + exports->AddressOfFunctions);
    DWORD* names = (DWORD*)((BYTE*)module + exports->AddressOfNames);
    WORD* ordinals = (WORD*)((BYTE*)module + exports->AddressOfNameOrdinals);
    
    for (DWORD i = 0; i < exports->NumberOfFunctions; i++) {
        HookInfo info;
        info.module_name = module_name;
        
        bool found = false;
        for (DWORD j = 0; j < exports->NumberOfNames; j++) {
            if (ordinals[j] == i) {
                info.function_name = (char*)((BYTE*)module + names[j]);
                found = true;
                break;
            }
        }
        
        if (!found) {
            info.function_name = "Function_" + std::to_string(i);
        }
        
        LPVOID func_addr = (BYTE*)module + functions[i];
        info.original_address = func_addr;
        
        BYTE buffer[10];
        if (ReadMemory(func_addr, buffer, sizeof(buffer))) {
            HookType type = IdentifyHookType(func_addr);
            info.is_hooked = (type != NONE);
            info.hook_size = (type == JMP) ? 5 : (type == CALL) ? 5 : 0;
            
            if (info.is_hooked) {
                if (type == JMP || type == CALL) {
                    int32_t offset = *(int32_t*)&buffer[1];
                    info.hooked_address = (BYTE*)func_addr + 5 + offset;
                }
                hooks.push_back(info);
            }
        }
    }
    
    return hooks;
}

bool HookDetection::ReadMemory(LPVOID address, BYTE* buffer, SIZE_T size) {
    // Use IsBadReadPtr to check if memory is readable
    if (IsBadReadPtr(address, size)) {
        return false;
    }
    
    try {
        memcpy(buffer, address, size);
        return true;
    } catch (...) {
        return false;
    }
}

bool HookDetection::WriteMemory(LPVOID address, BYTE* buffer, SIZE_T size) {
    DWORD old_protect;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    
    try {
        memcpy(address, buffer, size);
        VirtualProtect(address, size, old_protect, &old_protect);
        return true;
    } catch (...) {
        VirtualProtect(address, size, old_protect, &old_protect);
        return false;
    }
}

HookDetection::HookType HookDetection::IdentifyHookType(LPVOID address) {
    BYTE bytes[6];
    if (!ReadMemory(address, bytes, sizeof(bytes))) return NONE;
    
    if (bytes[0] == 0xE9) return JMP;
    if (bytes[0] == 0xE8) return CALL;
    if (bytes[0] == 0x68 && bytes[5] == 0xC3) return PUSH_RET;
    if (bytes[0] == 0x0F && bytes[1] == 0x34) return SYSENTER;
    if (bytes[0] == 0xCD && bytes[1] == 0x2E) return INT2E;
    
    return NONE;
}

bool HookDetection::IsJMPInstruction(BYTE* bytes) {
    return bytes[0] == 0xE9;
}

bool HookDetection::IsCALLInstruction(BYTE* bytes) {
    return bytes[0] == 0xE8;
}

bool HookDetection::IsPUSHRETInstruction(BYTE* bytes) {
    return bytes[0] == 0x68 && bytes[5] == 0xC3;
}

bool HookDetection::IsSYSENTERInstruction(BYTE* bytes) {
    return bytes[0] == 0x0F && bytes[1] == 0x34;
}

LPVOID HookDetection::GetModuleBase(const std::string& module_name) {
    HMODULE modules[1024];
    DWORD needed;
    
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
            char name[MAX_PATH];
            if (GetModuleBaseNameA(GetCurrentProcess(), modules[i], name, sizeof(name))) {
                if (_stricmp(name, module_name.c_str()) == 0) {
                    return (LPVOID)modules[i];
                }
            }
        }
    }
    
    return nullptr;
}

PIMAGE_DOS_HEADER HookDetection::GetDOSHeader(LPVOID base) {
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    return dos;
}

PIMAGE_NT_HEADERS HookDetection::GetNTHeaders(LPVOID base) {
    PIMAGE_DOS_HEADER dos = GetDOSHeader(base);
    if (!dos) return nullptr;
    
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    
    return nt;
}

PIMAGE_IMPORT_DESCRIPTOR HookDetection::GetImportDescriptors(LPVOID base) {
    PIMAGE_NT_HEADERS nt = GetNTHeaders(base);
    if (!nt) return nullptr;
    
    DWORD import_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!import_rva) return nullptr;
    
    return (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)base + import_rva);
}

PIMAGE_EXPORT_DIRECTORY HookDetection::GetExportDirectory(LPVOID base) {
    PIMAGE_NT_HEADERS nt = GetNTHeaders(base);
    if (!nt) return nullptr;
    
    DWORD export_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!export_rva) return nullptr;
    
    return (PIMAGE_EXPORT_DIRECTORY)((BYTE*)base + export_rva);
}

bool HookDetection::RemoveSystemHook(HHOOK hook_handle) {
    return UnhookWindowsHookEx(hook_handle) != FALSE;
}

std::vector<SystemHook> HookDetection::EnumerateSystemHooks() {
    std::vector<SystemHook> hooks;
    return hooks;
}

bool HookDetection::DetectUserModeHooks() {
    std::vector<std::string> critical_modules = {
        "ntdll.dll", "kernel32.dll", "user32.dll", "advapi32.dll"
    };
    
    for (const auto& module : critical_modules) {
        auto hooks = DetectIATHooks(module);
        if (!hooks.empty()) {
            return true;
        }
        
        hooks = DetectInlineHooks(module);
        if (!hooks.empty()) {
            return true;
        }
    }
    
    return false;
}

bool HookDetection::IsSSDTHooked() {
    return false;
}

bool HookDetection::IsIDTHooked() {
    return false;
}

bool HookDetection::RestoreOriginalFunction(const std::string& module_name, const std::string& function_name) {
    std::string system_path = "C:\\Windows\\System32\\";
    std::string dll_path = system_path + module_name + ".dll";
    
    HMODULE clean_module = LoadLibraryA(dll_path.c_str());
    if (!clean_module) return false;
    
    LPVOID clean_addr = (LPVOID)GetProcAddress(clean_module, function_name.c_str());
    if (!clean_addr) {
        FreeLibrary(clean_module);
        return false;
    }
    
    HMODULE current_module = GetModuleHandleA(module_name.c_str());
    if (!current_module) {
        FreeLibrary(clean_module);
        return false;
    }
    
    LPVOID hooked_addr = (LPVOID)GetProcAddress(current_module, function_name.c_str());
    if (!hooked_addr) {
        FreeLibrary(clean_module);
        return false;
    }
    
    BYTE buffer[32];
    if (ReadMemory(clean_addr, buffer, sizeof(buffer))) {
        if (!WriteMemory(hooked_addr, buffer, sizeof(buffer))) {
            FreeLibrary(clean_module);
            return false;
        }
    }
    
    FreeLibrary(clean_module);
    return true;
}

bool HookDetection::BypassHook(const std::string& function_name) {
    return true;
}

bool HookDetection::RedirectToOriginal(const std::string& function_name) {
    return true;
}

bool HookDetection::IsIRPHooked() {
    return false;
}

bool HookDetection::IsImportHooked(const std::string& module_name) {
    auto hooks = DetectIATHooks(module_name);
    return !hooks.empty();
}

HMODULE HookDetection::GetModuleFromAddress(LPVOID address) {
    HMODULE module = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)address, &module);
    return module;
}

std::string HookDetection::GetModuleNameFromAddress(LPVOID address) {
    HMODULE module = GetModuleFromAddress(address);
    if (!module) return "";
    
    char name[MAX_PATH];
    if (GetModuleBaseNameA(GetCurrentProcess(), module, name, sizeof(name))) {
        return std::string(name);
    }
    return "";
}

std::vector<BYTE> HookDetection::GetOriginalBytes(const std::string& module_name, const std::string& function_name) {
    std::vector<BYTE> result;
    std::string system_path = "C:\\Windows\\System32\\";
    std::string dll_path = system_path + module_name + ".dll";
    
    HMODULE clean_module = LoadLibraryA(dll_path.c_str());
    if (!clean_module) return result;
    
    LPVOID clean_addr = (LPVOID)GetProcAddress(clean_module, function_name.c_str());
    if (clean_addr) {
        BYTE buffer[32];
        if (ReadMemory(clean_addr, buffer, sizeof(buffer))) {
            result.assign(buffer, buffer + sizeof(buffer));
        }
    }
    
    FreeLibrary(clean_module);
    return result;
}

bool HookDetection::RemoveAllHooks() {
    // This would need to remove all hooks - simplified version
    return true;
}

bool HookDetection::DetectKernelModeHooks() {
    return false;
}

std::vector<HookInfo> HookDetection::DetectDetours(const std::string& module_name) {
    return DetectInlineHooks(module_name);
}

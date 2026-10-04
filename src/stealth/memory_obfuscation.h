#ifndef MEMORY_OBFUSCATION_H
#define MEMORY_OBFUSCATION_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstring>

class MemoryObfuscation {
public:
    MemoryObfuscation();
    ~MemoryObfuscation();

    // String obfuscation
    static std::string ObfuscateString(const std::string& str);
    static std::string DeobfuscateString(const std::string& str);
    static std::wstring ObfuscateWString(const std::wstring& str);
    static std::wstring DeobfuscateWString(const std::wstring& str);
    
    // Memory operations
    static bool ProtectMemory(LPVOID address, SIZE_T size, DWORD protection);
    static bool EncryptMemoryRegion(LPVOID address, SIZE_T size, const std::string& key);
    static bool DecryptMemoryRegion(LPVOID address, SIZE_T size, const std::string& key);
    
    // Runtime string encryption
    class SecureString {
    public:
        SecureString(const std::string& str);
        SecureString(const char* str);
        ~SecureString();
        std::string GetDecrypted() const;
        void Clear();
    private:
        std::vector<uint8_t> encrypted_data;
        std::string key;
    };
    
    // Variable obfuscation
    template<typename T>
    class SecureVar {
    public:
        SecureVar(const T& value) {
            key = GenerateKey(32);
            std::vector<uint8_t> data((uint8_t*)&value, (uint8_t*)&value + sizeof(T));
            encrypted_data = AESEncrypt(data, key);
            data_size = sizeof(T);
        }
        
        T Get() const {
            std::vector<uint8_t> decrypted = AESDecrypt(encrypted_data, key);
            T value;
            if (!decrypted.empty()) {
                memcpy(&value, decrypted.data(), std::min(decrypted.size(), sizeof(T)));
            }
            return value;
        }
        
        void Set(const T& value) {
            std::vector<uint8_t> data((uint8_t*)&value, (uint8_t*)&value + sizeof(T));
            encrypted_data = AESEncrypt(data, key);
        }
        
        void Clear() {
            for (size_t i = 0; i < encrypted_data.size(); i++) {
                encrypted_data[i] = 0;
            }
            encrypted_data.clear();
            key.clear();
        }
        
    private:
        std::vector<uint8_t> encrypted_data;
        std::string key;
        size_t data_size;
    };
    
    // String constants
    static std::string DecryptStringA(const std::string& encrypted, const std::string& key);
    static std::wstring DecryptStringW(const std::wstring& encrypted, const std::wstring& key);
    
    // Anti-analysis
    static void ObfuscateControlFlow();
    static void AddJunkCode();
    static void MutateCode();

private:
    // Encryption/Decryption functions
    static std::vector<uint8_t> XOREncrypt(const std::vector<uint8_t>& data, const std::string& key);
    static std::vector<uint8_t> XORDecrypt(const std::vector<uint8_t>& data, const std::string& key);
    static std::vector<uint8_t> AESEncrypt(const std::vector<uint8_t>& data, const std::string& key);
    static std::vector<uint8_t> AESDecrypt(const std::vector<uint8_t>& data, const std::string& key);
    
    // Key generation
    static std::string GenerateKey(size_t length = 32);
    static std::string DeriveKey(const std::string& password);
    
    // Memory protection flags
    static DWORD current_protection;
    static std::map<LPVOID, DWORD> protected_regions;
    
    // Thread-local storage for obfuscation
    static thread_local std::map<std::string, std::string> decrypted_cache;
};

// Macro for easy string obfuscation
#define OBFUSCATE(str) MemoryObfuscation::ObfuscateString(str)
#define DECRYPT(enc) MemoryObfuscation::DeobfuscateString(enc)

// Runtime encrypted string literal
#define ENCRYPTED_STR(str) MemoryObfuscation::SecureString(str)

#endif // MEMORY_OBFUSCATION_H
